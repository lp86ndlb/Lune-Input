"""Input, navigation and selection checks. Run via tests/check-input."""
import os
from pathlib import Path
import runpy
import time

import dbus
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib


def check():
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    name = "org.fcitx.Fcitx5"
    deadline = time.monotonic() + 20
    while not bus.name_has_owner(name):
        assert time.monotonic() < deadline, "Fcitx startup timed out"
        time.sleep(.1)

    def interface(path, suffix):
        return dbus.Interface(bus.get_object(name, path), "org.fcitx.Fcitx." + suffix)

    controller = interface("/controller", "Controller1")
    im = interface("/org/freedesktop/portal/inputmethod", "InputMethod1")
    path, _ = im.CreateInputContext([("program", "lune-test"), ("display", "x11:" + os.environ["DISPLAY"])])
    ctx = interface(path, "InputContext1")
    candidates = []
    preedit_text, caret, highlighted = "", -1, -1

    def receive(preedit, cursor, aux, down, words, selected, layout, prev, nxt):
        nonlocal preedit_text, caret, highlighted
        preedit_text = "".join(str(text) for text, _ in preedit)
        caret, highlighted = int(cursor), int(selected)
        candidates[:] = [str(word) for _, word in words]

    ctx.connect_to_signal("UpdateClientSideUI", receive)
    ctx.SetCapability(1 << 39)  # ClientSideInputPanel
    ctx.FocusIn()
    controller.SetCurrentIM("rime")

    def key(sym, code=0):
        commits = []
        for release in (False, True):
            shift = 1 if release and sym in (0xffe1, 0xffe2) else 0
            events, _ = ctx.ProcessKeyEventBatch(sym, code, shift, release, 0)
            commits.extend(str(value) for kind, value in events if kind == 0)
        return commits

    def panel():
        controller.CurrentInputMethod()  # round-trip after the update
        loop = GLib.MainContext.default()
        while loop.pending():
            loop.iteration(False)
        return list(candidates)

    def type_word(word):
        key(0xff1b)
        for char in word:
            key(ord(char))
        return panel()

    def test_arrows():
        left, right = 0xff51, 0xff53

        def move(sym, prefix):
            key(sym)
            assert panel() and highlighted == 0
            assert preedit_text.encode()[:caret].decode() == prefix, (preedit_text, caret, prefix)

        assert type_word("shang") and caret == 5
        move(right, "shang")
        move(left, "")
        move(left, "")
        move(right, "s")
        for spelling, fa_start, ru_start in (("shurufa", "shu'ru", "shu"),
                                             ("shu'ru'fa", "shu'ru'", "shu'")):
            assert type_word(spelling)[0] == "输入法"
            move(left, fa_start)
            move(left, ru_start)
            move(right, "shu'r")
            move(left, ru_start)
            move(left, "")
            move(right, "s")

        assert type_word("xi'en")
        move(0xff96, "xi'")  # KP_Left
        move(0xff98, "xi'e")  # KP_Right
        key(0xff08)  # BackSpace
        key(ord("a"))
        key(0xff57)  # End
        assert panel()[0] == "西安" and preedit_text == "xi'an"

        words = type_word("nihaoma")
        ctx.SelectCandidate(words.index("你"))
        move(left, "你hao")
        move(left, "你")
        move(left, "你")
        move(right, "你h")
        ctx.Reset()

        # The installed GTK switch must survive reopening and change real key events.
        settings_path = Path(os.environ["XDG_DATA_HOME"]) / "fcitx5/lune/settings.py"
        ui = runpy.run_path(str(settings_path), run_name="lune_test_settings")
        app = ui["Gtk"].Application(application_id="io.github.lp86ndlb.LuneInput.KeyTest")
        assert app.register(None)
        for enabled in (True, False):
            key(0xff1b)
            window = ui["Window"](app, ui["Settings"]())
            switch = window.controls["candidate_arrows"]
            assert switch.get_active() != enabled
            switch.set_active(enabled)
            window.apply_button.clicked()
            assert not window.changes(), window.status.get_text()
            window.destroy()
            deadline = time.monotonic() + 30
            while time.monotonic() < deadline:
                type_word("shang")
                key(right)
                if panel() and (caret, highlighted) == (5, int(enabled)):
                    break
                time.sleep(.1)
            else:
                raise AssertionError((enabled, caret, highlighted, candidates))
            assert ui["Settings"]().values["candidate_arrows"] == enabled
            if enabled:
                key(left)
                assert panel() and (caret, highlighted) == (5, 0)
                move(left, "")
                move(right, "s")
                words = type_word("shang")
                key(right)
                assert key(32) == [words[1]]
        key(0xff1b)
        for arrow in (left, right):
            _, handled = ctx.ProcessKeyEventBatch(arrow, 0, 0, False, 0)
            ctx.ProcessKeyEventBatch(arrow, 0, 0, True, 0)
            assert not handled

    try:
        deadline = time.monotonic() + 30
        while not type_word("w"):
            assert time.monotonic() < deadline, "Rime did not return candidates"
            time.sleep(.12)
        assert type_word("key")[0] == "可以"
        assert len(type_word("shi")) == 9
        test_arrows()
        repairs = {
            "hsang": "shang", "shnag": "shang",
            "qiing": "qing", "qiiing": "qing", "qiingwen": "qingwen",
            "qiing'wen": "qing'wen", "jinnnian": "jinnian",
            "zhongguorenmiinyinhang": "zhongguorenminyinhang",
        }
        assert "请" in type_word("qing") and "请问" in type_word("qingwen")
        for typo, corrected in repairs.items():
            for mouse in (False, True):
                expected = type_word(corrected)
                words = type_word(typo)
                assert words[1].replace(" ", "") == corrected + "〔纠错〕", (typo, words)
                if mouse:
                    ctx.SelectCandidate(1)
                else:
                    assert key(ord("2")) == []
                assert panel() == expected, (typo, corrected, panel())
                assert key(32) == [expected[0]]
        for unchanged in ("shang", "qing", "jinnian", "zhongguo", "mama", "naan",
                          "hello", "bidding", "qqqun", "linuxxitong", "qiingweenn"):
            assert all("纠错" not in word for word in type_word(unchanged)), unchanged
        for shift, code in ((0xffe1, 50), (0xffe2, 62)):
            assert type_word("hello")
            assert key(shift, code) == ["hello"]
            assert controller.CurrentInputMethod() == "keyboard-cn"
            assert key(shift, code) == [] and controller.CurrentInputMethod() == "rime"
        print("拼音光标、方向键选词开关、Shift 原文、纠错、点选及中文排序通过。")
    finally:
        ctx.FocusOut()
        ctx.DestroyIC()


if __name__ == "__main__":
    check()
