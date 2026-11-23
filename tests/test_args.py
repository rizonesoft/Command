"""The command line parser: the usual Windows quoting rules, empty and Unicode arguments."""
import unittest

from support import needs_windows, run

# Each line is a whole command line; the first word is the program name. Rules from the Microsoft
# documentation "Parsing C++ command-line arguments", with the 2008 rule that "" inside quotes is a quote.
SPLITS = [
    ('p "abc" d e', ["p", "abc", "d", "e"]),
    ('p a\\\\b d"e f"g h', ["p", "a\\\\b", "de fg", "h"]),
    ('p a\\\\\\"b c d', ["p", 'a\\"b', "c", "d"]),
    ('p a\\\\\\\\"b c" d e', ["p", "a\\\\b c", "d", "e"]),
    ('p a"b"" c d', ["p", 'ab" c d']),
    ('p ""', ["p", ""]),
    ('p "" x ""', ["p", "", "x", ""]),
    ('p "a b"', ["p", "a b"]),
    ('p "a b\\\\"', ["p", "a b\\"]),
    ('p "a\\"b"', ["p", 'a"b']),
    ('p a\\b', ["p", "a\\b"]),
    ('p \\"', ["p", '"']),
    ('p \\\\"', ["p", "\\"]),
    ('p x"y z"', ["p", "xy z"]),
    ('p "unterminated quote', ["p", "unterminated quote"]),
    ('p   spaced\t\ttabbed  ', ["p", "spaced", "tabbed"]),
    ('"C:\\Program Files\\x.exe" arg', ["C:\\Program Files\\x.exe", "arg"]),
    ('"C:\\dir\\" arg', ["C:\\dir\\", "arg"]),
    ('C:\\dir\\x.exe "C:\\with space\\" y', ["C:\\dir\\x.exe", 'C:\\with space" y']),  # backslash-quote is a quote
    ('C:\\dir\\x.exe "C:\\with space\\\\" y', ["C:\\dir\\x.exe", "C:\\with space\\", "y"]),
    ('p *.txt ?', ["p", "*.txt", "?"]),
    ('p \u00e9\u65e5\U0001F600 "\u00e9 \u65e5"', ["p", "\u00e9\u65e5\U0001F600", "\u00e9 \u65e5"]),
    ('', [""]),
]

REAL = ["", "a b", 'say "hi"', 'a\\"b', "trail\\", "trail\\\\", "C:\\path with space\\", " lead", "tail ", "--", "-",
        "*", "?", "\u00e9\u65e5\u672c\u8a9e\U0001F600", "tab\there", '"', '""', "\\", "\\\\", "a\\\\\\\"b c"]


@needs_windows
class Arguments(unittest.TestCase):
    def test_split_rules(self):
        text = "".join(line + "\n" for line, _ in SPLITS[:-1])
        result = run("targs", "--split", input=text.encode("utf-8"))
        self.assertEqual(result.code, 0, result.errtext)
        got = result.text.split("--\n")[:-1]
        self.assertEqual(len(got), len(SPLITS) - 1)
        for (line, want), block in zip(SPLITS, got):
            self.assertEqual(block.splitlines(), ["[" + a + "]" for a in want], line)

    def test_arguments_arrive_as_sent(self):
        result = run("targs", *REAL)
        self.assertEqual(result.code, 0, result.errtext)
        # targs prints argv[0] as well; its path varies, so it is dropped
        lines = result.text.split("\n")
        self.assertTrue(lines[0].startswith("[") and lines[0].endswith("targs.exe]"), lines[0])
        text = result.text[len(lines[0]) + 1:]
        self.assertEqual(text, "".join("[" + a + "]\n" for a in REAL))

    def test_empty_argument_is_kept_by_a_tool(self):
        result = run("tfind", "x", "")
        self.assertEqual(result.code, 2)  # an empty path does not exist; it is not dropped


if __name__ == "__main__":
    unittest.main()
