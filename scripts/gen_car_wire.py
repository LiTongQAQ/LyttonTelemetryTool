"""Builds apps/dash-app/race/carwire.inc from the Track N Race car wireframe
(third_party/track-n-race/car/f1-car-wireframe.svg, GPL-3.0, by NoGoat).

The drawing's inner geometry is authored nose up (x 0..350, y -18..914, 2.0 m wide); that frame is kept (the #car
group's rotation to nose-right is left out). Paths (M L H V Q C Z, absolute), rects with rounded corners and
ellipses become polylines; nested group transforms are applied. Each shape keeps its part (damage zone, tyre or
body), its role (solid: filled with the panel background so hidden lines drop out; wire / detail / tube: lines)
and whether it is closed.

    python scripts/gen_car_wire.py
"""
import math
import os
import re
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'third_party', 'track-n-race', 'car', 'f1-car-wireframe.svg')
OUT = os.path.join(ROOT, 'apps', 'dash-app', 'race', 'carwire.inc')
NS = '{http://www.w3.org/2000/svg}'
PARTS = {'body': 'BODY', 'cockpit': 'COCKPIT', 'suspension': 'SUSPENSION', 'z-fw-l': 'FW_L', 'z-fw-r': 'FW_R',
         'z-floor': 'FLOOR', 'z-diff': 'DIFF', 'z-sp-l': 'SP_L', 'z-sp-r': 'SP_R', 'z-rw': 'RW',
         't-fl': 'T_FL', 't-fr': 'T_FR', 't-rl': 'T_RL', 't-rr': 'T_RR'}
ROLES = {'solid': 'SOLID', 'wire': 'WIRE', 'detail': 'DETAIL', 'tube': 'TUBE'}


def parse_transform(s):
    m = [1, 0, 0, 1, 0, 0]
    for name, args in re.findall(r'(\w+)\(([^)]*)\)', s or ''):
        v = [float(x) for x in re.split(r'[ ,]+', args.strip()) if x]
        if name == 'matrix':
            t = v
        elif name == 'translate':
            t = [1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0]
        elif name == 'scale':
            t = [v[0], 0, 0, v[1] if len(v) > 1 else v[0], 0, 0]
        else:
            raise SystemExit('unsupported transform ' + name)
        m = mul(m, t)
    return m


def mul(a, b):
    return [a[0] * b[0] + a[2] * b[1], a[1] * b[0] + a[3] * b[1], a[0] * b[2] + a[2] * b[3], a[1] * b[2] + a[3] * b[3],
            a[0] * b[4] + a[2] * b[5] + a[4], a[1] * b[4] + a[3] * b[5] + a[5]]


def apply(m, p):
    return (m[0] * p[0] + m[2] * p[1] + m[4], m[1] * p[0] + m[3] * p[1] + m[5])


def path_polys(d):
    """Polylines (points, closed) of a path's d attribute."""
    toks = re.findall(r'[MLHVQCZmlhvqcz]|-?\d*\.?\d+(?:e-?\d+)?', d)
    polys, cur, i, cmd, pos, start = [], [], 0, None, (0.0, 0.0), (0.0, 0.0)

    def num():
        nonlocal i
        v = float(toks[i])
        i += 1
        return v
    while i < len(toks):
        if re.match(r'[A-Za-z]', toks[i]):
            cmd = toks[i]
            i += 1
            if cmd in 'Zz':
                if cur:
                    polys.append((cur, True))
                cur, pos = [], start
                continue
        if cmd != cmd.upper():
            raise SystemExit('relative path commands are not supported')
        if cmd == 'M':
            if cur:
                polys.append((cur, False))
            pos = (num(), num())
            start = pos
            cur = [pos]
            cmd = 'L'
        elif cmd == 'L':
            pos = (num(), num())
            cur.append(pos)
        elif cmd == 'H':
            pos = (num(), pos[1])
            cur.append(pos)
        elif cmd == 'V':
            pos = (pos[0], num())
            cur.append(pos)
        elif cmd == 'Q':
            c, e = (num(), num()), (num(), num())
            for k in range(1, 9):
                t = k / 8
                cur.append(((1 - t) ** 2 * pos[0] + 2 * (1 - t) * t * c[0] + t * t * e[0],
                            (1 - t) ** 2 * pos[1] + 2 * (1 - t) * t * c[1] + t * t * e[1]))
            pos = e
        elif cmd == 'C':
            c1, c2, e = (num(), num()), (num(), num()), (num(), num())
            for k in range(1, 11):
                t = k / 10
                a, b, c, dd = (1 - t) ** 3, 3 * (1 - t) ** 2 * t, 3 * (1 - t) * t * t, t ** 3
                cur.append((a * pos[0] + b * c1[0] + c * c2[0] + dd * e[0], a * pos[1] + b * c1[1] + c * c2[1] + dd * e[1]))
            pos = e
        else:
            raise SystemExit('unsupported path command ' + cmd)
    if cur:
        polys.append((cur, False))
    return polys


def rect_poly(x, y, w, h, rx):
    rx = min(rx, w / 2, h / 2)
    if rx <= 0:
        return [(x, y), (x + w, y), (x + w, y + h), (x, y + h)]
    pts = []
    for cx, cy, a0 in ((x + w - rx, y + rx, -90), (x + w - rx, y + h - rx, 0), (x + rx, y + h - rx, 90), (x + rx, y + rx, 180)):
        for k in range(7):
            a = math.radians(a0 + 15 * k)
            pts.append((cx + rx * math.cos(a), cy + rx * math.sin(a)))
    return pts


def main():
    root = ET.parse(SRC).getroot()
    car = next(g for g in root.iter(NS + 'g') if g.get('id') == 'car')
    shapes = []

    def walk(el, m, part, role):
        m = mul(m, parse_transform(el.get('transform')))
        part = PARTS.get(el.get('id'), part)
        role = ROLES.get(el.get('class'), role)
        tag = el.tag.replace(NS, '')
        polys = []
        if tag == 'path':
            polys = path_polys(el.get('d'))
        elif tag == 'rect':
            polys = [(rect_poly(float(el.get('x')), float(el.get('y')), float(el.get('width')), float(el.get('height')),
                                float(el.get('rx', 0))), True)]
        elif tag == 'ellipse':
            cx, cy, rx, ry = (float(el.get(k)) for k in ('cx', 'cy', 'rx', 'ry'))
            polys = [([(cx + rx * math.cos(2 * math.pi * k / 24), cy + ry * math.sin(2 * math.pi * k / 24)) for k in range(24)], True)]
        for pts, closed in polys:
            shapes.append((part, role, closed, [apply(m, p) for p in pts]))
        for ch in el:
            walk(ch, m, part, role)

    for ch in car:  # the inner, nose-up frame: the #car group's own transform is left out
        walk(ch, [1, 0, 0, 1, 0, 0], 'BODY', 'WIRE')
    xs = [p[0] for s in shapes for p in s[3]]
    ys = [p[1] for s in shapes for p in s[3]]
    x0, y0 = min(xs), min(ys)
    pts, rows = [], []
    for part, role, closed, poly in shapes:
        rows.append('    {%s, %s, %s, %d, %d},' % (part, role, 'true' if closed else 'false', len(pts) // 2, len(poly)))
        for x, y in poly:
            pts += ['%.1f' % (x - x0), '%.1f' % (y - y0)]
    out = ['// generated by scripts/gen_car_wire.py from third_party/track-n-race/car/f1-car-wireframe.svg',
           '// (Track N Race by NoGoat, GPL-3.0): top view, nose up, %.0f x %.0f units = 2.0 m wide' % (max(xs) - x0, max(ys) - y0),
           'constexpr float WIRE_W = %.1ff, WIRE_H = %.1ff;' % (max(xs) - x0, max(ys) - y0),
           'static const float WIRE_PTS[] = {' + ','.join(pts) + '};',
           'static const WireShape WIRE_SHAPES[] = {'] + rows + ['};']
    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print(len(shapes), 'shapes,', len(pts) // 2, 'points ->', OUT)


if __name__ == '__main__':
    main()
