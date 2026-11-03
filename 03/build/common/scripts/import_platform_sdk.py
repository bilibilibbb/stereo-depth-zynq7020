"""Freeze the actually generated current BSP without editing old references."""
from pathlib import Path
import argparse,hashlib,json,re,shutil
ROOT=Path(__file__).resolve().parents[1]

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))

def main():
    parser=argparse.ArgumentParser();parser.add_argument('receipt');args=parser.parse_args()
    receipt=(ROOT/args.receipt).resolve(strict=True);assert receipt.is_relative_to(ROOT/'build')
    r=read(receipt);assert r['status']=='PASS' and read(receipt.parent.parent/'guard.json')['status']=='PASS'
    assert r['hardware_contract_sha256']==sha(ROOT/'hardware/contract.json')
    for name,digest in r['bsp'].items():assert sha(receipt.parent/name)==digest,name
    bp=list(receipt.parent.glob('fsbl/*_bsp/ps7_cortexa9_0'));assert len(bp)==1
    bsp=bp[0];standalone=bsp/'libsrc/standalone_v6_8/src'
    values=dict(re.findall(r'^#define\s+(\w+)\s+(\S+)',(bsp/'include/xparameters.h').read_text(),re.M))
    expected={'XPAR_CPU_CORTEXA9_0_CPU_CLK_FREQ_HZ':666666687,'STDIN_BASEADDRESS':0xe0000000,'STDOUT_BASEADDRESS':0xe0000000,
              'XPAR_PS7_DDR_0_S_AXI_BASEADDR':0x100000,'XPAR_PS7_DDR_0_S_AXI_HIGHADDR':0x3fffffff,
              'XPAR_XEMACPS_0_BASEADDR':0xe000b000,'XPAR_XEMACPS_0_ENET_CLK_FREQ_HZ':125000000,
              'XPAR_XSDPS_0_BASEADDR':0xe0100000,'XPAR_XSDPS_0_SDIO_CLK_FREQ_HZ':100000000,'XPAR_XSDPS_0_HAS_CD':1,
              'XPAR_PS7_UART_0_UART_CLK_FREQ_HZ':100000000}
    for name,value in expected.items():assert int(values[name],0)==value,name
    old=ROOT/'reference/compute/reference/a9_measure_boot'
    for name in ('asm_vectors.S','cpu_init.S','xil-crt0.S'):
        assert (old/'cpu0'/name).read_text()==(standalone/name).read_text(),name
    assert (ROOT/'reference/compute/reference/color_a9_sdk/xil_cache.c').read_text()==(standalone/'xil_cache.c').read_text()
    # Retain the previously reviewed extra USE_AMP guards around the two SCU
    # sections. The vendor source has no such guards; CPU1 must not reset SCU.
    boot=(old/'cpu1/boot.S').read_text()
    for block in ('\t/*invalidate scu*/\n\tldr\tr7, =0xf8f0000c\n\tldr\tr6, =0xffff\n\tstr\tr6, [r7]\n',
                  '\t/*set scu enable bit in scu*/\n\tldr\tr7, =0xf8f00000\n\tldr\tr0, [r7]\n\torr\tr0, r0, #0x1\n\tstr\tr0, [r7]\n'):
        guarded='#if USE_AMP != 1\n'+block+'#endif\n'
        assert boot.count(guarded)==1;boot=boot.replace(guarded,block)
    assert boot==(standalone/'boot.S').read_text(),'Unexpected vendor boot difference'
    dest=ROOT/'reference/platform_sdk';assert not dest.exists(),'Keep prior import evidence; review before replacing'
    files={f'include/{p.name}':p for p in (bsp/'include').glob('*.h')}
    files.update({'lib/libxil.a':bsp/'lib/libxil.a','xil_cache.c':standalone/'xil_cache.c',
                  'Xilinx.spec':old/'sdk/Xilinx.spec'})
    for name,p in files.items():
        out=dest/name;out.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,out)
    report=dict(source_receipt=receipt.relative_to(ROOT).as_posix(),source_receipt_sha256=sha(receipt),
                hardware_contract_sha256=r['hardware_contract_sha256'],platform_id=r['platform_id'],
                files=[dict(path=n,source=p.relative_to(ROOT).as_posix(),sha256=sha(p)) for n,p in sorted(files.items())],
                checked_parameters=expected,
                startup='Keep original reviewed private dual-core boot and minimal Xilinx.spec; only differences from current vendor boot are two extra SCU USE_AMP guards. Other startup text and cache C match current generated BSP.',
                scope='Actual current standalone6.8/emacps3.8/sdps3.6/uartps3.7 BSP. Headers/library replace old application SDK dependency; frozen compute unchanged. No board or physical acceptance.')
    (dest/'IMPORT.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'files':len(files),'platform':r['platform_id'],'bytes':sum(p.stat().st_size for p in files.values())}))

def current_sdk():
    sdk=ROOT/'reference/platform_sdk';r=read(sdk/'IMPORT.json')
    assert r['hardware_contract_sha256']==sha(ROOT/'hardware/contract.json'),'Stale current BSP hardware identity'
    for item in r['files']:assert sha(sdk/item['path'])==item['sha256'],item['path']
    return sdk

if __name__=='__main__':main()
