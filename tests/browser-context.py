"""Type through the real Chromium X11 input client. Run with tests/check-input --browser."""
import ctypes
from ctypes.util import find_library
import json
import os
from pathlib import Path
import re
import runpy
import shutil
import subprocess
import time
from urllib.parse import quote

import dbus
import yaml


def wait_for(check, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = check()
        if result:
            return result
        time.sleep(.04)
    raise AssertionError("Timed out waiting for the browser or input method")


def check():
    home = Path.home()
    assert (home / "lune-private-x11-test").is_file()
    browser = next((exe for name in ("microsoft-edge", "google-chrome", "chromium", "chromium-browser")
                    if (exe := shutil.which(name))), None)
    if not browser:
        raise SystemExit("The browser test requires Edge, Chrome or Chromium.")

    steps = [("single", "", 0, 0), ("multi", "", 0, 0), ("multi", "主任", 0, 0),
             ("single", "电路", 2, 2), ("rich", "主任", 0, 0), ("multi", "😀替换主任", 2, 4),
             ("multi", "主任", 0, 0), ("multi", "电路", 2, 2),
             ("multi", "", 0, 0), ("password", "主任", 0, 0)]
    page = home / "form.html"
    page.write_text("""<!doctype html><meta charset="utf-8"><meta name="google" content="notranslate">
<input id="single"><textarea id="multi"></textarea><div contenteditable="true" id="rich"></div>
<input type="password" id="password"><script>
const steps = """ + json.dumps(steps, ensure_ascii=False) + """;
let index = -1, field;
function report() {
  const text = field.isContentEditable ? field.textContent : field.value;
  document.title = "LuneInputTest/" + index + "/" + encodeURIComponent(text) + "/";
}
function advance() {
  const [id, value, start, end] = steps[++index];
  field = document.getElementById(id);
  field.focus();
  if (field.isContentEditable) {
    field.textContent = value;
    const range = document.createRange();
    range.setStart(field.firstChild, start);
    range.setEnd(field.firstChild, end);
    getSelection().removeAllRanges();
    getSelection().addRange(range);
  } else {
    field.value = value;
    field.setSelectionRange(start, end);
  }
  report();
}
document.addEventListener("keydown", e => { if (e.key === "F8") { e.preventDefault(); advance(); } });
document.addEventListener("input", report);
advance();
</script>""")

    # XTest follows the browser's normal GTK IM path. Browser automation APIs
    # that insert DOM text or dispatch keys directly do not exercise Fcitx.
    xlib = ctypes.CDLL(find_library("X11"))
    xtest = ctypes.CDLL(find_library("Xtst"))
    display_type = ctypes.c_void_p
    xlib.XOpenDisplay.argtypes = [ctypes.c_char_p]
    xlib.XOpenDisplay.restype = display_type
    xlib.XStringToKeysym.argtypes = [ctypes.c_char_p]
    xlib.XStringToKeysym.restype = ctypes.c_ulong
    xlib.XKeysymToKeycode.argtypes = [display_type, ctypes.c_ulong]
    xlib.XSetInputFocus.argtypes = [display_type, ctypes.c_ulong, ctypes.c_int, ctypes.c_ulong]
    xlib.XSync.argtypes = [display_type, ctypes.c_int]
    xlib.XCloseDisplay.argtypes = [display_type]
    xtest.XTestFakeKeyEvent.argtypes = [display_type, ctypes.c_uint, ctypes.c_int, ctypes.c_ulong]
    display = xlib.XOpenDisplay(None)
    assert display

    def press(*keys):
        for key in keys:
            number = xlib.XKeysymToKeycode(display, xlib.XStringToKeysym(key.encode()))
            xtest.XTestFakeKeyEvent(display, number, 1, 0)
            xtest.XTestFakeKeyEvent(display, number, 0, 0)
        xlib.XSync(display, 0)

    process = subprocess.Popen([browser, "--app=" + page.as_uri(), "--user-data-dir=" + str(home / "browser"),
                                "--no-first-run", "--no-default-browser-check", "--disable-background-networking",
                                "--disable-sync", "--disable-gpu", "--ozone-platform=x11", "--password-store=basic"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        def window():
            tree = subprocess.check_output(["xwininfo", "-root", "-tree"], text=True)
            hit = re.search(r'^\s*(0x[0-9a-f]+) "LuneInputTest/', tree, re.MULTILINE)
            return hit and hit[1]

        wid = wait_for(window, 18)
        xlib.XSetInputFocus(display, int(wid, 16), 2, 0)
        xlib.XSync(display, 0)
        controller = dbus.Interface(dbus.SessionBus().get_object("org.fcitx.Fcitx5", "/controller"),
                                    "org.fcitx.Fcitx.Controller1")
        wait_for(lambda: "focus:1" in str(controller.DebugInfo()))
        controller.SetCurrentIM("rime")
        time.sleep(.4)
        data = Path(os.environ["XDG_DATA_HOME"]) / "fcitx5"
        settings = runpy.run_path(str(data / "lune/settings.py"), run_name="lune_browser_test")
        profile = data / "rime/build/luna_pinyin_simp.schema.yaml"

        def toggle(enabled):
            settings["Settings"]().save({"context": enabled})
            wait_for(lambda: yaml.safe_load(profile.read_bytes())["lune"]["context_candidates"] == enabled, 35)
            time.sleep(.15)

        def title():
            return subprocess.check_output(["xprop", "-id", wid, "_NET_WM_NAME"], text=True)

        def verify(index, code, expected, key="2"):
            if index:
                press("F8")
            wait_for(lambda: f"LuneInputTest/{index}/{quote(steps[index][1])}/" in title())
            press(*code)  # type the complete pinyin, then pause without another key
            time.sleep(.42)
            press(key)
            forms = expected if isinstance(expected, tuple) else (expected,)
            try:
                wait_for(lambda: any(f"LuneInputTest/{index}/{quote(value)}/" in title() for value in forms), 4)
            except AssertionError as exc:
                raise AssertionError((index, code, forms, title(), controller.CurrentInputMethod(),
                                      str(controller.DebugInfo()))) from exc

        verify(0, "sheshidu", "℃")
        verify(1, "ban", "版")
        verify(2, "ban", "班主任")
        verify(3, "ban", "电路板")
        verify(4, "ban", "班主任")
        verify(5, "ban", "😀班主任")
        toggle(False)
        verify(6, "ban", "版主任")
        verify(7, "ban", "电路版")
        toggle(True)
        verify(8, "ban", "版")
        verify(9, "ban", ("ban2主任", "版主任"))
        print("浏览器真实按键验证通过，覆盖前后文、快速连续输入、选区、密码框和联想开关。")
    finally:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        xlib.XCloseDisplay(display)


if __name__ == "__main__":
    check()
