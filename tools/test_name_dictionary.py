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
# hiragana; 5 has an empty ja; 6 has no ja and an English name that is not ASCII
# after the fold (an accent); 7 has 21 bytes of katakana and an English name
# that the fold makes fit (a typographic apostrophe); 8 has an English name of
# 16 bytes, cut by no one; 9 has only other languages' rows; 11 has the font's
# signs and fullwidth forms, which a compatibility normalization would fold out
# of the font; 12 and 13 end in the gender signs, in both languages.
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
       "5,1,,\n"
       "5,9,Fifth one,\n"
       "6,9,Café,\n"
       "7,1,テストテストテ,\n"
       "7,9,Sev’nth,\n"
       "8,9,abcdefghijklmnop,\n"
       "9,5,Neuf,\n"
       "11,1,ア♂Ｚ：２,\n"
       "11,9,Test two,\n"
       "12,1,テスト♀,\n"
       "12,9,Twelfth♀,\n"
       "13,9,Thirteen♂,\n")


class NameDictionaryTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.csv = os.path.join(self.dir.name, "names.csv")
        with open(self.csv, "w", encoding="utf-8", newline="") as f:
            f.write(CSV)

    def tearDown(self):
        self.dir.cleanup()

    def name(self, number, lang="ja"):
        return name_dictionary.sprite_name(name_dictionary.read_names(self.csv).get(number, {}), lang)

    def test_names_by_number_composed(self):
        names = name_dictionary.read_names(self.csv)
        self.assertEqual(sorted(names), [1, 2, 3, 4, 5, 6, 7, 8, 11, 12, 13], "a row of another language holds no number")
        self.assertEqual(names[1], {"ja": "テスト", "en": "Test"})
        self.assertEqual(names[2]["ja"], "ガピ", "NFC")
        self.assertEqual(len(names[2]["ja"].encode("utf-8")), 6)
        self.assertEqual(names[5], {"en": "Fifth one"}, "no ja key for an empty Japanese name")

    def test_the_rule_katakana_when_it_shows_else_english_whole_else_none(self):
        self.assertEqual(self.name(1), "テスト")
        self.assertEqual(self.name(2).encode("utf-8"), "ガピ".encode("utf-8"), "composed in bytes too")
        self.assertEqual(self.name(3), "Mr. Third", "18 bytes of katakana: the English name, not cut at its dot")
        self.assertEqual(self.name(4), "Fourth", "hiragana is outside the font")
        self.assertEqual(self.name(5), "Fifth one", "an empty ja is no name")
        self.assertIsNone(self.name(6), "an English name outside printable ASCII after the fold")
        self.assertEqual(self.name(7), "Sev'nth", "21 bytes of katakana: the English name, its apostrophe folded")
        self.assertIsNone(self.name(8), "16 bytes: not cut, a cut name would be another name")
        self.assertIsNone(self.name(9), "a number without a Japanese or English row")
        self.assertIsNone(self.name(10), "a number the CSV has no row of")
        self.assertEqual(self.name(11), "ア♂Ｚ：２", "the font's fullwidth forms stay: a katakana name is never folded")
        self.assertEqual(self.name(12), "テスト♀", "the gender sign is in the katakana font")

    def test_lang_en_is_the_english_name_alone_folded(self):
        self.assertEqual(self.name(1, "en"), "Test", "not the katakana name")
        self.assertEqual(self.name(12, "en"), "Twelfth F", "a trailing female sign: a space and F")
        self.assertEqual(self.name(13, "en"), "Thirteen M", "a trailing male sign: a space and M")
        self.assertEqual(self.name(13), "Thirteen M", "the fold serves the ja rule's English fallback too")
        self.assertEqual(self.name(7, "en"), "Sev'nth")
        self.assertIsNone(self.name(6, "en"), "an accent is not folded")
        self.assertEqual(self.name(5, "en"), "Fifth one")
        self.assertIsNone(self.name(9, "en"))
        with self.assertRaises(KeyError):
            self.name(1, "fr")

    def test_fold_english(self):
        fold = name_dictionary.fold_english
        self.assertEqual(fold("A♀"), "A F")
        self.assertEqual(fold("A♂"), "A M")
        self.assertEqual(fold("A♀B"), "A♀B", "a sign inside the name is not the collection's case")
        self.assertEqual(fold("A’b’c"), "A'b'c", "every typographic apostrophe")
        self.assertEqual(fold("Plain"), "Plain")

    def run_tool(self, *args, env=None):
        return subprocess.run([sys.executable, TOOL, *args], capture_output=True, env=env)

    def test_the_tool_prints_one_line_of_utf8_whatever_the_locale(self):
        # launchd gives a job no locale; the sender feeds the bytes to the pack.
        env = {"PATH": os.environ.get("PATH", ""), "LC_ALL": "C", "PYTHONUTF8": "0", "PYTHONCOERCECLOCALE": "0"}
        done = self.run_tool(self.csv, "--number", "2", env=env)
        self.assertEqual(done.returncode, 0, done.stderr)
        self.assertEqual(done.stdout, "ガピ\n".encode("utf-8"))
        self.assertEqual(done.stderr, b"")

    def test_the_tool_takes_lang(self):
        done = self.run_tool(self.csv, "--number", "1", "--lang", "en")
        self.assertEqual((done.returncode, done.stdout), (0, b"Test\n"))
        done = self.run_tool(self.csv, "--number", "1", "--lang", "ja")
        self.assertEqual((done.returncode, done.stdout), (0, "テスト\n".encode("utf-8")))
        done = self.run_tool(self.csv, "--number", "1", "--lang", "fr")
        self.assertEqual(done.returncode, 2)

    def test_the_tool_prints_an_empty_line_for_no_name(self):
        for number in ("6", "10", "0", "-1"):
            done = self.run_tool(self.csv, "--number=" + number)
            self.assertEqual((done.returncode, done.stdout), (0, b"\n"), number)

    def test_the_tool_refuses_a_bad_csv_with_exit_2_and_its_own_line(self):
        # The sender's contract: only this exit and this line make a config
        # error; an exit 0 with empty stdout would be a sprite named its number.
        bad = os.path.join(self.dir.name, "bad.csv")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("id,name\n1,x\n")
        for path in (bad, os.path.join(self.dir.name, "missing.csv")):
            done = self.run_tool(path, "--number", "1")
            self.assertEqual((done.returncode, done.stdout), (2, b""), path)
            self.assertTrue(done.stderr.startswith(b"name_dictionary: "), done.stderr)
            self.assertNotIn(self.dir.name.encode(), done.stderr, "no path")

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
        self.assertEqual(len(stderr.getvalue().splitlines()), 2, "one line a refusal")


if __name__ == "__main__":
    unittest.main()
