"""Wait for the unchanged project-wide mutex without starting heavy work."""
from contextlib import contextmanager,ExitStack
import time
from run_guarded import single_worker

@contextmanager
def queued_worker():
    with ExitStack() as stack:
        reported=False
        while True:
            try:
                stack.enter_context(single_worker())
                break
            except RuntimeError as exc:
                if str(exc)!='Another guarded task is active; run tasks serially.':raise
                if not reported:
                    print('WAITING_FOR_PROJECT_MUTEX: no heavy child launched',flush=True)
                    reported=True
                time.sleep(1)
        if reported:print('PROJECT_MUTEX_ACQUIRED: checking current available memory',flush=True)
        yield
