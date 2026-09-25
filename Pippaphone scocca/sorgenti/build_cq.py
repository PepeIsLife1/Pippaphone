"""Costruisce la scocca Q-Mesh da spec.py con CadQuery: STEP, STL, DXF e verifiche di ingombro."""
import json
import math
import os
import sys
import time

import cadquery as cq

sys.path.insert(0, os.path.dirname(__file__))
import spec

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
FONT = "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"



def _rrect_solid(u, v, w, h, r, z0, L):
    if abs(w - h) < 1e-6 and r >= w / 2 - 0.02:
        return cq.Workplane("XY").workplane(offset=z0).center(u, v).circle(w / 2).extrude(L).val()
    r = min(r, w / 2 - 0.02, h / 2 - 0.02)
    s = cq.Workplane("XY").workplane(offset=z0).center(u, v).rect(w, h).extrude(L)
    if r > 0.01:
        s = s.edges("|Z").fillet(r)
    return s.val()


def prism(p):
    a0, a1 = p["a0"], p["a1"]
    if p["axis"] == "y":
        w0, w1 = -a1, -a0
    else:
        w0, w1 = a0, a1
    L = w1 - w0
    k = p["kind"]
    if k == "rrect":
        s = _rrect_solid(p["u"], p["v"], p["w"], p["h"], p["r"], w0, L)
    elif k == "circle":
        s = cq.Workplane("XY").workplane(offset=w0).center(p["u"], p["v"]).circle(p["d"] / 2).extrude(L).val()
    elif k == "poly":
        s = cq.Workplane("XY").workplane(offset=w0).polyline(p["pts"]).close().extrude(L).val()
    elif k == "ring":
        o = _rrect_solid(p["u"], p["v"], p["w"], p["h"], p["r"], w0, L)
        i = _rrect_solid(p["u"], p["v"], p["wi"], p["hi"], p["ri"], w0 - 0.01, L + 0.02)
        s = o.cut(i)
    else:
        raise ValueError(k)
    if p["axis"] == "y":      # (u, v, w) -> (X=u, Y=-w, Z=v)
        s = s.rotate((0, 0, 0), (1, 0, 0), 90)
    elif p["axis"] == "x":    # (u, v, w) -> (X=w, Y=u, Z=v)
        s = s.rotate((0, 0, 0), (1, 0, 0), 90).rotate((0, 0, 0), (0, 0, 1), 90)
    return s


def envelope(e):
    s = cq.Workplane("XY").rect(e["w"], e["h"]).extrude(e["t"]).edges("|Z").fillet(e["r"])
    s = s.faces(">Z").edges().fillet(e["f_top"]).faces("<Z").edges().fillet(e["f_bot"])
    box = cq.Workplane("XY").workplane(offset=e["zmin"]).rect(e["w"] + 2, e["h"] + 2).extrude(e["zmax"] - e["zmin"])
    return s.intersect(box).val()


def text_solid(t):
    size, depth = t["size"], t["depth"]
    if t["face"] == "front":
        z0 = t.get("cut_to", t["z"] - depth)
        wp = cq.Workplane("XY").workplane(offset=z0).center(t["u"], t["v"])
        return wp.text(t["txt"], size, (t["z"] + 1) - z0, fontPath=FONT, halign="center", valign="center").val()
    pl = cq.Plane(origin=(t["u"], t["v"], t["z"] + depth), xDir=(-1, 0, 0), normal=(0, 0, -1))
    return cq.Workplane(pl).text(t["txt"], size, depth + 1, fontPath=FONT, halign="center", valign="center").val()


def texture_tools(t):
    """Scanalature a rombo: due gruppi di barre parallele, ciascuno ritagliato sul pannello."""
    x0, y0, x1, y1 = t["x0"], t["y0"], t["x1"], t["y1"]
    cx, cy_ = (x0 + x1) / 2, (y0 + y1) / 2
    diag = math.hypot(x1 - x0, y1 - y0) + 4
    n = int(diag / t["pitch"]) + 2
    region = _rrect_solid(cx, cy_, x1 - x0, y1 - y0, t["r"], -1.5, 3)
    bx, by, bw, bh, br = t["badge"]
    region = region.cut(_rrect_solid(bx, by, bw, bh, br, -2, 4))
    groups = []
    for ang in (45, -45):
        bars = []
        ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        for i in range(n):
            off = (i - (n - 1) / 2) * t["pitch"]
            # barra lunga diag, larga w, centrata a distanza off dalla diagonale
            px, py = cx + off * ca, cy_ + off * sa
            bar = (cq.Workplane("XY").workplane(offset=-1).center(px, py)
                   .rect(t["w"], diag).extrude(1 + t["depth"]).val()
                   .rotate(cq.Vector(px, py, 0), cq.Vector(px, py, 1), ang))
            cl = bar.intersect(region)
            if cl.Volume() > 1e-6:
                bars.append(cl)
        groups.append(bars)
    return groups


def build_part(part, texture=True, legends=True):
    cur = None
    for op, q in part["ops"]:
        if op == "env":
            s = envelope(q)
            cur = s if cur is None else cur.fuse(s)
        elif op == "add":
            s = prism(q)
            cur = s if cur is None else cur.fuse(s)
        elif op == "cut":
            cur = cur.cut(prism(q))
        elif op == "text":
            if legends:
                try:
                    cur = cur.cut(text_solid(q))
                except Exception as ex:
                    print("  testo non inciso:", q["txt"], ex)
        elif op == "texture":
            if texture:
                for bars in texture_tools(q):
                    cur = cur.cut(*bars)
    return cur.clean()


def build_all(texture=True):
    parts = spec.parts()
    built = {}
    for p in parts:
        t0 = time.time()
        built[p["name"]] = (p, build_part(p, texture=texture))
        if not p.get("group"):
            print("  %-18s %.1fs  vol %.0f mm3" % (p["name"], time.time() - t0, built[p["name"]][1].Volume()))
    return built


def build_mocks():
    ms = spec.mocks(os.path.join(HERE, "qmesh-main-fps.json"), os.path.join(HERE, "qmesh-kbd-fps.json"))
    out = []
    for m in ms:
        s = prism(m["prim"])
        if m["name"] == "PCB1":
            nx0, ny0, nx1, ny1 = spec.PCB1_NOTCH
            s = s.cut(prism(spec.box_p(nx0 - 1, ny0 - 1, nx1, ny1, spec.D["fz_pcb1f"] - 1, spec.D["fz_pcb1b"] + 1, r=0)))
            fx0, fy0, fx1, fy1 = spec.FPC_SLOT
            s = s.cut(prism(spec.box_p(fx0, fy0, fx1, fy1, spec.D["fz_pcb1f"] - 1, spec.D["fz_pcb1b"] + 1, r=0.8)))
            for x, y in spec.SCREWS:
                s = s.cut(prism(spec.cyl_p(x, y, 2.2, spec.D["fz_pcb1f"] - 1, spec.D["fz_pcb1b"] + 1)))
        if m["name"] == "PCB2":
            for x, y in spec.KBD_HOLES:
                s = s.cut(prism(spec.cyl_p(spec.kx(x), spec.ky(y), 2.2, spec.D["fz_pcb2f"] - 1, spec.D["fz_pcb2b"] + 1)))
        out.append((m, s))
    return out


def bb_overlap(a, b, tol=0.0):
    A, B = a.BoundingBox(), b.BoundingBox()
    return not (A.xmax < B.xmin - tol or B.xmax < A.xmin - tol or A.ymax < B.ymin - tol or B.ymax < A.ymin - tol
                or A.zmax < B.zmin - tol or B.zmax < A.zmin - tol)


def interferences(items, others=None, lim=1e-3):
    res = []
    if others is None:
        pairs = [(items[i], items[j]) for i in range(len(items)) for j in range(i + 1, len(items))]
    else:
        pairs = [(a, b) for a in items for b in others]
    for (na, sa), (nb, sb) in pairs:
        if not bb_overlap(sa, sb):
            continue
        try:
            v = sa.intersect(sb).Volume()
        except Exception:
            v = -1
        if v > lim or v < 0:
            res.append((na, nb, v))
    return res


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    t0 = time.time()
    print("costruzione parti...")
    # le verifiche usano il retro senza goffratura (e' solo sull'esterno); poi si salva quello goffrato
    built = build_all(texture=False)
    print("parti:", len(built), "in %.0fs" % (time.time() - t0))
    from OCP.BRepTools import BRepTools
    for name, (p, s) in built.items():
        BRepTools.Write_s(s.wrapped, os.path.join(OUT, name + ".brep"))
    if "--no-texture" not in sys.argv:
        t1 = time.time()
        rt = build_part(spec.back_cover(), texture=True)
        BRepTools.Write_s(rt.wrapped, os.path.join(OUT, "Retro.brep"))
        print("  Retro goffrato %.0fs, vol %.0f mm3" % (time.time() - t1, rt.Volume()))
    print("mock...")
    mocks = build_mocks()
    json.dump([p["name"] for p in spec.parts()], open(os.path.join(OUT, "parts.json"), "w"))
    enc = [(n, s) for n, (p, s) in built.items()]
    mk = [(m["name"], s) for m, s in mocks]
    print("verifica scocca contro PCB e componenti...")
    r1 = interferences(enc, mk)
    for a, b, v in r1:
        print("  COLLISIONE %-22s %-18s %.3f mm3" % (a, b, v))
    print("verifica tra le parti della scocca...")
    r2 = interferences(enc)
    for a, b, v in r2:
        print("  COLLISIONE %-22s %-18s %.3f mm3" % (a, b, v))
    print("collisioni scocca/PCB:", len(r1), " tra parti:", len(r2), " tempo %.0fs" % (time.time() - t0))
