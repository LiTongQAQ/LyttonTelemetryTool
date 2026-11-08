# Collects every CJK character in the program's string literals into apps/dash-app/race/ui_chars.inc, which main.cpp
# bakes into the fonts that otherwise load only the common character set (a rare one would be drawn as "?").
# Run after adding UI text with new characters:  python scripts/gen_ui_chars.py
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ['apps/dash-app/race', 'apps/dash-app/tracks', 'apps/dash-app/timing', 'apps/dash-app/hub']

chars = set()
for d in SOURCES:
    for f in sorted((ROOT / d).glob('*.cpp')):
        for lit in re.findall(r'"((?:[^"\\\n]|\\.)*)"', f.read_text(encoding='utf-8')):
            chars.update(ch for ch in lit if 0x3000 <= ord(ch) <= 0x9fff or 0xff00 <= ord(ch) <= 0xffef)
out = ROOT / 'apps/dash-app/race/ui_chars.inc'
out.write_text('// Every CJK character in the program\'s own strings (scripts/gen_ui_chars.py), baked into the fonts that load\n'
               '// only the common set, so a rare one (沥, 胎, 刹 ...) is not drawn as "?".\n'
               'R"(' + ''.join(sorted(chars)) + ')"\n', encoding='utf-8', newline='\n')
print(len(chars), 'characters ->', out)
