"""tcmp: two files, two folders, big files, deep folders, errors."""
import os
import unittest

from support import (Child, Holder, WorkDir, make_deep, make_junction, make_sparse, make_symlink, needs_windows,
                     remove_junction, win)


@needs_windows
class CompareFiles(WorkDir):
    def test_identical(self):
        self.write("a", b"same bytes")
        self.write("b", b"same bytes")
        result = self.tool("tcmp", "a", "b")
        self.assertEqual((result.code, result.text), (0, "a and b are identical\n"))

    def test_two_empty_files_are_identical(self):
        self.write("a", b"")
        self.write("b", b"")
        self.assertEqual(self.tool("tcmp", "a", "b").code, 0)

    def test_first_difference(self):
        self.write("a", b"abcdef")
        self.write("b", b"abXdef")
        result = self.tool("tcmp", "a", "b")
        self.assertEqual((result.code, result.text), (1, "differ at offset 2 (0x2): 0x63 vs 0x58\n"))

    def test_difference_at_the_buffer_boundaries(self):
        n = 3 * (1 << 20)
        base = bytearray(os.urandom(n))
        for at in (0, (1 << 20) - 1, 1 << 20, (1 << 20) + 1, 2 * (1 << 20), n - 1):
            other = bytearray(base)
            other[at] ^= 0xFF
            self.write("a", bytes(base))
            self.write("b", bytes(other))
            result = self.tool("tcmp", "a", "b")
            self.assertEqual(result.code, 1, at)
            self.assertTrue(result.text.startswith(f"differ at offset {at} (0x{at:x}): "), (at, result.text))

    def test_size_difference_with_equal_prefix(self):
        self.write("a", b"abc")
        self.write("b", b"abcdef")
        result = self.tool("tcmp", "a", "b")
        self.assertEqual(result.code, 1)
        self.assertEqual(result.text, "differ in size: a is 3 bytes, b is 6 bytes; the bytes they share are identical\n")
        self.write("e", b"")
        self.assertEqual(self.tool("tcmp", "a", "e").code, 1)

    def test_all_differences(self):
        self.write("a", b"0123456789")
        self.write("b", b"01X34YY789ZZ")
        result = self.tool("tcmp", "-a", "a", "b")
        self.assertEqual(result.code, 1)
        self.assertEqual(result.text,
            "differ at offset 2 (0x2): 1 byte\n"
            "differ at offset 5 (0x5): 2 bytes\n"
            "differ in size: a is 10 bytes, b is 12 bytes\n"
            "3 bytes differ in 2 places\n")
        self.assertEqual(self.tool("tcmp", "--all", "a", "a").text, "a and a are identical\n")

    def test_all_differences_across_a_buffer_boundary(self):
        base = bytearray(2 * (1 << 20))
        other = bytearray(base)
        for i in range((1 << 20) - 2, (1 << 20) + 3):
            other[i] = 1
        self.write("a", bytes(base))
        self.write("b", bytes(other))
        result = self.tool("tcmp", "-a", "a", "b")
        self.assertEqual(result.text, f"differ at offset {(1 << 20) - 2} (0x{(1 << 20) - 2:x}): 5 bytes\n5 bytes differ in 1 place\n")

    def test_quiet(self):
        self.write("a", b"1")
        self.write("b", b"2")
        self.write("c", b"1")
        for args, code in ((["a", "b"], 1), (["a", "c"], 0)):
            result = self.tool("tcmp", "-q", *args)
            self.assertEqual((result.code, result.out), (code, b""))

    def test_errors(self):
        self.write("a", b"1")
        self.path("d").mkdir()
        for args in (["a", "nothere"], ["nothere", "a"], ["a", "d"], ["d", "a"], ["a"], ["a", "a", "a"], ["--nope", "a", "a"]):
            result = self.tool("tcmp", *args)
            self.assertEqual(result.code, 2, args)
            self.assertEqual(result.out, b"", args)
            self.assertTrue(result.errtext.startswith("tcmp: "), args)
        self.assertTrue(self.tool("tcmp", "a", "nothere").errtext.rstrip().endswith("(error 2)"))

    def test_unicode_names(self):
        for name in ("\u00e9\u65e5.txt", "\U0001F600.txt"):
            self.write(name, b"1")
        self.assertEqual(self.tool("tcmp", "\u00e9\u65e5.txt", "\U0001F600.txt").code, 0)

    def test_locked_and_mid_file_read_errors(self):
        data = os.urandom(300000)
        a = self.write("a", data)
        self.write("b", data)
        holder = Holder(a, "None")
        try:
            self.assertEqual(self.tool("tcmp", "a", "b").code, 2)
        finally:
            holder.close()
        holder = Holder(a, "ReadWrite", lock=(100000, 1000))
        try:
            result = self.tool("tcmp", "a", "b")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertIn("(error 33)", result.errtext)

    def test_files_over_4_gib(self):
        size = (4 << 30) + 4096
        a = make_sparse(self.path("a.bin"), size, [(size - 100, b"tail")])
        if a is None:
            self.skipTest("this volume cannot make sparse files")
        make_sparse(self.path("b.bin"), size, [(size - 100, b"tail")])
        same = self.tool("tcmp", "a.bin", "b.bin", timeout=300)
        self.assertEqual((same.code, same.text), (0, "a.bin and b.bin are identical\n"), same.errtext)
        make_sparse(self.path("c.bin"), size, [(size - 100, b"tAil")])
        diff = self.tool("tcmp", "a.bin", "c.bin", timeout=300)
        self.assertEqual(diff.code, 1)
        self.assertEqual(diff.text, f"differ at offset {size - 99} (0x{size - 99:x}): 0x61 vs 0x41\n")

    def test_a_file_that_shrinks_while_compared_is_an_error_never_identical(self):
        size = (8 << 30) + 4096
        make_sparse(self.path("a.bin"), size) or self.skipTest("this volume cannot make sparse files")
        b = make_sparse(self.path("b.bin"), size)
        with Holder(b, "ReadWrite", then=f"$f.SetLength({size // 2})", delay=0.5) as holder:
            with Child("tcmp", "a.bin", "b.bin", cwd=self.dir) as child:
                holder.trigger()
                result = child.finish(timeout=300)
        self.assertEqual(result.code, 2, result.out)
        self.assertNotIn(b"identical", result.out)
        self.assertIn(b"changed while it was being read", result.err)

    def test_a_change_is_an_error_even_when_a_difference_is_found_first(self):
        size = (8 << 30) + 4096
        a = make_sparse(self.path("a.bin"), size, [(5 << 30, b"x")])
        a or self.skipTest("this volume cannot make sparse files")
        b = make_sparse(self.path("b.bin"), size, [(5 << 30, b"y")])
        with Holder(b, "ReadWrite", then=f"$f.SetLength({7 << 30})", delay=0.5) as holder:
            with Child("tcmp", "a.bin", "b.bin", cwd=self.dir) as child:
                holder.trigger()
                result = child.finish(timeout=300)
        self.assertEqual(result.code, 2, result.out)
        self.assertIn(b"changed while it was being read", result.err)

    def test_sizes_around_every_buffer_boundary(self):
        mib = 1 << 20
        sizes = [0, 1, mib - 1, mib, mib + 1, 2 * mib - 1, 2 * mib, 2 * mib + 1, 3 * mib + 7]
        for first in sizes:
            for second in sizes:
                self.write("a", b"\x00" * first)
                self.write("b", b"\x00" * second)
                result = self.tool("tcmp", "a", "b")
                label = (first, second, result.errtext)
                if first == second:
                    self.assertEqual((result.code, result.text), (0, "a and b are identical\n"), label)
                else:
                    self.assertEqual(result.code, 1, label)
                    self.assertEqual(result.text, f"differ in size: a is {first} bytes, b is {second} bytes; the bytes they share are identical\n", label)
        for size in sizes[1:]:  # equal sizes, one byte different at the first, last and middle positions
            for at in {0, size - 1, size // 2}:
                data = bytearray(size)
                other = bytearray(data)
                other[at] = 1
                self.write("a", bytes(data))
                self.write("b", bytes(other))
                result = self.tool("tcmp", "a", "b")
                self.assertEqual((result.code, result.text.split(":")[0]), (1, f"differ at offset {at} (0x{at:x})"), (size, at))

    def test_options(self):
        self.assertIn("Usage: tcmp", self.tool("tcmp", "--help").text)
        self.assertEqual(self.tool("tcmp", "--version").text, "tcmp (Command) 0.1.0\n")


@needs_windows
class CompareFolders(WorkDir):
    def test_identical_folders(self):
        for side in ("a", "b"):
            self.write(f"{side}/x.txt", b"x")
            self.write(f"{side}/sub/y.txt", b"y")
        result = self.tool("tcmp", "a", "b")
        self.assertEqual((result.code, result.text), (0, "2 same, 0 different, 0 only in the first, 0 only in the second\n"))
        verbose = self.tool("tcmp", "-v", "a", "b")
        self.assertEqual(verbose.text, "same:           sub\\y.txt\nsame:           x.txt\n2 same, 0 different, 0 only in the first, 0 only in the second\n")

    def test_only_different_and_same(self):
        self.write("a/same.txt", b"1")
        self.write("b/same.txt", b"1")
        self.write("a/diff.txt", b"aaaa")
        self.write("b/diff.txt", b"aaab")
        self.write("a/size.txt", b"1")
        self.write("b/size.txt", b"12")
        self.write("a/only-a.txt", b"1")
        self.write("b/only-b.txt", b"1")
        self.write("a/dir-a/f", b"1")
        self.write("b/sub/deep/only", b"1")
        self.write("a/sub/deep/only", b"1")
        self.write("a/sub/deep/left", b"1")
        result = self.tool("tcmp", "a", "b")
        self.assertEqual(result.code, 1)
        self.assertEqual(result.text,
            "different:      diff.txt\n"
            "only in first:  dir-a\\\n"
            "only in first:  only-a.txt\n"
            "only in second: only-b.txt\n"
            "different:      size.txt (size)\n"
            "only in first:  sub\\deep\\left\n"
            "2 same, 2 different, 3 only in the first, 1 only in the second\n")

    def test_kind_mismatch_and_case_insensitive_names(self):
        self.write("a/x", b"1")
        self.write("b/x/inner", b"1")
        self.write("a/Name.TXT", b"1")
        self.write("b/name.txt", b"1")
        result = self.tool("tcmp", "a", "b")
        self.assertEqual(result.code, 1)
        self.assertIn("different:      x (a file in the first, a folder in the second)\n", result.text)
        self.assertIn("1 same, 1 different, 0 only in the first, 0 only in the second\n", result.text)

    def test_empty_folders(self):
        self.path("a").mkdir()
        self.path("b").mkdir()
        result = self.tool("tcmp", "a", "b")
        self.assertEqual((result.code, result.text), (0, "0 same, 0 different, 0 only in the first, 0 only in the second\n"))

    def test_quiet_folders(self):
        self.write("a/x", b"1")
        self.path("b").mkdir()
        result = self.tool("tcmp", "-q", "a", "b")
        self.assertEqual((result.code, result.out), (1, b""))

    def test_unicode_names(self):
        self.write("a/\u65e5\u672c/\u00e9.txt", b"1")
        self.write("b/\u65e5\u672c/\u00e9.txt", b"2")
        result = self.tool("tcmp", "a", "b")
        self.assertEqual(result.code, 1)
        self.assertIn("different:      \u65e5\u672c\\\u00e9.txt\n", result.text)

    def test_junction_is_skipped_and_the_result_is_not_identical(self):
        self.write("real/f", b"1")
        for side in ("a", "b"):
            self.write(f"{side}/own", b"1")
        if not (make_junction(self.path("a", "link"), self.path("real")) and make_junction(self.path("b", "link"), self.path("real"))):
            self.skipTest("could not create a junction here")
        self.addCleanup(remove_junction, self.path("a", "link"))
        self.addCleanup(remove_junction, self.path("b", "link"))
        result = self.tool("tcmp", "a", "b")
        self.assertEqual(result.code, 1)
        self.assertIn("skipped:        link", result.text)
        self.assertIn("1 skipped", result.text)

    def test_a_junction_given_as_a_folder_is_skipped_like_one_inside(self):
        self.path("real").mkdir()
        self.path("plain").mkdir()
        if not make_junction(self.path("link"), self.path("real")):
            self.skipTest("could not create a junction here")
        self.addCleanup(remove_junction, self.path("link"))
        result = self.tool("tcmp", "link", "plain")
        self.assertEqual(result.code, 1)
        self.assertEqual(result.text, "skipped:        link (symbolic link or junction, not entered)\n0 same, 0 different, 0 only in the first, 0 only in the second, 1 skipped\n")
        self.assertEqual(self.tool("tcmp", "plain", "link").code, 1)
        self.assertEqual(self.tool("tcmp", "link\\", "plain\\").code, 1)  # trailing slashes change nothing

    def test_a_file_link_is_compared_by_what_it_reads(self):
        self.write("a/x", b"same bytes")
        self.write("b/real", b"same bytes")
        if not make_symlink(self.path("b", "x"), self.path("b", "real")):
            self.skipTest("this account cannot make symbolic links")
        self.path("b", "real").unlink()
        self.write("b/real", b"same bytes")
        result = self.tool("tcmp", "-v", "a", "b")
        self.assertIn("same:           x\n", result.text)
        self.assertNotIn("different:      x", result.text)

    def test_deep_folders_do_not_overflow_the_stack(self):
        for side in ("a", "b"):
            (make_deep(self.path(side), 1500) / "f").write_bytes(b"deep")
        a, b = win(self.path("a")), win(self.path("b"))  # absolute: relative paths keep the 260 character limit
        result = self.tool("tcmp", a, b, timeout=120)
        self.assertEqual((result.code, result.text), (0, "1 same, 0 different, 0 only in the first, 0 only in the second\n"), result.errtext)
        (self.path("b").joinpath(*(["a"] * 1500)) / "f").write_bytes(b"deeP")
        self.assertEqual(self.tool("tcmp", a, b, timeout=120).code, 1)

    def test_unreadable_file_in_a_folder_is_an_error(self):
        self.write("a/f", b"x" * 300000)
        self.write("b/f", b"x" * 300000)
        holder = Holder(self.path("a", "f"), "None")
        try:
            result = self.tool("tcmp", "a", "b")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertIn("(error 32)", result.errtext)


if __name__ == "__main__":
    unittest.main()
