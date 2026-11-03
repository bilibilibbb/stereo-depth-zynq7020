"""Local PC receiver/display server. Standard library only; no PC inference."""
import argparse
import asyncio
from dataclasses import asdict,replace
from pathlib import Path
import json
import time
from urllib.parse import urlsplit,parse_qs

from snapshot_wire import PREVIEW,PAIR,STATUS,RGB888,read_packet,decode_packet,pair_ack,make_packet,frozen_source

HERE=Path(__file__).resolve().parent

class Store:
    def __init__(self):
        self.preview=None;self.pair=None;self.status=None
        self.links={'preview':False,'result':False}
        self.errors={'preview':'','result':''}
        self.last_received={'preview':0.0,'result':0.0}
        self.received={'preview':0,'result':0}

    def publish(self,h,packet,channel):
        # Called only after full framing, CRC and semantic validation.
        if (channel=='preview' and h.kind!=PREVIEW) or (channel=='result' and h.kind not in (PAIR,STATUS)):
            raise ValueError('message on wrong channel')
        if h.kind==STATUS:
            self.status=h;return False
        attr='preview' if h.kind==PREVIEW else 'pair'
        old=getattr(self,attr)
        if old and h.boot==old[0].boot:
            if h.identity==old[0].identity and h!=old[0]:
                raise ValueError('same identity changed contents or metadata')
            if h.shot<old[0].shot or (h.shot==old[0].shot and (h.epoch,h.attempt)<= (old[0].epoch,old[0].attempt)):
                return False
        setattr(self,attr,(h,packet))
        if h.kind==PAIR and self.status and self.status.identity==h.identity:
            self.status=None
        self.last_received[channel]=time.monotonic();self.received[channel]+=1
        return True

    def state(self):
        now=time.monotonic()
        return {'links':self.links,'errors':self.errors,'received':self.received,
                'ages_seconds':{k:None if not v else round(now-v,2) for k,v in self.last_received.items()},
                'preview':asdict(self.preview[0]) if self.preview else None,
                'pair':asdict(self.pair[0]) if self.pair else None,
                'source':frozen_source(*self.pair) if self.pair else None,
                'status':asdict(self.status) if self.status else None}

async def receiver(store,host,port,channel):
    while True:
        writer=None
        try:
            reader,writer=await asyncio.wait_for(asyncio.open_connection(host,port),5)
            store.links[channel]=True;store.errors[channel]=''
            while True:
                h,packet=await read_packet(reader)
                store.publish(h,packet,channel)
                if h.kind==PAIR and store.pair and store.pair[0]==h:
                    # Also acknowledge an identical retransmission after a
                    # lost ACK. This proves full validated PC storage only.
                    writer.write(pair_ack(h))
                    await asyncio.wait_for(writer.drain(),5)
        except asyncio.CancelledError:
            raise
        except (OSError,ValueError,EOFError,asyncio.TimeoutError,asyncio.IncompleteReadError) as exc:
            store.errors[channel]=type(exc).__name__
        finally:
            store.links[channel]=False
            if writer:
                writer.close()
                try:await writer.wait_closed()
                except OSError:pass
        await asyncio.sleep(1)

async def http_client(store,reader,writer):
    try:
        request=await asyncio.wait_for(reader.readuntil(b'\r\n\r\n'),3)
        if len(request)>8192:raise ValueError('request too large')
        method,target,version=request.split(b'\r\n',1)[0].decode('ascii').split(' ')
        if method!='GET' or version not in ('HTTP/1.0','HTTP/1.1'):raise ValueError('method')
        url=urlsplit(target);query=parse_qs(url.query);status='200 OK';ctype='application/octet-stream'
        if url.path in ('/','/viewer.js'):
            name='viewer.html' if url.path=='/' else 'viewer.js'
            body=(HERE/name).read_bytes();ctype='text/html; charset=utf-8' if name.endswith('html') else 'text/javascript; charset=utf-8'
        elif url.path=='/api/state':
            body=json.dumps(store.state(),ensure_ascii=False).encode('utf-8');ctype='application/json; charset=utf-8'
        elif url.path in ('/api/preview','/api/pair'):
            item=store.preview if url.path.endswith('preview') else store.pair
            if not item or query.get('after',[''])[0]==item[0].key:
                body=b'';status='204 No Content'
            else:body=item[1]
        else:body=b'Not found';status='404 Not Found';ctype='text/plain'
        writer.write((f'HTTP/1.1 {status}\r\nContent-Type: {ctype}\r\nContent-Length: {len(body)}\r\n'
                      'Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n').encode('ascii'))
        writer.write(body);await asyncio.wait_for(writer.drain(),5)
    except (OSError,ValueError,UnicodeError,asyncio.TimeoutError,asyncio.LimitOverrunError,asyncio.IncompleteReadError):
        pass
    finally:
        writer.close()
        try:await writer.wait_closed()
        except OSError:pass

async def serve(args):
    store=Store();tasks=[]
    if args.fixture:
        packet=Path(args.fixture).read_bytes();h=decode_packet(packet)
        if h.kind!=PAIR or not h.flags&1:raise ValueError('fixture must be explicitly synthetic PAIR')
        store.publish(h,packet,'result')
        preview=make_packet(replace(h,kind=PREVIEW,format=RGB888,model_id=0,
                                    trigger_us=0,sgm_done_us=0,model_done_us=0),packet[96:96+h.width*h.height*3])
        store.publish(decode_packet(preview),preview,'preview')
    if args.board:
        for channel,port in (('preview',args.preview_port),('result',args.result_port)):
            tasks.append(asyncio.create_task(receiver(store,args.board,port,channel)))
    server=await asyncio.start_server(lambda r,w:http_client(store,r,w),'127.0.0.1',args.port,limit=8192)
    print(f'Viewer: http://127.0.0.1:{server.sockets[0].getsockname()[1]}',flush=True)
    try:
        async with server:
            if args.seconds:
                await asyncio.sleep(args.seconds)
            else:await server.serve_forever()
    finally:
        for task in tasks:task.cancel()
        await asyncio.gather(*tasks,return_exceptions=True)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--board',help='Explicit board IP/hostname; no network probe if omitted')
    p.add_argument('--preview-port',type=int,default=6000);p.add_argument('--result-port',type=int,default=6001)
    p.add_argument('--port',type=int,default=8765);p.add_argument('--fixture',help='Synthetic protocol test fixture only')
    p.add_argument('--seconds',type=float,default=0,help='Optional bounded UI inspection lifetime')
    args=p.parse_args()
    if not args.board and not args.fixture:p.error('choose --board or explicitly synthetic --fixture')
    asyncio.run(serve(args))
if __name__=='__main__':main()
