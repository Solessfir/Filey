# Side-by-side checks of Filey against the reference app on Windows, without touching the real pointer, keyboard or screen.
#
# Both apps start on the same fixture folder with their windows off-screen. The reference gets FakeInput.dll, which answers its pointer, focus and
# held-key queries from shared memory that the steps write, and is captured with PrintWindow; Filey writes its own frames (FILEY_CAPTURE_DIR),
# since an off-screen GL window keeps none of what it draws. Steps post input to the two windows only, and only the started processes are closed.
#
#   python Source/Tools/Compare/Compare.py run <name> "<steps>" [ref|filey|both]
#       steps, ';' separated: move x y | click x y | rclick x y | dbl x y | down x y | up x y | key VK | mods VK... | wait s | shot [tag] | frames n interval tag
#       writes <out>/<name><tag>_{ref,filey}.png and a stacked <name><tag>.png; frames also write <name><tag>_<side>.times in milliseconds
#   python Source/Tools/Compare/Compare.py zoom <name> x0 y0 x1 y1 scale      the same crop of both shots, enlarged, to zoom.png
#   python Source/Tools/Compare/Compare.py runs <png> y0 y1 x0 x1 threshold   column runs brighter than threshold in a band, for measuring positions
#   python Source/Tools/Compare/Compare.py frames <name> <tag> <side>         changed pixels between consecutive frames, for animation timing
#
# COMPARE_REFERENCE names the reference executable and COMPARE_REFERENCE_STATE its settings files, ';' separated, which are copied before every
# run and put back after it. COMPARE_OUT is the output folder (%TEMP%/filey-compare); Filey's settings live in its Config folder, which starts
# with "theme = fullmoon". COMPARE_PATH overrides the folder both apps open; COMPARE_W, COMPARE_H, COMPARE_X and COMPARE_Y the window.
import ctypes, ctypes.wintypes as wt, os, shutil, subprocess, sys, time
from PIL import Image, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
OUT = os.environ.get('COMPARE_OUT', os.path.join(os.environ.get('TEMP', '.'), 'filey-compare'))
FILEY = os.environ.get('COMPARE_FILEY', os.path.join(ROOT, 'Bin', 'debug', 'Filey.exe'))
REFERENCE = os.environ.get('COMPARE_REFERENCE', '')

u32 = ctypes.windll.user32; g32 = ctypes.windll.gdi32; k32 = ctypes.windll.kernel32
u32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
u32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
k32.CreateFileMappingA.restype = wt.HANDLE; k32.MapViewOfFile.restype = ctypes.c_void_p
k32.OpenProcess.restype = wt.HANDLE; k32.VirtualAllocEx.restype = ctypes.c_void_p
k32.VirtualAllocEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, wt.DWORD, wt.DWORD]
k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
k32.GetProcAddress.restype = ctypes.c_void_p; k32.GetProcAddress.argtypes = [wt.HMODULE, ctypes.c_char_p]
k32.GetModuleHandleA.restype = wt.HMODULE
k32.CreateRemoteThread.restype = wt.HANDLE
k32.CreateRemoteThread.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p, wt.DWORD, ctypes.c_void_p]

class Fake(ctypes.Structure):
    _fields_ = [('x', ctypes.c_long), ('y', ctypes.c_long), ('active', ctypes.c_long), ('hwnd', ctypes.c_long), ('keys', ctypes.c_ubyte * 256)]

def hook_dll():
    dll = os.path.join(OUT, 'FakeInput.dll')
    source = os.path.join(HERE, 'FakeInput.c')
    if os.path.exists(dll) and os.path.getmtime(dll) > os.path.getmtime(source): return dll
    os.makedirs(OUT, exist_ok=True)
    script = os.path.join(OUT, 'BuildFakeInput.bat')
    with open(script, 'w') as f:
        f.write('@echo off\r\nsetlocal\r\n'
                'for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer\\vswhere.exe" -latest -products * -property installationPath`) do set "VSROOT=%%I"\r\n'
                'set "PATH=%PATH%;%ProgramFiles(x86)%\\Microsoft Visual Studio\\Installer"\r\n'
                'call "%VSROOT%\\VC\\Auxiliary\\Build\\vcvars64.bat" >nul || exit /b 1\r\n'
                'set CLANG=clang\r\nwhere clang >nul 2>nul || set "CLANG=%VCINSTALLDIR%Tools\\Llvm\\x64\\bin\\clang.exe"\r\n'
                f'"%CLANG%" -O2 -shared -fuse-ld=lld "{source}" -o "{dll}" -luser32 -lkernel32\r\n')
    subprocess.run(['cmd', '/c', script], check=True, stdout=subprocess.DEVNULL)
    return dll

def inject(pid):
    mapping = k32.CreateFileMappingA(wt.HANDLE(-1), None, 4, 0, ctypes.sizeof(Fake), f'Local\\FakeInput{pid}'.encode())
    fake = Fake.from_address(k32.MapViewOfFile(mapping, 0xF001F, 0, 0, ctypes.sizeof(Fake)))
    process = k32.OpenProcess(0x1F0FFF, False, pid)
    path = hook_dll().encode('utf-16-le') + bytes(2)
    memory = k32.VirtualAllocEx(process, None, len(path), 0x3000, 4)
    k32.WriteProcessMemory(process, memory, path, len(path), None)
    load = k32.GetProcAddress(k32.GetModuleHandleA(b'kernel32.dll'), b'LoadLibraryW')
    k32.WaitForSingleObject(k32.CreateRemoteThread(process, None, 0, load, memory, 0, None), 5000)
    return mapping, fake

def windows_of(pid):
    found = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def each(h, _):
        owner = wt.DWORD(); u32.GetWindowThreadProcessId(h, ctypes.byref(owner))
        if owner.value == pid and u32.IsWindowVisible(h): found.append(h)
        return True
    u32.EnumWindows(each, 0)
    return found

class BIH(ctypes.Structure):
    _fields_ = [('a', wt.DWORD), ('w', ctypes.c_long), ('h', ctypes.c_long), ('p', wt.WORD), ('b', wt.WORD), ('c', wt.DWORD), ('s', wt.DWORD), ('x', ctypes.c_long), ('y', ctypes.c_long), ('u', wt.DWORD), ('i', wt.DWORD)]

class App:
    def __init__(self, exe, args, env, w, h, x, y):
        e = dict(os.environ); e.update(env)
        self.frames = None
        if exe == FILEY:
            self.frames = os.path.join(OUT, 'frames'); shutil.rmtree(self.frames, ignore_errors=True); os.makedirs(self.frames)
            e['FILEY_CAPTURE_DIR'] = self.frames; e['FILEY_CAPTURE_SEQUENCE'] = '1'
        self.process = subprocess.Popen([exe, *args], env=e, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.mapping, self.fake = inject(self.process.pid)
        self.window = None
        for _ in range(100):
            found = windows_of(self.process.pid)
            if found: self.window = found[0]; break
            time.sleep(0.1)
        assert self.window, 'the app opened no window'
        u32.ShowWindow(self.window, 1)
        u32.MoveWindow(self.window, x, y, w, h, True)
        self.fake.hwnd = self.window; self.fake.active = 1; self.fake.x, self.fake.y = -10000, -10000
        self.post(0x6, 1, 0); self.post(0x7, 0, 0)
        self.buttons = 0
        time.sleep(1.0)
    def post(self, message, w=0, l=0): u32.PostMessageW(self.window, message, w, l)
    def key(self, vk):
        self.post(0x100, vk, 1); time.sleep(0.03); self.post(0x101, vk, 0xC0000001); time.sleep(0.05)
    def mods(self, *vks):
        for i in range(256): self.fake.keys[i] = 0
        # The left-hand keys too, since apps ask for either.
        sides = {16: 0xA0, 17: 0xA2, 18: 0xA4}
        for v in vks:
            self.fake.keys[v] = 1
            if v in sides: self.fake.keys[sides[v]] = 1
    def mouse(self, x, y, wait=0.05):
        p = wt.POINT(x, y); u32.ClientToScreen(self.window, ctypes.byref(p))
        self.fake.x, self.fake.y = p.x, p.y
        self.post(0x200, self.buttons, (y << 16) | x); time.sleep(wait)
    def press(self, x, y, button, down):
        message = {('l', True): 0x201, ('l', False): 0x202, ('r', True): 0x204, ('r', False): 0x205}[(button, down)]
        bit = 1 if button == 'l' else 2
        self.fake.keys[bit] = 1 if down else 0
        self.buttons = bit if down else 0
        self.post(message, self.buttons, (y << 16) | x)
    def click(self, x, y, button='l'):
        self.mouse(x, y); self.press(x, y, button, True); time.sleep(0.05); self.press(x, y, button, False)
    def grab(self):
        r = wt.RECT(); u32.GetClientRect(self.window, ctypes.byref(r)); w, h = r.right, r.bottom
        dc = u32.GetDC(self.window); memory = g32.CreateCompatibleDC(dc); bitmap = g32.CreateCompatibleBitmap(dc, w, h)
        g32.SelectObject(memory, bitmap)
        u32.PrintWindow(self.window, memory, 3)
        pixels = ctypes.create_string_buffer(w * h * 4)
        g32.GetDIBits(memory, bitmap, 0, h, pixels, ctypes.byref(BIH(ctypes.sizeof(BIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)), 0)
        g32.DeleteObject(bitmap); g32.DeleteDC(memory); u32.ReleaseDC(self.window, dc)
        return Image.frombuffer('RGBA', (w, h), bytes(pixels), 'raw', 'BGRA', 0, 1).convert('RGB')
    def captured(self):
        names = sorted(f for f in os.listdir(self.frames) if f.startswith('frame-') and f.endswith('.bmp'))
        return [(int(n[:-4].split('-')[2]), os.path.join(self.frames, n)) for n in names]
    def shot(self):
        if self.frames:
            # The newest frame once no new one has come for a moment.
            start = time.time()
            while time.time() - start < 3:
                frames = self.captured()
                if frames and time.time() - os.path.getmtime(frames[-1][1]) > 0.15: break
                time.sleep(0.05)
            return Image.open(frames[-1][1]).convert('RGB')
        # A capture can catch a frame half drawn, so one is taken once two in a row agree.
        last = self.grab()
        for _ in range(8):
            time.sleep(0.03)
            now = self.grab()
            if now.tobytes() == last.tobytes(): break
            last = now
        return now
    def close(self):
        self.post(0x10)
        try: self.process.wait(5)
        except subprocess.TimeoutExpired: self.process.kill()

def fixture():
    root = os.path.join(OUT, 'Fixture')
    for folder in ['Alpha', 'Beta', 'Gamma folder', 'Docs']:
        os.makedirs(os.path.join(root, folder), exist_ok=True)
        with open(os.path.join(root, folder, 'inner.txt'), 'w') as f: f.write('x' * 100)
    for i in range(1, 201):
        path = os.path.join(root, 'Docs', f'file{i:03d}.txt')
        if not os.path.exists(path):
            with open(path, 'w') as f: f.write('x')
    for name, size in [('notes.txt', 1200), ('report.pdf', 52000), ('script.py', 800), ('archive.zip', 150000), ('readme.md', 300), ('data.json', 4000), ('song.mp3', 3200000)]:
        with open(os.path.join(root, name), 'wb') as f: f.write(b'a' * size)
    Image.new('RGB', (64, 48), (200, 80, 40)).save(os.path.join(root, 'photo.png'))
    config = os.path.join(OUT, 'Config'); os.makedirs(config, exist_ok=True)
    settings = os.path.join(config, 'Settings.txt')
    if not os.path.exists(settings):
        with open(settings, 'w') as f: f.write('theme = fullmoon\n')
    return root, config

def steps(app, text, side, name, tags):
    for step in [s.strip() for s in text.split(';') if s.strip()]:
        words = step.split(); op = words[0]
        n = [int(float(w)) if w.lstrip('-').replace('.', '').isdigit() else w for w in words[1:]]
        if op == 'move': app.mouse(n[0], n[1])
        elif op == 'click': app.click(n[0], n[1])
        elif op == 'rclick': app.click(n[0], n[1], 'r')
        elif op == 'dbl':
            app.click(n[0], n[1])
            if app.frames: app.click(n[0], n[1])
            else: app.post(0x203, 1, (n[1] << 16) | n[0]); app.post(0x202, 0, (n[1] << 16) | n[0])
        elif op == 'down': app.mouse(n[0], n[1]); app.press(n[0], n[1], 'l', True)
        elif op == 'up': app.mouse(n[0], n[1]); app.press(n[0], n[1], 'l', False)
        elif op == 'key': app.key(n[0])
        elif op == 'mods': app.mods(*n)
        elif op == 'wait': time.sleep(float(words[1]))
        elif op == 'shot':
            tag = words[1] if len(words) > 1 else ''
            app.shot().save(os.path.join(OUT, f'{name}{tag}_{side}.png')); tags.add(tag)
        elif op == 'frames':
            count, interval, tag = int(words[1]), float(words[2]), words[3]
            times = []
            if app.frames:
                before = {p for _, p in app.captured()}
                time.sleep(count * max(interval, 0.012))
                fresh = [(t, p) for t, p in app.captured() if p not in before][:count]
                images = [Image.open(p).convert('RGB') for _, p in fresh]
                times = [(t - fresh[0][0]) // 1000 for t, _ in fresh]
            else:
                start = time.perf_counter(); images = []
                for _ in range(count):
                    times.append(int((time.perf_counter() - start) * 1000)); images.append(app.grab())
                    if interval: time.sleep(interval)
            for i, image in enumerate(images):
                image.save(os.path.join(OUT, f'{name}{tag}{i:02d}_{side}.png')); tags.add(f'{tag}{i:02d}')
            with open(os.path.join(OUT, f'{name}{tag}_{side}.times'), 'w') as f: f.write(' '.join(map(str, times)))
        else:
            raise SystemExit(f'unknown step {op}')

def run(side, name, text, tags):
    root, config = fixture()
    path = os.environ.get('COMPARE_PATH', root)
    geometry = [int(os.environ.get(k, d)) for k, d in (('COMPARE_W', 1200), ('COMPARE_H', 800), ('COMPARE_X', 2100), ('COMPARE_Y', 40))]
    if side == 'filey':
        exe, env, saved = FILEY, {'FILEY_CONFIG_DIR': config}, []
    else:
        if not REFERENCE: raise SystemExit('COMPARE_REFERENCE is not set')
        exe, env = REFERENCE, {}
        # The reference keeps its session and settings where its user's are, so they are put back afterwards.
        backup = os.path.join(OUT, 'reference-state'); os.makedirs(backup, exist_ok=True)
        saved = [p for p in os.environ.get('COMPARE_REFERENCE_STATE', '').split(';') if p and os.path.exists(p)]
        for i, p in enumerate(saved): shutil.copy2(p, os.path.join(backup, f'{i}'))
    app = App(exe, [path], env, *geometry)
    time.sleep(1.5)
    try: steps(app, text, side, name, tags)
    finally:
        app.close()
        for i, p in enumerate(saved): shutil.copy2(os.path.join(os.path.join(OUT, 'reference-state'), f'{i}'), p)

def main(argv):
    os.makedirs(OUT, exist_ok=True)
    command = argv[1] if len(argv) > 1 else ''
    if command == 'run':
        name, text = argv[2], argv[3]; which = argv[4] if len(argv) > 4 else 'both'
        tags = set()
        for side in ('ref', 'filey'):
            if which in (side, 'both'): run(side, name, text, tags)
        for tag in sorted(tags):
            paths = [os.path.join(OUT, f'{name}{tag}_{s}.png') for s in ('ref', 'filey')]
            if all(os.path.exists(p) for p in paths):
                a, b = [Image.open(p) for p in paths]
                stacked = Image.new('RGB', (max(a.width, b.width), a.height + b.height)); stacked.paste(a, (0, 0)); stacked.paste(b, (0, a.height))
                stacked.save(os.path.join(OUT, f'{name}{tag}.png'))
        print(OUT, sorted(tags))
    elif command == 'zoom':
        name = argv[2]; x0, y0, x1, y1, z = map(int, argv[3:8])
        a, b = [Image.open(os.path.join(OUT, f'{name}_{s}.png')).crop((x0, y0, x1, y1)) for s in ('ref', 'filey')]
        w, h = (x1 - x0) * z, (y1 - y0) * z
        out = Image.new('RGB', (w, 2 * h + 4), (255, 0, 255)); out.paste(a.resize((w, h), Image.NEAREST), (0, 0)); out.paste(b.resize((w, h), Image.NEAREST), (0, h + 4))
        out.save(os.path.join(OUT, 'zoom.png')); print(os.path.join(OUT, 'zoom.png'))
    elif command == 'runs':
        image = Image.open(argv[2]).convert('L'); y0, y1, x0, x1, threshold = map(int, argv[3:8])
        columns = [x for x in range(x0, x1) if any(image.getpixel((x, y)) > threshold for y in range(y0, y1))]
        runs, start, last = [], None, None
        for x in columns:
            if last is None or x - last > 3:
                if start is not None: runs.append((start, last))
                start = x
            last = x
        if start is not None: runs.append((start, last))
        print(runs)
    elif command == 'frames':
        name, tag, side = argv[2:5]
        times_path = os.path.join(OUT, f'{name}{tag}_{side}.times')
        times = open(times_path).read().split() if os.path.exists(times_path) else []
        previous, i = None, 0
        while os.path.exists(os.path.join(OUT, f'{name}{tag}{i:02d}_{side}.png')):
            image = Image.open(os.path.join(OUT, f'{name}{tag}{i:02d}_{side}.png')).convert('RGB')
            if previous:
                changed = ImageChops.difference(image, previous).convert('L').point(lambda v: 255 if v > 6 else 0)
                print(i, times[i] if i < len(times) else '', sum(1 for v in changed.getdata() if v), changed.getbbox())
            previous, i = image, i + 1
    else:
        print(__doc__ or open(__file__).read().split('import ctypes')[0])

if __name__ == '__main__':
    main(sys.argv)
