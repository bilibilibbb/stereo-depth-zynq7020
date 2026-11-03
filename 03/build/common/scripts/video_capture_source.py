"""Exact retained-driver specialization: descriptor acquisition before pixel acquisition."""
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE='reference/compute/reference/v351a/retained_camera_driver.c'
OWNED='firmware/video_capture.c'


def derive(source):
    helpers=source.split('int rc_open(',1)[0]
    arm='int rc_arm('+source.split('int rc_arm(',1)[1].split('int rc_poll(',1)[0]
    poll='int rc_poll('+source.split('int rc_poll(',1)[1].split('int rc_release(',1)[0]
    old='c->io.flush(c->io.user,c->buffers[slot][0],RC_FRAME_BYTES);c->io.flush(c->io.user,c->buffers[slot][1],RC_FRAME_BYTES);'
    assert arm.count(old)==1
    arm=arm.replace(old,'',1).replace('int rc_arm(','int ss_video_capture_arm(',1)
    old='for(side=0;side<2;side++){c->last_source[side]=p[17+16*side];c->io.invalidate(c->io.user,c->buffers[slot][side],RC_FRAME_BYTES);}'
    assert poll.count(old)==1
    poll=poll.replace(old,'for(side=0;side<2;side++)c->last_source[side]=p[17+16*side];',1)
    poll=poll.replace('int rc_poll(','int ss_video_capture_poll(',1)
    return ('/* Derived by scripts/video_capture_source.py. Video owns both DMA slots.\n'
        ' * Init cleans all four buffers once; CPUs never write them thereafter.\n'
        ' * Poll validates the complete descriptor without touching pixel memory.\n'
        ' * The producer can arm the other slot immediately, then must invalidate\n'
        ' * each consumed eye before its first read. CPU1 reads a separate copy.\n'
        ' * All descriptor/identity/freshness/quarantine checks remain intact.\n'
        ' */\n'+helpers+arm+poll)


def verify():
    assert (ROOT/OWNED).read_text(encoding='utf-8')==derive((ROOT/BASE).read_text(encoding='utf-8'))


if __name__=='__main__':
    verify()
