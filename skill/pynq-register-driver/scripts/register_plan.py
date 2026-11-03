"""Validate a register write plan without reading/writing any device."""

def plan(registers,assignments,start_name=None):
    names={};offsets=set()
    for r in registers:
        name=r['name'];offset=r['offset'];mask=r.get('write_mask',0xffffffff)
        if name in names or offset in offsets or type(offset) is not int or offset<0 or offset%4:
            raise ValueError('Duplicate register or invalid 32-bit word offset')
        if r['access'] not in ('ro','rw','wo','w1c','read-clear') or type(mask) is not int or not 0<=mask<=0xffffffff:
            raise ValueError('Invalid register semantics')
        names[name]=r;offsets.add(offset)
    if start_name is not None and start_name not in assignments:
        raise ValueError('Explicit start value required')
    output=[]
    order=[n for n in assignments if n!=start_name]+([start_name] if start_name is not None else [])
    for name in order:
        if name not in names:raise ValueError('Unknown register: '+name)
        r=names[name];value=assignments[name]
        if r['access'] not in ('rw','wo','w1c'):raise ValueError('Register cannot be written: '+name)
        if type(value) is not int or not 0<=value<=0xffffffff or value & ~r.get('write_mask',0xffffffff):
            raise ValueError('Value exceeds the writable bit mask: '+name)
        output.append({'name':name,'offset':r['offset'],'value':value,'access':r['access']})
    return output
