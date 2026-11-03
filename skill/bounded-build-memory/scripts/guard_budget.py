"""Calculate a conservative growth budget; this is NOT an OS process guard."""
import argparse
import json


def budget(available, commit_headroom, private, resident, page=4096):
    values = (available, commit_headroom, private, resident, page)
    if any(type(v) is not int for v in values) or min(values) < 0 or page == 0:
        raise ValueError('Use nonnegative integer bytes and a positive page size')
    used = max(private, resident)
    remaining = min(available, commit_headroom)
    if used >= remaining:
        return {'action': 'stop_owned_job_and_change_work', 'used_bytes': used,
                'remaining_bytes': remaining, 'additional_commit_bytes': 0}
    cap = (private + (remaining-used-1)//2)//page*page
    growth = max(0, cap-private)
    return {'action': 'continue_with_monitor' if growth else 'no_page_growth_margin',
            'used_bytes': used, 'remaining_bytes': remaining,
            'job_commit_cap_bytes': cap, 'additional_commit_bytes': growth}


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('available','commit-headroom','private','resident'):
        p.add_argument('--'+name,type=int,required=True)
    a=p.parse_args()
    print(json.dumps(budget(a.available,a.commit_headroom,a.private,a.resident),indent=2))
