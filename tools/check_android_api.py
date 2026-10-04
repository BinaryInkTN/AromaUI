import os
import re
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
C_SRC = os.path.join(ROOT, 'src', 'backends', 'platforms', 'aroma_platform_android.c')
HEADER = os.path.join(ROOT, 'include', 'aroma_android.h')
IFACE = os.path.join(ROOT, 'src', 'backends', 'platforms', 'aroma_platform_interface.h')
HELPER = os.path.join(ROOT, 'tools', 'cli', 'templates', 'android', 'app', 'src', 'main', 'java', 'AromaHelper.java.tpl')
ACTIVITY = os.path.join(ROOT, 'tools', 'cli', 'templates', 'android', 'app', 'src', 'main', 'java', 'AromaActivity.java.tpl')
MANIFEST = os.path.join(ROOT, 'tools', 'cli', 'templates', 'android', 'app', 'src', 'main', 'AndroidManifest.xml')
PROJECT = os.path.join(ROOT, 'tools', 'cli', 'aroma_project.py')

errors = []

def check(cond, msg):
    if not cond:
        errors.append(msg)

c_src = open(C_SRC, encoding='utf-8', newline='').read()
header = open(HEADER, encoding='utf-8', newline='').read()
iface = open(IFACE, encoding='utf-8', newline='').read()
helper = open(HELPER, encoding='utf-8', newline='').read()
activity = open(ACTIVITY, encoding='utf-8', newline='').read()
manifest = open(MANIFEST, encoding='utf-8', newline='').read()
java_all = helper + activity

count = 0
for m in re.finditer(r'static inline \S+ (\w+)\(', header):
    fname = m.group(1)
    tail = header[m.end():m.end() + 1200]
    sm = re.search(r'platform->(\w+)\(', tail)
    if not sm:
        continue
    slot = sm.group(1)
    count += 1
    check(re.search(r'\b' + re.escape(slot) + r'\b', iface) is not None, 'slot missing in interface: ' + slot)
    check(re.search(r'\.' + re.escape(slot) + r'\s*=', c_src) is not None, 'table entry missing for slot: ' + slot)

for m in re.finditer(r'GetStaticMethodID\(env, helper, "(\w+)",', c_src):
    jname = m.group(1)
    check(re.search(r'\b' + re.escape(jname) + r'\s*\(', helper) is not None, 'java method missing: ' + jname)

for m in re.finditer(r'\{"(\w+)", "[^"]*", \(void \*\)(\w+)\}', c_src):
    jname, cfn = m.group(1), m.group(2)
    check(cfn in c_src, 'native fn missing: ' + cfn)
    check(re.search(r'native [^(]*\b' + re.escape(jname) + r'\s*\(', java_all) is not None, 'java native decl missing: ' + jname)

for need in ['AromaActivity', 'onRequestPermissionsResult', 'onActivityResult', 'REQ_PICK_IMAGE', 'takePersistableUriPermission', 'onNewIntent', 'REQ_PICK_CONTACT']:
    check(need in activity, 'AromaActivity missing: ' + need)
check('.AromaActivity' in manifest, 'manifest activity not updated')
for perm in ['BLUETOOTH_SCAN', 'BLUETOOTH_CONNECT', 'ACCESS_FINE_LOCATION', 'CAMERA', 'VIBRATE', 'POST_NOTIFICATIONS', 'WAKE_LOCK', 'ACCESS_NETWORK_STATE', 'READ_PHONE_STATE', 'INTERNET', 'RECORD_AUDIO', 'READ_CONTACTS', 'USE_BIOMETRIC', 'MODIFY_AUDIO_SETTINGS', 'SET_WALLPAPER', 'NFC']:
    check(perm in manifest, 'manifest missing permission: ' + perm)
check('bluetooth_le' in manifest, 'manifest missing ble feature')
check('android.hardware.nfc' in manifest, 'manifest missing nfc feature')
check('supportsPictureInPicture' in manifest, 'manifest missing pip flag')

diff = subprocess.run(['git', 'diff', '-U0', '--', C_SRC, HEADER, IFACE, HELPER, ACTIVITY, MANIFEST, PROJECT],
                      capture_output=True, text=True, cwd=ROOT).stdout
for line in diff.split('\n'):
    if not line.startswith('+') or line.startswith('+++'):
        continue
    body = line[1:]
    code = re.sub(r'"(?:[^"\\]|\\.)*"', '""', body)
    code = re.sub(r"'(?:[^'\\]|\\.)*'", "''", code)
    if '//' in code and 'http://' not in code and 'https://' not in code:
        check(False, 'added line comment: ' + body.strip()[:90])
    if '/*' in code:
        check(False, 'added block comment: ' + body.strip()[:90])
    if '<!--' in code:
        check(False, 'added xml comment: ' + body.strip()[:90])
    for kw in ['TODO', 'FIXME', 'XXX', 'HACK']:
        if kw in body:
            check(False, 'added marker %s: %s' % (kw, body.strip()[:90]))

if errors:
    print('ANDROID API CHECK FAILED:')
    for e in errors:
        print(' - ' + e)
    sys.exit(1)
print('android api check: OK (%d inline wrappers verified)' % count)
