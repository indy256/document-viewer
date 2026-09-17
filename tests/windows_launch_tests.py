import itertools
import argparse
import ctypes, ctypes.wintypes as w, json, os, subprocess, sys, tempfile, time
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument("--cross", action="store_true")
parser.add_argument("--no-foreground", action="store_true",
                    help="Skip desktop focus checks on noninteractive runners; still check window restoration")
parser.add_argument("executables", nargs="+", type=Path)
args = parser.parse_args()
cross = args.cross
check_foreground = not args.no_foreground
sys.stdout.reconfigure(encoding="utf-8")
executables = [path.resolve() for path in args.executables]
assert executables and all(path.is_file() for path in executables)
fixtures = tempfile.TemporaryDirectory(prefix="dv-launch-tests-")
work = Path(fixtures.name)
def pdf(path):
    objects=[b'<< /Type /Catalog /Pages 2 0 R >>',b'<< /Type /Pages /Kids [3 0 R] /Count 1 >>',b'<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 400] >>']
    data=b'%PDF-1.4\n'; offsets=[0]
    for i,obj in enumerate(objects,1):
        offsets.append(len(data));data+=f'{i} 0 obj\n'.encode()+obj+b'\nendobj\n'
    pos=len(data); data+=b'xref\n0 4\n0000000000 65535 f \n'
    data+=b''.join(f'{o:010d} 00000 n \n'.encode() for o in offsets[1:])
    data+=f'trailer\n<< /Size 4 /Root 1 0 R >>\nstartxref\n{pos}\n%%EOF\n'.encode();path.write_bytes(data)
second_name = 'second book \u041a\u043d\u0438\u0433\u0430.pdf'
for name in ['first.pdf', second_name]: pdf(work/name)
u=ctypes.windll.user32
cb=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
u.GetForegroundWindow.restype = w.HWND
u.FindWindowW.argtypes = [w.LPCWSTR, w.LPCWSTR]
u.FindWindowW.restype = w.HWND
u.ShowWindow.argtypes = [w.HWND, ctypes.c_int]
u.IsIconic.argtypes = [w.HWND]
u.SetForegroundWindow.argtypes = [w.HWND]
u.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                            ctypes.c_int, ctypes.c_int, ctypes.c_int, ctypes.c_int,
                            w.HWND, w.HMENU, w.HINSTANCE, w.LPVOID]
u.CreateWindowExW.restype = w.HWND
u.DestroyWindow.argtypes = [w.HWND]
u.PeekMessageW.argtypes = [ctypes.POINTER(w.MSG), w.HWND, w.UINT, w.UINT, w.UINT]
u.TranslateMessage.argtypes = [ctypes.POINTER(w.MSG)]
u.DispatchMessageW.argtypes = [ctypes.POINTER(w.MSG)]
u.DispatchMessageW.restype = w.LPARAM

def pump_messages():
    message = w.MSG()
    while u.PeekMessageW(ctypes.byref(message), None, 0, 0, 1):
        u.TranslateMessage(ctypes.byref(message))
        u.DispatchMessageW(ctypes.byref(message))
def windows(close=False):
    found=[]
    @cb
    def each(hwnd,_):
        text=ctypes.create_unicode_buffer(512);u.GetWindowTextW(hwnd,text,512)
        if 'Document Viewer' in text.value or 'Unable to open document' in text.value:
            found.append(text.value)
            if close:u.PostMessageW(hwnd,0x0010,0,0)
        return True
    u.EnumWindows(each,0)
    return found
session=Path(os.environ['LOCALAPPDATA'])/'Document Viewer'/'session.json'
backup=session.read_bytes() if session.exists() else None
assert not windows(),windows()
processes = []
cover = None

def background_viewer(minimized=False):
    hwnd = u.FindWindowW(None, windows()[0])
    assert hwnd
    if minimized:
        u.ShowWindow(hwnd, 6) # SW_MINIMIZE
        assert u.IsIconic(hwnd)
    if not check_foreground:
        return hwnd
    pump_messages()
    # Simulate the user switching to the launching application. Windows grants
    # foreground permission to the process that supplied the last input event.
    u.keybd_event(0x12, 0, 0, 0) # VK_MENU (Alt)
    try:
        assert u.SetForegroundWindow(cover), "Could not foreground the test window"
    finally:
        u.keybd_event(0x12, 0, 2, 0) # KEYEVENTF_KEYUP
    wait_for_foreground(cover)
    return hwnd

def wait_for_foreground(hwnd):
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        pump_messages()
        if not u.IsIconic(hwnd) and (not check_foreground or u.GetForegroundWindow() == hwnd):
            return
        time.sleep(0.05)
    raise AssertionError("File open did not restore the existing window" +
                         (" and bring it to the foreground" if check_foreground else ""))
def wait_for_documents(expected):
    deadline = time.monotonic() + 20
    state = {}
    while time.monotonic() < deadline:
        if session.exists():
            state = json.loads(session.read_text(encoding="utf-8"))
            actual = [Path(item['path']) for item in state.get('documents', [])]
            if actual == expected:
                return state
        time.sleep(0.1)
    raise AssertionError(f"Expected {expected}, saved state: {state}; windows: {windows()}")
try:
 if check_foreground:
  cover = u.CreateWindowExW(0, 'STATIC', 'File-open foreground test', 0x90800000,
                          100, 100, 400, 300, None, None, None, None)
  assert cover, "Could not create the test window"
 else:
  print("SKIP: desktop foreground checks (--no-foreground); window restoration is still checked", flush=True)
 for exe, sender in (itertools.permutations(executables, 2) if cross else ((exe, exe) for exe in executables)):
    print(f"Testing receiver: {exe}; sender: {sender}", flush=True)
    session.parent.mkdir(parents=True, exist_ok=True)
    session.write_text('{"version":1,"documents":[]}')
    first=subprocess.Popen([str(exe),'first.pdf'],cwd=work)
    processes.append(first)
    wait_for_documents([work/'first.pdf'])
    hwnd = background_viewer()
    second=subprocess.Popen([str(sender),second_name],cwd=work)
    processes.append(second)
    assert second.wait(20) == 0, f"Second launch failed: {windows()}"
    wait_for_foreground(hwnd)
    state = wait_for_documents([work/'first.pdf', work/second_name])
    assert state['activeTab'] == 1, state
    hwnd = background_viewer(minimized=True)
    duplicate=subprocess.Popen([str(sender),str(work/'first.pdf')],cwd=work.parent)
    processes.append(duplicate)
    assert duplicate.wait(20) == 0
    wait_for_foreground(hwnd)
    deadline=time.monotonic()+15
    while time.monotonic() < deadline:
        state=json.loads(session.read_text(encoding="utf-8"))
        if state['activeTab'] == 0: break
        time.sleep(0.1)
    assert len(state['documents']) == 2 and state['activeTab'] == 0, state
    windows(True)
    assert first.wait(10) == 0
    print("PASS: repeated launches opened tabs, selected existing tabs, and restored minimized windows" +
          (" with foreground activation" if check_foreground else ""), flush=True)
finally:
 if cover: u.DestroyWindow(cover)
 windows(True)
 for process in processes:
  if process.poll() is None:
   try: process.wait(10)
   except subprocess.TimeoutExpired: process.terminate(); process.wait(5)
 time.sleep(1)
 if backup is not None:session.write_bytes(backup)
 else:session.unlink(missing_ok=True)

fixtures.cleanup()
