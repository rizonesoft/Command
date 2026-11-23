"""Shared helpers: run the Windows programs from Windows or from WSL, with a timeout on every child."""
import os
import platform
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from pathlib import Path

BUILD = Path(os.environ["IMPOSSIBLE_BUILD"])
ON_NT = os.name == "nt"
ON_WSL = not ON_NT and "microsoft" in platform.release().lower()
RUNNABLE = ON_NT or ON_WSL
needs_windows = unittest.skipUnless(RUNNABLE, "the programs are Windows executables: needs Windows, directly or through WSL")

WORK_ROOT = BUILD / "test-work"
_wsl_paths = {}


def win(path):
    """The Windows spelling of a path, for arguments."""
    path = str(path)
    if not ON_WSL:
        return path
    if path not in _wsl_paths:
        _wsl_paths[path] = subprocess.run(["wslpath", "-w", path], capture_output=True, text=True, check=True, timeout=30).stdout.strip()
    return _wsl_paths[path]


def long_path(path):
    """A path that Python can use even when it is longer than 260 characters."""
    path = os.path.abspath(str(path))
    return "\\\\?\\" + path if ON_NT and not path.startswith("\\\\?\\") else path


def kill_tree(proc):
    """Stops a child and everything it started."""
    if proc.poll() is not None:
        return
    if ON_NT:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(proc.pid)], capture_output=True, timeout=30)
    else:
        for sig in (signal.SIGTERM, signal.SIGKILL):
            try:
                os.killpg(proc.pid, sig)
            except ProcessLookupError:
                break
            try:
                proc.wait(timeout=3)
                break
            except subprocess.TimeoutExpired:
                continue
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        pass


class Result:
    def __init__(self, code, out, err):
        self.code, self.out, self.err = code, out, err

    @property
    def text(self):
        return self.out.decode("utf-8", "replace")

    @property
    def errtext(self):
        return self.err.decode("utf-8", "replace")


def popen(args, **kw):
    if not ON_NT:
        kw["start_new_session"] = True
    return subprocess.Popen(args, **kw)


def run(tool, *args, input=None, cwd=None, timeout=60):
    """Runs one of the built programs and returns Result. Arguments are passed as given."""
    command = [str(BUILD / f"{tool}.exe"), *map(str, args)]
    for attempt in range(8):
        proc = popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd)
        try:
            out, err = proc.communicate(input=input, timeout=timeout)
        except subprocess.TimeoutExpired:
            kill_tree(proc)
            proc.communicate()
            raise
        # WSL sometimes fails to start a Windows program at all (UtilAcceptVsock); the program never ran.
        if ON_WSL and (b"UtilAcceptVsock" in err or b"UtilAcceptVsock" in out):
            time.sleep(0.5)
            continue
        return Result(proc.returncode, out, err)
    raise RuntimeError("WSL could not start the program after 8 tries")


class WorkDir(unittest.TestCase):
    """A test case with a scratch folder under the build folder, which Windows can see."""

    def setUp(self):
        WORK_ROOT.mkdir(exist_ok=True)
        self.dir = Path(tempfile.mkdtemp(dir=WORK_ROOT))
        self.addCleanup(shutil.rmtree, long_path(self.dir), True)

    def path(self, *names):
        return self.dir.joinpath(*names)

    def write(self, name, data):
        p = self.path(name)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
        return p

    def run(self, tool, *args, **kw):
        kw.setdefault("cwd", self.dir)
        return run(tool, *args, **kw)


def make_sparse(path, size, writes=()):
    """A sparse file of this size with some bytes written at offsets, or None when the volume cannot do it."""
    path = Path(path)
    path.write_bytes(b"")
    fsutil = "fsutil.exe" if ON_WSL else "fsutil"
    try:
        done = subprocess.run([fsutil, "sparse", "setflag", win(path)], capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if done.returncode != 0:
        return None
    with open(path, "r+b") as f:
        f.truncate(size)
        for offset, data in writes:
            f.seek(offset)
            f.write(data)
    return path


def powershell():
    for name in ("pwsh.exe", "pwsh", "powershell.exe", "powershell"):
        found = shutil.which(name)
        if found:
            return found
    return None


class Holder:
    """A PowerShell child that keeps a file open (or a byte range of it locked) until closed."""

    def __init__(self, path, share, lock=None):
        shell = powershell()
        if not shell:
            raise unittest.SkipTest("PowerShell is not available to hold a file open")
        lock_cmd = f"$f.Lock({lock[0]},{lock[1]});" if lock else ""
        script = (f"$f=[IO.File]::Open('{win(path)}','Open','ReadWrite','{share}');{lock_cmd}"
                  "[Console]::Out.WriteLine('READY');[Console]::Out.Flush();Start-Sleep -Seconds 120")
        self.proc = popen([shell, "-NoProfile", "-Command", script], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        deadline = time.time() + 60
        line = ""
        while time.time() < deadline:
            line = self.proc.stdout.readline()
            if line.strip() == "READY" or self.proc.poll() is not None:
                break
        if line.strip() != "READY":
            self.close()
            raise unittest.SkipTest("PowerShell could not hold the file open")

    def close(self):
        kill_tree(self.proc)
        for stream in (self.proc.stdout, self.proc.stderr):
            if stream:
                stream.close()
