"""Tests of name_dictionary.py on a CSV written here, with made-up names (no
species name enters the repository): python3 -m unittest discover tools."""
import contextlib
import io
import json
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import name_dictionary  # noqa: E402

CSV = ("pokemon_species_id,local_language_id,name,genus\n"
       "1,1,テスト,たね\n"
       "1,2,Tesuto,\n"
       "1,9,Test,Seed\n"
       "2,9,Second,\n"
       "2,1,ガピ,\n"
       "3,1,テストテスト,\n"
       "3,9,Third,\n"
       "4,1,ぴ,\n"
       "4,9,Fourth,\n"
       "5,9,Fifth one,\n")


class NameDictionaryTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.csv = os.path.join(self.dir.name, "names.csv")
        with open(self.csv, "w", encoding="utf-8", newline="") as f:
            f.write(CSV)

    def tearDown(self):
        self.dir.cleanup()

    def test_entries_in_number_order_composed(self):
        entries = name_dictionary.read_names(self.csv)
        self.assertEqual([e["no"] for e in entries], [1, 2, 3, 4, 5])
        self.assertEqual(entries[0], {"no": 1, "ja": "テスト", "en": "Test"})
        self.assertEqual(entries[1]["ja"], "ガピ", "NFC")
        self.assertEqual(len(entries[1]["ja"].encode("utf-8")), 6)
        self.assertEqual(entries[4], {"no": 5, "en": "Fifth one"}, "no ja key without a Japanese name")
        text = name_dictionary.dumps(entries)
        self.assertEqual(json.loads(text), entries)
        self.assertEqual(text.count("\n"), len(entries) + 2, "one object a line")
        self.assertIn('"ja": "テスト"', text, "UTF-8 as it is, not escaped")

    def test_compare_with_an_order_file(self):
        order = os.path.join(self.dir.name, "order.txt")
        with open(order, "wb") as f:
            f.write("Test.gif\r\n\nSecond.gif\nThird.GIF\nfourth.gif\nFifth one.gif\nSixth.gif\nTest.gif\n".encode("utf-8"))
        names = name_dictionary.read_order(order)
        self.assertEqual(len(names), 6, "empty lines and a repeat skipped")
        with open(order, "wb") as f:
            f.write("Caf\u00e9.gif\nCafe\u0301.gif\n".encode("utf-8"))
        self.assertEqual(name_dictionary.read_order(order), ["Caf\u00e9.gif"], "a decomposed spelling is the same name")
        with open(order, "wb") as f:
            f.write("Test.gif\r\n\nSecond.gif\nThird.GIF\nfourth.gif\nFifth one.gif\nSixth.gif\nTest.gif\n".encode("utf-8"))
        places, mismatches, shown = name_dictionary.compare(name_dictionary.read_names(self.csv), names)
        self.assertEqual(places, 6)
        self.assertEqual(mismatches, [(4, "fourth", "Fourth"), (6, "Sixth", None)])
        # 1 テスト and 2 ガピ show; 3 is 18 bytes, 4 hiragana, 5 and 6 have no ja.
        self.assertEqual(shown, 2)

    def test_main_writes_the_file_and_reports(self):
        out = os.path.join(self.dir.name, "names.json")
        order = os.path.join(self.dir.name, "order.txt")
        with open(order, "w", encoding="utf-8") as f:
            f.write("Test.gif\nsecond.gif\n")
        stdout = io.StringIO()
        with contextlib.redirect_stdout(stdout):
            self.assertEqual(name_dictionary.main([self.csv, "-o", out, "--order", order]), 0)
        with open(out, encoding="utf-8") as f:
            self.assertEqual(json.load(f)[2], {"no": 3, "ja": "テストテスト", "en": "Third"})
        lines = stdout.getvalue().splitlines()
        self.assertEqual(lines[0], "5 species, 4 with a Japanese name, 5 with an English name -> " + out)
        self.assertEqual(lines[1], "2 places in the order: 1 file names equal the English name, 2 katakana names the HP bar shows")
        self.assertEqual(lines[2], "  place 2: file 'second', English 'Second'")

    def test_bad_csv(self):
        bad = os.path.join(self.dir.name, "bad.csv")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("id,name\n1,x\n")
        with self.assertRaises(name_dictionary.DictionaryError):
            name_dictionary.read_names(bad)
        for raw in (b"pokemon_species_id,local_language_id,name,genus\n1,1,\xff\xfe,x\n",
                    b"pokemon_species_id,local_language_id,name,genus\n1,1,a\x00b,x\n"):
            with open(bad, "wb") as f:
                f.write(raw)
            with self.assertRaises(name_dictionary.DictionaryError):
                name_dictionary.read_names(bad)
        with open(bad, "w", encoding="utf-8-sig") as f:
            f.write("pokemon_species_id,local_language_id,name,genus\n1,1,テ,x\n")
        self.assertEqual(name_dictionary.read_names(bad), [{"no": 1, "ja": "テ"}], "a BOM is fine")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("pokemon_species_id,local_language_id,name,genus\n1,5,Fr,\n")
        with self.assertRaises(name_dictionary.DictionaryError):
            name_dictionary.read_names(bad)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(name_dictionary.main([bad, "-o", os.path.join(self.dir.name, "n.json")]), 2)
            self.assertEqual(name_dictionary.main([os.path.join(self.dir.name, "missing.csv"), "-o", "x"]), 2)


if __name__ == "__main__":
    unittest.main()
