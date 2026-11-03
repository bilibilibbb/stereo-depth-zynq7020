"""Native display only: fixed disparity colors and original validity masks."""
import math,struct
from PIL import Image
from snapshot_wire import RGB888,YUYV,frozen_source

COLORS=((20,45,140),(0,184,225),(167,224,68),(255,240,134),(214,54,54))

def color(value):
    t=max(0,min(4,value/16));i=min(3,int(t));f=t-i
    return tuple(int(math.floor(a+(b-a)*f+.5)) for a,b in zip(COLORS[i],COLORS[i+1]))

def preview(h,packet):
    raw=packet[96:]
    if h.format==RGB888:image=Image.frombytes('RGB',(h.width,h.height),raw)
    elif h.format==YUYV:
        # Sensor YUYV uses studio-range BT.601, as the existing web preview.
        y=Image.frombytes('L',(h.width,h.height),raw[0::2])
        u=Image.frombytes('L',(h.width//2,h.height),raw[1::4]).resize((h.width,h.height),Image.Resampling.NEAREST)
        v=Image.frombytes('L',(h.width//2,h.height),raw[3::4]).resize((h.width,h.height),Image.Resampling.NEAREST)
        y=y.point([max(0,min(255,round((x-16)*255/219))) for x in range(256)])
        uv=[max(0,min(255,round((x-128)*255/224+128))) for x in range(256)]
        image=Image.merge('YCbCr',(y,u.point(uv),v.point(uv))).convert('RGB')
    else:raise ValueError('preview format')
    return image.transpose(Image.Transpose.ROTATE_180)

def pair(h,packet,shared=True):
    n=h.width*h.height;raw=memoryview(packet)
    photo=Image.frombytes('RGB',(h.width,h.height),bytes(raw[96:96+3*n]))
    disparities=raw[96+3*n:96+4*n];valid=raw[96+4*n:96+5*n]
    model=struct.unpack_from('<%df'%n,raw,96+5*n)
    first=bytearray(n*3);second=bytearray(n*3);palette=[color(i) for i in range(64)]
    for i,(d,v,m) in enumerate(zip(disparities,valid,model)):
        sg=bool(v&2) if h.format>=4 else bool(v)
        md=bool(v&4) if h.format>=4 else True
        if shared:sg=md=sg and md
        invalid=(90,100,109) if ((i//h.width//8+i%h.width//8)%2) else (60,71,81)
        first[3*i:3*i+3]=bytes(palette[d] if sg else invalid)
        second[3*i:3*i+3]=bytes(color(m) if md else invalid)
    return photo,Image.frombytes('RGB',(h.width,h.height),bytes(first)),Image.frombytes('RGB',(h.width,h.height),bytes(second))

def legend(width=512,height=12):
    row=bytes(c for x in range(width) for c in color(64*x/(width-1)))
    return Image.frombytes('RGB',(width,height),row*height)
