#!/usr/bin/env python3
"""Give launchd jobs the environment that copies their os_log to stderr.

  sudo launchd-env.py MOUNTED_ROOT LABEL...

Adds OS_ACTIVITY_DT_MODE=YES (os_log mirrored to stderr, as under Xcode) and
CFLOG_FORCE_STDERR=1 to the EnvironmentVariables of each named job in
/System/Library/xpc/launchd.plist (binary plist, owner/mode kept). The jobs'
stdout/stderr stay /dev/null; boot with sweetnull=NAME,... (xnu 0048) to see
what they write there in the kernel log. Pointing a job at /dev/console
instead stops it from launching (see NOTES.md, image v8/v9).
"""
import os, plistlib, sys

ENV = {'OS_ACTIVITY_DT_MODE': 'YES', 'CFLOG_FORCE_STDERR': '1'}

root, labels = sys.argv[1], set(sys.argv[2:])
if not labels:
    sys.exit(__doc__)
path = os.path.join(root, 'System/Library/xpc/launchd.plist')
st = os.stat(path)
with open(path, 'rb') as f:
    plist = plistlib.load(f)
done = set()
for section in plist.values():
    if not isinstance(section, dict):
        continue
    for job in section.values():
        if isinstance(job, dict) and job.get('Label') in labels:
            job.setdefault('EnvironmentVariables', {}).update(ENV)
            done.add(job['Label'])
missing = labels - done
if missing:
    sys.exit(f'no such jobs: {", ".join(sorted(missing))}')
with open(path, 'wb') as f:
    plistlib.dump(plist, f, fmt=plistlib.FMT_BINARY)
os.chown(path, st.st_uid, st.st_gid)
os.chmod(path, st.st_mode & 0o7777)
print('os_log to stderr for:', ', '.join(sorted(done)))
