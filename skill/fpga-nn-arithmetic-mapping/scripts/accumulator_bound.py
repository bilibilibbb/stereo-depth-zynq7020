"""Exact integer range for a sum of products plus bias, before requantization."""
import argparse
import json


def bound(a_min,a_max,b_min,b_max,terms,bias_min=0,bias_max=0):
    if any(type(v) is not int for v in (a_min,a_max,b_min,b_max,terms,bias_min,bias_max)):
        raise ValueError('Integer ranges required')
    if a_min>a_max or b_min>b_max or bias_min>bias_max or terms<1:
        raise ValueError('Invalid range or term count')
    corners=[a*b for a in (a_min,a_max) for b in (b_min,b_max)]
    low,high=min(corners)*terms+bias_min,max(corners)*terms+bias_max
    width=1
    while low < -(1<<(width-1)) or high > (1<<(width-1))-1:
        width+=1
    return {'minimum':low,'maximum':high,'signed_bits':width,
            'scope':'Independent operand range bound; add zero-point correction, bias and rescale stages separately.'}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for n in ('a-min','a-max','b-min','b-max','terms'):p.add_argument('--'+n,type=int,required=True)
    p.add_argument('--bias-min',type=int,default=0);p.add_argument('--bias-max',type=int,default=0)
    a=p.parse_args();print(json.dumps(bound(a.a_min,a.a_max,a.b_min,a.b_max,a.terms,a.bias_min,a.bias_max),indent=2))
