"""Reusable full-buffer transfers for an exclusively owned AXI simple DMA.

Target PYNQ _SDMAChannel public transfer/wait/idle/error/running behavior.
No allocation, bitstream loading, reset or buffer freeing occurs here.
"""
from dataclasses import dataclass
import math
import time


@dataclass(frozen=True)
class BufferContract:
    input_shape: tuple
    input_dtype: str
    output_shape: tuple
    output_dtype: str
    max_transfer_bytes: int
    send_alignment: int
    recv_alignment: int


class TransferFault(RuntimeError):
    pass


class DMASession:
    def __init__(self,dma,input_buffer,output_buffer,contract,*,clock=time.monotonic,sleep=time.sleep):
        self.dma=dma;self.input=input_buffer;self.output=output_buffer;self.contract=contract
        self.clock=clock;self.sleep=sleep;self.state='ready'
        self._validate_buffers()

    def _validate_buffers(self):
        c=self.contract
        if type(c.max_transfer_bytes) is not int or c.max_transfer_bytes<=0:
            raise ValueError('Positive maximum transaction bytes required')
        intervals=[]
        for buf,shape,dtype,alignment in (
            (self.input,c.input_shape,c.input_dtype,c.send_alignment),
            (self.output,c.output_shape,c.output_dtype,c.recv_alignment)):
            if tuple(buf.shape)!=tuple(shape) or buf.dtype.str!=dtype or not buf.flags.c_contiguous:
                raise ValueError('Shape, dtype including byte order, or contiguity mismatch')
            addr=int(buf.device_address);nbytes=int(buf.nbytes)
            if type(alignment) is not int or alignment<=0 or addr<0 or addr%alignment:
                raise ValueError('DMA address alignment mismatch')
            if not 0<nbytes<=c.max_transfer_bytes:
                raise ValueError('Transfer exceeds configured transaction capacity')
            intervals.append((addr,addr+nbytes))
        if max(x[0] for x in intervals)<min(x[1] for x in intervals):
            raise ValueError('Input and output device ranges must not alias')

    def run(self,fill_input,*,start_core=None,timeout=1.0):
        if self.state!='ready':raise TransferFault('Session is not reusable; quiesce/reset the owned path first')
        if not math.isfinite(timeout) or timeout<=0:raise ValueError('Finite positive timeout required')
        self._validate_buffers()
        tx,rx=self.dma.sendchannel,self.dma.recvchannel
        if not tx.running or not rx.running:raise TransferFault('DMA channels must already be running')
        if tx.error or rx.error:raise TransferFault('Clear the existing hardware fault before starting')
        fill_input(self.input)
        # Commit dirty output cache lines before hardware writes the full buffer.
        self.output.flush()
        self.state='active'
        deadline=self.clock()+timeout
        try:
            rx.transfer(self.output,nbytes=self.output.nbytes)
            # PYNQ simple-DMA transfer flushes the input buffer itself.
            tx.transfer(self.input,nbytes=self.input.nbytes)
            if start_core is not None:start_core()
            while True:
                if tx.error or rx.error:raise TransferFault('DMA reports a transfer error')
                if tx.idle and rx.idle:break
                if self.clock()>=deadline:raise TransferFault('DMA deadline exceeded; check TLAST, reset and backpressure')
                self.sleep(0.0005)
            # wait finalizes the driver's bookkeeping and invalidates received data.
            tx.wait();rx.wait()
            if tx.transferred!=self.input.nbytes or rx.transferred!=self.output.nbytes:
                raise TransferFault('Completed byte count differs from the full-buffer contract')
        except BaseException:
            # Keep strong references: timeout is not hardware quiescence.
            self.state='fault'
            raise
        self.state='ready'
        return self.output

    def recover_after_reset(self,quiescence_proven):
        if self.state!='fault':raise TransferFault('Only a faulted session needs recovery')
        if not quiescence_proven():raise TransferFault('Data path is not proven quiescent')
        if not self.dma.sendchannel.running or not self.dma.recvchannel.running:
            raise TransferFault('Restart the owned DMA after the platform-specific reset')
        self.state='ready'
