"""Tests of name_dictionary.py on a CSV written here, with made-up names (no
species name enters the repository): python3 -m unittest discover tools."""
import contextlib
import io
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import name_dictionary  # noqa: E402

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "name_dictionary.py")

# 1 fits; 2 fits, decomposed in the CSV; 3 is 18 bytes of katakana; 4 is
# hiragana; 5 has no ja; 6 has no ja and an English name that is not ASCII;
# 7 has neither name fit; 8 has an English name of 16 bytes, cut by no one; 9 has
# only other languages' rows.
CSV = ("pokemon_species_id,local_language_id,name,genus\n"
       "1,1,テスト,たね\n"
       "1,2,Tesuto,\n"
       "1,9,Test,Seed\n"
       "2,9,Second,\n"
       "2,1,ガピ,\n"
       "3,1,テストテスト,\n"
       "3,9,Mr. Third,\n"
       "4,1,ぴ,\n"
       "4,9,Fourth,\n"
       "5,9,Fifth one,\n"
       "6,9,Café,\n"
       "7,1,テストテストテ,\n"
       "7,9,Sev’nth,\n"
       "8,9,abcdefghijklmnop,\n"
       "9,5,Neuf,\n")


class NameDictionaryTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.csv = os.path.join(self.dir.name, "names.csv")
        with open(self.csv, "w", encoding="utf-8", newline="") as f:
            f.write(CSV)

    def tearDown(self):
        self.dir.cleanup()

    def name(self, number):
        return name_dictionary.sprite_name(name_dictionary.read_names(self.csv).get(number, {}))

    def test_names_by_number_composed(self):
        names = name_dictionary.read_names(self.csv)
        self.assertEqual(sorted(names), [1, 2, 3, 4, 5, 6, 7, 8], "a row of another language holds no number")
        self.assertEqual(names[1], {"ja": "テスト", "en": "Test"})
        self.assertEqual(names[2]["ja"], "ガピ", "NFC")
        self.assertEqual(len(names[2]["ja"].encode("utf-8")), 6)
        self.assertEqual(names[5], {"en": "Fifth one"}, "no ja key without a Japanese name")

    def test_the_rule_katakana_when_it_shows_else_english_whole_else_none(self):
        self.assertEqual(self.name(1), "テスト")
        self.assertEqual(self.name(2).encode("utf-8"), "ガピ".encode("utf-8"), "composed in bytes too")
        self.assertEqual(self.name(3), "Mr. Third", "18 bytes of katakana: the English name, not cut at its dot")
        self.assertEqual(self.name(4), "Fourth", "hiragana is outside the font")
        self.assertEqual(self.name(5), "Fifth one", "no ja")
        self.assertIsNone(self.name(6), "an English name outside printable ASCII")
        self.assertIsNone(self.name(7), "neither fits")
        self.assertIsNone(self.name(8), "16 bytes: not cut, a cut name would be another name")
        self.assertIsNone(self.name(9), "a number without a Japanese or English row")
        self.assertIsNone(self.name(10), "a number the CSV has no row of")

    def run_tool(self, *args, env=None):
        return subprocess.run([sys.executable, TOOL, *args], capture_output=True, env=env)

    def test_the_tool_prints_one_line_of_utf8_whatever_the_locale(self):
        # launchd gives a job no locale; the sender feeds the bytes to the pack.
        env = {"PATH": os.environ.get("PATH", ""), "LC_ALL": "C", "PYTHONUTF8": "0", "PYTHONCOERCECLOCALE": "0"}
        done = self.run_tool(self.csv, "--number", "2", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout, "ガピ\n".encode("utf-8"))
        self.assertEqual(done.stderr, b"")

    def test_the_tool_prints_an_empty_line_for_no_name(self):
        for number in ("6", "10", "0", "-1"):
            done = self.run_tool(self.csv, "--number=" + number)
            self.assertEqual((done.returncode, done.stdout), (0, b"\n"), number)

    def test_bad_csv(self):
        bad = os.path.join(self.dir.name, "bad.csv")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("id,name\n1,x\n")
        with self.assertRaises(name_dictionary.DictionaryError):
            name_dictionary.read_names(bad)
        # Not UTF-8 (a NUL is no test: csv takes it since Python 3.11).
        with open(bad, "wb") as f:
            f.write(b"pokemon_species_id,local_language_id,name,genus\n1,1,\xff\xfe,x\n")
        with self.assertRaises(name_dictionary.DictionaryError):
            name_dictionary.read_names(bad)
        with open(bad, "w", encoding="utf-8-sig") as f:
            f.write("pokemon_species_id,local_language_id,name,genus\n1,1,テ,x\n")
        self.assertEqual(name_dictionary.read_names(bad), {1: {"ja": "テ"}}, "a BOM is fine")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("pokemon_species_id,local_language_id,name,genus\n1,5,Fr,\n")
        with self.assertRaises(name_dictionary.DictionaryError):
            name_dictionary.read_names(bad)
        with contextlib.redirect_stderr(io.StringIO()) as stderr:
            self.assertEqual(name_dictionary.main([bad, "--number", "1"]), 2)
            self.assertEqual(name_dictionary.main([os.path.join(self.dir.name, "missing.csv"), "--number", "1"]), 2)
        self.assertTrue(all(line.startswith("name_dictionary: ") for line in stderr.getvalue().splitlines()),
                        "the sender keeps only the tool's own line")
        self.assertNotIn(self.dir.name, stderr.getvalue(), "no path")


if __name__ == "__main__":
    unittest.main()
