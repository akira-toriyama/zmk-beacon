"""Tests of sprite_fetch.py against an HTTP server started here, serving a GIF
and a CSV written here with made-up names (no personal file, species name or
real source enters the repository): python3 -m unittest discover tools."""
import contextlib
import functools
import http.server
import io
import os
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import sprite_fetch  # noqa: E402
from test_sprite_pack import gif  # noqa: E402

GIF = gif(2, 2, [(0, 0, 0), (255, 0, 0)], [dict(rect=(0, 0, 2, 2), pixels=bytes([0, 1, 1, 0]), delay=10)])
CSV = ("pokemon_species_id,local_language_id,name,genus\n"
       "1,1,テスト,\n"
       "1,9,Test,\n"
       "2,9,Second♀,\n"
       "3,9,Say \"hi\",\n"
       "4,1,テストテストテ,\n"
       "4,9,Café,\n")
D = sprite_fetch.DEFAULTS


class Handler(http.server.SimpleHTTPRequestHandler):
    requests = []  # (path, Authorization)

    def do_GET(self):
        Handler.requests.append((self.path, self.headers.get("Authorization")))
        super().do_GET()

    def log_message(self, *args):
        pass


class SpriteFetchTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = tempfile.TemporaryDirectory()
        cls.server = http.server.ThreadingHTTPServer(
            ("127.0.0.1", 0), functools.partial(Handler, directory=cls.root.name))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = sprite_fetch.RAW_BASE
        sprite_fetch.RAW_BASE = "http://127.0.0.1:%d" % cls.server.server_address[1]
        cls.place(D["sprites_repo"], D["sprites_ref"], D["sprites_dir"] + "/1.gif", GIF)
        for n in (2, 3, 4, 7):
            cls.place(D["sprites_repo"], D["sprites_ref"], D["sprites_dir"] + "/%d.gif" % n, GIF)
        cls.place(D["sprites_repo"], D["sprites_ref"], D["sprites_dir"] + "/5.gif", b"<html>not a gif</html>")
        cls.place(D["names_repo"], D["names_ref"], D["names_path"], CSV.encode("utf-8"))
        cls.place("Other/mirror", "abc123", "pics/1.gif", GIF)
        cls.place("Other/mirror", "abc123", "names/x.csv", CSV.encode("utf-8"))

    @classmethod
    def tearDownClass(cls):
        sprite_fetch.RAW_BASE = cls.base
        cls.server.shutdown()
        cls.server.server_close()
        cls.root.cleanup()

    @classmethod
    def place(cls, repo, ref, path, body):
        full = os.path.join(cls.root.name, repo, ref, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, "wb") as f:
            f.write(body)

    def setUp(self):
        Handler.requests.clear()
        self.out = tempfile.TemporaryDirectory()
        self.env = dict(os.environ)
        os.environ.pop(sprite_fetch.TOKEN_ENV, None)

    def tearDown(self):
        os.environ.clear()
        os.environ.update(self.env)
        self.out.cleanup()

    def run_tool(self, *args):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = sprite_fetch.main(["--out", self.out.name, *args])
        return code, out.getvalue(), err.getvalue()

    def fragment(self):
        with open(os.path.join(self.out.name, "sprite.conf"), encoding="utf-8") as f:
            return f.read()

    def test_the_default_route_en(self):
        code, out, err = self.run_tool("--number", "1")
        self.assertEqual((code, err), (0, ""))
        with open(os.path.join(self.out.name, "sprite.gif"), "rb") as f:
            self.assertEqual(f.read(), GIF)
        gif_path = os.path.join(os.path.abspath(self.out.name), "sprite.gif")
        self.assertEqual(self.fragment(), 'CONFIG_BEACON_SPRITE_GIF="%s"\nCONFIG_BEACON_SPRITE_NAME="Test"\n' % gif_path)
        self.assertTrue(os.path.isabs(gif_path))
        self.assertEqual([p for p, _ in Handler.requests],
                         ["/%s/%s/%s/1.gif" % (D["sprites_repo"], D["sprites_ref"], D["sprites_dir"]),
                          "/%s/%s/%s" % (D["names_repo"], D["names_ref"], D["names_path"])])
        self.assertEqual(out, "sprite_fetch: a %d byte GIF, an ASCII name, from %s@%s\n"
                         % (len(GIF), D["sprites_repo"], D["sprites_ref"][:12]))
        self.assertNotIn("Test", out, "the name is never printed")

    def test_lang_ja_is_the_katakana_name_and_the_alphabet_is_said(self):
        code, out, err = self.run_tool("--number", "1", "--lang", "ja")
        self.assertEqual(code, 0, err)
        self.assertIn('CONFIG_BEACON_SPRITE_NAME="テスト"\n', self.fragment())
        self.assertIn("a katakana name", out)
        self.assertNotIn("テスト", out)

    def test_the_fold_reaches_the_fragment(self):
        code, _, err = self.run_tool("--number", "2")
        self.assertEqual(code, 0, err)
        self.assertIn('CONFIG_BEACON_SPRITE_NAME="Second F"\n', self.fragment())

    def test_a_name_given_skips_the_csv(self):
        code, _, err = self.run_tool("--number", "1", "--name", "Mine")
        self.assertEqual(code, 0, err)
        self.assertIn('CONFIG_BEACON_SPRITE_NAME="Mine"\n', self.fragment())
        self.assertEqual(len(Handler.requests), 1, "the GIF only")
        self.assertFalse(os.path.exists(os.path.join(self.out.name, "names.csv")))

    def test_a_name_given_that_the_bar_or_kconfig_refuses_is_usage(self):
        for name in ("abcdefghijklmnop", 'a"b', "a\\b", "a??b", "ひらがな", "mixedテ"):
            code, _, err = self.run_tool("--number", "1", "--name", name)
            self.assertEqual(code, 2, name)
            self.assertTrue(err.startswith("sprite_fetch: --name: "), err)
            self.assertFalse(os.path.exists(os.path.join(self.out.name, "sprite.conf")), name)

    def test_no_name_falls_to_the_number(self):
        # 7: no row; 3: an English name Kconfig cannot carry; 4 in en: an accent; 4 in ja: too long, then the accent.
        for args in (("--number", "7"), ("--number", "3"), ("--number", "4"), ("--number", "4", "--lang", "ja")):
            code, out, err = self.run_tool(*args)
            self.assertEqual(code, 0, (args, err))
            self.assertIn('CONFIG_BEACON_SPRITE_NAME="%s"\n' % args[1], self.fragment(), args)
            self.assertIn("an ASCII name", out)

    def test_a_missing_gif_is_exit_1_and_writes_no_fragment(self):
        code, _, err = self.run_tool("--number", "6")
        self.assertEqual(code, 1)
        self.assertIn("HTTP 404", err)
        self.assertIn("/6.gif", err)
        self.assertFalse(os.path.exists(os.path.join(self.out.name, "sprite.conf")))
        self.assertFalse(os.path.exists(os.path.join(self.out.name, "sprite.gif")))

    def test_a_body_that_is_no_gif_is_exit_1(self):
        code, _, err = self.run_tool("--number", "5")
        self.assertEqual(code, 1)
        self.assertIn("not a GIF", err)
        self.assertFalse(os.path.exists(os.path.join(self.out.name, "sprite.gif")))

    def test_a_missing_csv_is_exit_1(self):
        code, _, err = self.run_tool("--number", "1", "--names-ref", "nowhere")
        self.assertEqual(code, 1)
        self.assertIn("HTTP 404", err)
        self.assertFalse(os.path.exists(os.path.join(self.out.name, "sprite.conf")))

    def test_a_csv_that_is_not_the_names_is_exit_1(self):
        self.place(D["names_repo"], "badcsv", D["names_path"], b"id,name\n1,x\n")
        code, _, err = self.run_tool("--number", "1", "--names-ref", "badcsv")
        self.assertEqual(code, 1)
        self.assertIn("not PokeAPI's", err)

    def test_another_repository_needs_its_ref(self):
        for what in ("sprites", "names"):
            code, _, err = self.run_tool("--number", "1", "--%s-repo" % what, "Other/mirror")
            self.assertEqual(code, 2, what)
            self.assertIn("--%s-ref" % what, err)
        self.assertEqual(Handler.requests, [], "refused before any request")

    def test_another_repository_with_its_ref_dir_and_path(self):
        code, out, err = self.run_tool("--number", "1", "--sprites-repo", "Other/mirror", "--sprites-ref", "abc123",
                                       "--sprites-dir", "/pics/", "--names-repo", "Other/mirror", "--names-ref",
                                       "abc123", "--names-path", "names/x.csv")
        self.assertEqual((code, err), (0, ""))
        self.assertEqual([p for p, _ in Handler.requests], ["/Other/mirror/abc123/pics/1.gif", "/Other/mirror/abc123/names/x.csv"])
        self.assertIn("from Other/mirror@abc123", out)

    def test_the_default_repository_takes_another_ref(self):
        self.place(D["sprites_repo"], "newer", D["sprites_dir"] + "/1.gif", GIF)
        code, _, err = self.run_tool("--number", "1", "--sprites-ref", "newer", "--name", "X")
        self.assertEqual((code, err), (0, ""))
        self.assertEqual(Handler.requests[0][0], "/%s/newer/%s/1.gif" % (D["sprites_repo"], D["sprites_dir"]))

    def test_bad_usage(self):
        for args in (("--number", "0"), ("--number", "-1"), ("--number", "abc"), ("--number", "1.5"),
                     ("--number", "1", "--sprites-repo", "nodash"), ("--number", "1", "--sprites-ref", "a b")):
            code, _, err = self.run_tool(*args)
            self.assertEqual(code, 2, args)
            self.assertTrue(err.startswith("sprite_fetch: "), err)
        self.assertEqual(Handler.requests, [])

    def test_the_token_goes_as_a_bearer_with_both_requests(self):
        os.environ[sprite_fetch.TOKEN_ENV] = "secret-1"
        code, out, err = self.run_tool("--number", "1")
        self.assertEqual((code, err), (0, ""))
        self.assertEqual([a for _, a in Handler.requests], ["Bearer secret-1", "Bearer secret-1"])
        self.assertNotIn("secret-1", out)
        del os.environ[sprite_fetch.TOKEN_ENV]
        self.run_tool("--number", "1")
        self.assertEqual([a for _, a in Handler.requests[2:]], [None, None])

    def test_the_tool_runs_as_a_process(self):
        import subprocess
        tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), "sprite_fetch.py")
        done = subprocess.run([sys.executable, tool, "--out", self.out.name, "--number", "1",
                               "--sprites-repo", "Other/mirror"], capture_output=True)
        self.assertEqual(done.returncode, 2, done.stderr)
        done = subprocess.run([sys.executable, tool], capture_output=True)
        self.assertEqual(done.returncode, 2)


if __name__ == "__main__":
    unittest.main()
