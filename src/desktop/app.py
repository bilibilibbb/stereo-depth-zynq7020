"""Portable Windows launcher; unchanged SDP1 validation and display, no inference."""
from pathlib import Path
import argparse, asyncio, ctypes, json, os, queue, socket, sys, threading, time
from collections import deque

if not getattr(sys,'frozen',False):sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'common'))
import viewer_server as viewer
from snapshot_wire import PAIR, read_packet, pair_ack
import network
import render

PRODUCT='stereo_snapshot_desktop_11459'
VERSION='1.2.1'


class FrameRate:
    """Bounded recent UNIQUE completed frames; no repaint count or interpolation."""
    def __init__(self):
        self.times=deque(maxlen=240);self.total=0;self.key=None
    def add(self,key,now=None):
        if key==self.key:return
        self.key=key;self.total+=1;self.times.append(time.monotonic() if now is None else now)
    def state(self,now=None):
        now=time.monotonic() if now is None else now
        times=[t for t in self.times if now-t<=5]
        gaps=[(b-a)*1000 for a,b in zip(times,times[1:])]
        ordered=sorted(gaps)
        fps=(len(times)-1)/(times[-1]-times[0]) if len(times)>1 and times[-1]>times[0] else 0
        return dict(unique_frames=self.total,fps=round(fps,2),samples=len(times),
            gap_p95_ms=round(ordered[min(len(ordered)-1,int(.95*len(ordered)))],2) if ordered else None,
            gap_max_ms=round(max(gaps),2) if gaps else None,
            age_ms=round((now-self.times[-1])*1000,1) if self.times else None)


class DesktopStore(viewer.Store):
    def __init__(self):
        super().__init__();self.connection={'adapter':None,'local_ip':None};self.preview_rate=FrameRate()
    def publish(self,h,packet,channel):
        accepted=super().publish(h,packet,channel)
        if accepted and channel=='preview':self.preview_rate.add(h.key)
        return accepted
    def state(self):
        return dict(super().state(),preview_rate=self.preview_rate.state(),desktop=dict(product=PRODUCT,version=VERSION,**self.connection))


async def receive(store,host,port,channel,local_ip,index):
    while True:
        writer=None;sock=None
        try:
            sock=socket.socket(socket.AF_INET,socket.SOCK_STREAM);sock.setblocking(False)
            if local_ip:sock.bind((local_ip,0))
            if index:
                # Windows IP_UNICAST_IF takes the interface index in network order.
                sock.setsockopt(socket.IPPROTO_IP,31,socket.htonl(index))
            await asyncio.wait_for(asyncio.get_running_loop().sock_connect(sock,(host,port)),5)
            reader,writer=await asyncio.open_connection(sock=sock);sock=None
            store.links[channel]=True;store.errors[channel]=''
            while True:
                packet=read_packet(reader)
                h,data=await asyncio.wait_for(packet,12) if channel=='preview' else await packet
                store.publish(h,data,channel)
                if h.kind==PAIR and store.pair and store.pair[0]==h:
                    writer.write(pair_ack(h));await asyncio.wait_for(writer.drain(),5)
        except asyncio.CancelledError:raise
        except (OSError,ValueError,EOFError,asyncio.TimeoutError,asyncio.IncompleteReadError) as exc:
            store.errors[channel]=type(exc).__name__
        finally:
            store.links[channel]=False
            if sock:sock.close()
            if writer:
                writer.close()
                try:await writer.wait_closed()
                except OSError:pass
        await asyncio.sleep(1)


class Engine:
    def __init__(self,port=8765):
        self.store=DesktopStore();self.loop=asyncio.new_event_loop();self.tasks=[]
        self.port=port;self.ready=threading.Event();self.error=None;self.server=None
        self.thread=threading.Thread(target=self.run,daemon=True);self.thread.start()
        if not self.ready.wait(5):raise RuntimeError('本机接收服务启动超时')
        if self.error:raise self.error
    def run(self):
        asyncio.set_event_loop(self.loop)
        async def start():
            viewer.HERE=network.resources()/'assets'
            for p in range(self.port,self.port+16):
                try:
                    self.server=await asyncio.start_server(lambda r,w:viewer.http_client(self.store,r,w),'127.0.0.1',p,limit=8192)
                    self.port=self.server.sockets[0].getsockname()[1];return
                except OSError as e:
                    if getattr(e,'winerror',None)!=10048 and e.errno!=10048:raise
            raise RuntimeError('本机展示端口被占用')
        try:self.loop.run_until_complete(start())
        except Exception as exc:self.error=exc
        finally:self.ready.set()
        if not self.error:self.loop.run_forever()
        self.loop.close()
    async def _target(self,adapter,local_ip,host=network.BOARD,ports=(6000,6001)):
        for task in self.tasks:task.cancel()
        await asyncio.gather(*self.tasks,return_exceptions=True)
        self.store.connection=dict(adapter=adapter['name'],local_ip=local_ip)
        self.tasks=[asyncio.create_task(receive(self.store,host,port,channel,local_ip,adapter['index']))
            for port,channel in zip(ports,('preview','result'))]
    def target(self,adapter,local_ip,**kwargs):
        return asyncio.run_coroutine_threadsafe(self._target(adapter,local_ip,**kwargs),self.loop)
    def state(self):
        async def get():return self.store.state()
        return asyncio.run_coroutine_threadsafe(get(),self.loop).result(timeout=2)
    def frames(self):
        async def get():return self.store.preview,self.store.pair
        return asyncio.run_coroutine_threadsafe(get(),self.loop).result(timeout=2)
    def snapshot(self):
        async def get():return self.store.state(),self.store.preview,self.store.pair
        return asyncio.run_coroutine_threadsafe(get(),self.loop).result(timeout=2)
    @property
    def url(self):return 'http://127.0.0.1:%d/'%self.port
    def close(self):
        async def stop():
            for t in self.tasks:t.cancel()
            await asyncio.gather(*self.tasks,return_exceptions=True)
            if self.server:self.server.close();await self.server.wait_closed()
        try:asyncio.run_coroutine_threadsafe(stop(),self.loop).result(timeout=3)
        finally:self.loop.call_soon_threadsafe(self.loop.stop);self.thread.join(timeout=3)


class Application:
    def __init__(self,args):
        import tkinter as tk
        from tkinter import ttk
        self.tk=tk;self.args=args;self.events=queue.Queue();self.adapters=[];self.busy=False
        self.lease=None;self.closed=False;self.selected=None
        self.last_preview=None;self.last_pair=None;self.pair_item=None;self.images={};self.photos={};self.canvases={}
        self.pair_generation=0;self.pair_job=None;self.pair_wake=threading.Event()
        self.root=tk.Tk();self.root.title('双目拍照展示');self.root.geometry('1120x740');self.root.minsize(960,650)
        self.root.configure(bg='#edf2f5');style=ttk.Style();style.theme_use('clam')
        style.configure('TFrame',background='#edf2f5');style.configure('TLabel',background='#edf2f5',font=('Microsoft YaHei UI',10))
        style.configure('Title.TLabel',font=('Microsoft YaHei UI',18,'bold'),foreground='#183044')
        style.configure('TButton',padding=8,font=('Microsoft YaHei UI',10))
        frame=ttk.Frame(self.root,padding=24);frame.pack(fill='both',expand=True)
        ttk.Label(frame,text='按 PS_KEY1 拍照',style='Title.TLabel').pack(anchor='w',pady=(0,12))
        self.status=tk.StringVar(value='正在检查有线连接…')
        self.status_label=ttk.Label(frame,textvariable=self.status,wraplength=1000)
        self.status_label.pack(anchor='w',pady=(0,10))
        row=ttk.Frame(frame);row.pack(fill='x',pady=(0,8))
        self.network_row=row
        self.combo=ttk.Combobox(row,state='readonly',width=34);self.combo.pack(side='left',fill='x',expand=True)
        ttk.Button(row,text='重新检测',command=self.scan).pack(side='left',padx=6)
        ttk.Button(row,text='连接所选网卡',command=self.connect_selected).pack(side='left')
        ttk.Button(row,text='退出',command=self.close).pack(side='left',padx=(6,0))
        content=ttk.Frame(frame);content.pack(fill='both',expand=True)
        content.columnconfigure(0,weight=1);content.columnconfigure(1,weight=2)
        content.rowconfigure(0,weight=1)
        left=ttk.Frame(content,padding=(0,0,16,0));left.grid(row=0,column=0,sticky='nsew')
        right=ttk.Frame(content);right.grid(row=0,column=1,sticky='nsew')
        ttk.Label(left,text='视频流显示').pack(anchor='w',pady=(0,6))
        self.make_canvas(left,'preview',320,240).pack(fill='both',expand=True)
        ttk.Label(left,text='拍照图像').pack(anchor='w',pady=(10,6))
        self.make_canvas(left,'photo',220,145).pack(fill='both',expand=True)
        self.result_title=tk.StringVar(value='拍照结果')
        ttk.Label(right,textvariable=self.result_title).pack(anchor='w',pady=(0,6))
        results=ttk.Frame(right);results.pack(fill='both',expand=True)
        results.columnconfigure(0,weight=1);results.columnconfigure(1,weight=1);results.rowconfigure(1,weight=1)
        ttk.Label(results,text='纯 SGM').grid(row=0,column=0,sticky='w',pady=(0,6))
        ttk.Label(results,text='小模型处理后').grid(row=0,column=1,sticky='w',padx=(8,0),pady=(0,6))
        self.make_canvas(results,'sgm',300,225).grid(row=1,column=0,sticky='nsew',padx=(0,4))
        self.make_canvas(results,'model',300,225).grid(row=1,column=1,sticky='nsew',padx=(4,0))
        self.make_canvas(right,'legend',512,12).pack(fill='x',pady=(14,4))
        self.images['legend']=render.legend()
        self.engine=Engine(port=args.port)
        self.pair_worker=threading.Thread(target=self.render_pairs,daemon=True);self.pair_worker.start()
        self.root.protocol('WM_DELETE_WINDOW',self.close);self.root.after(100,self.poll);self.root.after(200,self.scan)
        self.root.mainloop()
    def background(self,fn):
        if self.busy:return
        self.busy=True
        def work():
            try:fn()
            except Exception as exc:self.events.put(('error',str(exc)))
            finally:self.events.put(('idle',None))
        threading.Thread(target=work,daemon=True).start()
    def scan(self):
        def work():self.events.put(('adapters',network.inspect_adapters()))
        self.background(work)
    def connect_selected(self):
        i=self.combo.current()
        if 0<=i<len(self.adapters):self.connect(self.adapters[i])
    def connect(self,adapter):
        if not adapter['up']:
            self.status.set('网卡尚未接线。请连接板卡 J23 后重新检测。');return
        local=network.compatible_address(adapter)
        if local:
            self.selected=adapter;self.engine.target(adapter,local)
            self.status.set('正在连接板卡…')
            return
        if self.lease and self.lease.running():
            self.status.set('临时网络配置已在使用；切换网卡前请退出并重新打开应用。');return
        host=network.available_host(self.adapters)
        if not host:
            self.status.set('两个候选地址均已被本机使用，请检查有线网卡配置。');return
        self.status.set('需要添加临时直连地址，请确认 Windows 管理员权限提示。')
        def work():
            self.lease=network.ElevatedLease(adapter,host)
            for _ in range(24):
                if self.closed:return
                if not self.lease.running():raise RuntimeError('配网未完成，请根据提示检查连接后重试。')
                current=next((a for a in network.inspect_adapters() if a['id']==adapter['id']),None)
                if current and network.compatible_address(current)==host:
                    # Address DAD precedes the host route in the helper.
                    time.sleep(.5);self.events.put(('configured',current));return
                time.sleep(.5)
            raise RuntimeError('等待配网完成超时，请退出应用后重新连接。')
        self.background(work)
    def make_canvas(self,parent,name,width,height):
        canvas=self.tk.Canvas(parent,width=width,height=height,bg='#10202d',highlightthickness=0)
        self.canvases[name]=canvas
        canvas.bind('<Configure>',lambda e,n=name:self.draw(n))
        return canvas
    def draw(self,name):
        from PIL import Image,ImageTk
        canvas=self.canvases[name];canvas.delete('all')
        if name not in self.images:
            canvas.create_text(max(1,canvas.winfo_width())//2,max(1,canvas.winfo_height())//2,text='等待图像',fill='#99adbb');return
        image=self.images[name].copy();size=(max(1,canvas.winfo_width()),max(1,canvas.winfo_height()))
        if name=='legend':image=image.resize(size)
        else:image.thumbnail(size,Image.Resampling.BILINEAR)
        self.photos[name]=ImageTk.PhotoImage(image,master=self.root)
        canvas.create_image(size[0]//2,size[1]//2,image=self.photos[name],anchor='center')
    def redraw_pair(self):
        if not self.pair_item:return
        self.pair_generation+=1
        # Keep only the newest render request. Colorizing a photo result
        # must not stall the main-thread ordinary video/Tk event loop.
        self.pair_job=(self.pair_generation,self.pair_item,True)
        self.pair_wake.set()
    def render_pairs(self):
        while True:
            self.pair_wake.wait();self.pair_wake.clear()
            if self.closed:return
            job=self.pair_job
            if not job:continue
            generation,(h,packet),shared=job
            try:images=render.pair(h,packet,shared)
            except Exception as exc:self.events.put(('error',str(exc)));continue
            if not self.closed:self.events.put(('pair_images',(generation,images)))
    def poll(self):
        if self.closed:return
        tick=time.monotonic()
        while True:
            try:kind,value=self.events.get_nowait()
            except queue.Empty:break
            if kind=='idle':self.busy=False
            elif kind=='error':self.status.set(value)
            elif kind=='adapters':
                self.adapters=value
                self.combo['values']=[a['name']+('（已接线）' if a['up'] else '（未接线）') for a in value]
                chosen=network.choose_adapter(value)
                if chosen:
                    self.combo.current(value.index(chosen));self.root.after(100,lambda a=chosen:self.connect(a))
                elif value:
                    self.combo.current(0);self.status.set('请选择连接板卡的有线网卡，再点击“连接所选网卡”。')
                else:self.status.set('未找到物理有线网卡。可连接 USB 转以太网适配器后重新检测。')
            elif kind=='configured':self.connect(value)
            elif kind=='pair_images':
                generation,images=value
                if generation==self.pair_generation:
                    for name,img in zip(('photo','sgm','model'),images):self.images[name]=img
                    for name in ('photo','sgm','model'):self.draw(name)
        try:
            state,preview,pair=self.engine.snapshot()
            if preview and preview[0].key!=self.last_preview:
                self.images['preview']=render.preview(*preview);self.draw('preview');self.last_preview=preview[0].key
            if pair and pair[0].key!=self.last_pair:
                self.pair_item=pair;self.redraw_pair();self.last_pair=pair[0].key
                self.result_title.set('合成数据测试' if pair[0].flags&1 else '拍照结果')
            if state['received']['preview'] and state['links']['preview'] and state['links']['result']:
                self.status.set('')
                if preview and pair and preview[0].boot!=pair[0].boot:
                    self.status.set('板卡已重启，当前保留上一启动的照片；按 PS_KEY1 更新结果。')
            elif self.selected and not self.busy and not state['links']['preview']:
                self.status.set('等待板卡连接。请确认电源、SD 卡与 J23 网口；接收程序会自动重连。')
        except Exception as exc:self.status.set(str(exc))
        if self.status.get():self.status_label.pack(before=self.network_row,anchor='w',pady=(0,10))
        else:self.status_label.pack_forget()
        # Inspect the newest complete frame promptly. Unchanged images are not
        # reconverted, so this removes the former 2-FPS UI cap without a queue.
        # A 16-ms deadline leaves headroom for 30 distinct camera frames/s.
        # Render cost belongs inside the period, not on top of a fixed sleep.
        self.root.after(max(1,16-int((time.monotonic()-tick)*1000)),self.poll)
    def close(self):
        self.closed=True;self.pair_wake.set()
        self.engine.close()
        if self.lease:self.lease.close_handle()
        self.root.destroy()


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--no-browser',action='store_true');parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--inspect-network',metavar='JSON');parser.add_argument('--self-test',metavar='JSON')
    parser.add_argument('--headless-seconds',type=float,default=0);parser.add_argument('--report')
    args=parser.parse_args()
    if args.inspect_network:
        Path(args.inspect_network).write_text(json.dumps(network.inspect_adapters(),ensure_ascii=False,indent=2),encoding='utf-8');return 0
    if args.self_test:
        import self_test
        result=self_test.run();Path(args.self_test).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');return 0
    if args.headless_seconds:
        adapters=network.inspect_adapters();adapter=network.choose_adapter(adapters)
        if not adapter or not network.compatible_address(adapter):raise RuntimeError('Headless check requires existing unambiguous network; no configuration is permitted.')
        engine=Engine(args.port)
        try:
            engine.target(adapter,network.compatible_address(adapter)).result(timeout=3)
            time.sleep(args.headless_seconds);result=engine.state()
            if args.report:Path(args.report).write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
            if not result['received']['preview']:raise RuntimeError('No validated real preview received')
        finally:engine.close()
        return 0
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateMutexW.argtypes=[ctypes.c_void_p,ctypes.c_bool,ctypes.c_wchar_p]
    kernel.CreateMutexW.restype=ctypes.c_void_p;kernel.CloseHandle.argtypes=[ctypes.c_void_p]
    mutex=kernel.CreateMutexW(None,False,'Local\\StereoSnapshotDesktop11459')
    if not mutex:raise ctypes.WinError(ctypes.get_last_error())
    try:
        if ctypes.get_last_error()==183:
            ctypes.windll.user32.MessageBoxW(None,'接收应用已经运行，请使用现有的启动窗口和展示页面。','双目拍照展示',0)
            return 0
        Application(args);return 0
    finally:kernel.CloseHandle(mutex)


if __name__=='__main__':
    try:raise SystemExit(main())
    except Exception as exc:
        check_flags=('--self-test','--inspect-network','--headless-seconds')
        if any(flag in sys.argv for flag in check_flags):
            import traceback
            for flag in ('--self-test','--inspect-network','--report'):
                if flag in sys.argv:
                    Path(sys.argv[sys.argv.index(flag)+1]).write_text(json.dumps(dict(status='ERROR',error=str(exc),traceback=traceback.format_exc()),ensure_ascii=False),encoding='utf-8')
                    break
            raise SystemExit(2)
        elif getattr(sys,'frozen',False):
            ctypes.windll.user32.MessageBoxW(None,str(exc),'双目拍照展示',0x10)
            raise SystemExit(2)
        else:raise
