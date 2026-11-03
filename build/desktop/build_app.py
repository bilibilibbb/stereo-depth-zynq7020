"""Build a portable one-folder Windows app; no global Python installation changes."""
from pathlib import Path
import hashlib, json, os, shutil, subprocess, sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parent


def main():
    stage=HERE/'stage';native=stage/'native';assets=stage/'assets'
    native.mkdir(parents=True,exist_ok=True);assets.mkdir(exist_ok=True)
    compiler=Path(os.environ['WINDIR'])/'Microsoft.NET/Framework64/v4.0.30319/csc.exe'
    subprocess.run([str(compiler),'/nologo','/target:exe','/platform:anycpu','/optimize+',
        '/reference:System.Management.dll','/reference:System.Web.Extensions.dll','/reference:System.Windows.Forms.dll',
        '/out:'+str(native/'NetworkSetup.exe'),str(HERE/'NetworkSetup.cs')],check=True)
    html=(ROOT/'common/viewer.html').read_text(encoding='utf-8')
    html=html.replace('</head>','<style>#preview{transform:rotate(180deg)}</style></head>')
    (assets/'viewer.html').write_text(html,encoding='utf-8');shutil.copyfile(ROOT/'common/viewer.js',assets/'viewer.js')
    import PyInstaller.__main__
    PyInstaller.__main__.run([str(HERE/'app.py'),'--noconfirm','--onedir','--windowed','--noupx',
        '--name','双目拍照展示','--paths',str(ROOT/'common'),'--paths',str(HERE),
        '--add-data',str(assets)+os.pathsep+'assets','--add-binary',str(native/'NetworkSetup.exe')+os.pathsep+'native',
        '--hidden-import','self_test','--hidden-import','app',
        '--exclude-module','numpy','--exclude-module','matplotlib',
        '--distpath',str(HERE/'dist'),'--workpath',str(HERE/'build'),'--specpath',str(HERE)])
    import PyInstaller
    release=HERE/'dist/双目拍照展示'
    (release/'使用说明.txt').write_text('''双目拍照展示 · Windows 10/11 x64

1. 把整个压缩包解压到电脑本地文件夹。保留 _internal 文件夹；不要只复制 EXE。
2. 板卡插入已准备好的 SD 卡，接通电源。网线从板卡 J23 接到电脑有线网口（可用 USB 网卡）。
3. 双击“双目拍照展示.exe”。程序检测有线网卡并连接板卡，在同一窗口显示预览和两张结果。
4. 如需临时地址，Windows 会请求管理员权限。有多张已连接网卡时，选择接板卡的那一张。
5. 镜头前放置光线充足、有纹理的物体，保持静止约 3 秒，短按 PS_KEY1。
6. 两张结果一起更新，分别为纯 SGM 和小模型结果。颜色使用统一的视差范围，不是实测米制距离。
7. 展示时保留应用运行。结束时点“退出”，应用释放自己添加的临时网络配置。

软件不需要安装 Python、Vitis 或 Vivado，不需要浏览器，也不需要联网下载运行库。
预览、对应原图、SGM 与小模型结果均在应用内展示。兼容接收接口只监听本机 127.0.0.1。
只收图，不在电脑上执行推理。板卡程序与标定数据已在 SD 卡中。

联网方式：板卡 192.168.1.10，预览 TCP 6000，结果 TCP 6001。
优先沿用已有可用地址。自动配置只增加临时 /32 地址及到板卡的单条 /32 路由，
不更改 DHCP、DNS、默认网关或 Wi-Fi；仅在所选物理有线网卡上操作。
退出时删除本次添加的地址与路由。正常已有设置不被覆盖；未获管理员权限时仍可手动配网。
不要使用 J11（PL 网口）。当前镜头朝向与真实标定已匹配，不要随意移动两相机的相对位置。

复现范围：本版本针对已准备好的这块板卡与 SD 卡。换一台 Windows 电脑后仍需有线网卡驱动可用，
且其管理策略允许本地应用与必要的网卡配置。软件目前未经商业代码签名。
实际换机、临时地址添加与退出恢复的测试状态见随包“验证记录.json”。
''',encoding='utf-8-sig')
    manifest=dict(product='stereo_snapshot_desktop_11459',version='1.2.1',python=sys.version,pyinstaller=PyInstaller.__version__,
        original_common={n:hashlib.sha256((ROOT/'common'/n).read_bytes()).hexdigest() for n in ('viewer_server.py','snapshot_wire.py','viewer.html','viewer.js')},
        files={p.relative_to(release).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(release.rglob('*')) if p.is_file()},
        hardware_or_firmware_changed=False)
    (release/'构建清单.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print('RELEASE',release,flush=True)


if __name__=='__main__':main()
