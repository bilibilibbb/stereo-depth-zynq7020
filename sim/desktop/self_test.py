"""Packaged offline checks. Explicit synthetic frames never enter the real UI."""
import asyncio, http.client, json, socket, struct, threading
from dataclasses import replace
import network
import viewer_server as viewer
from snapshot_wire import Header,PREVIEW,PAIR,PAIR_ACK,FROZEN_META,make_packet,decode_packet


def adapter(name,index,up=True,ips=()):
    return dict(id=name,name=name,index=index,up=up,physical=True,ips=list(ips))


def run():
    import app
    import render
    rate=app.FrameRate()
    for i in range(151):rate.add(str(i),10+i/30)
    rate.add('150',15.001)
    measured=rate.state(15)
    assert measured['unique_frames']==151 and measured['fps']==30 and measured['gap_max_ms']==33.33
    assert rate.state(21)['fps']==0
    assert [render.color(v) for v in (0,16,32,48,64)]==list(render.COLORS)
    a=adapter('wired',7,ips=[dict(address='192.168.1.100',dad='Preferred')])
    b=adapter('other',8)
    assert network.choose_adapter([a])==a
    assert network.choose_adapter([a,b]) is None
    assert network.choose_adapter([adapter('down',1,False)]) is None
    assert network.compatible_address(a)=='192.168.1.100'
    for bad in ('192.168.1.10','192.168.1.0','192.168.1.255','169.254.1.2','10.0.0.3'):
        assert network.compatible_address(adapter('x',1,ips=[dict(address=bad,dad='Preferred')])) is None
    assert network.compatible_address(adapter('x',1,ips=[dict(address='192.168.1.100',dad='Duplicate')])) is None
    assert network.available_host([a])=='192.168.1.101'
    assert network.available_host([a,adapter('b',8,ips=[dict(address='192.168.1.101')])]) is None
    n=320*240
    h=Header(PAIR,0,0,1,1,1,1,200,100,110,150,11459,320,240,5,1,0,0)
    payload=bytes([100,120,140])*n+bytes([12])*n+bytes([7])*n+struct.pack('<f',12.5)*n+FROZEN_META.pack(9,9,1,70,90,220)
    pair=make_packet(h,payload);preview=make_packet(replace(h,kind=PREVIEW,format=1,model_id=0,trigger_us=0,sgm_done_us=0,model_done_us=0),bytes([100,120,140])*n)
    photo,sgm_image,model_image=render.pair(decode_packet(pair),pair)
    assert photo.getpixel((0,0))==(100,120,140)
    assert sgm_image.getpixel((30,40))==render.color(12)
    assert model_image.getpixel((30,40))==render.color(12.5)
    masked=bytearray(payload);masked[3*n]=255;masked[4*n]=4
    masked_packet=make_packet(h,masked);masked_header=decode_packet(masked_packet)
    _,s,m=render.pair(masked_header,masked_packet,True)
    assert s.getpixel((0,0))==m.getpixel((0,0))==(60,71,81)
    _,s,m=render.pair(masked_header,masked_packet,False)
    assert s.getpixel((0,0))==(60,71,81) and m.getpixel((0,0))==render.color(12.5)
    grey=make_packet(replace(h,kind=PREVIEW,format=2,model_id=0,width=2,height=1,trigger_us=0,sgm_done_us=0,model_done_us=0),bytes((16,128,235,128)))
    image=render.preview(decode_packet(grey),grey)
    assert image.getpixel((0,0))==(255,255,255) and image.getpixel((1,0))==(0,0,0)
    bad=bytearray(pair);bad[100]^=1
    try:decode_packet(bad)
    except ValueError:pass
    else:raise AssertionError('CRC corruption accepted')
    engine=app.Engine(port=0)
    try:
        async def exercise():
            acked=asyncio.Event();connections={'preview':0,'result':0}
            async def preview_client(r,w):
                connections['preview']+=1
                w.write(preview);await w.drain();w.close();await w.wait_closed()
            async def pair_client(r,w):
                connections['result']+=1
                try:
                    # Fragmented TCP and exact retransmission use the actual strict parser.
                    for p in (pair,pair):
                        for i in range(0,len(p),7333):w.write(p[i:i+7333]);await w.drain()
                        ack=decode_packet(await asyncio.wait_for(r.readexactly(96),3))
                        assert ack.kind==PAIR_ACK and ack.identity==h.identity and ack.status==decode_packet(pair).crc
                    acked.set()
                finally:w.close();await w.wait_closed()
            ps=await asyncio.start_server(preview_client,'127.0.0.1',0)
            rs=await asyncio.start_server(pair_client,'127.0.0.1',0)
            try:
                await engine._target(dict(name='synthetic loopback',index=0),None,host='127.0.0.1',ports=(ps.sockets[0].getsockname()[1],rs.sockets[0].getsockname()[1]))
                await asyncio.wait_for(acked.wait(),6)
                assert engine.store.received['result']==1
                for _ in range(30):
                    if connections['preview']>=2:break
                    await asyncio.sleep(.1)
                assert connections['preview']>=2,'Reconnect not exercised'
                return connections
            finally:
                ps.close();rs.close();await ps.wait_closed();await rs.wait_closed()
        connections=asyncio.run_coroutine_threadsafe(exercise(),engine.loop).result(timeout=10)
        def get(path):
            connection=http.client.HTTPConnection('127.0.0.1',engine.port,timeout=2)
            try:
                connection.request('GET',path);response=connection.getresponse()
                assert response.status==200
                return response.read()
            finally:connection.close()
        state=json.loads(get('/api/state'))
        assert state['desktop']['product']==app.PRODUCT and state['pair']['flags']==1
        assert get('/api/pair')==pair
        html=get('/').decode('utf-8')
        assert 'rotate(180deg)' in html and 'PS_KEY1' in html
        return dict(status='PASS',network_selection='single/multiple/down/collision/DAD',
            actual_protocol='CRC rejection, fragmented delivery, full validated ACK, exact retransmission deduplication',
            synthetic_loopback_connections=connections,http='loopback only; original HTML/JS and exact full paired packet',
            native_display='fixed color anchors, model/SGM same scale, common/own mask distinction, RGB photo and YUYV range/orientation',
            new_board_operations=0,network_changes=0,visual_rendering_verified=False)
    finally:engine.close()


if __name__=='__main__':print(json.dumps(run(),ensure_ascii=False,indent=2))
