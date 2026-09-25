"""Esporta STEP / STL / DXF dalla scocca costruita (out/*.brep)."""
import json
import os
import sys

import cadquery as cq
from OCP.BRep import BRep_Builder
from OCP.BRepTools import BRepTools
from OCP.TopoDS import TopoDS_Shape

sys.path.insert(0, os.path.dirname(__file__))
import spec
import build_cq

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")


def load(name):
    s = TopoDS_Shape()
    BRepTools.Read_s(s, os.path.join(OUT, name + ".brep"), BRep_Builder())
    return cq.Shape.cast(s)


def key_tree(parts):
    """Albero di stampa: tutti i tasti uniti da canali sottili attaccati alle alette (nascoste)."""
    keys = [p for p in parts if p["name"].startswith("Tasto_") or p["name"].startswith("Laterale_")]
    rows = {}
    for p in keys:
        n = p["name"]
        k = next((k for k in spec.KEYS if "Tasto_" + k["id"] == n), None)
        if k is None:
            r = 4 if n == "Tasto_NAV" else 5
        else:
            r = {"row": k.get("row", 0), "bottom": 3, "nav": 4}[k["kind"]] if k["kind"] != "row" else k["row"]
        rows.setdefault(r, []).append(p)
    solids = []
    rows_placed = []
    y = 0.0
    zt = 13.1
    for r in sorted(rows):
        items = []
        for p in rows[r]:
            s = load(p["name"])
            if p["name"].startswith("Laterale_"):
                # sdraiato: faccia esterna verso +Z
                s = s.rotate((0, 0, 0), (0, 1, 0), -90)
            bb = s.BoundingBox()
            lift = p["name"].startswith("Laterale_") or p["name"] == "Tasto_NAV"
            items.append((bb.xmin, s, bb, lift))
        items.sort(key=lambda t: t[0])
        x = 0.0
        row_h = max(it[2].ylen for it in items)
        placed = []
        for _, s, bb, lift in items:
            s2 = s.translate((x - bb.xmin, y - bb.ymin, (zt - bb.zmin) if lift else 0))
            placed.append(s2)
            x += bb.xlen + 2.0
        rows_placed.append((y - 2.2, placed))
        solids += placed
        y += row_h + 4.5
    # barra sotto ogni fila, canali verso ciascun tasto, due dorsali laterali
    xmax = max(s.BoundingBox().xmax for s in solids)
    for bar_y, placed in rows_placed:
        solids.append(cq.Solid.makeBox(xmax + 2.6, 1.4, 1.4, cq.Vector(-1.0, bar_y - 0.7, zt - 0.2)))
        for s2 in placed:
            b = s2.BoundingBox()
            gx = (b.xmin + b.xmax) / 2
            solids.append(cq.Solid.makeBox(0.8, (b.ymin + 0.3) - bar_y, 0.45, cq.Vector(gx - 0.4, bar_y, zt)))
    y0 = rows_placed[0][0] - 0.7
    y1 = rows_placed[-1][0] + 0.7
    for xx in (-1.6, xmax + 0.2):
        solids.append(cq.Solid.makeBox(1.4, y1 - y0, 1.4, cq.Vector(xx, y0, zt - 0.2)))
    tree = solids[0]
    for s in solids[1:]:
        tree = tree.fuse(s)
    return tree.clean()


def dxf_outputs():
    import ezdxf
    # lente
    doc = ezdxf.new("R2010")
    msp = doc.modelspace()
    w = spec.P["lens"]
    r = spec.P["lens_r"]
    wp = cq.Workplane("XY").rect(w, w).extrude(1).edges("|Z").fillet(r).faces("<Z").wires().val()
    cq.exporters.exportDXF(cq.Workplane("XY").add(cq.Face.makeFromWires(wp)), os.path.join(OUT, "Lente_66x66_acrilico_1mm.dxf"))
    # contorno PCB 1 rev. 1 (coordinate KiCad: y verso il basso -> qui y invertita come in KiCad)
    f = spec.pcb1_features()
    Wd, Ld = f["size"]
    board = cq.Workplane("XY").center(Wd / 2, -Ld / 2).rect(Wd, Ld).extrude(1).edges("|Z").fillet(f["radius"])
    nx0, ny0, nx1, ny1 = f["notch"]
    board = board.cut(cq.Workplane("XY").center((nx0 + nx1) / 2 - 0.5, -(ny0 + ny1) / 2 + 0.5)
                      .rect(nx1 - nx0 + 1, ny1 - ny0 + 1).extrude(1))
    sx0, sy0, sx1, sy1 = f["slot"]
    board = board.cut(cq.Workplane("XY").center((sx0 + sx1) / 2, -(sy0 + sy1) / 2).rect(sx1 - sx0, sy1 - sy0)
                      .extrude(1).edges("|Z").fillet(0.79))
    for x, y, dd in f["holes"]:
        board = board.cut(cq.Workplane("XY").center(x, -y).circle(dd / 2).extrude(1))
    cq.exporters.exportDXF(cq.Workplane("XY").add(board.faces("<Z").val()), os.path.join(OUT, "PCB1_rev1_contorno.dxf"))


if __name__ == "__main__":
    parts = spec.parts()
    names = [p["name"] for p in parts]
    shapes = {n: load(n) for n in names}
    # assieme STEP con nomi e colori
    assy = cq.Assembly(name="Q-Mesh_scocca")
    for p in parts:
        c = p["color"]
        assy.add(shapes[p["name"]], name=p["name"], color=cq.Color(c[0], c[1], c[2], 1.0 - p.get("alpha", 0) * 0.5))
    assy.export(os.path.join(OUT, "QMesh_scocca_assieme.step"))
    # riferimento PCB
    ref = cq.Assembly(name="Q-Mesh_riferimento_PCB_rev1")
    for i, (m, s) in enumerate(build_cq.build_mocks()):
        c = m["color"]
        ref.add(s, name="%s_%d" % (m["name"], i), color=cq.Color(*c))
    ref.export(os.path.join(OUT, "QMesh_riferimento_PCB_rev1.step"))
    # singoli pezzi da stampare
    for n in ("Frontale", "Retro", "Supporto_tastiera"):
        cq.exporters.export(shapes[n], os.path.join(OUT, n + ".step"))
        cq.exporters.export(shapes[n], os.path.join(OUT, n + ".stl"), tolerance=0.02, angularTolerance=0.1)
    tree = key_tree(parts)
    cq.exporters.export(tree, os.path.join(OUT, "Tasti_albero.stl"), tolerance=0.01, angularTolerance=0.1)
    cq.exporters.export(tree, os.path.join(OUT, "Tasti_albero.step"))
    print("albero tasti: %.0f mm3, bbox %.1f x %.1f x %.1f" % (tree.Volume(), tree.BoundingBox().xlen,
                                                              tree.BoundingBox().ylen, tree.BoundingBox().zlen))
    print("solidi nell'albero:", len(tree.Solids()))
    dxf_outputs()
    for n in ("Frontale", "Retro", "Supporto_tastiera"):
        print(n, "solidi:", len(shapes[n].Solids()), "valido:", shapes[n].isValid())
    print("fatto")
