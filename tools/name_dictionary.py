#!/usr/bin/env python3
"""The sprite sender's name dictionary from PokeAPI's species names.

  name_dictionary.py <pokemon_species_names.csv> -o <names.json> [--order <file>]

Reads PokeAPI's data/v2/csv/pokemon_species_names.csv (columns
pokemon_species_id, local_language_id, name, genus; languages.csv: 1 is
ja-Hrkt, the katakana names, 9 is en) and writes the dictionary the sender
reads (tools/sprite-sender, SpriteSenderCore/NameDictionary.swift): a JSON
array in number order, one object a species, `no`, `ja` (composed, NFC) and
`en`, one a line. Every species goes in, whether or not its name fits the HP
bar: the sender decides that (sprite_pack.check_name's rule, 15 bytes of the
katakana font) and falls back to the file name.

--order <file> compares the dictionary with the sender's order file (one GIF
file name a line, its line the place; empty lines and repeats skipped as
Rotation.order does): a place whose file name without its extension is not
the English name is printed with both spellings, so a GIF at the wrong place
would show the wrong katakana name. It also counts the places whose katakana
name the HP bar can show. The output names species, which the user's
collection is made of: it is for the terminal, never for a commit or a doc
(the dictionary lives next to the order file, off every repository).

Standard library only (macOS /usr/bin/python3, 3.9). Exit 0; 2 for a CSV
without the columns or without a name in either language, or an unreadable
file.
"""
import argparse
import csv
import json
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
    """[{"no", "ja", "en"}, ...] in number order from PokeAPI's CSV; a species
    without a name in one language gets no key for it."""
    ja = {}
    en = {}
    try:
        with open(csv_path, newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
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
                    ja[number] = unicodedata.normalize("NFC", name)
                elif language == LANGUAGE_EN:
                    en[number] = unicodedata.normalize("NFC", name)
    except OSError as e:
        raise DictionaryError("cannot read the CSV: %s" % e.strerror)
    if not ja and not en:
        raise DictionaryError("the CSV names no species in Japanese or English")
    entries = []
    for number in sorted(set(ja) | set(en)):
        entry = {"no": number}
        if number in ja:
            entry["ja"] = ja[number]
        if number in en:
            entry["en"] = en[number]
        entries.append(entry)
    return entries


def dumps(entries):
    """The file: one object a line, UTF-8 as it is (the sender reads any JSON;
    a line a species diffs by species)."""
    lines = [json.dumps(e, ensure_ascii=False, separators=(", ", ": ")) for e in entries]
    return "[\n" + ",\n".join(lines) + "\n]\n"


def read_order(path):
    """The order file's names: one a line, exactly, empty lines skipped, a
    name's first line its place (Rotation.order of the sender)."""
    with open(path, "rb") as f:
        data = f.read()
    names = []
    seen = set()
    for line in data.split(b"\n"):
        if line.endswith(b"\r"):
            line = line[:-1]
        if not line:
            continue
        name = line.decode("utf-8", "replace")
        if name not in seen:
            seen.add(name)
            names.append(name)
    return names


def shows(ja):
    """Whether the HP bar shows the katakana name (the sender's rule)."""
    try:
        sprite_pack.check_name(ja)
    except sprite_pack.PackError:
        return False
    return any(ord(c) >= 0x80 for c in ja)


def compare(entries, order):
    """(places, mismatches, shown): the order's places, the [(place, stem,
    en)] whose file name without its extension differs from the English name
    (en None when the dictionary has no entry), and the places whose katakana
    name the HP bar shows."""
    by_number = {e["no"]: e for e in entries}
    mismatches = []
    shown = 0
    for place, file_name in enumerate(order, 1):
        stem = file_name.rsplit(".", 1)[0] if "." in file_name else file_name
        entry = by_number.get(place, {})
        if stem != entry.get("en"):
            mismatches.append((place, stem, entry.get("en")))
        if "ja" in entry and shows(entry["ja"]):
            shown += 1
    return len(order), mismatches, shown


def main(argv=None):
    parser = argparse.ArgumentParser(description="the sprite sender's name dictionary from PokeAPI's CSV")
    parser.add_argument("csv", help="PokeAPI's pokemon_species_names.csv")
    parser.add_argument("-o", "--output", required=True, help="the names.json to write")
    parser.add_argument("--order", help="the sender's order file to compare the English names with")
    args = parser.parse_args(argv)
    try:
        entries = read_names(args.csv)
        with open(args.output, "w", encoding="utf-8", newline="\n") as f:
            f.write(dumps(entries))
        print("%d species, %d with a Japanese name, %d with an English name -> %s" % (
            len(entries), sum("ja" in e for e in entries), sum("en" in e for e in entries), args.output))
        if args.order:
            places, mismatches, shown = compare(entries, read_order(args.order))
            print("%d places in the order: %d file names equal the English name, %d katakana names the HP bar shows" % (
                places, places - len(mismatches), shown))
            for place, stem, en in mismatches:
                print("  place %d: file %r, English %s" % (place, stem, repr(en) if en is not None else "none"))
    except DictionaryError as e:
        print("name_dictionary: %s" % e, file=sys.stderr)
        return 2
    except OSError as e:
        print("name_dictionary: %s: %s" % (e.strerror, e.filename), file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
