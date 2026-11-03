"""Small INT16 tensor conformance helpers; no PYNQ or NumPy dependency."""
import struct

def reorder_i16(payload,channels,height,width,source_layout,target_layout,byteorder='<'):
    if any(type(n) is not int or n<=0 for n in (channels,height,width)):
        raise ValueError('Positive fixed C/H/W required')
    if source_layout not in ('CHW','HWC') or target_layout not in ('CHW','HWC') or byteorder not in ('<','>'):
        raise ValueError('Supported layouts: CHW/HWC; explicit byte order required')
    count=channels*height*width
    if len(payload)!=count*2:raise ValueError('Payload size does not match INT16 tensor')
    result=bytearray(len(payload))
    def index(c,h,w,layout):
        return (c*height+h)*width+w if layout=='CHW' else (h*width+w)*channels+c
    for c in range(channels):
        for h in range(height):
            for w in range(width):
                a=index(c,h,w,source_layout)*2;b=index(c,h,w,target_layout)*2
                result[b:b+2]=payload[a:a+2]
    return bytes(result)

def compare_i16(payload,expected,byteorder='<',max_examples=8):
    if byteorder not in ('<','>') or len(payload)%2 or len(payload)//2!=len(expected):
        raise ValueError('Explicit byte order and equal INT16 lengths required')
    if type(max_examples) is not int or max_examples<0:raise ValueError('Nonnegative example limit required')
    mismatches=0;examples=[];max_error=0
    for i,((actual,),reference) in enumerate(zip(struct.iter_unpack(byteorder+'h',payload),expected)):
        if type(reference) is not int or not -32768<=reference<=32767:raise ValueError('INT16 references required')
        if actual!=reference:
            mismatches+=1;max_error=max(max_error,abs(actual-reference))
            if len(examples)<max_examples:examples.append({'index':i,'actual':actual,'expected':reference})
    return {'values':len(expected),'mismatches':mismatches,'max_integer_error':max_error,'examples':examples}
