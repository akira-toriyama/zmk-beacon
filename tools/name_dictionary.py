#!/usr/bin/env python3
"""A sprite's name from PokeAPI's species names.

  name_dictionary.py <pokemon_species_names.csv> --number <n>

Reads PokeAPI's data/v2/csv/pokemon_species_names.csv (columns
pokemon_species_id, local_language_id, name, genus; languages.csv: 1 is
ja-Hrkt, the katakana names, 9 is en) and prints the one name the HP bar shows
for species <n> (sprite_name, composed, NFC): the sprite sender runs this each
day (tools/sprite-sender, Main.spriteName). The line is empty when the CSV has
no such name: the sender then names the sprite after its number.

The rule lives here so that every route to a sprite shares it: the sender has
none of its own. canon's assets/sprite-name.py copies it for a local build
(it runs before zmk-beacon's pin is checked out): change the two together.

The output names a species, which the user's collection is made of: it is for
the sender's pipe, never for a log, a commit or a doc. No message carries a
value of the CSV, not even a number.

Standard library only (macOS /usr/bin/python3, 3.9). stdout is UTF-8 whatever
the locale: launchd gives its jobs none. Exit 0; 2 for a CSV without the
columns, not UTF-8 (a BOM is fine) or without a name in either language, or an
unreadable file.
"""
import argparse
import csv
import os
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sprite_pack  # noqa: E402

LANGUAGE_JA = 1  # ja-Hrkt in PokeAPI's languages.csv
LANGUAGE_EN = 9


class DictionaryError(ValueError):
    pass


def read_names(csv_path):
    """{number: {"ja", "en"}} from PokeAPI's CSV, composed (NFC); a species
    without a name in one language gets no key for it."""
    names = {}
    try:
        with open(csv_path, newline="", encoding="utf-8-sig") as f:
            for row in csv.DictReader(f):
                try:
                    number = int(row["pokemon_species_id"])
                    language = int(row["local_language_id"])
                    name = row["name"]
                except (KeyError, TypeError, ValueError):
                    raise DictionaryError("not PokeAPI's pokemon_species_names.csv: "
                                          "pokemon_species_id, local_language_id and name")
                if number < 1 or not name:
                    continue
                if language == LANGUAGE_JA:
                    names.setdefault(number, {})["ja"] = unicodedata.normalize("NFC", name)
                elif language == LANGUAGE_EN:
                    names.setdefault(number, {})["en"] = unicodedata.normalize("NFC", name)
    except OSError as e:
        raise DictionaryError("cannot read the CSV: %s" % e.strerror)
    except UnicodeDecodeError:
        raise DictionaryError("the CSV is not UTF-8")
    except csv.Error as e:
        raise DictionaryError("the CSV does not parse: %s" % e)
    if not names:
        raise DictionaryError("the CSV names no species in Japanese or English")
    return names


def sprite_name(species):
    """The name the HP bar shows for one species of read_names, or None: the
    user's rule (canon t-rq9h), its katakana name when the bar can show it,
    else its English one, whole or not at all (a cut name would be another
    name). No character is folded to fit: a name outside the bar's two alphabets
    gives none."""
    for language in ("ja", "en"):
        name = species.get(language)
        if name is None:
            continue
        try:
            return sprite_pack.check_name(name)
        except sprite_pack.PackError:
            pass
    return None


def main(argv=None):
    parser = argparse.ArgumentParser(description="a sprite's name from PokeAPI's species names")
    parser.add_argument("csv", help="PokeAPI's pokemon_species_names.csv")
    parser.add_argument("--number", type=int, required=True, help="the species' number")
    args = parser.parse_args(argv)
    try:
        name = sprite_name(read_names(args.csv).get(args.number, {}))
    except DictionaryError as e:
        print("name_dictionary: %s" % e, file=sys.stderr)
        return 2
    sys.stdout.buffer.write(((name or "") + "\n").encode("utf-8"))
    sys.stdout.buffer.flush()
    return 0


if __name__ == "__main__":
    sys.exit(main())
