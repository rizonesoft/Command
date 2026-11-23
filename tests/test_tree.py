"""ttree: layout, sizes, totals, depth limit, reparse points, deep folders, errors."""
import unittest

from support import (WorkDir, make_deep, make_junction, make_sparse, make_symlink, needs_windows, remove_junction, win)


@needs_windows
class Tree(WorkDir):
    def build(self):
        self.write("root/a/one.txt", b"x" * 10)
        self.write("root/a/sub/two.txt", b"y" * 100)
        self.write("root/b.txt", b"z" * 1000)
        self.path("root", "empty").mkdir()

    def test_layout_sizes_and_totals_in_ascii(self):
        self.build()
        result = self.tool("ttree", "-b", "--ascii", "root")
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text,
            "root  1,110\n"
            "+-- a\\  110\n"
            "|   +-- one.txt  10\n"
            "|   \\-- sub\\  100\n"
            "|       \\-- two.txt  100\n"
            "+-- b.txt  1,000\n"
            "\\-- empty\\  0\n"
            "\n"
            "3 folders, 3 files, 1,110 in total\n")

    def test_box_characters_and_human_sizes(self):
        self.write("r/big.bin", b"0" * 1536)
        result = self.tool("ttree", "r")
        self.assertEqual(result.text, "r  1.5 KiB\n\u2514\u2500\u2500 big.bin  1.5 KiB\n\n0 folders, 1 file, 1.5 KiB in total\n")
        self.write("r/sub/x", b"1")
        nested = self.tool("ttree", "r").text
        self.assertIn("\u251c\u2500\u2500 big.bin  1.5 KiB\n\u2514\u2500\u2500 sub\\  1 B\n    \u2514\u2500\u2500 x  1 B\n", nested)
        self.write("r/a/b/c", b"1")
        self.assertIn("\u2502   \u2514\u2500\u2500 b\\", self.tool("ttree", "r").text)

    def test_depth_limit_keeps_totals(self):
        self.build()
        zero = self.tool("ttree", "-b", "-d", "0", "root")
        self.assertEqual(zero.text, "root  1,110\n\n3 folders, 3 files, 1,110 in total\n")
        one = self.tool("ttree", "-b", "--ascii", "-d", "1", "root")
        self.assertEqual(one.text, "root  1,110\n+-- a\\  110\n+-- b.txt  1,000\n\\-- empty\\  0\n\n3 folders, 3 files, 1,110 in total\n")
        self.assertEqual(self.tool("ttree", "-b", "-d", "9", "root").text, self.tool("ttree", "-b", "root").text)
        self.assertEqual(self.tool("ttree", "-d", "x", "root").code, 2)

    def test_folders_only(self):
        self.build()
        result = self.tool("ttree", "-b", "--ascii", "-D", "root")
        self.assertEqual(result.text, "root  1,110\n+-- a\\  110\n|   \\-- sub\\  100\n\\-- empty\\  0\n\n3 folders, 3 files, 1,110 in total\n")

    def test_default_path_is_the_current_folder(self):
        self.write("f", b"123")
        result = self.tool("ttree", "-b")
        self.assertTrue(result.text.startswith(".  3\n"), result.text)

    def test_unicode_names(self):
        self.write("r/\u65e5\u672c\u8a9e/\u00e9\U0001F600.txt", b"1234")
        result = self.tool("ttree", "-b", "--ascii", "r")
        self.assertEqual(result.text, "r  4\n\\-- \u65e5\u672c\u8a9e\\  4\n    \\-- \u00e9\U0001F600.txt  4\n\n1 folder, 1 file, 4 in total\n")

    def test_empty_folder(self):
        self.path("e").mkdir()
        self.assertEqual(self.tool("ttree", "-b", "e").text, "e  0\n\n0 folders, 0 files, 0 in total\n")

    def test_missing_path_and_a_file_are_errors(self):
        self.write("f", b"x")
        self.assertEqual(self.tool("ttree", "nothere").code, 2)
        result = self.tool("ttree", "f")
        self.assertEqual(result.code, 2)
        self.assertIn("is not a folder", result.errtext)
        both = self.tool("ttree", "-b", "nothere", "f")
        self.assertEqual(both.code, 2)

    def test_several_roots(self):
        self.write("a/x", b"1")
        self.write("b/y", b"22")
        result = self.tool("ttree", "-b", "a", "b")
        self.assertEqual(result.code, 0)
        self.assertEqual(result.text.count("in total"), 2)

    def test_junction_is_listed_not_followed_or_counted(self):
        self.write("real/f.txt", b"x" * 500)
        self.write("top/own.txt", b"y" * 5)
        if not make_junction(self.path("top", "link"), self.path("real")):
            self.skipTest("could not create a junction here")
        self.addCleanup(remove_junction, self.path("top", "link"))
        result = self.tool("ttree", "-b", "--ascii", "top")
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text, "top  5\n+-- link\\  [junction]\n\\-- own.txt  5\n\n0 folders, 1 file, 5 in total\n")

    def test_a_junction_given_as_the_path_is_listed_not_entered(self):
        self.write("real/f.txt", b"x" * 5)
        if not make_junction(self.path("link"), self.path("real")):
            self.skipTest("could not create a junction here")
        self.addCleanup(remove_junction, self.path("link"))
        result = self.tool("ttree", "-b", "link")
        self.assertEqual((result.code, result.text), (0, "link  [junction]\n\n0 folders, 0 files, 0 in total\n"))
        self.assertEqual(self.tool("ttree", "-b", "link\\").text, result.text.replace("link  ", "link\\  "))

    def test_a_file_link_is_listed_and_the_summary_does_not_count_it(self):
        self.write("r/real.txt", b"12345")
        if not make_symlink(self.path("r", "alias.txt"), self.path("r", "real.txt")):
            self.skipTest("this account cannot make symbolic links")
        result = self.tool("ttree", "-b", "--ascii", "r")
        self.assertEqual(result.text, "r  5\n+-- alias.txt  [symlink]\n\\-- real.txt  5\n\n0 folders, 1 file, 5 in total\n")

    def test_deep_folders_do_not_overflow_the_stack(self):
        bottom = make_deep(self.path("deep"), 1500)
        (bottom / "f.bin").write_bytes(b"q" * 77)
        root = win(self.path("deep"))  # absolute: relative paths keep the 260 character limit of Windows
        result = self.tool("ttree", "-b", "-d", "2", root, timeout=120)
        self.assertEqual(result.code, 0, result.errtext)
        self.assertEqual(result.text.splitlines()[-1], "1500 folders, 1 file, 77 in total")
        self.assertTrue(result.text.startswith(root + "  77\n\u2514\u2500\u2500 a\\  77\n    \u2514\u2500\u2500 a\\  77\n"))
        full = self.tool("ttree", "-b", "--ascii", "-D", root, timeout=120)
        self.assertEqual(full.code, 0, full.errtext)
        self.assertEqual(full.text.count("\n"), 1502 + 1)

    def test_sizes_over_4_gib(self):
        size = (4 << 30) + 4096
        if make_sparse(self.path("r", "big.bin"), size) is None:
            self.skipTest("this volume cannot make sparse files")
        self.write("r/small", b"1")
        exact = self.tool("ttree", "-b", "r")
        self.assertIn("big.bin  4,294,971,392\n", exact.text)
        self.assertTrue(exact.text.endswith("0 folders, 2 files, 4,294,971,393 in total\n"))
        self.assertIn("big.bin  4.0 GiB\n", self.tool("ttree", "r").text)

    def test_options(self):
        self.assertIn("Usage: ttree", self.tool("ttree", "--help").text)
        self.assertEqual(self.tool("ttree", "--version").text, "ttree (Command) 0.1.0\n")
        result = self.tool("ttree", "--nope")
        self.assertEqual(result.code, 2)
        self.assertTrue(result.errtext.startswith("ttree: "))


if __name__ == "__main__":
    unittest.main()
