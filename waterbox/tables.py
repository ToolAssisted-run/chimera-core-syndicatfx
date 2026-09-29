#!/usr/bin/env python3
"""tables.py - the core's declarations in ONE place: buttons (wire order), axes, settings, firmware.
Generates waterbox/sfx-tables.h (for the driver), waterbox/waterbox.config and waterbox/default_keybinds.json.

usage: tables.py [--data CD_SYNDICAT_DATA_DIR]   (the firmware sizes/hashes come from a known-good CD;
       without --data the firmware part of the existing waterbox.config is kept)"""
import sys, os, json, hashlib, re

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# ---- buttons: (config name, kind, code, default host binding). kind 'mouse' code = bit, 'key' code = SDL scancode
BUTTONS = [("Mouse Left Button", "mouse", 0, "WMouse L"), ("Mouse Right Button", "mouse", 1, "WMouse R")]
for i, c in enumerate("1234567890"):
    BUTTONS.append(("Key " + c, "key", 30 + i, "Number" + c))
for i, c in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
    BUTTONS.append(("Key " + c, "key", 4 + i, c))
for i in range(12):
    BUTTONS.append(("Key F%d" % (i + 1), "key", 58 + i, "F%d" % (i + 1)))
BUTTONS += [
    ("Key Escape", "key", 41, "Escape"), ("Key Enter", "key", 40, "Enter"), ("Key Space", "key", 44, "Space"),
    ("Key Backspace", "key", 42, "Backspace"), ("Key Tab", "key", 43, "Tab"),
    ("Key Up", "key", 82, "Up"), ("Key Down", "key", 81, "Down"), ("Key Left", "key", 80, "Left"), ("Key Right", "key", 79, "Right"),
    ("Key LeftShift", "key", 225, "Shift, LeftShift"), ("Key RightShift", "key", 229, "RightShift"),
    ("Key LeftCtrl", "key", 224, "Ctrl, LeftCtrl"), ("Key RightCtrl", "key", 228, "RightCtrl"),
    ("Key LeftAlt", "key", 226, "Alt, LeftAlt"), ("Key RightAlt", "key", 230, "RightAlt"),
    ("Key Minus", "key", 45, "Minus"), ("Key Equals", "key", 46, "Equals"), ("Key Period", "key", 55, "Period"),
    ("Key Comma", "key", 54, "Comma"), ("Key Slash", "key", 56, "Slash"), ("Key Semicolon", "key", 51, "Semicolon"),
    ("Key Quote", "key", 52, "Apostrophe"), ("Key Home", "key", 74, "Home"), ("Key End", "key", 77, "End"),
    ("Key Pageup", "key", 75, "PageUp"), ("Key Pagedown", "key", 78, "PageDown"), ("Key Insert", "key", 73, "Insert"),
    ("Key Delete", "key", 76, "Delete"), ("Key Pause", "key", 72, "Pause"),
    ("Key KeyPadPlus", "key", 87, "KeypadAdd"), ("Key KeyPadMinus", "key", 86, "KeypadSubtract"),
]
AXES = [("Mouse Position X", 0, 65535, 32768, "WMouse X"), ("Mouse Position Y", 0, 65535, 32768, "WMouse Y")]

SETTINGS = [
    {"name": "language", "display": "Language", "type": "enum", "default": "English", "sync": True,
     "values": ["English", "French", "Italian"],
     "description": "The game's language (the original '-c' option): menus from SyndicatFX's translations, mission "
                    "briefings from the CD's own sets. Changes play (texts, layout), so a movie records it."},
]
LANG_ARG = {"English": "0", "French": "1", "Italian": "2"}

# ---- firmware: the game files syndicatfx installs from the CD (util/install copy_data), plus the original
# MSPR-0 sprites (the port's installer takes those from its fan pack; the core runs the original ones)
def firmware_names():
    inst = open(os.path.join(ROOT, "extern/syndicatfx/util/install")).read()
    body = inst[inst.index("copy_data()"):inst.index("extract_packages()")]
    names = set()
    for m in re.finditer(r'for fn in ([^;]+); do\n((?:.*\n)*?)\s*done', body):
        vals = m.group(1).split()
        for line in m.group(2).splitlines():
            mm = re.search(r'install_file(?:_to)? "data/([^"]+)"', line)
            if mm:
                for v in vals: names.add(mm.group(1).replace("${fn}", v).upper())
    names |= {"MSPR-0.DAT", "MSPR-0.TAB"}
    return sorted(names)

def main():
    data = None
    if "--data" in sys.argv: data = sys.argv[sys.argv.index("--data") + 1]
    cfg_path = os.path.join(HERE, "waterbox.config")
    old = json.load(open(cfg_path)) if os.path.exists(cfg_path) else {}
    if data:
        have = {f.upper(): f for f in os.listdir(data)}
        fw = []
        for n in firmware_names():
            b = open(os.path.join(data, have[n]), "rb").read()
            fw.append({"id": n, "display": n, "name": n, "size": len(b), "sha1": hashlib.sha1(b).hexdigest().upper(),
                       "description": "Syndicate Plus CD (EA/Bullfrog 1994): SYNDICAT\\DATA\\" + n})
    else:
        fw = old.get("firmware", [])
    cfg = {
        "coreName": "SyndicatFX",
        "kind": "game",
        "systemId": "Syndicate",
        "author": "Bullfrog (1993); SyndicatFX by Mefistotelis, Unavowed, Gynvael Coldwind and fans; chimera port by Sergio Martin",
        "url": "https://github.com/ToolAssisted-run/chimera-core-syndicatfx",
        "deterministic": True,
        "_memory_note": "[sbrk, sealed, invisible, plain, mmap] MiB: the 64 MiB i386 arena and the program's 16 MiB stack are mmap'd; the game files are copied into sealed memory",
        "memoryLayoutMiB": [64, 32, 4, 4, 160],
        "video": {"_comment": "buffer capacity; the live size (320x200 menus, 640x480 missions) comes from GetVideoWidth/Height",
                  "width": 640, "height": 480, "virtualWidth": 640, "virtualHeight": 480,
                  "vsyncNumerator": 16, "vsyncDenominator": 1, "getBgra": "GetVideoBgra"},
        "audio": {"rate": 44100, "samplesPerFrame": 65536, "channels": 2, "get": "GetAudio"},
        "lag": {"inputWasRead": "InputWasRead"},
        "input": {"name": "Syndicate Controller",
                  "_comment": "index order is the wire order (waterbox/tables.py); mouse position is 0..65535 over the live picture",
                  "buttons": [b[0] for b in BUTTONS],
                  "axes": [{"name": a[0], "min": a[1], "max": a[2], "neutral": a[3]} for a in AXES]},
        "settings": SETTINGS,
        "firmware": fw,
    }
    with open(cfg_path, "w") as f: json.dump(cfg, f, indent=2); f.write("\n")
    keyb = {"_comment": ["Default bindings: the host mouse on the pointer and its buttons, the keyboard 1:1."],
            "AllTrollers": {"Syndicate Controller": {b[0]: b[3] for b in BUTTONS}},
            "AllTrollersAutoFire": {"Syndicate Controller": {}},
            "AllTrollersAnalog": {"Syndicate Controller": {a[0]: {"Value": a[4], "Mult": 1.0, "Deadzone": 0.0} for a in AXES}}}
    with open(os.path.join(HERE, "default_keybinds.json"), "w") as f: json.dump(keyb, f, indent=2); f.write("\n")
    with open(os.path.join(HERE, "sfx-tables.h"), "w") as h:
        h.write("/* generated by waterbox/tables.py - do not edit */\n#ifndef SFX_TABLES_H\n#define SFX_TABLES_H\n")
        h.write("typedef struct { const char *name; int mouse; int code; } SfxButton;\n")
        h.write("static const SfxButton sfx_buttons[] = {\n")
        for b in BUTTONS: h.write('    {"%s", %d, %d},\n' % (b[0], 1 if b[1] == "mouse" else 0, b[2]))
        h.write("};\n#define SFX_BUTTON_COUNT %d\n#define SFX_AXIS_COUNT %d\n" % (len(BUTTONS), len(AXES)))
        h.write("typedef struct { const char *name; unsigned size; const char *sha1; } SfxFirmware;\n")
        h.write("static const SfxFirmware sfx_firmware[] = {\n")
        for e in fw: h.write('    {"%s", %d, "%s"},\n' % (e["name"], e["size"], e["sha1"]))
        h.write("};\n#define SFX_FIRMWARE_COUNT %d\n" % len(fw))
        h.write("static const char *const sfx_languages[][2] = {%s};\n#endif\n" % ", ".join('{"%s", "%s"}' % (k, v) for k, v in LANG_ARG.items()))
    print("buttons %d, axes %d, firmware %d" % (len(BUTTONS), len(AXES), len(fw)))

if __name__ == "__main__":
    main()
