"""Render explicit numeric HDL values and check what Vivado actually bound.

Vivado 2018.3 interpreted the unsized decimal BUILD_ID=3743439402 as a string
and truncated it to ASCII '9402'. Checking the command alone missed that defect.
"""
import re

def numeric_arguments(values):
    for name,value in values.items():
        if not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9]*',name) or not isinstance(value,int) or not 0<=value<=0xffffffff:
            raise ValueError('Expected unsigned32 numeric parameter')
    return ' '.join(f"{name}=32'h{value:08x}" for name,value in values.items())

def verify_elaborated_generics(log,top,expected):
    marker=re.search(r"synthesizing module '"+re.escape(top)+r"'(?:\s|\[)",log)
    if not marker:raise ValueError('Actual top elaboration not found: '+top)
    section=log[marker.end():]
    following=re.search(r"(?:synthesizing module '|done synthesizing module ')",section)
    if following:section=section[:following.start()]
    found={}
    for name,raw in re.findall(r'Parameter\s+(\w+)\s+bound to:\s*([^\r\n]+)',section):
        if name not in expected:continue
        raw=raw.strip()
        if 'type: string' in raw:raise ValueError('Numeric parameter became a string: '+name+' '+raw)
        bits=re.fullmatch(r"(\d+)'([bhd])([0-9a-fA-F]+)\s*",raw)
        integer=re.fullmatch(r'(-?\d+)\s*- type: integer\s*',raw)
        if bits:
            width=int(bits[1]);value=int(bits[3],{'b':2,'h':16,'d':10}[bits[2]])
            if width>32 or value>0xffffffff:raise ValueError('Unexpected parameter width/value: '+raw)
        elif integer:value=int(integer[1])&0xffffffff
        else:raise ValueError('Unknown numeric elaboration: '+raw)
        if name in found:raise ValueError('Duplicate parameter: '+name)
        found[name]=value
    if found!=expected:raise ValueError('Actual parameter binding mismatch: '+repr(found)+' expected '+repr(expected))
    return found
