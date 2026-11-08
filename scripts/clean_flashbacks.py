"""Cut flashbacks out of an .f1tr recording made before the recorder did it itself.

Same rule as the hub (apps/dash-app/hub/hub.cpp): when the session time goes back more than 0.5 s, everything after
the new time is dropped and the recording clock carries on from there. The original file is kept; the result is
written next to it as <name>_clean.f1tr.

    python scripts/clean_flashbacks.py recording.f1tr [more.f1tr ...]
"""
import struct
import sys
from pathlib import Path


def clean(src: Path) -> Path:
    data = src.read_bytes()
    if data[:4] != b"F1TR":
        raise SystemExit(f"{src}: not an .f1tr recording")
    out = [data[:8]]
    cuts = []           # (session time, index into out, recording ns)
    last_st, uid, shift, flashbacks, dropped = -1.0, None, 0, 0, 0
    o = 8
    while o + 10 <= len(data):
        t, n = struct.unpack_from("<QH", data, o)
        rec = data[o + 10:o + 10 + n]
        o += 10 + n
        if len(rec) < n:
            break
        t -= shift
        if n >= 29:
            u = struct.unpack_from("<Q", rec, 7)[0]
            st = struct.unpack_from("<f", rec, 15)[0]
            if uid is None:
                uid = u
            if u == uid:
                if last_st >= 0 and st < last_st - 0.5:
                    while cuts and cuts[-1][0] > st:
                        cuts.pop()
                    if cuts:
                        _, idx, ns = cuts[-1]
                        dropped += len(out) - idx
                        del out[idx:]
                        shift += t - ns  # the clock carries on from the cut
                        t = ns
                        flashbacks += 1
                        print(f"  flashback at {st:8.1f}s session time: back from {last_st:.1f}s")
                elif not cuts or st >= cuts[-1][0] + 0.1:
                    cuts.append((st, len(out), t))
                last_st = st
        out.append(struct.pack("<QH", t, n) + rec)
    dst = src.with_name(src.stem + "_clean.f1tr")
    dst.write_bytes(b"".join(out))
    print(f"{src.name}: {flashbacks} flashbacks, {dropped} packets dropped -> {dst.name}")
    return dst


if __name__ == "__main__":
    for a in sys.argv[1:]:
        clean(Path(a))
