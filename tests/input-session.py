"""Candidate order, selection and Shift regression checks. Run via tests/check-input."""
import os
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

    def receive(preedit, cursor, aux, down, words, selected, layout, prev, nxt):
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

    try:
        deadline = time.monotonic() + 30
        while not type_word("w"):
            assert time.monotonic() < deadline, "Rime did not return candidates"
            time.sleep(.12)
        assert type_word("key")[0] == "可以"
        assert len(type_word("shi")) == 9
        for typo in ("hsang", "shnag"):
            for mouse in (False, True):
                words = type_word(typo)
                assert words[1].replace(" ", "") == "shang〔纠错〕", words
                if mouse:
                    ctx.SelectCandidate(1)
                else:
                    assert key(ord("2")) == []
                assert panel()[0] == "上"
                assert key(32) == ["上"]
        assert all("纠错" not in word for word in type_word("shang"))
        for shift, code in ((0xffe1, 50), (0xffe2, 62)):
            assert type_word("hello")
            assert key(shift, code) == ["hello"]
            assert controller.CurrentInputMethod() == "keyboard-cn"
            assert key(shift, code) == [] and controller.CurrentInputMethod() == "rime"
        print("Shift 原文、纠错候选、点选及中文排序通过。")
    finally:
        ctx.FocusOut()
        ctx.DestroyIC()


if __name__ == "__main__":
    check()
