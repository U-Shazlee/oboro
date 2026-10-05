#!/usr/bin/env python3
"""Oboro Host: the PC side of Oboro, next to Sunshine.

It gives the 3DS three things Sunshine does not:

  * the game library: your installed Steam games, found by themselves, plus
    any custom games you add (any .exe, shortcut or launcher link);
  * launching: the 3DS streams Sunshine's "Desktop" and asks this program to
    start the game on it;
  * the PC's numbers for the stats tiles: CPU, RAM, GPU, VRAM, temperature
    and the game's own frame rate.

    python oboro_host.py              run it (http://<this PC>:48100)
    python oboro_host.py --install    start it with Windows from now on, and
                                      add "Oboro Host" to the Start menu
    python oboro_host.py --uninstall  undo --install
    python oboro_host.py --once       print the library and one stats sample

    python oboro_host.py --key        print the key the 3DS asks for

Add custom games at http://localhost:48100 in a browser ON THIS PC. That
page only answers this PC itself, and shows the key. A device that sends the
key can read the library and the stats and can start a game that is already
in the library; it can never add one. Only the Python standard library is
used.
"""

import argparse
import ctypes
import html
import json
import os
import re
import secrets
import shutil
import subprocess
import sys
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlsplit

PORT = 48100
WINDOWS = sys.platform == "win32"
DATA_DIR = Path(os.environ.get("APPDATA") or Path.home() / ".config") / "Oboro"
GAMES_FILE = DATA_DIR / "games.json"
KEY_FILE = DATA_DIR / "key.txt"
# The 3DS reads at most 1 MiB of cover art.
ART_LIMIT = 1024 * 1024
# Installed with Steam but not games.
STEAM_TOOLS = re.compile(r"^(Steamworks Common Redistributables|Proton |Steam Linux Runtime|Steam Audio)", re.I)


# ---- Key --------------------------------------------------------------------

def host_key():
    """The 12 digits a 3DS must send, made on first use. Digits, because the
    3DS types them on its number pad."""
    try:
        key = KEY_FILE.read_text(encoding="ascii").strip()
        if re.fullmatch(r"\d{12}", key):
            return key
    except (OSError, ValueError):
        pass
    key = "%012d" % secrets.randbelow(10 ** 12)
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    KEY_FILE.write_text(key, encoding="ascii")
    return key


def spaced(key):
    return " ".join(key[i:i + 4] for i in range(0, len(key), 4))


# ---- Steam library ----------------------------------------------------------

def steam_root():
    if WINDOWS:
        import winreg
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\Valve\Steam") as key:
                return Path(winreg.QueryValueEx(key, "SteamPath")[0])
        except OSError:
            return None
    for candidate in (Path.home() / ".local/share/Steam", Path.home() / ".steam/steam"):
        if candidate.is_dir():
            return candidate
    return None


def _vdf_values(text, key):
    """Every value of `key` in Valve's text format ("key"  "value")."""
    return [v.replace("\\\\", "\\") for v in re.findall(r'"%s"\s+"((?:[^"\\]|\\.)*)"' % key, text)]


def steam_games():
    """Installed Steam games as [{"id": "steam-<appid>", "title", "source"}]."""
    root = steam_root()
    if not root:
        return []
    folders = {root}
    try:
        text = (root / "steamapps" / "libraryfolders.vdf").read_text(encoding="utf-8", errors="replace")
        folders.update(Path(p) for p in _vdf_values(text, "path"))
    except OSError:
        pass
    games = {}
    for folder in folders:
        for manifest in (folder / "steamapps").glob("appmanifest_*.acf"):
            try:
                text = manifest.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            appid, name = _vdf_values(text, "appid"), _vdf_values(text, "name")
            if appid and name and appid[0].isdigit() and not STEAM_TOOLS.match(name[0]):
                games[appid[0]] = {"id": "steam-" + appid[0], "title": name[0], "source": "Steam"}
    return sorted(games.values(), key=lambda g: g["title"].lower())


def steam_art(appid):
    """Steam's own cached portrait cover of a game, or None."""
    root = steam_root()
    if not root:
        return None
    cache = root / "appcache" / "librarycache"
    flat = cache / (appid + "_library_600x900.jpg")
    if flat.is_file():
        return flat
    # Newer clients keep a folder per app, sometimes with a hashed subfolder.
    for name in ("library_600x900.jpg", "library_capsule.jpg"):
        for found in (cache / appid).rglob(name):
            return found
    return None


# ---- Custom games -----------------------------------------------------------

def load_custom():
    try:
        games = json.loads(GAMES_FILE.read_text(encoding="utf-8"))
        return [g for g in games if isinstance(g, dict) and g.get("id") and g.get("title") and g.get("path")]
    except (OSError, ValueError):
        return []


def save_custom(games):
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    GAMES_FILE.write_text(json.dumps(games, indent=2), encoding="utf-8")


def is_link(path):
    """A launcher link such as com.epicgames.launcher://apps/...?action=launch"""
    return re.match(r"^[A-Za-z][A-Za-z0-9+.-]+://", path) is not None


def add_custom(title, path, cover=""):
    """Add a game; returns an error message or None."""
    title, path, cover = title.strip(), path.strip().strip('"'), cover.strip().strip('"')
    if not title or not path:
        return "A name and a path are needed."
    if not is_link(path) and not Path(path).is_file():
        return "That file does not exist: " + path
    if cover and not Path(cover).is_file():
        return "That cover image does not exist: " + cover
    games = load_custom()
    # The id travels in URLs and becomes a file name on the 3DS.
    slug = re.sub(r"[^a-z0-9]+", "-", title.lower()).strip("-")[:30] or "game"
    used = {g["id"] for g in games}
    game_id, n = "custom-" + slug, 2
    while game_id in used:
        game_id, n = "custom-%s-%d" % (slug, n), n + 1
    games.append({"id": game_id, "title": title, "path": path, "cover": cover})
    save_custom(games)
    return None


def remove_custom(game_id):
    save_custom([g for g in load_custom() if g["id"] != game_id])


def library():
    """What the 3DS shows: the desktop, Steam games, then custom games."""
    custom = [{"id": g["id"], "title": g["title"], "source": "Custom"} for g in load_custom()]
    return [{"id": "desktop", "title": "Desktop", "source": "PC"}] + steam_games() + custom


def open_target(target):
    if WINDOWS:
        os.startfile(target)  # .exe, shortcut or launcher link, as a double click would
    elif is_link(target):
        subprocess.Popen(["xdg-open", target])
    else:
        subprocess.Popen([target], cwd=str(Path(target).parent))


def launch(game_id):
    """Start a game that is in the library. False if it is not."""
    if game_id == "desktop":
        return True
    if re.fullmatch(r"steam-\d+", game_id):
        if not any(g["id"] == game_id for g in steam_games()):
            return False
        open_target("steam://rungameid/" + game_id[6:])
        return True
    for game in load_custom():
        if game["id"] == game_id:
            open_target(game["path"])
            return True
    return False


def art_file(game_id):
    if re.fullmatch(r"steam-\d+", game_id):
        return steam_art(game_id[6:])
    for game in load_custom():
        if game["id"] == game_id and game.get("cover"):
            return Path(game["cover"])
    return None


# ---- CPU and memory ---------------------------------------------------------

def _cpu_times():
    """(idle, total) processor time since boot, in the OS's own units."""
    if WINDOWS:
        idle, kernel, user = ctypes.c_uint64(), ctypes.c_uint64(), ctypes.c_uint64()
        ctypes.windll.kernel32.GetSystemTimes(ctypes.byref(idle), ctypes.byref(kernel), ctypes.byref(user))
        return idle.value, kernel.value + user.value  # kernel time includes idle
    with open("/proc/stat") as f:
        fields = [int(x) for x in f.readline().split()[1:]]
    return fields[3] + fields[4], sum(fields)


_last_cpu = _cpu_times()


def cpu_percent():
    """Processor load since the previous call."""
    global _last_cpu
    idle, total = _cpu_times()
    d_idle, d_total = idle - _last_cpu[0], total - _last_cpu[1]
    _last_cpu = (idle, total)
    return round(100.0 * (1.0 - d_idle / d_total), 1) if d_total > 0 else None


class _MemoryStatus(ctypes.Structure):
    _fields_ = [("dwLength", ctypes.c_uint32), ("dwMemoryLoad", ctypes.c_uint32)] + [
        (name, ctypes.c_uint64) for name in (
            "ullTotalPhys", "ullAvailPhys", "ullTotalPageFile", "ullAvailPageFile",
            "ullTotalVirtual", "ullAvailVirtual", "ullAvailExtendedVirtual")]


def ram_percent():
    if WINDOWS:
        status = _MemoryStatus()
        status.dwLength = ctypes.sizeof(status)
        ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status))
        return float(status.dwMemoryLoad)
    info = {}
    with open("/proc/meminfo") as f:
        for line in f:
            key, value = line.split(":")
            info[key] = int(value.split()[0])
    return round(100.0 * (1.0 - info["MemAvailable"] / info["MemTotal"]), 1)


# ---- GPU (NVIDIA) -----------------------------------------------------------

_NVIDIA_SMI = shutil.which("nvidia-smi")


def _number(text):
    try:
        return float(text)
    except ValueError:
        return None


def gpu_stats():
    """(load %, temperature C, VRAM %) of the first NVIDIA card, or Nones."""
    if not _NVIDIA_SMI:
        return None, None, None
    try:
        out = subprocess.run(
            [_NVIDIA_SMI, "--query-gpu=utilization.gpu,temperature.gpu,memory.used,memory.total",
             "--format=csv,noheader,nounits"],
            capture_output=True, text=True, timeout=3,
            creationflags=subprocess.CREATE_NO_WINDOW if WINDOWS else 0).stdout
        # Some cards answer "[N/A]" for one field: keep the others.
        load, temp, used, total = [_number(x) for x in out.splitlines()[0].split(",")]
        return load, temp, round(100.0 * used / total, 1) if used is not None and total else None
    except (OSError, ValueError, IndexError, subprocess.SubprocessError):
        return None, None, None


# ---- Game frame rate (RivaTuner Statistics Server) --------------------------

def game_fps():
    """Frame rate of the app RTSS saw render most recently, or None."""
    if not WINDOWS:
        return None
    kernel32 = ctypes.windll.kernel32
    kernel32.OpenFileMappingW.restype = ctypes.c_void_p
    kernel32.MapViewOfFile.restype = ctypes.c_void_p
    kernel32.MapViewOfFile.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_size_t]
    kernel32.UnmapViewOfFile.argtypes = [ctypes.c_void_p]
    kernel32.CloseHandle.argtypes = [ctypes.c_void_p]
    FILE_MAP_READ = 0x0004
    # Opening (never creating) the mapping: it only exists while RTSS runs.
    handle = kernel32.OpenFileMappingW(FILE_MAP_READ, False, "RTSSSharedMemoryV2")
    if not handle:
        return None
    view = kernel32.MapViewOfFile(handle, FILE_MAP_READ, 0, 0, 0)
    try:
        if not view:
            return None
        # RTSS_SHARED_MEMORY: signature, version, app entry size, app array
        # offset, app array size (all DWORD).
        signature, _version, entry_size, array_offset, array_count = (ctypes.c_uint32 * 5).from_address(view)
        if signature != 0x52545353 or entry_size < 284 or array_count > 4096:  # 'RTSS'
            return None
        now = kernel32.GetTickCount()
        best_time, best_fps = 0, None
        for i in range(array_count):
            entry = view + array_offset + i * entry_size
            pid = ctypes.c_uint32.from_address(entry).value
            # After the process name (MAX_PATH): flags, time0, time1, frames.
            _flags, time0, time1, frames = (ctypes.c_uint32 * 4).from_address(entry + 4 + 260)
            if not pid or time1 <= time0 or not frames:
                continue
            # Only an app that rendered within the last two seconds counts.
            if (now - time1) & 0xFFFFFFFF > 2000 or time1 < best_time:
                continue
            best_time, best_fps = time1, round(1000.0 * frames / (time1 - time0), 1)
        return best_fps
    finally:
        if view:
            kernel32.UnmapViewOfFile(view)
        kernel32.CloseHandle(handle)


def sample():
    gpu, gpu_temp, vram = gpu_stats()
    return {"cpu": cpu_percent(), "ram": ram_percent(), "gpu": gpu, "vram": vram,
            "gpu_temp": gpu_temp, "fps": game_fps()}


# ---- Serving ----------------------------------------------------------------

_stats_cache = {"at": 0.0, "body": b"{}"}
# Proves a form was sent from the page this program served (a web site open
# in the browser cannot read it, so it cannot add a game behind your back).
_form_token = secrets.token_urlsafe(16)

PAGE = """<!doctype html><meta charset="utf-8"><title>Oboro Host</title>
<style>body{font:16px system-ui;max-width:44em;margin:2em auto;padding:0 1em}
input[type=text]{width:100%%;padding:.4em;margin:.2em 0 .8em;box-sizing:border-box}
td{padding:.2em .8em .2em 0}.err{color:#b00}small{color:#666}</style>
<h1>Oboro Host</h1>
<p>Key for the 3DS: <b style="font-size:1.4em;letter-spacing:.05em">%(key)s</b><br>
<small>Oboro asks for it once, after the PC's address. Anyone who has it can start your games: keep it
private.</small></p>
<p>%(count)d games are shown on the 3DS: the desktop, %(steam)d installed Steam games (found automatically)
and the custom games below.</p>
<p class="err">%(error)s</p>
<h2>Custom games</h2>
<table>%(rows)s</table>
<h2>Add a custom game</h2>
<form method="post" action="/add"><input type="hidden" name="token" value="%(token)s">
<label>Name<input type="text" name="title" required></label>
<label>Path to the game's .exe or shortcut, or a launcher link
<input type="text" name="path" required placeholder="C:\\Games\\MyGame\\game.exe"></label>
<label>Cover image, portrait .jpg or .png under 1 MB <small>(optional)</small>
<input type="text" name="cover" placeholder="C:\\Pictures\\mygame.jpg"></label>
<button>Add game</button></form>
<p><small>Tip: Shift + right-click a file in Explorer and choose "Copy as path". On the 3DS, press Y in the
library to refresh.</small></p>"""


class Handler(BaseHTTPRequestHandler):
    # The port may face the internet (remote play): drop idle connections.
    timeout = 15

    def _send(self, status, body, content_type="application/json"):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _local(self):
        """The request comes from a browser on this PC, addressed to this PC."""
        host = (self.headers.get("Host") or "").split(":")[0]
        return self.client_address[0] == "127.0.0.1" and host in ("localhost", "127.0.0.1")

    def _keyed(self):
        """The request carries this PC's key (the 3DS sends it every time)."""
        return secrets.compare_digest((self.headers.get("X-Oboro-Key") or "").encode(), host_key().encode())

    def _page(self, error=""):
        games = library()
        rows = "".join(
            '<tr><td>%s</td><td><small>%s</small></td><td><form method="post" action="/remove">'
            '<input type="hidden" name="token" value="%s"><input type="hidden" name="id" value="%s">'
            '<button>Remove</button></form></td></tr>'
            % (html.escape(g["title"]), html.escape(g["path"]), _form_token, html.escape(g["id"]))
            for g in load_custom()) or "<tr><td><small>None yet.</small></td></tr>"
        page = PAGE % {"count": len(games), "steam": sum(g["source"] == "Steam" for g in games),
                       "error": html.escape(error), "rows": rows, "token": _form_token,
                       "key": spaced(host_key())}
        self._send(200, page.encode(), "text/html; charset=utf-8")

    def do_GET(self):
        url = urlsplit(self.path)
        query = parse_qs(url.query)
        if url.path in ("/stats", "/library", "/art") and not self._keyed():
            self.send_error(401)
        elif url.path == "/stats":
            # One measurement a second at most, however many consoles ask.
            now = time.monotonic()
            if now - _stats_cache["at"] >= 1.0:
                _stats_cache["at"], _stats_cache["body"] = now, json.dumps(sample()).encode()
            self._send(200, _stats_cache["body"])
        elif url.path == "/library":
            self._send(200, json.dumps({"games": library()}).encode())
        elif url.path == "/art":
            path = art_file(query.get("id", [""])[0])
            try:
                if not path or path.stat().st_size > ART_LIMIT:
                    raise OSError
                self._send(200, path.read_bytes(), "image/png" if path.suffix.lower() == ".png" else "image/jpeg")
            except OSError:
                self.send_error(404)
        elif url.path == "/" and self._local():
            self._page()
        else:
            self.send_error(404)

    def do_POST(self):
        url = urlsplit(self.path)
        try:
            length = min(int(self.headers.get("Content-Length") or 0), 65536)
        except ValueError:
            length = 0
        form = parse_qs(self.rfile.read(length).decode("utf-8", errors="replace"))
        field = lambda name: form.get(name, [""])[0]
        if url.path == "/launch":
            # A header a web page cannot send across sites: only the 3DS (or
            # a deliberate request) starts games, never a link in a browser.
            if self.headers.get("X-Oboro") != "1":
                self.send_error(403)
            elif not self._keyed():
                self.send_error(401)
            elif launch(parse_qs(url.query).get("id", [""])[0]):
                self._send(200, b'{"ok":true}')
            else:
                self.send_error(404)
        elif url.path in ("/add", "/remove") and self._local() and secrets.compare_digest(field("token"), _form_token):
            error = ""
            if url.path == "/add":
                error = add_custom(field("title"), field("path"), field("cover")) or ""
            else:
                remove_custom(field("id"))
            self._page(error)
        else:
            self.send_error(403)

    def log_message(self, *args):
        pass  # the 3DS asks every two seconds: that would bury the console


# ---- Starting with Windows --------------------------------------------------

RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"


def _start_menu_shortcut():
    return Path(os.environ["APPDATA"]) / "Microsoft/Windows/Start Menu/Programs/Oboro Host.lnk"


def _make_shortcut(link, target, arguments):
    """A .lnk through Windows' own shell object (the standard library has no
    writer for them). Values go in as environment variables, not as script."""
    script = ("$s = (New-Object -ComObject WScript.Shell).CreateShortcut($env:OBORO_LINK); "
              "$s.TargetPath = $env:OBORO_TARGET; $s.Arguments = $env:OBORO_ARGS; "
              "$s.Description = 'Oboro Host: games and PC stats for the 3DS'; $s.Save()")
    env = dict(os.environ, OBORO_LINK=str(link), OBORO_TARGET=str(target), OBORO_ARGS=arguments)
    subprocess.run(["powershell", "-NoProfile", "-Command", script], env=env, check=True,
                   creationflags=subprocess.CREATE_NO_WINDOW)


def set_autostart(enabled):
    if not WINDOWS:
        sys.exit("--install is for Windows; on Linux add this script to your desktop's autostart.")
    import winreg
    with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE) as key:
        if enabled:
            # pythonw: no console window at sign-in.
            pythonw = Path(sys.executable).with_name("pythonw.exe")
            python = pythonw if pythonw.is_file() else Path(sys.executable)
            script = Path(__file__).resolve()
            winreg.SetValueEx(key, "OboroHost", 0, winreg.REG_SZ, '"%s" "%s"' % (python, script))
            print("Oboro Host will start when you sign in to Windows.")
            try:
                # Clicking it starts Oboro Host if needed and opens its page.
                _make_shortcut(_start_menu_shortcut(), python, '"%s" --open' % script)
                print('"Oboro Host" is in the Start menu: click it to start it now.')
            except (OSError, KeyError, subprocess.SubprocessError):
                print("Couldn't add the Start menu shortcut. Start it now with: python %s" % script)
        else:
            try:
                winreg.DeleteValue(key, "OboroHost")
            except FileNotFoundError:
                pass
            try:
                _start_menu_shortcut().unlink()
            except (OSError, KeyError):
                pass
            print("Oboro Host no longer starts with Windows.")


def main():
    parser = argparse.ArgumentParser(description="Oboro Host: game library, launcher and PC stats for the 3DS")
    parser.add_argument("--port", type=int, default=PORT, help="TCP port (the 3DS expects %d)" % PORT)
    parser.add_argument("--once", action="store_true", help="print the library and one stats sample, then exit")
    parser.add_argument("--key", action="store_true", help="print the key the 3DS asks for, then exit")
    parser.add_argument("--open", action="store_true", help="also open the page in the browser (the Start menu does)")
    parser.add_argument("--install", action="store_true", help="start with Windows, and add a Start menu shortcut")
    parser.add_argument("--uninstall", action="store_true", help="undo --install")
    args = parser.parse_args()
    if args.install or args.uninstall:
        set_autostart(args.install)
        return
    if args.key:
        print(spaced(host_key()))
        return
    if args.once:
        time.sleep(0.5)  # CPU load is measured between two readings
        print(json.dumps({"library": library(), "stats": sample()}, indent=2))
        return
    page = "http://localhost:%d" % args.port
    try:
        server = ThreadingHTTPServer(("0.0.0.0", args.port), Handler)
    except OSError:
        # Most likely Oboro Host is running already: show its page instead.
        if args.open:
            webbrowser.open(page)
        sys.exit("Port %d is in use: Oboro Host is probably running already." % args.port)
    if args.open:
        webbrowser.open(page)
    print("Oboro Host on port %d. Add custom games at http://localhost:%d  (Ctrl+C to stop)" % (args.port, args.port))
    print("Key for the 3DS: %s" % spaced(host_key()))
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
