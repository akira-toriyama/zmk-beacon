#!/usr/bin/env python3
"""The GIF and the name of one sprite for a build on GitHub Actions
(.github/workflows/prospector-sprite.yml), fetched from a repository at a
fixed commit and written as the Kconfig fragment the build embeds them from.

  sprite_fetch.py --number <n> --out <dir> [--lang ja|en] [--name <text>]
                  [--sprites-repo <owner/repo>] [--sprites-ref <commit>] [--sprites-dir <path>]
                  [--names-repo <owner/repo>] [--names-ref <commit>] [--names-path <path>]

Writes into <dir>: sprite.gif, the file <sprites-dir>/<n>.gif of the sprites
repository at its ref; names.csv, PokeAPI's pokemon_species_names.csv of the
names repository at its ref, unless --name is given; and sprite.conf, the
fragment with CONFIG_BEACON_SPRITE_GIF (the GIF's absolute path, as
CMakeLists.txt demands) and CONFIG_BEACON_SPRITE_NAME. The name is --name
when given (it must be one the HP bar shows and Kconfig takes, else exit 2),
else name_dictionary.py's for the number and --lang (en by default here, the
Actions route's choice; ja is the sender's), else the number itself, as the
sender names a sprite with no name: a species row the CSV lacks, or a name
outside the bar's alphabets after the fold, never stops the build.

Each file comes from raw.githubusercontent.com/<repo>/<ref>/<path> (RAW_BASE),
one request, no clone. The defaults are PokeAPI's two repositories at the
commits this module was tried with (DEFAULTS); another repository (a mirror,
a fork, a private copy) needs its ref given too, since the default commit
means nothing there and a branch would not reproduce: exit 2 without it. The
environment's SPRITES_TOKEN, when set, goes as a bearer token with both
requests (a private repository; the caller's GITHUB_TOKEN does not reach
another repository).

Exit 0; 1 when a file cannot be fetched (an HTTP error, the network, a body
that is no GIF) or the CSV is not the one expected; 2 for bad usage. The
messages name the number, the repositories and the refs, which the run's
inputs show anyway, never the name: its alphabet only (sprite_pack.name_kind),
as every tool here. Standard library only; the ZMK build image's Python runs
it (3.12 in zmk-build-arm:stable, 2026-11).
"""
import argparse
import os
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import name_dictionary  # noqa: E402
import sprite_pack  # noqa: E402

RAW_BASE = "https://raw.githubusercontent.com"
TOKEN_ENV = "SPRITES_TOKEN"
DEFAULTS = {
    "sprites_repo": "PokeAPI/sprites",
    "sprites_ref": "1b3b7c0a538dd2226ae08d0d2f8955125075bbd4",  # 2026-11-03
    "sprites_dir": "sprites/pokemon/versions/generation-v/black-white/animated",
    "names_repo": "PokeAPI/pokeapi",
    "names_ref": "6ea97b5641faa12e090866e8144ae8f57764fc64",  # 2026-11-05
    "names_path": "data/v2/csv/pokemon_species_names.csv",
}
REPO_RE = re.compile(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+")
REF_RE = re.compile(r"[A-Za-z0-9_.~/-]+")
TIMEOUT_S = 60


class Usage(ValueError):
    """Exit 2."""


class FetchError(RuntimeError):
    """Exit 1."""


def kconfig_takes(name):
    """Whether the fragment can carry the name: no double quote or backslash
    (Kconfig escaping) and no "??" (a C trigraph in autoconf.h), as build.sh
    refuses them for --sprite-name."""
    return '"' not in name and "\\" not in name and "??" not in name


def resolve(args, what):
    """(repo, ref) for "sprites" or "names": the defaults, or the given
    repository with its given ref; Usage for another repository without one."""
    repo = getattr(args, what + "_repo") or DEFAULTS[what + "_repo"]
    ref = getattr(args, what + "_ref")
    if not REPO_RE.fullmatch(repo):
        raise Usage("--%s-repo must be owner/repo" % what)
    if not ref:
        if repo != DEFAULTS[what + "_repo"]:
            raise Usage("--%s-repo names another repository: give --%s-ref too, a commit of it "
                        "(the default ref is a commit of %s)" % (what, what, DEFAULTS[what + "_repo"]))
        ref = DEFAULTS[what + "_ref"]
    if not REF_RE.fullmatch(ref):
        raise Usage("--%s-ref is no git ref" % what)
    return repo, ref


def raw_url(repo, ref, path):
    return "%s/%s/%s/%s" % (RAW_BASE, repo, urllib.parse.quote(ref, safe="/~"),
                            urllib.parse.quote(path.strip("/"), safe="/"))


def fetch(url, token):
    """The body at url, FetchError otherwise (its text names the URL and the
    reason, never the body)."""
    headers = {"User-Agent": "zmk-beacon sprite_fetch"}
    if token:
        headers["Authorization"] = "Bearer " + token
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=headers), timeout=TIMEOUT_S) as response:
            return response.read()
    except urllib.error.HTTPError as e:
        raise FetchError("%s: HTTP %d" % (url, e.code))
    except urllib.error.URLError as e:
        raise FetchError("%s: %s" % (url, e.reason))
    except OSError as e:
        raise FetchError("%s: %s" % (url, e))


def check_given_name(name):
    """--name, when the HP bar shows it and Kconfig takes it; Usage otherwise."""
    try:
        sprite_pack.check_name(name)
    except sprite_pack.PackError as e:
        raise Usage("--name: %s" % e)
    if not kconfig_takes(name):
        raise Usage('--name: no double quote, backslash or "??"')
    return name


def dictionary_name(args, repo, ref, token, out_dir):
    """The dictionary's name for the number and language, else the number."""
    csv_path = os.path.join(out_dir, "names.csv")
    with open(csv_path, "wb") as f:
        f.write(fetch(raw_url(repo, ref, args.names_path or DEFAULTS["names_path"]), token))
    try:
        species = name_dictionary.read_names(csv_path).get(args.number, {})
    except name_dictionary.DictionaryError as e:
        raise FetchError("%s@%s: %s" % (repo, ref[:12], e))
    name = name_dictionary.sprite_name(species, args.lang)
    if name is None or not kconfig_takes(name):
        return str(args.number)
    return name


def main(argv=None):
    parser = argparse.ArgumentParser(description="the GIF and the name of one sprite, for a build on GitHub Actions")
    parser.add_argument("--number", required=True, help="the sprite's number: <number>.gif, and the species row")
    parser.add_argument("--out", required=True, help="the directory for sprite.gif, names.csv and sprite.conf")
    parser.add_argument("--lang", choices=sorted(name_dictionary.LANGUAGES), default="en",
                        help="the name's language: en (default) or ja, the katakana name when the HP bar shows it")
    parser.add_argument("--name", default="", help="the name itself, in place of the dictionary's")
    for what in ("sprites", "names"):
        parser.add_argument("--%s-repo" % what, default="", help="owner/repo (default %s)" % DEFAULTS[what + "_repo"])
        parser.add_argument("--%s-ref" % what, default="",
                            help="a commit of it (default %s with the default repository; required with another)"
                                 % DEFAULTS[what + "_ref"][:12])
    parser.add_argument("--sprites-dir", default="", help="the GIFs' directory (default %s)" % DEFAULTS["sprites_dir"])
    parser.add_argument("--names-path", default="", help="the species names CSV (default %s)" % DEFAULTS["names_path"])
    args = parser.parse_args(argv)
    try:
        if not re.fullmatch(r"[1-9][0-9]*", args.number.strip()):
            raise Usage("--number must be a positive integer")
        args.number = int(args.number)
        token = os.environ.get(TOKEN_ENV) or None
        # Every usage error before the first request.
        repo, ref = resolve(args, "sprites")
        names = None if args.name else resolve(args, "names")
        if args.name:
            check_given_name(args.name)
        out_dir = os.path.abspath(args.out)
        os.makedirs(out_dir, exist_ok=True)
        gif_url = raw_url(repo, ref, "%s/%d.gif" % ((args.sprites_dir or DEFAULTS["sprites_dir"]).strip("/"), args.number))
        gif = fetch(gif_url, token)
        if gif[:6] not in (b"GIF87a", b"GIF89a"):
            raise FetchError("%s: not a GIF" % gif_url)
        gif_path = os.path.join(out_dir, "sprite.gif")
        with open(gif_path, "wb") as f:
            f.write(gif)
        name = args.name or dictionary_name(args, names[0], names[1], token, out_dir)
        with open(os.path.join(out_dir, "sprite.conf"), "w", encoding="utf-8") as f:
            f.write('CONFIG_BEACON_SPRITE_GIF="%s"\nCONFIG_BEACON_SPRITE_NAME="%s"\n' % (gif_path, name))
    except Usage as e:
        print("sprite_fetch: %s" % e, file=sys.stderr)
        return 2
    except FetchError as e:
        print("sprite_fetch: %s" % e, file=sys.stderr)
        return 1
    print("sprite_fetch: a %d byte GIF, %s, from %s@%s" % (len(gif), sprite_pack.name_kind(name), repo, ref[:12]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
