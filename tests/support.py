"""Shared helpers: run the Windows programs from Windows or from WSL, with a timeout on every child."""
import os
import platform
import queue
import shutil
import signal
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path

BUILD = Path(os.environ["IMPOSSIBLE_BUILD"])
ON_NT = os.name == "nt"
ON_WSL = not ON_NT and "microsoft" in platform.release().lower()
RUNNABLE = ON_NT or ON_WSL
needs_windows = unittest.skipUnless(RUNNABLE, "the programs are Windows executables: needs Windows, directly or through WSL")
# Locks, junctions, sparse files and long paths are Windows' own file system, which WSL's disk is not.
needs_windows_files = unittest.skipUnless(ON_NT, "needs Windows' own file system: python3 ../SDK/tools/prove.py .")

WORK_ROOT = Path(tempfile.gettempdir()) / f"command-test-work-{os.getpid()}"  # on Windows a Windows disk
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


def kill_windows_program(exe):
    """Stops the Windows processes started from this private copy of a program (see private_program): their
    ids are looked up by the unique path, then each is stopped by its own id."""
    shell = powershell()
    if shell:
        script = (f"Get-Process | Where-Object {{ $_.Path -eq '{win(exe)}' }} | "
                  "ForEach-Object { Stop-Process -Id $_.Id -Force }")
        subprocess.run([shell, "-NoProfile", "-Command", script], capture_output=True, timeout=60)


def kill_tree(proc):
    """Stops a child and everything it started."""
    if proc.poll() is not None:
        return
    if ON_WSL and getattr(proc, "exe", None):
        kill_windows_program(proc.exe)
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


def remove_tree(path):
    """Deletes a folder tree, however deep, without Python's recursion, and fails if anything is left."""
    if ON_NT:  # cmd's rmdir gives up on very deep trees: take the folders down one at a time, deepest first
        folders, pending = [], [long_path(path)]
        while pending:
            here = pending.pop()
            folders.append(here)
            for entry in os.scandir(here):
                if entry.is_dir(follow_symlinks=False) and not entry.stat(follow_symlinks=False).st_file_attributes & 0x400:  # not a reparse point
                    pending.append(entry.path)
                else:
                    try:
                        os.unlink(entry.path)
                    except OSError:
                        os.rmdir(entry.path)  # a junction or folder link: the link goes, not its target
        for folder in reversed(folders):
            os.rmdir(folder)
    else:
        subprocess.run(["rm", "-rf", str(path)], capture_output=True, timeout=300)
    if os.path.lexists(long_path(path)):
        raise OSError(f"the test folder {path} could not be removed")


def private_program(tool):
    """The program to start. Through WSL, killing the Linux side of the interop link does not stop the
    Windows program, so it runs from a copy in a folder of its own and that exact path is what gets
    stopped: no other process can match it."""
    exe = BUILD / f"{tool}.exe"
    if not ON_WSL:
        return exe, None
    home = WORK_ROOT / "private"
    home.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(dir=home))
    shutil.copy(exe, folder / exe.name)
    return folder / exe.name, folder


def cmd_exe():
    return "cmd" if ON_NT else "cmd.exe"


def make_junction(link, target):
    """Makes a junction (no privilege needed); False when it could not."""
    done = subprocess.run([cmd_exe(), "/c", "mklink", "/J", win(link), win(target)], capture_output=True, timeout=60)
    return done.returncode == 0


def make_symlink(link, target):
    """Makes a file symbolic link; False when this account may not (it needs Developer Mode or elevation)."""
    done = subprocess.run([cmd_exe(), "/c", "mklink", win(link), win(target)], capture_output=True, timeout=60)
    return done.returncode == 0


def remove_junction(link):
    subprocess.run([cmd_exe(), "/c", "rmdir", win(link)], capture_output=True, timeout=60)


def make_deep(base, depth, name="a"):
    """A chain of folders `depth` deep under base; returns the bottom folder."""
    bottom = Path(base)
    os.makedirs(long_path(bottom), exist_ok=True)
    if ON_NT:
        for _ in range(depth):
            bottom = bottom / name
            os.mkdir(long_path(bottom))
    else:  # walk down by file descriptor, so every step is one short lookup instead of a thousand
        fd = os.open(bottom, os.O_RDONLY | os.O_DIRECTORY)
        for _ in range(depth):
            os.mkdir(name, dir_fd=fd)
            below = os.open(name, os.O_RDONLY | os.O_DIRECTORY, dir_fd=fd)
            os.close(fd)
            fd = below
        os.close(fd)
        bottom = bottom.joinpath(*([name] * depth))
    return bottom


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
    proc = subprocess.Popen(args, **kw)
    proc.exe = args[0]
    return proc


class Child:
    """One program run: started from a private copy, killed by its own id when it is not done in time, and
    cleaned up on exit. Every wait takes a timeout. Use as a context manager."""

    def __init__(self, tool, *args, cwd=None, stdin=subprocess.PIPE):
        self.exe, self.folder = private_program(tool)
        self.proc = popen([str(self.exe), *map(str, args)], stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=cwd)

    def write(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def finish(self, input=None, timeout=60):
        """Waits for the program, reading both outputs while it runs. On timeout it is killed and this raises."""
        try:
            out, err = self.proc.communicate(input=input, timeout=timeout)
        except subprocess.TimeoutExpired:
            self.kill()
            self.proc.communicate(timeout=30)
            raise
        if ON_WSL and (b"UtilAcceptVsock" in err or b"UtilAcceptVsock" in out):
            raise RuntimeError("WSL could not start the program: see vm.min_free_kbytes in the Impossible SDK README")
        return Result(self.proc.returncode, out, err)

    def kill(self):
        kill_tree(self.proc)

    def close(self):
        self.kill()
        for stream in (self.proc.stdin, self.proc.stdout, self.proc.stderr):
            try:
                if stream:
                    stream.close()
            except OSError:
                pass
        if self.folder:
            shutil.rmtree(self.folder, ignore_errors=True)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()


def run(tool, *args, input=None, cwd=None, timeout=60):
    """Runs one of the built programs and returns Result. Arguments are passed as given."""
    with Child(tool, *args, cwd=cwd) as child:
        return child.finish(input, timeout)


@needs_windows_files
class WorkDir(unittest.TestCase):
    """A test case with a scratch folder under the build folder, which Windows can see."""

    def setUp(self):
        WORK_ROOT.mkdir(exist_ok=True)
        self.dir = Path(tempfile.mkdtemp(dir=WORK_ROOT))
        self.addCleanup(remove_tree, self.dir)

    def path(self, *names):
        return self.dir.joinpath(*names)

    def write(self, name, data):
        p = self.path(name)
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(data if isinstance(data, bytes) else data.encode("utf-8"))
        return p

    def tool(self, tool, *args, **kw):
        kw.setdefault("cwd", self.dir)
        return run(tool, *args, **kw)


def make_sparse(path, size, writes=()):
    """A sparse file of this size with some bytes written at offsets, or None when the volume cannot do it."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"")
    fsutil = "fsutil.exe" if ON_WSL else "fsutil"
    try:
        done = subprocess.run([fsutil, "sparse", "setflag", win(path)], capture_output=True, timeout=60)
    except (OSError, subprocess.TimeoutExpired):
        return None
    if done.returncode != 0:
        return None
    with open(path, "r+b") as f:
        # Not truncate(): on Windows Python writes the zeros. Writing the last byte of a sparse file leaves a hole.
        f.seek(size - 1)
        f.write(b"\0")
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
    """A PowerShell child that keeps a file open (or a byte range of it locked) until closed.

    With `then`, it waits for trigger(), pauses `delay` seconds, then runs that PowerShell code on the open
    file (as $f). Every wait is bounded."""

    def __init__(self, path, share, lock=None, then=None, delay=0):
        shell = powershell()
        if not shell:
            raise unittest.SkipTest("PowerShell is not available to hold a file open")
        lock_cmd = f"$f.Lock({lock[0]},{lock[1]});" if lock else ""
        act = f"[void][Console]::In.ReadLine();Start-Sleep -Milliseconds {int(delay * 1000)};{then};" if then else ""
        script = (f"$f=[IO.File]::Open('{win(path)}','Open','ReadWrite','{share}');{lock_cmd}"
                  f"[Console]::Out.WriteLine('READY');[Console]::Out.Flush();{act}[void][Console]::In.ReadLine()")
        self.proc = popen([shell, "-NoProfile", "-Command", script], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, text=True)
        self.proc.exe = None  # the shell is shared with other work: it ends when its input ends
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(self.proc.stdout.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=60)
        except queue.Empty:
            line = ""
        if line.strip() != "READY":
            self.close()
            raise RuntimeError("PowerShell did not report that it holds the file")

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def trigger(self):
        self.proc.stdin.write("go\n")
        self.proc.stdin.flush()

    def close(self):
        try:
            self.proc.stdin.close()  # the script ends, and the file is released, when its input ends
            self.proc.wait(timeout=20)
        except (OSError, subprocess.TimeoutExpired):
            kill_tree(self.proc)
        for stream in (self.proc.stdout, self.proc.stderr):
            if stream:
                try:
                    stream.close()
                except OSError:
                    pass
