"""Hardware-free behavior checks; fixtures do not represent physical PYNQ boards."""
from pathlib import Path
from types import SimpleNamespace
import hashlib
import importlib.util
import sys
import struct
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

def load(name,path):
    spec=importlib.util.spec_from_file_location(name,ROOT/path)
    mod=importlib.util.module_from_spec(spec);sys.modules[name]=mod;spec.loader.exec_module(mod);return mod

memory=load('budget_helper','bounded-build-memory/scripts/guard_budget.py')
life=load('lifetime_helper','fixed-graph-memory-fusion/scripts/lifetime_check.py')
acc=load('accumulator_helper','fpga-nn-arithmetic-mapping/scripts/accumulator_bound.py')
overlay=load('overlay_helper','pynq-overlay-contract/scripts/overlay_contract.py')
dma_api=load('dma_helper','pynq-dma-buffer-reuse/scripts/dma_session.py')
registers_api=load('register_helper','pynq-register-driver/scripts/register_plan.py')
tensor_api=load('tensor_helper','pynq-tensor-contract/scripts/tensor_contract.py')
timing_api=load('timing_helper','pynq-stage-profiling/scripts/stage_timing.py')
repro_api=load('repro_helper','pynq-reproducible-runs/scripts/run_manifest.py')

class BudgetTests(unittest.TestCase):
    def test_growth_accounts_for_lost_available_memory(self):
        r=memory.budget(2**30,2**31,2**28,2**29)
        d=r['additional_commit_bytes']
        self.assertGreater(d,0);self.assertLess(r['used_bytes']+d,r['remaining_bytes']-d)
    def test_commit_headroom_can_stop_job(self):
        self.assertEqual(memory.budget(2**30,2**27,2**28,2**27)['action'],'stop_owned_job_and_change_work')
    def test_equal_remaining_is_not_allowed(self):
        self.assertEqual(memory.budget(8192,65536,8192,0)['additional_commit_bytes'],0)
    def test_subpage_margin_is_not_allocatable(self):
        self.assertEqual(memory.budget(8193,65536,8192,0)['action'],'no_page_growth_margin')

class TensorTests(unittest.TestCase):
    def rows(self,last=2):
        return [dict(name='a',offset=0,bytes=1024,birth=0,last_consumer=last),
                dict(name='b',offset=0,bytes=512,birth=3,last_consumer=4)]
    def test_retired_tensor_space_is_reused(self):
        r=life.check(self.rows());self.assertTrue(r['valid']);self.assertEqual(r['allocation_bytes'],1024)
    def test_last_consumer_same_event_prevents_reuse(self):
        self.assertFalse(life.check(self.rows(3))['valid'])
    def test_dma_lifetime_extension_prevents_overwrite(self):
        self.assertFalse(life.check(self.rows(7))['valid'])
    def test_separate_memory_is_safe_while_live(self):
        rows=self.rows(7);rows[1]['offset']=1024
        self.assertEqual(life.check(rows)['peak_live_bytes'],1536)

class ArithmeticTests(unittest.TestCase):
    def test_int16_min_times_min_needs_46_bits(self):
        r=acc.bound(-32768,32767,-32768,32767,16384)
        self.assertEqual(r['maximum'],2**44);self.assertEqual(r['signed_bits'],46)
    def test_positive_bias_crosses_boundary(self):
        self.assertEqual(acc.bound(1,1,127,127,1,1,1)['signed_bits'],9)
    def test_negative_only_boundary(self):
        self.assertEqual(acc.bound(-128,-128,1,1,1)['signed_bits'],8)
    def test_invalid_terms_rejected(self):
        with self.assertRaises(ValueError):acc.bound(-1,1,-1,1,0)

class OverlayTests(unittest.TestCase):
    def configuration(self,name='pipe/dma0',addr=0x40400000):
        metadata={name:dict(type='xilinx.com:ip:axi_dma:7.1',phys_addr=addr,addr_range=65536,parameters={'C_INCLUDE_SG':'0'})}
        contract=dict(ip_name=name,vlnv='xilinx.com:ip:axi_dma:7.1',minimum_address_range=256,parameters={'C_INCLUDE_SG':0})
        return metadata,contract
    def test_names_and_64bit_addresses_are_configurable(self):
        for name,addr in [('pipe/dma0',0x40400000),('dsp_chain/stream',0xA00000000)]:
            self.assertEqual(overlay.check_ip(*self.configuration(name,addr))['phys_addr'],addr)
    def test_missing_ip_wrong_version_and_sg_rejected(self):
        for fault in ('name','version','sg'):
            metadata,c=self.configuration()
            if fault=='name':c['ip_name']='missing'
            if fault=='version':c['vlnv']='xilinx.com:ip:axi_dma:7.0'
            if fault=='sg':metadata[c['ip_name']]['parameters']['C_INCLUDE_SG']='1'
            with self.assertRaises(ValueError):overlay.check_ip(metadata,c)
    def test_register_window_too_small(self):
        metadata,c=self.configuration();metadata[c['ip_name']]['addr_range']=4
        with self.assertRaises(ValueError):overlay.check_ip(metadata,c)
    def test_hwh_mutation_breaks_source_binding(self):
        with tempfile.TemporaryDirectory() as tmp:
            bit=Path(tmp)/'demo.bit';hwh=Path(tmp)/'demo.hwh'
            bit.write_bytes(b'SYNTHETIC_BIT_FIXTURE');hwh.write_bytes(b'SYNTHETIC_HWH_FIXTURE')
            hashes={k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in [('bit',bit),('hwh',hwh)]}
            overlay.check_artifacts(bit,hwh,hashes)
            hwh.write_bytes(b'OTHER_BUILD')
            with self.assertRaises(ValueError):overlay.check_artifacts(bit,hwh,hashes)

class Buffer:
    def __init__(self,address,events):
        self.device_address=address;self.shape=(4,);self.dtype=SimpleNamespace(str='<i2')
        self.flags=SimpleNamespace(c_contiguous=True);self.nbytes=8;self.events=events
        self.cpu=[0]*4;self.device=[0]*4
    def flush(self):self.events.append(('flush',self.device_address));self.device=list(self.cpu)
    def invalidate(self):self.events.append(('invalidate',self.device_address));self.cpu=list(self.device)

class Clock:
    def __init__(self):self.now=0
    def __call__(self):return self.now
    def sleep(self,dt):self.now+=dt

class Channel:
    def __init__(self,owner,role):self.owner=owner;self.role=role;self.buf=None;self.transferred=0;self.running=True
    @property
    def idle(self):return self.owner.complete
    @property
    def error(self):return self.owner.started and self.owner.error_on_start
    def transfer(self,buf,nbytes):
        self.owner.events.append(('transfer',self.role));self.buf=buf;self.transferred=0
        if self.role=='tx':
            buf.flush();self.owner.started=True
            assert self.owner.recvchannel.buf is not None,'RX must be armed before TX'
            self.owner.recvchannel.buf.device=[x*2 for x in buf.device]
            self.owner.complete=not self.owner.stall
    def wait(self):
        assert self.idle
        self.owner.events.append(('wait',self.role))
        if self.role=='rx':self.buf.invalidate()
        self.transferred=self.buf.nbytes-(2 if self.role=='rx' and self.owner.short else 0)
        self.buf=None

class FakeDMA:
    def __init__(self,events):
        self.events=events;self.started=False;self.complete=False;self.stall=False;self.short=False;self.error_on_start=False
        self.sendchannel=Channel(self,'tx');self.recvchannel=Channel(self,'rx')

class DMATests(unittest.TestCase):
    def setUp(self):
        self.events=[];self.src=Buffer(4096,self.events);self.dst=Buffer(8192,self.events)
        self.dma=FakeDMA(self.events);self.clock=Clock()
        self.contract=dma_api.BufferContract((4,),'<i2',(4,),'<i2',16383,8,8)
        self.session=dma_api.DMASession(self.dma,self.src,self.dst,self.contract,clock=self.clock,sleep=self.clock.sleep)
    def fill(self,buf):buf.cpu=[1,2,3,4]
    def test_receive_first_and_cache_visible(self):
        result=self.session.run(self.fill)
        self.assertEqual(result.cpu,[2,4,6,8])
        self.assertEqual([x for x in self.events if x[0]=='transfer'],[('transfer','rx'),('transfer','tx')])
        self.assertIn(('invalidate',8192),self.events)
    def test_two_calls_reuse_the_same_buffers(self):
        first=self.session.run(self.fill);second=self.session.run(self.fill)
        self.assertIs(first,second);self.assertIs(self.session.input,self.src)
    def test_missing_tlast_timeout_keeps_buffers_and_blocks_reuse(self):
        self.dma.stall=True
        with self.assertRaises(dma_api.TransferFault):self.session.run(self.fill,timeout=.001)
        self.assertEqual(self.session.state,'fault');self.assertIs(self.session.output,self.dst)
        count=len(self.events)
        with self.assertRaises(dma_api.TransferFault):self.session.run(self.fill)
        self.assertEqual(len(self.events),count)
        with self.assertRaises(dma_api.TransferFault):self.session.recover_after_reset(lambda:False)
    def test_early_tlast_short_packet_is_rejected(self):
        self.dma.short=True
        with self.assertRaises(dma_api.TransferFault):self.session.run(self.fill)
        self.assertEqual(self.session.state,'fault')
    def test_error_after_start_is_not_success(self):
        self.dma.error_on_start=True
        with self.assertRaises(dma_api.TransferFault):self.session.run(self.fill)
        self.assertEqual(self.session.state,'fault')
    def test_aliasing_buffers_rejected(self):
        self.dst.device_address=self.src.device_address
        with self.assertRaises(ValueError):dma_api.DMASession(self.dma,self.src,self.dst,self.contract)
    def test_wrong_dtype_or_noncontiguous_layout_rejected(self):
        self.src.dtype.str='>i2'
        with self.assertRaises(ValueError):self.session.run(self.fill)
        self.src.dtype.str='<i2';self.src.flags.c_contiguous=False
        with self.assertRaises(ValueError):self.session.run(self.fill)
    def test_length_over_capacity_rejected(self):
        contract=dma_api.BufferContract((4,),'<i2',(4,),'<i2',7,8,8)
        with self.assertRaises(ValueError):dma_api.DMASession(self.dma,self.src,self.dst,contract)
    def test_nonfinite_deadline_rejected(self):
        with self.assertRaises(ValueError):self.session.run(self.fill,timeout=float('inf'))

class RegisterTests(unittest.TestCase):
    def rows(self):
        return [dict(name='length',offset=16,access='rw',write_mask=65535),
                dict(name='start',offset=0,access='wo',write_mask=1),
                dict(name='status',offset=4,access='read-clear')]
    def test_start_is_after_configuration(self):
        result=registers_api.plan(self.rows(),{'start':1,'length':128},'start')
        self.assertEqual([r['name'] for r in result],['length','start'])
    def test_read_clear_register_is_not_written(self):
        with self.assertRaises(ValueError):registers_api.plan(self.rows(),{'status':0})
    def test_reserved_bits_are_not_written(self):
        with self.assertRaises(ValueError):registers_api.plan(self.rows(),{'start':3})
    def test_invalid_word_offset_is_rejected(self):
        rows=self.rows();rows[0]['offset']=17
        with self.assertRaises(ValueError):registers_api.plan(rows,{'length':128})

class TensorContractTests(unittest.TestCase):
    def test_channel_ids_are_reordered_correctly(self):
        raw=struct.pack('<4h',1,2,10,20)
        hwc=tensor_api.reorder_i16(raw,2,1,2,'CHW','HWC')
        self.assertEqual(struct.unpack('<4h',hwc),(1,10,2,20))
        self.assertEqual(tensor_api.reorder_i16(hwc,2,1,2,'HWC','CHW'),raw)
    def test_wrong_endianness_is_detected(self):
        raw=struct.pack('>2h',1,-32768)
        self.assertEqual(tensor_api.compare_i16(raw,[1,-32768],'>')['mismatches'],0)
        self.assertEqual(tensor_api.compare_i16(raw,[1,-32768],'<')['mismatches'],2)
    def test_partial_tensor_rejected(self):
        with self.assertRaises(ValueError):tensor_api.reorder_i16(b'\0\0',2,1,2,'CHW','HWC')
    def test_difference_examples_are_bounded(self):
        r=tensor_api.compare_i16(struct.pack('<10h',*([0]*10)),[1]*10,max_examples=2)
        self.assertEqual(r['mismatches'],10);self.assertEqual(len(r['examples']),2)

class TimingTests(unittest.TestCase):
    def row(self,ident='a',shift=0,**kw):
        r=dict(id=ident,begin_ns=shift,prepared_ns=shift+1000,submitted_ns=shift+2000,completed_ns=shift+8000,consumed_ns=shift+10000)
        r.update(kw);return r
    def test_explicit_warmup_only_is_removed(self):
        r=timing_api.summarize([self.row('warm',warmup=True),self.row()])
        self.assertEqual(r['samples'],1);self.assertEqual(r['stages']['end_to_end_ms']['median'],.01)
    def test_duplicate_results_are_not_throughput(self):
        with self.assertRaises(ValueError):timing_api.summarize([self.row(),self.row(shift=1000)])
    def test_invalid_time_order_is_rejected(self):
        with self.assertRaises(ValueError):timing_api.summarize([self.row(completed_ns=1000)])
    def test_completed_events_are_sorted_without_summing_overlap(self):
        r=timing_api.summarize([self.row('late',3000),self.row('early')])
        self.assertEqual(r['distinct_output_intervals_ms'],[.003])

class ReproTests(unittest.TestCase):
    def test_input_changes_are_identifiable(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'input.bin';p.write_bytes(b'one')
            first=repro_api.manifest({'input':p})['files'][0]
            self.assertEqual(first,repro_api.manifest({'input':p})['files'][0])
            p.write_bytes(b'two')
            self.assertNotEqual(first['sha256'],repro_api.manifest({'input':p})['files'][0]['sha256'])
            self.assertEqual(first['filename'],'input.bin');self.assertNotIn(tmp,str(first))
    def test_missing_artifact_is_not_silently_recorded(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(FileNotFoundError):repro_api.manifest({'input':Path(tmp)/'absent.bin'})

if __name__=='__main__':unittest.main(verbosity=2)
