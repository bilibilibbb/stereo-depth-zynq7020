"""Summarize same-clock host events; these are not pure PL execution cycles."""
import argparse,json,math

def percentile(values,q):
    values=sorted(values);x=(len(values)-1)*q;a=int(x);b=min(a+1,len(values)-1)
    return values[a]+(values[b]-values[a])*(x-a)

def summarize(rows):
    used=[];seen=set()
    for row in rows:
        if row.get('warmup',False):continue
        if row['id'] in seen:raise ValueError('Duplicate completed result identity')
        seen.add(row['id'])
        times=[row[k] for k in ('begin_ns','prepared_ns','submitted_ns','completed_ns','consumed_ns')]
        if any(type(t) is not int or t<0 for t in times) or times!=sorted(times):
            raise ValueError('Use monotonic integer nanoseconds from one clock per record')
        used.append(row)
    if not used:raise ValueError('No measured samples after explicit warmup exclusion')
    pairs={'prepare_ms':('begin_ns','prepared_ns'),'submission_ms':('prepared_ns','submitted_ns'),
           'submit_to_complete_ms':('submitted_ns','completed_ns'),
           'consume_ms':('completed_ns','consumed_ns'),'end_to_end_ms':('begin_ns','consumed_ns')}
    stats={}
    for name,(a,b) in pairs.items():
        values=[(r[b]-r[a])/1e6 for r in used]
        stats[name]={'samples':values,'median':percentile(values,.5),'p95_linear':percentile(values,.95)}
    complete=sorted(r['completed_ns'] for r in used)
    return {'samples':len(used),'stages':stats,
            'distinct_output_intervals_ms':[(b-a)/1e6 for a,b in zip(complete,complete[1:])],
            'scope':'Same-clock host observations; submission-to-completion includes actual software/queue/transfer wait.'}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('events_json');a=p.parse_args()
    with open(a.events_json,encoding='utf-8') as f:print(json.dumps(summarize(json.load(f)),indent=2))
