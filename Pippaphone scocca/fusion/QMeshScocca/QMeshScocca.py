# Q-Mesh: scocca apribile in stile BlackBerry Classic - script per Autodesk Fusion
#
# Uso: Utilita' > Script e componenti aggiuntivi > "+" accanto a I miei script > scegli questa
# cartella, poi Esegui. Crea un nuovo progetto con i componenti Frontale, Retro,
# Supporto_tastiera, Lente e Tasti, costruiti con schizzi ed estrusioni nella timeline.
#
# Le misure stanno in qmesh_spec.py (dizionario P, in mm): cambiale li' e rilancia lo script.
# Lo stesso file genera anche gli STEP/STL con CadQuery, quindi le due versioni coincidono.

import adsk.core
import adsk.fusion
import importlib
import math
import os
import sys
import traceback

# --------------------------------------------------------------------------- opzioni
OPZIONI = dict(
    NUOVO_DOCUMENTO=True,   # crea un nuovo progetto a ogni esecuzione
    TASTI=True,             # costruisce i 44 tasti (qualche minuto in piu')
    SCRITTE=True,           # lettere sui tasti e logo
    TEXTURE=False,          # goffratura del retro: ~150 scanalature, lenta; nello STEP c'e' gia'
    PARAMETRI_UTENTE=True,  # copia le misure principali nei parametri utente (solo come riferimento)
)

CM = 0.1  # l'API di Fusion lavora in centimetri

_log = []


def log(msg):
    _log.append(msg)


class Builder:
    def __init__(self, design, spec):
        self.design = design
        self.root = design.rootComponent
        self.spec = spec
        self.planes = {}
        self.nfeat = 0

    # ------------------------------------------------------------------ piani e schizzi
    def plane(self, comp, axis, pos):
        key = (comp.entityToken if hasattr(comp, "entityToken") else comp.name, axis, round(pos, 4))
        if key in self.planes:
            return self.planes[key]
        base = {"z": comp.xYConstructionPlane, "y": comp.xZConstructionPlane, "x": comp.yZConstructionPlane}[axis]
        if abs(pos) < 1e-9:
            self.planes[key] = base
            return base
        planes = comp.constructionPlanes
        pl = None
        for sign in (1, -1):
            inp = planes.createInput()
            inp.setByOffset(base, adsk.core.ValueInput.createByReal(sign * pos * CM))
            pl = planes.add(inp)
            o = pl.geometry.origin
            coord = {"z": o.z, "y": o.y, "x": o.x}[axis] / CM
            if abs(coord - pos) < 1e-3:
                break
            pl.deleteMe()
            pl = None
        if pl is None:
            raise RuntimeError("piano non creato: %s %.3f" % (axis, pos))
        pl.isLightBulbOn = False
        self.planes[key] = pl
        return pl

    def new_sketch(self, comp, axis, pos):
        sk = comp.sketches.add(self.plane(comp, axis, pos))
        sk.isComputeDeferred = True

        def P3(u, v):
            if axis == "z":
                m = adsk.core.Point3D.create(u * CM, v * CM, pos * CM)
            elif axis == "y":
                m = adsk.core.Point3D.create(u * CM, pos * CM, v * CM)
            else:
                m = adsk.core.Point3D.create(pos * CM, u * CM, v * CM)
            return sk.modelToSketchSpace(m)

        return sk, P3

    @staticmethod
    def _rrect(sk, P3, u, v, w, h, r):
        if abs(w - h) < 1e-6 and r >= w / 2 - 0.02:
            sk.sketchCurves.sketchCircles.addByCenterRadius(P3(u, v), w / 2 * CM)
            return
        r = max(0.0, min(r, w / 2 - 0.02, h / 2 - 0.02))
        a, b = P3(u - w / 2, v - h / 2), P3(u + w / 2, v + h / 2)
        x0, x1 = sorted((a.x, b.x))
        y0, y1 = sorted((a.y, b.y))
        Pt = adsk.core.Point3D.create
        lines = sk.sketchCurves.sketchLines
        if r * CM < 1e-5:
            lines.addTwoPointRectangle(Pt(x0, y0, 0), Pt(x1, y1, 0))
            return
        rc = r * CM
        arcs = sk.sketchCurves.sketchArcs
        a1 = arcs.addByCenterStartSweep(Pt(x1 - rc, y1 - rc, 0), Pt(x1, y1 - rc, 0), math.pi / 2)
        a2 = arcs.addByCenterStartSweep(Pt(x0 + rc, y1 - rc, 0), Pt(x0 + rc, y1, 0), math.pi / 2)
        a3 = arcs.addByCenterStartSweep(Pt(x0 + rc, y0 + rc, 0), Pt(x0, y0 + rc, 0), math.pi / 2)
        a4 = arcs.addByCenterStartSweep(Pt(x1 - rc, y0 + rc, 0), Pt(x1 - rc, y0, 0), math.pi / 2)
        for s, e in ((a1, a2), (a2, a3), (a3, a4), (a4, a1)):
            lines.addByTwoPoints(s.endSketchPoint, e.startSketchPoint)

    def draw(self, sk, P3, prim):
        k = prim["kind"]
        if k == "circle":
            sk.sketchCurves.sketchCircles.addByCenterRadius(P3(prim["u"], prim["v"]), prim["d"] / 2 * CM)
        elif k == "rrect":
            self._rrect(sk, P3, prim["u"], prim["v"], prim["w"], prim["h"], prim["r"])
        elif k == "ring":
            self._rrect(sk, P3, prim["u"], prim["v"], prim["w"], prim["h"], prim["r"])
            self._rrect(sk, P3, prim["u"], prim["v"], prim["wi"], prim["hi"], prim["ri"])
        elif k == "poly":
            pts = [P3(u, v) for u, v in prim["pts"]]
            lines = sk.sketchCurves.sketchLines
            first = last_end = None
            for p in pts[1:]:
                ln = lines.addByTwoPoints(last_end if last_end is not None else pts[0], p)
                if first is None:
                    first = ln
                last_end = ln.endSketchPoint
            lines.addByTwoPoints(last_end, first.startSketchPoint)
        else:
            raise ValueError(k)

    @staticmethod
    def profiles(sk, prim):
        sk.isComputeDeferred = False
        coll = adsk.core.ObjectCollection.create()
        if prim is not None and prim["kind"] == "ring":
            for i in range(sk.profiles.count):
                pr = sk.profiles.item(i)
                if pr.profileLoops.count == 2:
                    coll.add(pr)
        else:
            for i in range(sk.profiles.count):
                coll.add(sk.profiles.item(i))
        return coll

    # ------------------------------------------------------------------ operazioni
    def extrude(self, comp, prims, op, body=None):
        """Estrude uno o piu' prismi che condividono asse e intervallo (stesso schizzo)."""
        p0 = prims[0]
        mid = (p0["a0"] + p0["a1"]) / 2
        sk, P3 = self.new_sketch(comp, p0["axis"], mid)
        for q in prims:
            self.draw(sk, P3, q)
        coll = self.profiles(sk, p0 if len(prims) == 1 else None)
        if coll.count == 0:
            raise RuntimeError("nessun profilo")
        opm = {"new": adsk.fusion.FeatureOperations.NewBodyFeatureOperation,
               "join": adsk.fusion.FeatureOperations.JoinFeatureOperation,
               "cut": adsk.fusion.FeatureOperations.CutFeatureOperation}[op]
        ext = comp.features.extrudeFeatures
        inp = ext.createInput(coll if coll.count > 1 else coll.item(0), opm)
        inp.setSymmetricExtent(adsk.core.ValueInput.createByReal((p0["a1"] - p0["a0"]) * CM), True)
        if body is not None and op != "new":
            inp.participantBodies = [body]
        f = ext.add(inp)
        sk.isVisible = False
        self.nfeat += 1
        return f

    def fillet_face_edges(self, comp, body, top):
        faces = []
        for i in range(body.faces.count):
            fc = body.faces.item(i)
            g = fc.geometry
            if fc.geometry.surfaceType == adsk.core.SurfaceTypes.PlaneSurfaceType and abs(g.normal.z) > 0.999:
                faces.append(fc)
        fc = max(faces, key=lambda f: f.centroid.z) if top else min(faces, key=lambda f: f.centroid.z)
        return fc

    def fillet(self, comp, body, top, r):
        fc = self.fillet_face_edges(comp, body, top)
        edges = adsk.core.ObjectCollection.create()
        for i in range(fc.edges.count):
            edges.add(fc.edges.item(i))
        fl = comp.features.filletFeatures
        fi = fl.createInput()
        val = adsk.core.ValueInput.createByReal(r * CM)
        try:
            fi.edgeSetInputs.addConstantRadiusEdgeSet(edges, val, True)
        except Exception:
            fi.addConstantRadiusEdgeSet(edges, val, True)
        fl.add(fi)
        self.nfeat += 1

    def envelope(self, comp, e, name):
        s = self.spec
        f = self.extrude(comp, [s.rr("z", 0, 0, e["w"], e["h"], e["r"], 0, e["t"])], "new")
        body = f.bodies.item(0)
        body.name = name
        self.fillet(comp, body, True, e["f_top"])
        self.fillet(comp, body, False, e["f_bot"])
        if e["zmin"] > 1e-6:
            self.extrude(comp, [s.rr("z", 0, 0, e["w"] + 4, e["h"] + 4, 0, -1, e["zmin"])], "cut", body)
        if e["zmax"] < e["t"] - 1e-6:
            self.extrude(comp, [s.rr("z", 0, 0, e["w"] + 4, e["h"] + 4, 0, e["zmax"], e["t"] + 1)], "cut", body)
        return body

    def text(self, comp, body, t):
        if t["face"] == "front":
            z0, z1 = t.get("cut_to", t["z"] - t["depth"]), t["z"] + 1
        else:
            z0, z1 = -1, t["depth"]
        mid = (z0 + z1) / 2
        sk, P3 = self.new_sketch(comp, "z", mid)
        sk.isComputeDeferred = False
        w = max(len(t["txt"]), 2) * t["size"] * 1.1
        c1, c2 = P3(t["u"] - w / 2, t["v"] - t["size"]), P3(t["u"] + w / 2, t["v"] + t["size"])
        ti = sk.sketchTexts.createInput2(t["txt"], t["size"] * CM)
        ti.setAsMultiLine(c1, c2, adsk.core.HorizontalAlignments.CenterHorizontalAlignment,
                          adsk.core.VerticalAlignments.MiddleVerticalAlignment, 0)
        ti.fontName = "Arial"
        try:
            ti.textStyle = adsk.fusion.TextStyles.TextStyleBold
        except Exception:
            pass
        if t["face"] == "back":
            try:
                ti.isHorizontalFlip = True
            except Exception:
                pass
        st = sk.sketchTexts.add(ti)
        ext = comp.features.extrudeFeatures
        inp = ext.createInput(st, adsk.fusion.FeatureOperations.CutFeatureOperation)
        inp.setSymmetricExtent(adsk.core.ValueInput.createByReal((z1 - z0) * CM), True)
        inp.participantBodies = [body]
        ext.add(inp)
        sk.isVisible = False
        self.nfeat += 1

    def build_part(self, comp, part):
        body = None
        name = part["name"]
        for op, q in part["ops"]:
            try:
                if op == "env":
                    body = self.envelope(comp, q, name)
                elif op == "add":
                    if body is None:
                        body = self.extrude(comp, [q], "new").bodies.item(0)
                        body.name = name
                    else:
                        self.extrude(comp, [q], "join", body)
                elif op == "cut":
                    self.extrude(comp, [q], "cut", body)
                elif op == "text":
                    if OPZIONI["SCRITTE"]:
                        self.text(comp, body, q)
                elif op == "texture":
                    if OPZIONI["TEXTURE"]:
                        prims = self.spec.texture_prims(q)
                        self.extrude(comp, prims, "cut", body)
            except Exception:
                log("%s: operazione %s non riuscita\n%s" % (name, op, traceback.format_exc().splitlines()[-1]))
            adsk.doEvents()
        return body


def new_component(root, name):
    occ = root.occurrences.addNewComponent(adsk.core.Matrix3D.create())
    occ.component.name = name
    return occ.component


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        here = os.path.dirname(os.path.realpath(__file__))
        if here not in sys.path:
            sys.path.insert(0, here)
        import qmesh_spec as spec
        importlib.reload(spec)
        del _log[:]

        if OPZIONI["NUOVO_DOCUMENTO"]:
            app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.ParametricDesignType
        root = design.rootComponent
        try:
            design.fusionUnitsManager.distanceDisplayUnits = adsk.fusion.DistanceUnits.MillimeterDistanceUnits
        except Exception:
            pass

        if OPZIONI["PARAMETRI_UTENTE"]:
            for k in ("wall", "plate", "plate_disp", "lens", "lens_t", "gap_pcb", "batt_t", "back_t",
                      "corner_r", "fillet_front", "fillet_back", "key_clear", "flange"):
                try:
                    design.userParameters.add("qm_" + k, adsk.core.ValueInput.createByString("%g mm" % spec.P[k]),
                                              "mm", "Q-Mesh (riferimento: le misure si cambiano in qmesh_spec.py)")
                except Exception:
                    pass

        parts = spec.parts()
        b = Builder(design, spec)
        prog = ui.createProgressDialog()
        prog.isCancelButtonShown = True
        todo = [p for p in parts if OPZIONI["TASTI"] or p.get("group") != "tasti"]
        prog.show("Q-Mesh", "Costruzione %v di %m ...", 0, len(todo))
        comps = {}
        for i, p in enumerate(todo):
            if prog.wasCancelled:
                break
            cname = "Tasti" if p.get("group") == "tasti" else p["name"]
            if cname not in comps:
                comps[cname] = new_component(root, cname)
            b.build_part(comps[cname], p)
            prog.progressValue = i + 1
            adsk.doEvents()
        prog.hide()
        msg = "Q-Mesh: %d parti, %d operazioni." % (len(todo), b.nfeat)
        if _log:
            msg += "\n\nOperazioni non riuscite (%d):\n" % len(_log) + "\n".join(_log[:15])
        ui.messageBox(msg)
    except Exception:
        if ui:
            ui.messageBox("Errore:\n{}".format(traceback.format_exc()))
