"""Check explicit tensor allocation/liveness, without claiming to optimize layout."""
import argparse
import json


def check(rows):
    seen=set()
    for row in rows:
        if row['name'] in seen:
            raise ValueError('Duplicate tensor name')
        seen.add(row['name'])
        for key in ('offset','bytes','birth','last_consumer'):
            if type(row[key]) is not int or row[key] < 0:
                raise ValueError('Use nonnegative integer offsets, sizes and event indexes')
        if row['bytes']==0 or row['last_consumer'] < row['birth']:
            raise ValueError('Invalid tensor interval')
    conflicts=[]
    for i,a in enumerate(rows):
        for b in rows[i+1:]:
            memory_overlap=max(a['offset'],b['offset']) < min(a['offset']+a['bytes'],b['offset']+b['bytes'])
            # Event bounds are inclusive: reuse at a last-reader event is unsafe.
            live_overlap=max(a['birth'],b['birth']) <= min(a['last_consumer'],b['last_consumer'])
            if memory_overlap and live_overlap:
                conflicts.append([a['name'],b['name']])
    events={r['birth'] for r in rows}|{r['last_consumer'] for r in rows}
    live_peak=max((sum(r['bytes'] for r in rows if r['birth']<=t<=r['last_consumer']) for t in events),default=0)
    return {'valid':not conflicts,'conflicts':conflicts,
            'allocation_bytes':max((r['offset']+r['bytes'] for r in rows),default=0),
            'peak_live_bytes':live_peak,
            'scope':'Serialized, inclusive event indexes; asynchronous consumers must extend lifetimes.'}


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('json_file');a=p.parse_args()
    with open(a.json_file,encoding='utf-8') as f: result=check(json.load(f))
    print(json.dumps(result,indent=2));raise SystemExit(0 if result['valid'] else 1)
