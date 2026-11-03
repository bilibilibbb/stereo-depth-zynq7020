"""Strict model-independent SDP1 framing. No inference or image enhancement."""
from dataclasses import dataclass
import asyncio
import math
import struct
import zlib

HEADER = struct.Struct("!4sBBHIIQQIIQQQQIHHHHIII")
assert HEADER.size == 96
PREVIEW, PAIR, STATUS, PAIR_ACK = 1, 2, 3, 4
RGB888, YUYV, PAIR_F32, PAIR_F32_ROI = 1, 2, 3, 4
PAIR_F32_FROZEN = 5
FROZEN_META = struct.Struct("!QIIQQQ")
MAX_PAYLOAD = 640 * 480 * 3


@dataclass(frozen=True)
class Header:
    kind: int
    size: int
    crc: int
    boot: int
    shot: int
    epoch: int
    attempt: int
    trigger_us: int
    locked_us: int
    sgm_done_us: int
    model_done_us: int
    model_id: int
    width: int
    height: int
    format: int
    flags: int
    status: int
    error: int

    @property
    def identity(self):
        return (self.boot, self.shot, self.epoch, self.attempt)

    @property
    def key(self):
        return ":".join(str(v) for v in self.identity)

    def encode(self):
        return HEADER.pack(b"SDP1", 1, self.kind, HEADER.size, self.size, self.crc,
                           self.boot, self.shot, self.epoch, self.attempt,
                           self.trigger_us, self.locked_us, self.sgm_done_us,
                           self.model_done_us, self.model_id, self.width,
                           self.height, self.format, self.flags, self.status, self.error, 0)


def decode_header(raw):
    if len(raw) != HEADER.size:
        raise ValueError("header length")
    magic, version, kind, length, *v = HEADER.unpack(raw)
    if magic != b"SDP1" or version != 1 or length != HEADER.size or v[-1]:
        raise ValueError("header identity/reserved")
    h = Header(kind, *v[:-1])
    if not h.boot or h.flags & ~1 or h.kind not in (PREVIEW, PAIR, STATUS, PAIR_ACK):
        raise ValueError("session, flags or kind")
    if h.size > MAX_PAYLOAD:
        raise ValueError("payload limit")
    if h.kind == PAIR_ACK:
        if not h.shot or h.model_id not in (1, 11459) or any((h.size,h.crc,h.width,h.height,h.format,h.flags,
                                                          h.trigger_us,h.locked_us,h.sgm_done_us,h.model_done_us,h.error)):
            raise ValueError("ACK layout")
        return h
    if h.kind == STATUS:
        if h.width or h.height or h.format or h.size or h.crc:
            raise ValueError("status layout")
        return h
    if not h.shot or h.status or h.error:
        raise ValueError("frame identity/status")
    if not (1 <= h.width <= 640 and 1 <= h.height <= 480):
        raise ValueError("dimensions")
    if h.kind == PREVIEW:
        bpp = {RGB888: 3, YUYV: 2}.get(h.format)
        if not bpp or h.size != h.width*h.height*bpp or (h.format == YUYV and h.width % 2):
            raise ValueError("preview format/size")
        if h.trigger_us or h.sgm_done_us or h.model_done_us:
            raise ValueError("preview times")
    else:
        if (h.width, h.height, h.size) != (320, 240, 320*240*9+(40 if h.format == PAIR_F32_FROZEN else 0)) or h.format not in (PAIR_F32, PAIR_F32_ROI, PAIR_F32_FROZEN):
            raise ValueError("pair layout")
        if h.model_id not in (1, 11459):
            raise ValueError("unknown model")
        if not h.locked_us <= h.sgm_done_us <= h.model_done_us or (h.format != PAIR_F32_FROZEN and h.trigger_us > h.locked_us):
            raise ValueError("pair clock ordering")
    return h


def validate_payload(h, payload):
    if len(payload) != h.size or zlib.crc32(payload) != h.crc:
        raise ValueError("payload length/CRC")
    if h.kind != PAIR:
        return
    pixels = h.width * h.height
    p = memoryview(payload)
    disparity, valid = p[3*pixels:4*pixels], p[4*pixels:5*pixels]
    for d, v in zip(disparity, valid):
        allowed = 7 if h.format in (PAIR_F32_ROI,PAIR_F32_FROZEN) else 1
        good = v & 1
        if v & ~allowed or (good and d >= 64) or (not good and d != 255) or (v & 2 and not good):
            raise ValueError("SGM disparity/validity")
    for (value,) in struct.iter_unpack("<f", p[5*pixels:9*pixels]):
        if not math.isfinite(value):
            raise ValueError("nonfinite model output")
        if h.model_id == 11459 and not 0 <= value <= 63.99609375:
            raise ValueError("small-model output range")
    if h.format == PAIR_F32_FROZEN:
        source,epoch,attempt,armed,acquired,selected = FROZEN_META.unpack(p[9*pixels:])
        if not source or not epoch or not attempt or not armed <= acquired <= h.locked_us <= h.model_done_us <= selected or h.trigger_us > selected:
            raise ValueError("frozen source identity/times")


def frozen_source(h, packet):
    if h.format != PAIR_F32_FROZEN:
        return None
    source,epoch,attempt,armed,acquired,selected = FROZEN_META.unpack_from(packet,96+9*h.width*h.height)
    return dict(frame=source,epoch=epoch,attempt=attempt,capture_started_us=armed,
                capture_ready_us=acquired,selected_us=selected,
                result_age_ms=(selected-acquired)/1000,
                completed_age_ms=(selected-h.model_done_us)/1000,
                request_wait_ms=(selected-h.trigger_us)/1000)


def decode_packet(packet):
    h = decode_header(packet[:HEADER.size])
    if len(packet) != HEADER.size + h.size:
        raise ValueError("packet length")
    validate_payload(h, memoryview(packet)[HEADER.size:])
    return h


async def read_packet(reader, payload_timeout=10):
    # Validate all length fields before allocating/reading a payload.
    raw = await reader.readexactly(HEADER.size)
    h = decode_header(raw)
    payload = await asyncio.wait_for(reader.readexactly(h.size), payload_timeout)
    validate_payload(h, payload)
    return h, raw + payload


def make_packet(h, payload):
    from dataclasses import replace
    h = replace(h, size=len(payload), crc=zlib.crc32(payload))
    packet = h.encode() + payload
    decode_packet(packet)
    return packet


def pair_ack(h):
    if h.kind != PAIR:
        raise ValueError("ACK requires a validated pair")
    return Header(PAIR_ACK,0,0,h.boot,h.shot,h.epoch,h.attempt,0,0,0,0,
                  h.model_id,0,0,0,0,h.crc,0).encode()
