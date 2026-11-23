"""tfind: matches, exit codes, binary files, big lines and files, folders, errors."""
import os
import subprocess
import time
import unittest

from support import (Child, Holder, WorkDir, make_deep, make_junction, make_sparse, needs_windows, remove_junction, win)


@needs_windows
class Find(WorkDir):
    def test_found_prints_path_line_and_text(self):
        self.write("f.txt", "first\nhello world\nthird hello\n")
        result = self.tool("tfind", "hello", "f.txt")
        self.assertEqual((result.code, result.text), (0, "f.txt:2:hello world\nf.txt:3:third hello\n"))

    def test_not_found_is_one_and_prints_nothing(self):
        self.write("f.txt", "abc\n")
        result = self.tool("tfind", "zzz", "f.txt")
        self.assertEqual((result.code, result.out, result.err), (1, b"", b""))

    def test_missing_path_is_two(self):
        result = self.tool("tfind", "x", "nothere.txt")
        self.assertEqual(result.code, 2)
        self.assertEqual(result.errtext.count("\n"), 1)
        self.assertTrue(result.errtext.rstrip().endswith("(error 2)"), result.errtext)

    def test_an_error_beats_a_match(self):
        self.write("f.txt", "x\n")
        result = self.tool("tfind", "x", "f.txt", "nothere.txt")
        self.assertEqual(result.code, 2)
        self.assertEqual(result.text, "f.txt:1:x\n")

    def test_ignore_case_is_ascii_only(self):
        self.write("f.txt", "Hello WORLD\nÉcole\n")
        self.assertEqual(self.tool("tfind", "hello world", "f.txt").code, 1)
        self.assertEqual(self.tool("tfind", "-i", "hello world", "f.txt").text, "f.txt:1:Hello WORLD\n")
        self.assertEqual(self.tool("tfind", "--ignore-case", "HELLO", "f.txt").code, 0)
        self.assertEqual(self.tool("tfind", "-i", "école", "f.txt").code, 1)  # only A to Z fold
        self.assertEqual(self.tool("tfind", "École", "f.txt").text, "f.txt:2:École\n")

    def test_options_n_l_q(self):
        self.write("f.txt", "a1\nb\na2\n")
        self.assertEqual(self.tool("tfind", "-N", "a", "f.txt").text, "f.txt:a1\nf.txt:a2\n")
        self.assertEqual(self.tool("tfind", "-l", "a", "f.txt").text, "f.txt\n")
        quiet = self.tool("tfind", "-q", "a", "f.txt")
        self.assertEqual((quiet.code, quiet.out), (0, b""))
        self.assertEqual(self.tool("tfind", "-q", "zzz", "f.txt").code, 1)
        self.assertEqual(self.tool("tfind", "-Ni", "A1", "f.txt").text, "f.txt:a1\n")

    def test_line_endings_and_last_line_without_newline(self):
        self.write("f.txt", b"one\r\ntwo\r\nlast line")
        self.assertEqual(self.tool("tfind", "two", "f.txt").out, b"f.txt:2:two\n")
        self.assertEqual(self.tool("tfind", "last", "f.txt").out, b"f.txt:3:last line\n")
        self.assertEqual(self.tool("tfind", "line", "f.txt").out, b"f.txt:3:last line\n")

    def test_empty_file(self):
        self.write("e.txt", b"")
        self.assertEqual(self.tool("tfind", "x", "e.txt").code, 1)

    def test_binary_file_is_reported_and_not_printed(self):
        self.write("b.bin", b"\x00\x01needle\x02\xff")
        result = self.tool("tfind", "needle", "b.bin")
        self.assertEqual((result.code, result.text), (0, "b.bin: binary file matches\n"))
        self.assertNotIn(b"\x00", result.out)
        self.assertEqual(self.tool("tfind", "absent", "b.bin").code, 1)

    def test_binary_match_across_a_read_boundary(self):
        data = b"\x00" * ((1 << 20) - 3) + b"needle" + b"\x00" * 100
        self.write("b.bin", data)
        self.assertEqual(self.tool("tfind", "needle", "b.bin").text, "b.bin: binary file matches\n")

    def test_nul_later_in_a_text_file_makes_a_matching_line_binary(self):
        self.write("t.txt", b"text " * 20000 + b"\nneedle\x00here\nneedle ok\n")
        result = self.tool("tfind", "needle", "t.txt")
        self.assertEqual((result.code, result.text), (0, "t.txt: binary file matches\n"))

    def test_utf16_file_is_an_error(self):
        self.write("u.txt", "needle\n".encode("utf-16"))
        result = self.tool("tfind", "needle", "u.txt")
        self.assertEqual(result.code, 2)
        self.assertIn("UTF-16", result.errtext)

    def test_very_long_line_over_the_buffer(self):
        line = b"x" * 3_000_000 + b"NEEDLE" + b"y" * 10
        self.write("long.txt", b"a\n" + line + b"\nz\n")
        result = self.tool("tfind", "NEEDLE", "long.txt")
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.out, b"long.txt:2:" + line + b"\n")

    def test_match_split_across_the_read_buffer(self):
        body = b"a" * ((1 << 20) - 3) + b"needle\n"
        self.write("f.txt", body + b"next needle\n")
        result = self.tool("tfind", "-N", "needle", "f.txt")
        self.assertEqual(result.out, b"f.txt:" + body[:-1] + b"\nf.txt:next needle\n")

    def test_standard_input(self):
        result = self.tool("tfind", "b", input=b"abc\nxyz\nbcd")
        self.assertEqual((result.code, result.text), (0, "(stdin):1:abc\n(stdin):3:bcd\n"))
        self.assertEqual(self.tool("tfind", "b", "-", input=b"b").text, "(stdin):1:b\n")

    def test_folders_are_searched_in_full_in_sorted_order(self):
        self.write("tree/b.txt", "needle b\n")
        self.write("tree/a/deep/c.txt", "needle c\n")
        self.write("tree/a/z.txt", "no\n")
        self.write("tree/A2.txt", "needle a2\n")
        result = self.tool("tfind", "needle", "tree")
        self.assertEqual(result.text, "tree\\a\\deep\\c.txt:1:needle c\ntree\\A2.txt:1:needle a2\ntree\\b.txt:1:needle b\n")
        self.assertEqual(self.tool("tfind", "-l", "needle", "tree", "tree\\b.txt").text.count("\n"), 4)

    def test_unicode_names_and_text(self):
        name = "日本語 é\U0001F600.txt"
        self.write("dir é/" + name, "добрый день\n")
        result = self.tool("tfind", "день", "dir é")
        self.assertEqual(result.text, "dir é\\" + name + ":1:добрый день\n")

    def test_quoting_edge_cases_reach_the_program_intact(self):
        content = 'say "hi" now\nback\\slash\nending\\\nquote\\"x\n'
        self.write("f.txt", content)
        for pattern, line in (('"hi"', 1), ("back\\slash", 2), ("ending\\", 3), ('quote\\"', 4), ("\\", 2), ('say "hi" now', 1)):
            result = self.tool("tfind", "-N", pattern, "f.txt")
            self.assertEqual(result.code, 0, (pattern, result.errtext))
            self.assertEqual(result.text.splitlines()[0], "f.txt:" + content.splitlines()[line - 1], pattern)

    def test_empty_and_bad_patterns_are_usage_errors(self):
        self.write("f.txt", "x\n")
        for args in (["", "f.txt"], [], ["--nope", "x"], ["a\nb", "f.txt"]):
            result = self.tool("tfind", *args)
            self.assertEqual(result.code, 2, args)
            self.assertEqual(result.out, b"", args)
            self.assertTrue(result.errtext.startswith("tfind: "), args)

    def test_options_help_version(self):
        self.assertIn("Usage: tfind", self.tool("tfind", "--help").text)
        self.assertEqual(self.tool("tfind", "--version").text, "tfind (Command) 0.1.0\n")

    def test_deep_folder_does_not_overflow_the_stack(self):
        bottom = make_deep(self.path("deep"), 1500)
        (bottom / "f.txt").write_text("the needle\n")
        root = win(self.path("deep"))  # relative paths keep the 260 character limit of Windows; absolute ones do not
        result = self.tool("tfind", "needle", root, timeout=120)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text, root + "\\a" * 1500 + "\\f.txt:1:the needle\n")

    def test_long_absolute_path(self):
        bottom = make_deep(self.path("long"), 40, "folder-name-of-twenty")
        (bottom / "f.txt").write_text("needle\n")
        full = win(self.path("long"))
        result = self.tool("tfind", "needle", full)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertGreater(len(result.text), 400)

    def test_junction_is_not_entered(self):
        self.write("real/f.txt", "needle\n")
        self.path("holder").mkdir()
        if not make_junction(self.path("holder", "link"), self.path("real")):
            self.skipTest("could not create a junction here")
        self.addCleanup(remove_junction, self.path("holder", "link"))
        result = self.tool("tfind", "needle", "holder")
        self.assertEqual((result.code, result.out), (1, b""))
        named = self.tool("tfind", "needle", "holder\\link")  # named on the command line: the same, not entered
        self.assertEqual((named.code, named.out), (1, b""))

    def test_a_line_of_exactly_the_limit_is_searched(self):
        limit = 64 << 20
        for tail in (b"", b"\n"):
            self.write("edge.txt", b"a" * limit + tail)
            result = self.tool("tfind", "-l", "a", "edge.txt", timeout=120)
            self.assertEqual((result.code, result.text), (0, "edge.txt\n"), (tail, result.errtext))
        self.write("crlf.txt", b"a" * limit + b"\r\n")  # the CR LF does not count as part of the line
        crlf = self.tool("tfind", "-l", "a", "crlf.txt", timeout=120)
        self.assertEqual((crlf.code, crlf.text), (0, "crlf.txt\n"), crlf.errtext)
        self.write("over.txt", b"a" * (limit + 1) + b"\n")
        result = self.tool("tfind", "-l", "a", "over.txt", timeout=120)
        self.assertEqual(result.code, 2)
        self.assertIn("longer than 64 MiB", result.errtext)

    def piped(self, pieces, *flags):
        """Starts tfind on a pipe and sends the pieces with a pause between them; every wait is bounded."""
        with Child("tfind", *flags, "needle", cwd=self.dir) as child:
            for piece in pieces:
                child.write(piece)
                time.sleep(1)
            child.proc.stdin.close()
            result = child.finish(timeout=60)
        return result.code, result.text, result.errtext

    def test_quiet_stops_at_a_match_without_waiting_for_the_rest_of_the_input(self):
        with Child("tfind", "-q", "needle", cwd=self.dir) as child:
            child.write(b"needle\n")  # the writer stays open: the answer must not wait for 64 KiB or the end
            try:
                code = child.proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                child.kill()
                self.fail("tfind -q waited for more input after a conclusive match")
        self.assertEqual(code, 0)

    def test_the_same_standard_input_twice_is_refused(self):
        result = self.tool("tfind", "x", "-", "-", input=b"x")
        self.assertEqual(result.code, 2)
        self.assertIn("only once", result.errtext)

    def test_a_trailing_slash_does_not_enter_a_junction(self):
        self.write("real/f.txt", "needle\n")
        if not make_junction(self.path("j"), self.path("real")):
            self.skipTest("could not create a junction here")
        self.addCleanup(remove_junction, self.path("j"))
        for operand in ("j", "j\\", "j/"):
            result = self.tool("tfind", "needle", operand)
            self.assertEqual((result.code, result.out), (1, b""), operand)

    def test_binary_detection_waits_for_the_first_64_kib_across_short_reads(self):
        code, out, err = self.piped([b"needle\n", b"more text\n", b"\x00 binary after two short reads\n"])
        self.assertEqual((code, out), (0, "(stdin): binary file matches\n"), err)

    def test_a_utf16_mark_split_across_short_reads_is_still_detected(self):
        code, out, err = self.piped([b"\xff", b"\xfen\x00e\x00e\x00d\x00l\x00e\x00"])
        self.assertEqual(code, 2)
        self.assertIn("UTF-16", err)

    def test_matches_before_a_read_error_stay_printed_and_the_error_is_reported(self):
        body = b"hit\n" + b"x" * ((1 << 20) + 100000)
        p = self.write("half.txt", body)
        holder = Holder(p, "ReadWrite", lock=(1 << 20, 1000))
        try:
            result = self.tool("tfind", "hit", "half.txt")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertEqual(result.text, "half.txt:1:hit\n")
        self.assertIn("(error 33)", result.errtext)

    def test_read_error_mid_file_is_an_error(self):
        p = self.write("half.txt", ("line of text\n" * 30000).encode())
        holder = Holder(p, "ReadWrite", lock=(100000, 1000))
        try:
            result = self.tool("tfind", "zzz", "half.txt")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertIn("(error 33)", result.errtext)

    def test_locked_file_is_an_error(self):
        p = self.write("l.txt", "needle\n")
        holder = Holder(p, "None")
        try:
            result = self.tool("tfind", "needle", "l.txt")
        finally:
            holder.close()
        self.assertEqual(result.code, 2)
        self.assertIn("(error 32)", result.errtext)

    def test_file_over_4_gib(self):
        size = (4 << 30) + 4096
        p = make_sparse(self.path("big.bin"), size, [(size - 40, b"needle past four gibibytes\n")])
        if p is None:
            self.skipTest("this volume cannot make sparse files")
        found = self.tool("tfind", "needle", "big.bin", timeout=300)
        self.assertEqual((found.code, found.text), (0, "big.bin: binary file matches\n"), found.errtext)
        self.assertEqual(self.tool("tfind", "absent", "big.bin", timeout=300).code, 1)


if __name__ == "__main__":
    unittest.main()
