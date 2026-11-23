"""thex: the dump format, exact round trips, offsets and lengths, errors."""
import os
import random
import shutil
import unittest

from support import Holder, WorkDir, kill_tree, make_sparse, needs_windows, popen, private_program
import subprocess
import time


def row(offset, data, width=16, digits=8):
    """One dump line, built independently of the C code from the format in the README."""
    hexes = ""
    for i in range(width):
        hexes += f"{data[i]:02x} " if i < len(data) else "   "
        if i % 8 == 7 and i + 1 < width:
            hexes += " "
    text = "".join(chr(b) if 32 <= b < 127 else "." for b in data)
    return f"{offset:0{digits}x}  {hexes} |{text}|\n"


@needs_windows
class Dump(WorkDir):
    def test_format(self):
        data = b"Hello world\n\x00\x01\x02\x03abc\x7f\xff"
        self.write("f", data)
        result = self.tool("thex", "f")
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text,
            "00000000  48 65 6c 6c 6f 20 77 6f  72 6c 64 0a 00 01 02 03  |Hello world.....|\n"
            "00000010  61 62 63 7f ff                                    |abc..|\n")
        self.assertEqual(result.text, row(0, data[:16]) + row(16, data[16:]))

    def test_empty_file_prints_nothing(self):
        self.write("e", b"")
        result = self.tool("thex", "e")
        self.assertEqual((result.code, result.out), (0, b""))

    def test_offset_and_length(self):
        data = bytes(range(256))
        self.write("f", data)
        result = self.tool("thex", "-s", "0x10", "-n", "4", "f")
        self.assertEqual(result.text, row(16, bytes([16, 17, 18, 19])))
        self.assertEqual(self.tool("thex", "-s", "1k", "f").code, 2)  # past the end
        self.assertEqual(self.tool("thex", "-s", "256", "f").out, b"")  # exactly at the end is fine
        self.assertEqual(self.tool("thex", "-n", "0", "f").out, b"")
        self.assertEqual(self.tool("thex", "-s", "250", "-n", "100", "f").text.count("\n"), 1)
        for spelling in (["-s16"], ["--skip=16"], ["--skip", "0x10"], ["-s", "0X10"]):
            self.assertTrue(self.tool("thex", *spelling, "-n", "1", "f").text.startswith("00000010  10 "), spelling)

    def test_width(self):
        self.write("f", bytes(range(40)))
        for width in (1, 4, 8, 9, 20, 40, 256):
            result = self.tool("thex", "-w", str(width), "f")
            self.assertEqual(result.code, 0)
            self.assertEqual(len(result.text.splitlines()), -(-40 // width), width)
        for bad in ("0", "257", "x"):
            self.assertEqual(self.tool("thex", "-w", bad, "f").code, 2)

    def test_standard_input(self):
        result = self.tool("thex", input=b"abc")
        self.assertEqual(result.text, row(0, b"abc"))
        skipped = self.tool("thex", "-s", "1", input=b"abc")
        self.assertTrue(skipped.text.startswith("00000001  62 63"))
        self.assertEqual(self.tool("thex", "-s", "9", input=b"abc").code, 2)

    def test_over_4_gib_offsets(self):
        size = (4 << 30) + 4096
        p = make_sparse(self.path("big"), size, [(4 << 30, b"past four gibibytes")])
        if p is None:
            self.skipTest("this volume cannot make sparse files")
        result = self.tool("thex", "-s", str(4 << 30), "-n", "19", "big", timeout=120)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text, row(4 << 30, b"past four gibibytes"[:16]) + row((4 << 30) + 16, b"tes"))
        self.write("dump.txt", result.out)
        back = self.tool("thex", "-r", "dump.txt")
        self.assertEqual(back.out, b"past four gibibytes")
        end = self.tool("thex", "-s", str(size - 2), "big")
        self.assertTrue(end.text.startswith("100000ffe  00 00"), end.text)

    def test_missing_folder_and_unicode_names(self):
        result = self.tool("thex", "nothere")
        self.assertEqual(result.code, 2)
        self.assertTrue(result.errtext.rstrip().endswith("(error 2)"), result.errtext)
        self.path("d").mkdir()
        self.assertEqual(self.tool("thex", "d").code, 2)
        name = "déjà 日本.bin"
        self.write(name, b"\xc3\xa9")
        self.assertTrue(self.tool("thex", name).text.startswith("00000000  c3 a9"))

    def test_read_error_mid_file(self):
        p = self.write("half", os.urandom(300000))
        holder = Holder(p, "ReadWrite", lock=(100000, 1000))
        try:
            result = self.tool("thex", "half")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertIn("(error 33)", result.errtext)

    def test_options(self):
        self.assertIn("Usage: thex", self.tool("thex", "--help").text)
        self.assertEqual(self.tool("thex", "--version").text, "thex (Command) 0.1.0\n")
        for bad in (["--nope"], ["a", "b"], ["-r", "-n", "1"], ["-o", "x"], ["-s"], ["-s", "12x"]):
            result = self.tool("thex", *bad)
            self.assertEqual(result.code, 2, bad)
            self.assertTrue(result.errtext.startswith("thex: "), bad)


@needs_windows
class Reverse(WorkDir):
    def test_round_trip_is_exact(self):
        rng = random.Random(7)
        sizes = [0, 1, 7, 8, 9, 16, 17, 257, 4096, 70000]
        for n in sizes:
            data = rng.randbytes(n)
            self.write("in.bin", data)
            for width in ((16, 1, 3, 32, 256) if n < 300 else (16,) if n > 4096 else (16, 5)):
                dump = self.tool("thex", "-w", str(width), "in.bin", timeout=120)
                self.assertEqual(dump.code, 0, dump.errtext)
                back = self.tool("thex", "-r", input=dump.out, timeout=120)
                self.assertEqual((back.code, back.out), (0, data), (n, width))

    def test_round_trip_through_a_file_and_output_option(self):
        data = os.urandom(5000)
        self.write("in.bin", data)
        self.write("dump.txt", self.tool("thex", "in.bin").out)
        self.write("out.bin", b"old contents")
        result = self.tool("thex", "-r", "-o", "out.bin", "dump.txt")
        self.assertEqual((result.code, result.out), (0, b""))
        self.assertEqual(self.path("out.bin").read_bytes(), data)
        self.assertEqual(sorted(p.name for p in self.dir.iterdir()), ["dump.txt", "in.bin", "out.bin"])

    def test_ranges_round_trip_with_their_offsets(self):
        data = os.urandom(1000)
        self.write("in.bin", data)
        dump = self.tool("thex", "-s", "100", "-n", "300", "in.bin")
        self.assertEqual(self.tool("thex", "-r", input=dump.out).out, data[100:400])

    def test_bad_dump_is_an_error_and_leaves_the_output_alone(self):
        self.write("out.bin", b"keep me")
        cases = {
            "gap": "00000000  01 02 03\n00000009  04\n",
            "bad hex": "00000000  01 zz 03\n",
            "one digit": "00000000  1 2\n",
            "no offset": "hello\n",
            "three digits": "00000000  123\n",
        }
        for name, text in cases.items():
            self.write("bad.txt", text)
            result = self.tool("thex", "-r", "-o", "out.bin", "bad.txt")
            self.assertEqual(result.code, 2, name)
            self.assertIn("line ", result.errtext, name)
            self.assertEqual(self.path("out.bin").read_bytes(), b"keep me", name)
            self.assertEqual(sorted(p.name for p in self.dir.iterdir()), ["bad.txt", "out.bin"], name)
            self.assertEqual(self.tool("thex", "-r", "bad.txt").code, 2, name)

    def test_killed_run_leaves_the_destination_alone(self):
        self.write("out.bin", b"keep me")
        exe, folder = private_program("thex")
        if folder:
            self.addCleanup(shutil.rmtree, folder, True)
        proc = popen([str(exe), "-r", "-o", "out.bin"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=subprocess.PIPE, cwd=self.dir)
        proc.stdin.write(b"00000000  01 02 03\n")
        proc.stdin.flush()
        deadline = time.time() + 20  # wait until the run has made its temporary file: it is then mid-run
        while time.time() < deadline and len(list(self.dir.iterdir())) < 2:
            time.sleep(0.1)
        self.assertEqual(len(list(self.dir.iterdir())), 2, "the run never created its temporary file")
        kill_tree(proc)  # like Ctrl+C: nothing gets to clean up
        for stream in (proc.stdin, proc.stdout, proc.stderr):
            try:
                stream.close()
            except OSError:
                pass
        self.assertEqual(self.path("out.bin").read_bytes(), b"keep me")  # its own leftover temporary file is expected

    def test_a_temporary_name_that_exists_is_never_touched(self):
        self.write("out.bin", b"keep me")
        self.write("out.bin.thex-tmp", b"somebody else's file")
        for dump, code in (("00000000  01 02\n", 0), ("00000000  zz\n", 2)):
            self.write("d.txt", dump)
            self.assertEqual(self.tool("thex", "-r", "-o", "out.bin", "d.txt").code, code)
            self.assertEqual(self.path("out.bin.thex-tmp").read_bytes(), b"somebody else's file")
        self.assertEqual(self.path("out.bin").read_bytes(), b"\x01\x02")

    def test_a_temporary_name_that_is_a_hard_link_to_the_destination_is_never_touched(self):
        self.write("out.bin", b"keep me")
        try:
            os.link(self.path("out.bin"), self.path("out.bin.thex-tmp"))
        except (OSError, NotImplementedError):
            self.skipTest("this volume has no hard links")
        self.write("d.txt", "00000000  zz\n")
        self.assertEqual(self.tool("thex", "-r", "-o", "out.bin", "d.txt").code, 2)
        self.assertEqual(self.path("out.bin").read_bytes(), b"keep me")

    def test_offsets_that_wrap_around_are_an_error(self):
        self.write("d.txt", "ffffffffffffffff  41\n00000000  42\n")
        result = self.tool("thex", "-r", "d.txt")
        self.assertEqual(result.code, 2)
        self.assertIn("line 1", result.errtext)
        self.write("d.txt", "fffffffffffffffe  41 42\n")
        self.assertEqual(self.tool("thex", "-r", "d.txt").code, 2)
        self.write("d.txt", "fffffffffffffffe  41\n")
        self.assertEqual(self.tool("thex", "-r", "d.txt").out, b"A")

    def test_offset_only_final_line_is_accepted(self):
        self.write("d.txt", "00000000  61 62 |ab|\n00000002\n")
        self.assertEqual(self.tool("thex", "-r", "d.txt").out, b"ab")


if __name__ == "__main__":
    unittest.main()
