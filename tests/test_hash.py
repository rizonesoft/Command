"""thash: published vectors, random data against hashlib and zlib, check mode, errors."""
import hashlib
import os
import random
import unittest
import zlib

from blake3_ref import blake3
from support import ON_WSL, Holder, WorkDir, make_sparse, needs_windows, run, win

SHA256 = {
    b"": "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
    b"abc": "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
    b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq": "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
    b"a" * 1000000: "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
}
SHA1 = {
    b"": "da39a3ee5e6b4b0d3255bfef95601890afd80709",
    b"abc": "a9993e364706816aba3e25717850c26c9cd0d89d",
    b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq": "84983e441c3bd26ebaae4aa1f95129e5e54670f1",
    b"a" * 1000000: "34aa973cd4c4daa4f61eeb2bdbad27316534016f",
}
MD5 = {  # RFC 1321 appendix A.5
    b"": "d41d8cd98f00b204e9800998ecf8427e",
    b"a": "0cc175b9c0f1b6a831c399e269772661",
    b"abc": "900150983cd24fb0d6963f7d28e17f72",
    b"message digest": "f96b697d7cb7938d525a2f31aaf161d0",
    b"abcdefghijklmnopqrstuvwxyz": "c3fcd3d76192e4007dfb496cca67e13b",
    b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789": "d174ab98d277d9f5a5611c2c9f419d9f",
    b"1234567890" * 8: "57edf4a22be3c955ac49da2e2107b67a",
}
CRC32 = {b"": "00000000", b"123456789": "cbf43926", b"a": "e8b7be43"}
BLAKE3 = {  # from the BLAKE3 test vectors: input bytes are 0, 1, 2 ... 250, 0, 1 ...
    0: "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262",
    1: "2d3adedff11b61f14c886e35afa036736dcd87a74d27b5c1510225d0f592e213",
}
LENGTHS = [0, 1, 2, 3, 55, 56, 57, 63, 64, 65, 119, 120, 127, 128, 129, 1000, 1023, 1024, 1025, 2047, 2048,
           2049, 3071, 3072, 3073, 4095, 4096, 4097, 7168, 8192, 8193, 65535, 65536, 65537, 1048575,
           1048576, 1048577]
BIG = 5000000  # only against hashlib and zlib: the Python BLAKE3 would take minutes


def blake3_input(n):
    return bytes(i % 251 for i in range(n))


def digest_of(out, tag):
    for line in out.splitlines():
        if line.startswith(tag + " ("):
            return line.rsplit(" = ", 1)[1]
    raise AssertionError(f"no {tag} line in {out!r}")


@needs_windows
class HashVectors(WorkDir):
    def hash(self, data, algorithm):
        p = self.write("v.bin", data)
        result = self.tool("thash", "-a", algorithm, "v.bin")
        self.assertEqual(result.code, 0, result.errtext)
        self.assertTrue(result.text.endswith("  v.bin\n"), result.text)
        return result.text.split()[0]

    def test_sha256_published(self):
        for data, want in SHA256.items():
            self.assertEqual(self.hash(data, "sha256"), want, data[:20])

    def test_sha1_published(self):
        for data, want in SHA1.items():
            self.assertEqual(self.hash(data, "sha1"), want, data[:20])

    def test_md5_published(self):
        for data, want in MD5.items():
            self.assertEqual(self.hash(data, "md5"), want, data)

    def test_crc32_published(self):
        for data, want in CRC32.items():
            self.assertEqual(self.hash(data, "crc32"), want, data)

    def test_blake3_published(self):
        for n, want in BLAKE3.items():
            self.assertEqual(self.hash(blake3_input(n), "blake3"), want, n)
        self.assertEqual(self.hash(b"abc", "blake3"), "6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85")

    def test_blake3_reference_on_every_boundary(self):
        for n in [n for n in LENGTHS if n <= 70000] + [200000, 300000]:
            data = blake3_input(n)
            self.assertEqual(self.hash(data, "blake3"), blake3(data).hex(), n)

    def test_random_data_all_algorithms_in_one_pass(self):
        rng = random.Random(20261123)
        for n in LENGTHS + [BIG]:
            data = rng.randbytes(n)
            p = self.write("r.bin", data)
            result = self.tool("thash", "-a", "all", "r.bin")
            self.assertEqual(result.code, 0, result.errtext)
            self.assertEqual(digest_of(result.text, "SHA256"), hashlib.sha256(data).hexdigest(), n)
            self.assertEqual(digest_of(result.text, "SHA1"), hashlib.sha1(data).hexdigest(), n)
            self.assertEqual(digest_of(result.text, "MD5"), hashlib.md5(data).hexdigest(), n)
            self.assertEqual(digest_of(result.text, "CRC32"), f"{zlib.crc32(data):08x}", n)
            if n <= 70000:
                self.assertEqual(digest_of(result.text, "BLAKE3"), blake3(data).hex(), n)
            elif n >= 1048576:  # too slow for the Python reference: check it is independent of how the bytes arrive
                piped = self.tool("thash", "-a", "blake3", input=data)
                self.assertEqual(piped.text.split()[0], digest_of(result.text, "BLAKE3"), n)

    def test_standard_input_in_odd_pieces(self):
        data = random.Random(5).randbytes(300000)
        result = self.tool("thash", "-a", "sha256,crc32", input=data)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(digest_of(result.text, "SHA256").replace("(-)", ""), hashlib.sha256(data).hexdigest())
        plain = self.tool("thash", input=data)
        self.assertEqual(plain.text, hashlib.sha256(data).hexdigest() + "  -\n")
        dash = self.tool("thash", "-", input=data)
        self.assertEqual(dash.text, plain.text)

    def test_empty_file(self):
        self.write("empty", b"")
        result = self.tool("thash", "-a", "all", "empty")
        self.assertEqual(result.code, 0)
        self.assertEqual(digest_of(result.text, "SHA256"), SHA256[b""])
        self.assertEqual(digest_of(result.text, "BLAKE3"), BLAKE3[0])

    def test_file_over_4_gib_by_size_arithmetic(self):
        size = (4 << 30) + 4096
        tail = b"end of the sparse file\n"
        p = make_sparse(self.path("big.bin"), size, [(size - len(tail), tail)])
        if p is None:
            self.skipTest("this volume cannot make sparse files")
        crc = zlib.crc32(b"\0" * (1 << 24), 0)
        crc = 0
        block = b"\0" * (1 << 24)
        remaining = size - len(tail)
        while remaining:
            take = min(remaining, len(block))
            crc = zlib.crc32(block[:take], crc)
            remaining -= take
        crc = zlib.crc32(tail, crc)
        result = self.tool("thash", "-a", "crc32", "big.bin", timeout=300)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text.split()[0], f"{crc:08x}")


@needs_windows
class HashBehaviour(WorkDir):
    def test_unicode_name_round_trip_through_check(self):
        name = "café 日本語 \U0001F600.txt"
        self.write(name, b"unicode name")
        result = self.tool("thash", name)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.out, (hashlib.sha256(b"unicode name").hexdigest() + "  " + name + "\n").encode())
        self.write("sums.txt", result.out)
        checked = self.tool("thash", "-c", "sums.txt")
        self.assertEqual((checked.code, checked.out), (0, (name + ": OK\n").encode()))

    def test_check_reports_each_file_and_exit_codes(self):
        self.write("a.txt", b"alpha")
        self.write("b.txt", b"bravo")
        sums = self.tool("thash", "-a", "all", "a.txt", "b.txt").out
        self.write("sums.txt", sums)
        for algo in ("sha256", "sha1", "md5", "crc32", "blake3"):
            one = self.tool("thash", "-a", algo, "--tag", "a.txt", "b.txt").out
            self.write("one.txt", one)
        ok = self.tool("thash", "-c", "sums.txt")
        self.assertEqual(ok.code, 0, ok.errtext)
        self.assertEqual(ok.text.count(": OK"), 10)
        self.write("b.txt", b"changed")
        bad = self.tool("thash", "-c", "sums.txt")
        self.assertEqual(bad.code, 1)
        self.assertIn("a.txt: OK", bad.text)
        self.assertIn("b.txt: FAILED\n", bad.text)
        self.assertIn("did NOT match", bad.errtext)
        quiet = self.tool("thash", "-q", "-c", "sums.txt")
        self.assertEqual(quiet.code, 1)
        self.assertNotIn(": OK", quiet.text)
        self.assertIn("b.txt: FAILED", quiet.text)

    def test_check_plain_lines_and_binary_marker(self):
        self.write("f", b"plain")
        for algo, make in (("sha256", hashlib.sha256), ("sha1", hashlib.sha1), ("md5", hashlib.md5)):
            self.write("l.txt", f"{make(b'plain').hexdigest()} *f\n")
            self.assertEqual(self.tool("thash", "-c", "l.txt").code, 0, algo)
        self.write("l.txt", f"{zlib.crc32(b'plain'):08X}  f\n")
        self.assertEqual(self.tool("thash", "-c", "l.txt").code, 0)
        self.write("l.txt", f"{blake3(b'plain').hex()}  f\n")
        self.assertEqual(self.tool("thash", "-c", "l.txt").code, 1)  # read as SHA-256, so it does not match
        self.assertEqual(self.tool("thash", "-a", "blake3", "-c", "l.txt").code, 0)

    def test_check_missing_file_malformed_line_and_empty_list_are_errors(self):
        self.write("l.txt", f"{hashlib.sha256(b'x').hexdigest()}  nothere\n")
        result = self.tool("thash", "-c", "l.txt")
        self.assertEqual(result.code, 2)
        self.assertIn("nothere: FAILED open or read", result.text)
        self.assertIn("(error 2)", result.errtext)
        self.write("l.txt", "this is not a checksum line\n")
        result = self.tool("thash", "-c", "l.txt")
        self.assertEqual(result.code, 2)
        self.assertIn("line 1: improperly formatted", result.errtext)
        self.write("l.txt", "")
        self.assertEqual(self.tool("thash", "-c", "l.txt").code, 2)
        self.assertEqual(self.tool("thash", "-c", "missing-list.txt").code, 2)

    def test_check_good_and_bad_lines_mixed(self):
        self.write("f", b"x")
        self.write("l.txt", f"{hashlib.md5(b'x').hexdigest()}  f\ngarbage\n")
        result = self.tool("thash", "-c", "l.txt")
        self.assertEqual(result.code, 2)
        self.assertIn("f: OK", result.text)

    def test_check_from_standard_input(self):
        self.write("f", b"x")
        line = f"{hashlib.sha256(b'x').hexdigest()}  f\n".encode()
        self.assertEqual(self.tool("thash", "-c", "-", input=line).code, 0)

    def test_missing_file_is_one_line_with_the_windows_error(self):
        result = self.tool("thash", "nothere.bin")
        self.assertEqual(result.code, 2)
        self.assertEqual(result.out, b"")
        self.assertEqual(result.errtext.count("\n"), 1)
        self.assertTrue(result.errtext.startswith("thash: nothere.bin: "), result.errtext)
        self.assertTrue(result.errtext.rstrip().endswith("(error 2)"), result.errtext)

    def test_an_error_does_not_stop_the_other_files(self):
        self.write("good", b"good")
        result = self.tool("thash", "nothere", "good")
        self.assertEqual(result.code, 2)
        self.assertEqual(result.text, hashlib.sha256(b"good").hexdigest() + "  good\n")

    def test_folder_is_an_error(self):
        self.path("d").mkdir()
        result = self.tool("thash", "d")
        self.assertEqual(result.code, 2)
        self.assertIn("is a folder", result.errtext)

    def test_locked_file_is_an_error_and_prints_no_hash(self):
        p = self.write("locked.bin", b"data")
        holder = Holder(p, "None")
        try:
            result = self.tool("thash", "locked.bin")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertEqual(result.out, b"")
        self.assertIn("(error 32)", result.errtext)

    def test_read_error_in_the_middle_of_a_file_is_an_error(self):
        p = self.write("half.bin", os.urandom(300000))
        holder = Holder(p, "ReadWrite", lock=(100000, 1000))
        try:
            result = self.tool("thash", "half.bin")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertEqual(result.out, b"")
        self.assertIn("(error 33)", result.errtext)
        self.assertRegex(result.errtext, r"read failed in the block at offset \d+")

    def test_options(self):
        for flag in ("--help", "-h"):
            result = self.tool("thash", flag)
            self.assertEqual(result.code, 0)
            self.assertIn("Usage: thash", result.text)
        result = self.tool("thash", "--version")
        self.assertEqual((result.code, result.text), (0, "thash (Command) 0.1.0\n"))
        for bad in (["--nope"], ["-a", "sha7", "x"], ["-a"], ["-q", "x"], ["-c", "x", "y"]):
            result = self.tool("thash", *bad)
            self.assertEqual(result.code, 2, bad)
            self.assertEqual(result.out, b"", bad)
            self.assertTrue(result.errtext.startswith("thash: "), bad)

    def test_arguments_after_double_dash_are_files(self):
        self.write("-a", b"dash")
        result = self.tool("thash", "--", "-a")
        self.assertEqual(result.text, hashlib.sha256(b"dash").hexdigest() + "  -a\n")

    def test_option_spellings(self):
        self.write("f", b"f")
        want = hashlib.md5(b"f").hexdigest() + "  f\n"
        for args in (["-a", "md5"], ["-amd5"], ["--algorithm", "md5"], ["--algorithm=MD5"]):
            self.assertEqual(self.tool("thash", *args, "f").text, want, args)

    def test_empty_argument_is_a_missing_file(self):
        result = self.tool("thash", "")
        self.assertEqual(result.code, 2)

    def test_absolute_path_and_long_path(self):
        deep = self.path(*(["segment-of-a-long-folder-name"] * 12))
        os.makedirs(deep if not os.name == "nt" else "\\\\?\\" + str(deep))
        target = deep / "f.txt"
        (target if os.name != "nt" else __import__("pathlib").Path("\\\\?\\" + str(target))).write_bytes(b"deep")
        full = win(target)
        self.assertGreater(len(full), 300)
        result = run("thash", full)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text.split()[0], hashlib.sha256(b"deep").hexdigest())


if __name__ == "__main__":
    unittest.main()
