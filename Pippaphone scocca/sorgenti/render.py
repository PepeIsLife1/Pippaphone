"""Render PNG della scocca (VTK offscreen): fronte, retro, esploso, sezione."""
import os
import sys

import cadquery as cq
import vtk
from vtk.util.numpy_support import numpy_to_vtk, numpy_to_vtkIdTypeArray
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import spec
import build_cq
from export import load

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
D = spec.D


def polydata(shape, tol=0.03):
    vs, ts = shape.tessellate(tol, 0.2)
    pts = np.array([(v.x, v.y, v.z) for v in vs], dtype=np.float64)
    tri = np.array(ts, dtype=np.int64)
    cells = np.hstack([np.full((len(tri), 1), 3, dtype=np.int64), tri]).ravel()
    pd = vtk.vtkPolyData()
    p = vtk.vtkPoints()
    p.SetData(numpy_to_vtk(pts, deep=True))
    pd.SetPoints(p)
    ca = vtk.vtkCellArray()
    ca.SetCells(len(tri), numpy_to_vtkIdTypeArray(cells, deep=True))
    pd.SetPolys(ca)
    n = vtk.vtkPolyDataNormals()
    n.SetInputData(pd)
    n.SetFeatureAngle(35)
    n.SplittingOn()
    n.ConsistencyOn()
    n.Update()
    return n.GetOutput()


def actor(shape, color, opacity=1.0, spec_=0.25, power=18, offset=(0, 0, 0), metal=False):
    m = vtk.vtkPolyDataMapper()
    m.SetInputData(polydata(shape))
    a = vtk.vtkActor()
    a.SetMapper(m)
    pr = a.GetProperty()
    pr.SetColor(*color)
    pr.SetOpacity(opacity)
    pr.SetSpecular(0.6 if metal else spec_)
    pr.SetSpecularPower(40 if metal else power)
    pr.SetDiffuse(0.85)
    pr.SetAmbient(0.12)
    a.SetPosition(*offset)
    return a


def scene(actors, cam, path, size=(1600, 1200), parallel=False, bg=((0.93, 0.94, 0.95), (0.80, 0.82, 0.85))):
    r = vtk.vtkRenderer()
    r.GradientBackgroundOn()
    r.SetBackground(*bg[1])
    r.SetBackground2(*bg[0])
    for a in actors:
        r.AddActor(a)
    kit = vtk.vtkLightKit()
    kit.SetKeyLightIntensity(0.95)
    kit.AddLightsToRenderer(r)
    r.SetUseDepthPeeling(1)
    r.SetMaximumNumberOfPeels(8)
    w = vtk.vtkRenderWindow()
    w.SetOffScreenRendering(1)
    w.SetMultiSamples(0)
    w.SetAlphaBitPlanes(1)
    w.AddRenderer(r)
    w.SetSize(*size)
    c = r.GetActiveCamera()
    c.SetFocalPoint(*cam["focal"])
    c.SetPosition(*cam["pos"])
    c.SetViewUp(*cam["up"])
    c.SetViewAngle(cam.get("angle", 30))
    if parallel:
        c.ParallelProjectionOn()
    if cam.get("fit", True):
        r.ResetCamera()
    c.Zoom(cam.get("zoom", 1.0))
    if parallel and "scale" in cam:
        c.SetParallelScale(cam["scale"])
    r.ResetCameraClippingRange()
    r.UseFXAAOn()
    w.Render()
    f = vtk.vtkWindowToImageFilter()
    f.SetInput(w)
    f.SetScale(1)
    f.Update()
    wr = vtk.vtkPNGWriter()
    wr.SetFileName(path)
    wr.SetInputConnection(f.GetOutputPort())
    wr.Write()
    print("render", path)


COL = {
    "Frontale": (0.14, 0.15, 0.17), "Retro": (0.17, 0.18, 0.20), "Supporto_tastiera": (0.42, 0.44, 0.48),
    "Lente": (0.62, 0.76, 0.86),
}


def part_color(n):
    if n in COL:
        return COL[n]
    if n == "Tasto_NAV" or n.startswith("Laterale_"):
        return (0.78, 0.12, 0.16) if n.endswith("PTT") else (0.62, 0.64, 0.67)
    return (0.07, 0.07, 0.08)


def screen_plane():
    cyd = spec.P["disp_top"] + spec.P["disp_h"] / 2
    a = spec.P["disp_act"]
    s = cq.Workplane("XY").workplane(offset=spec.Z(spec.D["fz_disp_f"]) - 0.05).center(spec.X(35), spec.Y(cyd)) \
        .rect(a, a).extrude(0.04).val()
    return s


def antenna():
    x, z = spec.X(spec.P["sma_x"]), spec.Z(spec.P["sma_fz"])
    y0 = spec.Y(-1.85)
    nut = cq.Workplane("XZ").workplane(offset=-y0).center(x, z).polygon(6, 9.2).extrude(-2.0).val()
    body = cq.Workplane("XZ").workplane(offset=-(y0 + 2.0)).center(x, z).circle(4.6).extrude(-42).val()
    tip = cq.Solid.makeSphere(4.6, cq.Vector(x, y0 + 44, z))
    return nut, body.fuse(tip)


ONLY = set(sys.argv[1:])


def want(name):
    return not ONLY or name in ONLY


def main():
    parts = spec.parts()
    shapes = {p["name"]: load(p["name"]) for p in parts}
    mocks = build_cq.build_mocks()
    nut, ant = antenna()
    scr = screen_plane()

    def enc_actors(offsets=None, skip=()):
        acts = []
        for p in parts:
            n = p["name"]
            if n in skip:
                continue
            off = (0, 0, 0)
            if offsets:
                off = offsets(n)
            op = 0.22 if n == "Lente" else 1.0
            metal = n == "Tasto_NAV" or n.startswith("Laterale_")
            acts.append(actor(shapes[n], part_color(n), op, offset=off, metal=metal,
                              spec_=0.45 if n.startswith("Tasto_") else 0.25))
        return acts

    T = D["T"]
    # 1. fronte 3/4
    acts = enc_actors(skip=("Retro", "Supporto_tastiera"))
    acts.append(actor(scr, (0.05, 0.17, 0.26), 1.0, spec_=0.8, power=60))
    acts.append(actor(nut, (0.72, 0.62, 0.30), metal=True))
    acts.append(actor(ant, (0.06, 0.06, 0.07), spec_=0.2))
    acts.append(actor(shapes["Retro"], part_color("Retro")))
    scene(acts, dict(focal=(0, 8, T / 2), pos=(-260, -330, 720), up=(0.0, 1, 0.3), zoom=1.15, angle=18),
          os.path.join(OUT, "render_fronte.png"), size=(1400, 1600))
    scene(acts, dict(focal=(0, 12, T / 2), pos=(-40, -60, 900), up=(0, 1, 0), zoom=1.0, angle=14),
          os.path.join(OUT, "vista_fronte.png"), size=(1000, 1400))
    # 2. retro 3/4
    acts = enc_actors(skip=("Supporto_tastiera",))
    acts.append(actor(nut, (0.72, 0.62, 0.30), metal=True))
    acts.append(actor(ant, (0.06, 0.06, 0.07), spec_=0.2))
    scene(acts, dict(focal=(0, 8, T / 2), pos=(300, -300, -700), up=(0.0, 1, -0.3), zoom=1.15, angle=18),
          os.path.join(OUT, "render_retro.png"), size=(1400, 1600))
    scene(acts, dict(focal=(0, 12, T / 2), pos=(60, -90, -900), up=(0, 1, 0), zoom=1.0, angle=14),
          os.path.join(OUT, "vista_retro.png"), size=(1000, 1400))
    # 3. esploso
    def off(n):
        if n == "Retro":
            return (0, 0, -52)
        if n == "Supporto_tastiera":
            return (0, 0, 12)
        if n == "Lente":
            return (0, 0, 82)
        if n.startswith("Laterale_"):
            return (22, 0, 62)
        if n.startswith("Tasto_"):
            return (0, 0, 42)
        if n == "Frontale":
            return (0, 0, 64)
        return (0, 0, 0)
    acts = enc_actors(offsets=off)
    for m, s in mocks:
        n = m["name"]
        if n == "Batteria":
            o = (0, 0, -24)
        elif n.startswith(("Speaker", "Motore")):
            o = (0, 0, -52)
        elif n in ("PCB2", "KMR2", "NAV_corpo", "NAV_stelo", "LED_kbd") or n.startswith("P2_"):
            o = (0, 0, 24)
        elif n == "Display":
            o = (0, 0, 38)
        else:
            o = (0, 0, 0)
        col = m["color"]
        acts.append(actor(s, col, offset=o, spec_=0.3))
    scene(acts, dict(focal=(0, 0, 8), pos=(-560, -330, 330), up=(0.35, 0.25, 0.9), zoom=1.2, angle=22),
          os.path.join(OUT, "render_esploso.png"), size=(1600, 1500))
    # 4. sezione longitudinale (piano X = xs)
    xs = spec.X(34.75)
    half = cq.Solid.makeBox(200, 300, 100, cq.Vector(xs, -150, -50))
    acts = []
    for p in parts:
        n = p["name"]
        s = shapes[n]
        bb = s.BoundingBox()
        if bb.xmax < xs:
            continue
        cs = s.intersect(half) if bb.xmin < xs else s
        if cs.Volume() < 1e-6:
            continue
        acts.append(actor(cs, part_color(n) if n != "Frontale" else (0.30, 0.32, 0.36),
                          0.5 if n == "Lente" else 1.0))
    for m, s in mocks:
        bb = s.BoundingBox()
        if bb.xmax < xs:
            continue
        cs = s.intersect(half) if bb.xmin < xs else s
        if cs.Volume() < 1e-6:
            continue
        acts.append(actor(cs, m["color"]))
    scene(acts, dict(focal=(xs, 0, T / 2), pos=(xs - 400, 0, T / 2), up=(0, 0, 1), scale=19.8, fit=False),
          os.path.join(OUT, "render_sezione.png"), size=(2800, 820), parallel=True)
    yk = spec.Y(spec.ky(14))
    scene(acts, dict(focal=(xs, yk, T / 2 - 0.5), pos=(xs - 400, yk, T / 2 - 0.5), up=(0, 0, 1), scale=9.0, fit=False),
          os.path.join(OUT, "render_sezione_tastiera.png"), size=(2000, 1000), parallel=True)
    # 5. dettaglio tastiera
    acts = enc_actors(skip=("Retro", "Supporto_tastiera"))
    acts.append(actor(scr, (0.05, 0.17, 0.26), 1.0, spec_=0.8, power=60))
    ky = spec.Y(spec.ky(20))
    scene(acts, dict(focal=(0, ky + 4, T), pos=(-60, ky - 160, T + 200), up=(0, 1, 0), zoom=2.4, angle=20),
          os.path.join(OUT, "render_tastiera.png"), size=(1600, 1200))


if __name__ == "__main__":
    main()
