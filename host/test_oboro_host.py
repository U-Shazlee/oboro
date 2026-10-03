"""Self-check for oboro_host.py: python host/test_oboro_host.py

Serves on loopback only, with the custom-game list in a temporary folder,
and never starts a real game (only the no-op "desktop" entry is launched).
"""
import json
import sys
import tempfile
import threading
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import oboro_host as host

tmp = Path(tempfile.mkdtemp())
host.DATA_DIR, host.GAMES_FILE = tmp, tmp / "games.json"
server = host.ThreadingHTTPServer(("127.0.0.1", 0), host.Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()
base = "http://127.0.0.1:%d" % server.server_address[1]


def call(path, data=None, headers=None):
    """(status, body) of a request; data makes it a POST."""
    body = urllib.parse.urlencode(data).encode() if data is not None else None
    try:
        with urllib.request.urlopen(urllib.request.Request(base + path, body, headers or {}), timeout=5) as r:
            return r.status, r.read()
    except urllib.error.HTTPError as e:
        return e.code, b""


# Valve's text format, including an escaped Windows path.
assert host._vdf_values('"path"\t\t"D:\\\\Steam Library"\n"appid"  "620"', "path") == ["D:\\Steam Library"]
assert host.STEAM_TOOLS.match("Proton 9.0") and not host.STEAM_TOOLS.match("Portal 2")

status, body = call("/stats")
assert status == 200 and set(json.loads(body)) == {"cpu", "ram", "gpu", "vram", "gpu_temp", "fps"}

games = json.loads(call("/library")[1])["games"]
assert games[0] == {"id": "desktop", "title": "Desktop", "source": "PC"}
assert all(set(g) == {"id", "title", "source"} for g in games)
steam = [g for g in games if g["source"] == "Steam"]
print("steam games found on this PC:", len(steam), [g["title"] for g in steam[:5]])

# Adding needs the page's token, a real file, and comes from this PC only.
exe = tmp / "My Game.exe"
exe.write_bytes(b"x")
cover = tmp / "cover.png"
cover.write_bytes(b"\x89PNG fake")
assert call("/add", {"title": "X", "path": str(exe), "token": "wrong"})[0] == 403
assert not host.load_custom()
token = host._form_token
status, page = call("/add", {"title": "Nope", "path": str(tmp / "missing.exe"), "token": token})
assert status == 200 and b"does not exist" in page and not host.load_custom()
status, page = call("/add", {"title": "My <Game> 2!", "path": str(exe), "cover": str(cover), "token": token})
assert status == 200 and b"My &lt;Game&gt; 2!" in page
assert call("/add", {"title": "My Game 2", "path": str(exe), "token": token})[0] == 200
ids = [g["id"] for g in host.load_custom()]
assert ids == ["custom-my-game-2", "custom-my-game-2-2"], ids
games = json.loads(call("/library")[1])["games"]
assert {"id": "custom-my-game-2", "title": "My <Game> 2!", "source": "Custom"} in games

# The management page is refused when addressed by another name (DNS rebinding).
assert call("/", headers={"Host": "evil.example"})[0] == 404
assert call("/")[0] == 200

assert call("/art?id=custom-my-game-2") == (200, b"\x89PNG fake")
assert call("/art?id=custom-my-game-2-2")[0] == 404
assert call("/art?id=../../secret")[0] == 404
if steam:
    status, art = call("/art?id=" + steam[0]["id"])
    print("steam cover for", steam[0]["title"], "->", status, len(art), "bytes")

# Launching: only with the header, only what is in the library.
assert call("/launch?id=desktop", {})[0] == 403
assert call("/launch?id=desktop", {}, {"X-Oboro": "1"})[0] == 200
assert call("/launch?id=custom-nope", {}, {"X-Oboro": "1"})[0] == 404
assert call("/launch?id=steam-999999999", {}, {"X-Oboro": "1"})[0] == 404

assert call("/remove", {"id": "custom-my-game-2", "token": token})[0] == 200
assert [g["id"] for g in host.load_custom()] == ["custom-my-game-2-2"]
server.shutdown()
print("ok")
