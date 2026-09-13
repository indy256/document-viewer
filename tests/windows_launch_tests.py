import ctypes, ctypes.wintypes as w, json, os, subprocess, sys, tempfile, time
from pathlib import Path
executables = [Path(arg).resolve() for arg in sys.argv[1:]]
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
for name in ['first.pdf', 'second book.pdf']: pdf(work/name)
u=ctypes.windll.user32
cb=ctypes.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
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
def wait_for_documents(expected):
    deadline = time.monotonic() + 20
    state = {}
    while time.monotonic() < deadline:
        if session.exists():
            state = json.loads(session.read_text())
            actual = [Path(item['path']) for item in state.get('documents', [])]
            if actual == expected:
                return state
        time.sleep(0.1)
    raise AssertionError(f"Expected {expected}, saved state: {state}; windows: {windows()}")
try:
 for exe in executables:
    print(f"Testing repeated launches: {exe}", flush=True)
    session.parent.mkdir(parents=True, exist_ok=True)
    session.write_text('{"version":1,"documents":[]}')
    first=subprocess.Popen([str(exe),'first.pdf'],cwd=work)
    processes.append(first)
    wait_for_documents([work/'first.pdf'])
    second=subprocess.Popen([str(exe),'second book.pdf'],cwd=work)
    processes.append(second)
    assert second.wait(20) == 0, f"Second launch failed: {windows()}"
    state = wait_for_documents([work/'first.pdf', work/'second book.pdf'])
    assert state['activeTab'] == 1, state
    duplicate=subprocess.Popen([str(exe),str(work/'first.pdf')],cwd=work.parent)
    processes.append(duplicate)
    assert duplicate.wait(20) == 0
    deadline=time.monotonic()+15
    while time.monotonic() < deadline:
        state=json.loads(session.read_text())
        if state['activeTab'] == 0: break
        time.sleep(0.1)
    assert len(state['documents']) == 2 and state['activeTab'] == 0, state
    windows(True)
    assert first.wait(10) == 0
    print("PASS: repeated launches opened two tabs and reactivated the existing tab", flush=True)
finally:
 windows(True)
 for process in processes:
  if process.poll() is None:
   try: process.wait(10)
   except subprocess.TimeoutExpired: process.terminate(); process.wait(5)
 time.sleep(1)
 if backup is not None:session.write_bytes(backup)
 else:session.unlink(missing_ok=True)

fixtures.cleanup()
