"""Q-Mesh: scocca apribile, specifica geometrica condivisa.

La stessa specifica viene costruita da CadQuery (build_cq.py, per STEP/STL e verifiche)
e dallo script Fusion 360 (fusion/QMeshScocca.py), cosi' le due versioni coincidono.

Unita': mm. Assi del modello:
  X = larghezza (0 al centro, + verso destra guardando il fronte)
  Y = lunghezza (0 al centro, + verso l'alto del telefono)
  Z = spessore (0 = superficie del retro, T = superficie del fronte)
Nelle formule uso anche le coordinate della PCB 1 (xp, yp: origine nell'angolo in alto a
sinistra, y verso il basso), le coordinate della PCB 2 tastiera (xk, yk) e la profondita'
dal fronte fz (0 = faccia frontale, cresce verso il retro).

Primitive (tutte prismi estrusi lungo un asse):
  rrect: rettangolo arrotondato  (u, v) centro, w x h, raggio r
  circle: cerchio                (u, v) centro, diametro d
  poly:   poligono               pts [(u, v), ...]
  ring:   rrect esterno meno rrect interno (w, h, r, wi, hi, ri)
Per axis 'z' (u, v) = (X, Y); per 'y' (u, v) = (X, Z); per 'x' (u, v) = (Y, Z).
L'estrusione va da a0 ad a1 lungo l'asse.
"""
import math

P = dict(
    # corpo e pareti
    wall=1.6, gap_x=0.4, gap_y=0.25, corner_r=8.0, fillet_front=1.2, fillet_back=2.5,
    # PCB 1 rev. 1
    pcb1_w=70.0, pcb1_l=130.3, pcb1_t=1.0, pcb1_r=6.0,
    # frontale
    plate=1.2, plate_disp=1.6,
    # display Adafruit TL034WVS05 (datasheet): 70,45 x 70,32 x 1,89, area attiva 60,48
    disp_w=70.45, disp_h=70.32, disp_t=1.89, disp_act=60.48, disp_top=8.5, disp_tape=0.1,
    # lente acrilica
    lens=66.0, lens_t=1.0, lens_r=2.0, lens_clear=0.2, win_margin=0.6,
    # tasti
    key_lo=0.40, key_hi=0.86, key_flat=0.6, flange=0.35, flange_t=0.5, nub=0.3, nub_d=1.6,
    key_clear=0.15, web=0.4, fret=1.2, legends=True,
    # tastiera (PCB 2) e pila
    kmr2_h=1.9, pcb2_t=0.6, pcb2_w=69.5, pcb2_l=39.5, pcb2_x=0.25, pcb2_y=82.3, gap_pcb=2.5,
    # batteria LiPo 505060 (5,0 x 50 x 60)
    batt_w=50.0, batt_l=60.0, batt_t=5.0, batt_tape=0.3, batt_swell=0.3, back_t=1.2,
    # navigatore Alps SKRHABE010: corpo 1,8, altezza totale con stelo 5,0 (stelo da misurare)
    nav_body_h=1.8, nav_total_h=5.0, nav_stem_d=2.6, nav_cap=9.0, nav_cap_proud=1.5,
    # SMA da pannello con pigtail U.FL (esagono 8 mm)
    sma_x=10.5, sma_fz=7.5, sma_hex=8.0, sma_hex_len=2.5, sma_hole=6.6,
    # viti M2 e inserti in ottone da incollare (la resina SLA non si fonde)
    insert_d=3.3, insert_len=3.0, boss_d=4.6, screw_clear=2.3, cbore_d=4.4, cbore_h=1.7,
    # tasti laterali Panasonic EVQ-P7C (alti 1,35, attuatore 0,6 oltre il bordo PCB)
    evq_h=1.35, evq_tip=70.6,
    # speaker 1511 e motore 0820
    spk_w=15.0, spk_h=11.0, spk_t=3.5, mot_d=8.0, mot_t=2.0,
    # retro: texture
    texture=True, tex_pitch=1.6, tex_w=0.35, tex_d=0.2,
)


def derive(p=P):
    d = dict(p)
    d["W"] = p["pcb1_w"] + 2 * (p["gap_x"] + p["wall"])
    d["H"] = p["pcb1_l"] + 2 * (p["gap_y"] + p["wall"])
    d["cav_w"] = d["W"] - 2 * p["wall"]
    d["cav_h"] = d["H"] - 2 * p["wall"]
    d["cav_r"] = p["corner_r"] - p["wall"]
    # pila, profondita' dal fronte
    d["fz_flange"] = p["plate"] + p["flange_t"]
    d["fz_sw"] = d["fz_flange"] + p["nub"]
    d["fz_pcb2f"] = d["fz_sw"] + p["kmr2_h"]
    d["fz_pcb2b"] = d["fz_pcb2f"] + p["pcb2_t"]
    d["fz_pcb1f"] = d["fz_pcb2b"] + p["gap_pcb"]
    d["fz_pcb1b"] = d["fz_pcb1f"] + p["pcb1_t"]
    d["fz_inner"] = d["fz_pcb1b"] + p["batt_tape"] + p["batt_t"] + p["batt_swell"]
    d["T"] = d["fz_inner"] + p["back_t"]
    d["Zp"] = d["T"] - d["fz_inner"]              # piano di separazione frontale / retro
    d["fz_disp_f"] = p["plate_disp"] + p["disp_tape"]
    d["fz_disp_b"] = d["fz_disp_f"] + p["disp_t"]
    d["fz_act"] = d["fz_pcb1b"] + p["evq_h"] / 2     # asse attuatori tasti laterali
    return d


D = derive()

# ------------------------------------------------------------------ coordinate
def X(xp):
    return xp + D["gap_x"] + D["wall"] - D["W"] / 2


def Y(yp):
    return D["H"] / 2 - (yp + D["gap_y"] + D["wall"])


def Z(fz):
    return D["T"] - fz


def kx(xk):
    return xk + P["pcb2_x"]


def ky(yk):
    return yk + P["pcb2_y"]


def rr(axis, u, v, w, h, r, a0, a1):
    return dict(kind="rrect", axis=axis, u=u, v=v, w=w, h=h, r=min(r, w / 2 - 1e-3, h / 2 - 1e-3),
                a0=min(a0, a1), a1=max(a0, a1))


def cy(axis, u, v, dd, a0, a1):
    return dict(kind="circle", axis=axis, u=u, v=v, d=dd, a0=min(a0, a1), a1=max(a0, a1))


def pg(axis, pts, a0, a1):
    return dict(kind="poly", axis=axis, pts=[tuple(q) for q in pts], a0=min(a0, a1), a1=max(a0, a1))


def ring(axis, u, v, w, h, r, wi, hi, ri, a0, a1):
    return dict(kind="ring", axis=axis, u=u, v=v, w=w, h=h, r=r, wi=wi, hi=hi, ri=ri,
                a0=min(a0, a1), a1=max(a0, a1))


def box_p(x0, y0, x1, y1, fz0, fz1, r=0.0):
    """Blocco in coordinate PCB 1 (xp, yp) e profondita' fz."""
    return rr("z", (X(x0) + X(x1)) / 2, (Y(y0) + Y(y1)) / 2, abs(x1 - x0), abs(y1 - y0), r, Z(fz0), Z(fz1))


def cyl_p(xp, yp, dd, fz0, fz1):
    return cy("z", X(xp), Y(yp), dd, Z(fz0), Z(fz1))


# ------------------------------------------------------------------ dati di progetto
KW = (P["pcb2_w"] - 9 * 0.35) / 10          # larghezza tasto riga (6,635)
ROWS_Y = (13.45, 21.35, 29.25)
ROW_KEYS = [list("QWERTYUIOP"), list("ASDFGHJKL") + ["DEL"], ["ALT"] + list("ZXCVBNM") + ["$", "ENT"]]
ROW_IDS = [list("QWERTYUIOP"), list("ASDFGHJKL") + ["BKSP"], ["ALT"] + list("ZXCVBNM") + ["DOLLAR", "ENTER"]]
BOTTOM = [("LSHIFT", "aA", 7.1, 11.0), ("ZERO_MIC", "0", 7.4, 18.6), ("SPACE", "", 22.8, 34.05),
          ("SYM", "SYM", 7.4, 49.5), ("RSHIFT", "aA", 7.1, 57.1)]
BOTTOM_Y = 36.5
NAV_KEYS = [("CALL", 7.7), ("MENU", 20.9), ("BACK", 48.7), ("END", 62.2)]
NAV_Y = 4.5
NAV_POS = (34.75, 4.7)
KBD_HOLES = [(3.0, 36.0), (66.5, 36.0)]
KBD_LEDS = [(1.4, 9.4), (68.1, 9.4), (1.4, 17.4), (68.1, 17.4), (1.4, 25.3), (68.1, 25.3), (6.5, 33.2), (63.0, 33.2)]

BUTTONS = [("PWR", 12.0, 6.0), ("VOL+", 30.0, 6.0), ("PTT", 39.0, 8.0), ("VOL-", 50.0, 6.0)]
SCREWS = [(4.5, 127.0), (65.5, 127.0)]
TABS = [24.0, 46.0]
LED = (44.5, 3.5)
MIC = (44.0, 127.3)
USB = (35.0, 130.3)
SD_Y = 116.3
SPK = (54.0, 121.5)
MOT = (22.0, 125.4)
BATT = (9.5, 29.1, 60.5, 89.5)
FRONT_POSTS = [(18.0, 4.0), (48.6, 4.0)]
BACK_POSTS = [(18.0, 4.0), (44.5, 4.0)]
PCB1_NOTCH = (0.0, 0.0, 16.0, 7.2)
FPC_SLOT = (22.0, 79.3, 48.0, 80.9)
J7_NEW = (14.5, 95.5)
C70_NEW = (5.5, 95.5)


def key_layout():
    """Tasti: lista di dict con centro (xk, yk), dimensioni e interruttore."""
    keys = []
    for ri, yc in enumerate(ROWS_Y):
        for ci in range(10):
            xc = ci * (KW + 0.35) + KW / 2
            keys.append(dict(kind="row", row=ri, col=ci, x=xc, y=yc, w=KW, h=7.9 - P["fret"] - 2 * P["key_clear"],
                             sx=xc, sy=yc, label=ROW_KEYS[ri][ci], id=ROW_IDS[ri][ci]))
    bx = 7.4
    for kid, lab, w, sx in BOTTOM:
        keys.append(dict(kind="bottom", x=bx + w / 2, y=BOTTOM_Y, w=w, h=5.1, sx=sx, sy=BOTTOM_Y, label=lab, id=kid))
        bx += w + 0.35
    for lab, sx in NAV_KEYS:
        keys.append(dict(kind="nav", x=sx, y=NAV_Y, w=11.0, h=6.6, sx=sx, sy=NAV_Y, label=lab, id=lab))
    return keys


KEYS = key_layout()


# ------------------------------------------------------------------ parti
def front_shell():
    d = D
    T = d["T"]
    ops = [("env", dict(w=d["W"], h=d["H"], r=P["corner_r"], t=T, f_top=P["fillet_front"],
                        f_bot=P["fillet_back"], zmin=d["Zp"], zmax=T))]
    add = lambda q: ops.append(("add", q))
    cut = lambda q: ops.append(("cut", q))
    # cavita'
    cut(rr("z", 0, 0, d["cav_w"], d["cav_h"], d["cav_r"], -1, Z(P["plate"])))
    # piastra ispessita sotto la lente e ferma-display
    dy0, dy1 = P["disp_top"] - 0.3, P["disp_top"] + P["disp_h"] + 0.3
    add(box_p(-0.6, dy0, 70.6, dy1, P["plate"] - 0.01, P["plate_disp"]))
    for x0, x1 in ((-0.6, 8.0), (62.0, 70.6)):
        add(box_p(x0, P["disp_top"] - 1.15, x1, P["disp_top"] - 0.1, P["plate"] - 0.01, 3.4))
        add(box_p(x0, P["disp_top"] + P["disp_h"] + 0.1, x1, P["disp_top"] + P["disp_h"] + 1.2, P["plate"] - 0.01, 3.4))
    # lente e finestra
    cyd = P["disp_top"] + P["disp_h"] / 2
    ls = P["lens"] + 2 * P["lens_clear"]
    cut(rr("z", X(35), Y(cyd), ls, ls, P["lens_r"] + P["lens_clear"], Z(P["lens_t"]), T + 1))
    ws = P["disp_act"] + 2 * P["win_margin"]
    cut(rr("z", X(35), Y(cyd), ws, ws, 1.0, Z(P["plate_disp"] + 0.4), T + 1))
    # bordo superiore: finta capsula, LED, appoggi PCB
    cut(rr("z", X(35), Y(3.2), 14.0, 1.0, 0.5, Z(0.35), T + 1))
    add(cyl_p(LED[0], LED[1], 3.0, P["plate"] - 0.01, 5.6))
    cut(cyl_p(LED[0], LED[1], 2.0, 5.7, 0.5))
    cut(cyl_p(LED[0], LED[1], 1.2, 0.6, -1))
    for xp, yp in FRONT_POSTS:
        add(cyl_p(xp, yp, 2.0, P["plate"] - 0.01, d["fz_pcb1f"] - 0.05))
    # SMA: foro nella parete alta + due battute per l'esagono
    sx = P["sma_x"]
    hx = P["sma_hex"] / 2 + 0.05
    cut(cy("y", X(sx), Z(P["sma_fz"]), P["sma_hole"], Y(0.5), Y(-3.0)))
    add(box_p(sx - 5, -0.6, sx + 5, P["sma_hex_len"] + 0.1, P["plate"] - 0.01, P["sma_fz"] - hx))
    add(box_p(sx - 5, -0.6, sx + 5, P["sma_hex_len"] + 0.1, P["sma_fz"] + hx, d["fz_inner"] - 0.1))
    # tasti laterali: tasca interna + asola
    for name, yc, L in BUTTONS:
        cut(rr("x", Y(yc), Z(d["fz_act"]), L + 1.6, 4.0, 0.6, X(70.2), X(71.2)))
        cut(rr("x", Y(yc), Z(d["fz_act"]), L + 0.3, 2.7, 1.0, X(70.0), X(72.8)))
    # microSD (fianco sinistro) + incavo per l'unghia
    sdz = d["fz_pcb1b"] + 0.85
    cut(rr("x", Y(SD_Y), Z(sdz), 12.4, 1.7, 0.6, X(-2.8), X(0.2)))
    cut(rr("x", Y(SD_Y), Z(sdz), 14.0, 4.2, 2.1, X(-2.8), X(-1.3)))
    # USB-C
    usbz = d["fz_pcb1b"] + 1.63
    cut(rr("y", X(USB[0]), Z(usbz), 12.6, 6.8, 3.4, Y(129.8), Y(133.5)))
    # sedi dei ganci del retro
    for xt in TABS:
        cut(rr("y", X(xt), Z(12.35), 8.6, 1.1, 0.2, Y(-1.05), Y(0.1)))
    # torrette viti in basso (inserti M2)
    for xp, yp in SCREWS:
        add(cyl_p(xp, yp, P["boss_d"], P["plate"] - 0.01, d["fz_pcb1f"]))
        cut(cyl_p(xp, yp, P["insert_d"], d["fz_pcb1f"] + 0.05, d["fz_pcb1f"] - P["insert_len"]))
        cut(cyl_p(xp, yp, 2.0, d["fz_pcb1f"] - P["insert_len"], 2.8))
    # microfono: tubo dal fronte alla PCB
    add(cyl_p(MIC[0], MIC[1], 3.0, P["plate"] - 0.01, d["fz_pcb1f"] - 0.1))
    cut(cyl_p(MIC[0], MIC[1], 1.4, d["fz_pcb1f"], 0.6))
    cut(cyl_p(MIC[0], MIC[1], 0.9, 0.7, -1))
    # tastiera: aperture
    c = P["key_clear"]
    for ri, yc in enumerate(ROWS_Y):
        cut(rr("z", X(kx(P["pcb2_w"] / 2)), Y(ky(yc)), P["pcb2_w"] + 2 * c, 6.4 + 2 * c, 0.3, Z(P["plate"] + 0.1), T + 1))
    cut(rr("z", X(kx(34.0)), Y(ky(BOTTOM_Y)), 53.2 + 2 * c, 5.1 + 2 * c, 0.6, Z(P["plate"] + 0.1), T + 1))
    for lab, sx_ in NAV_KEYS:
        cut(rr("z", X(kx(sx_)), Y(ky(NAV_Y)), 11.0 + 2 * c, 6.6 + 2 * c, 1.1, Z(P["plate"] + 0.1), T + 1))
    cut(rr("z", X(kx(NAV_POS[0])), Y(ky(NAV_POS[1])), 10.0, 10.0, 1.8, Z(P["plate"] + 0.1), T + 1))
    # nervature sotto i fret (appoggiano sulla PCB 2), interrotte sui LED e sul 5 vie
    web_y = (9.03, 17.4, 25.3, 33.2)
    zb = d["fz_pcb2f"] - 0.05
    for wy in web_y:
        gaps = [(0.2, 2.9), (66.6, 69.3)] if wy < 30 else [(5.3, 7.8), (61.8, 64.3)]
        if wy < 10:
            gaps.append((29.4, 40.2))
        xs = [-0.9] + [g for gg in sorted(gaps) for g in gg] + [70.4]
        for i in range(0, len(xs), 2):
            x0, x1 = xs[i], xs[i + 1]
            add(box_p(kx(x0), ky(wy) - P["web"] / 2, kx(x1), ky(wy) + P["web"] / 2, P["plate"] - 0.01, zb))
    # pioli sulla barra nav
    for xk in (14.3, 28.1, 41.5, 55.45):
        add(cyl_p(kx(xk), ky(NAV_Y), 1.2, P["plate"] - 0.01, zb))
    # torrette PCB 2 (viti M2 x 3 dal retro della tastiera)
    for hx_, hy_ in KBD_HOLES:
        add(cyl_p(kx(hx_), ky(hy_), P["boss_d"], P["plate"] - 0.01, d["fz_pcb2f"]))
        cut(cyl_p(kx(hx_), ky(hy_), P["insert_d"], d["fz_pcb2f"] + 0.05, d["fz_pcb2f"] - P["insert_len"]))
    # logo sul mento
    ops.append(("text", dict(txt="Q·MESH", u=X(35), v=Y(125.6), size=2.0, depth=0.3, face="front", z=T)))
    return dict(name="Frontale", color=(0.12, 0.13, 0.15), ops=ops)


def back_cover():
    d = D
    T, Zp = d["T"], d["Zp"]
    ops = [("env", dict(w=d["W"], h=d["H"], r=P["corner_r"], t=T, f_top=P["fillet_front"],
                        f_bot=P["fillet_back"], zmin=0, zmax=Zp))]
    add = lambda q: ops.append(("add", q))
    cut = lambda q: ops.append(("cut", q))
    cl = 0.15
    ow, oh = d["cav_w"] - 2 * cl, d["cav_h"] - 2 * cl
    add(ring("z", 0, 0, ow, oh, d["cav_r"] - cl, ow - 1.6, oh - 1.6, d["cav_r"] - cl - 0.8, Zp - 0.01, Zp + 1.0))
    # interruzioni del bordino (SMA, USB-C)
    cut(box_p(P["sma_x"] - 5.6, -1.0, P["sma_x"] + 5.6, 3.2, d["fz_inner"] - 1.2, d["fz_inner"] - 0.01))
    cut(box_p(28.5, 128.5, 41.6, 132.0, d["fz_inner"] - 1.2, d["fz_inner"] - 0.01))
    # ganci in alto
    for xt in TABS:
        add(box_p(xt - 4, -0.1, xt + 4, 0.7, 11.9, d["fz_inner"] + 0.01))
        add(box_p(xt - 4, -0.95, xt + 4, 0.0, 11.9, 12.8))
    # torrette viti (a gradino per non toccare i pad dello speaker)
    for xp, yp in SCREWS:
        add(cyl_p(xp, yp, 6.0, d["fz_inner"] + 0.01, d["fz_inner"] - 1.8))
        add(cyl_p(xp, yp, P["boss_d"], d["fz_inner"] - 1.7, d["fz_pcb1b"]))
        cut(cyl_p(xp, yp, P["cbore_d"], T + 1, T - P["cbore_h"]))
        cut(cyl_p(xp, yp, P["screw_clear"], T + 1, d["fz_pcb1b"] - 0.5))
    # appoggi sulla PCB in alto
    for xp, yp in BACK_POSTS:
        add(cyl_p(xp, yp, 2.4, d["fz_inner"] + 0.01, d["fz_pcb1b"] + 0.05))
    # cornice batteria
    x0, y0, x1, y1 = BATT
    rz0, rz1 = d["fz_inner"] + 0.01, d["fz_pcb1b"] + 1.5
    add(box_p(x0 - 0.8, y0 - 0.8, x0, y1 + 0.8, rz0, rz1))
    add(box_p(x1, y0 - 0.8, x1 + 0.8, y1 + 0.8, rz0, rz1))
    add(box_p(x0 - 0.8, y0 - 0.8, x1 + 0.8, y0, rz0, rz1))
    add(box_p(x0 - 0.8, y1, 21.5, y1 + 0.8, rz0, rz1))
    add(box_p(48.6, y1, x1 + 0.8, y1 + 0.8, rz0, rz1))
    # speaker: sede + griglia
    sw, sh = P["spk_w"] + 0.4, P["spk_h"] + 0.4
    add(ring("z", X(SPK[0]), Y(SPK[1]), sw + 1.6, sh + 1.6, 1.0, sw, sh, 0.5, Zp - 0.01, Zp + P["spk_t"] + 0.1))
    for i in range(5):
        for j in range(3):
            cut(cyl_p(SPK[0] + (i - 2) * 1.8, SPK[1] + (j - 1) * 1.8, 0.9, T + 1, d["fz_inner"] - 0.2))
    # motore vibrazione
    add(ring("z", X(MOT[0]), Y(MOT[1]), P["mot_d"] + 2.0, P["mot_d"] + 2.0, P["mot_d"] / 2 + 1.0,
             P["mot_d"] + 0.4, P["mot_d"] + 0.4, P["mot_d"] / 2 + 0.2, Zp - 0.01, Zp + P["mot_t"] + 0.4))
    # esterno: righe sulla zona antenne
    for i in range(7):
        cut(rr("z", X(35), Y(6.15 + 3 * i), 58.0, 0.5, 0.25, -1, 0.3))
    # pannello goffrato + targhetta con logo
    panel = (3.0, 30.15, 67.0, 120.15)
    ops.append(("texture", dict(x0=X(panel[0]), y0=Y(panel[3]), x1=X(panel[2]), y1=Y(panel[1]), r=5.0,
                                pitch=P["tex_pitch"], w=P["tex_w"], depth=P["tex_d"],
                                badge=(X(35), Y(75.15), 26.0, 7.0, 3.5))))
    cut(ring("z", X(35), Y((panel[1] + panel[3]) / 2), 64.8, 90.8, 5.4, 64.0, 90.0, 5.0, -1, 0.3))
    ops.append(("text", dict(txt="Q·MESH", u=X(35), v=Y(75.15), size=2.6, depth=0.3, face="back", z=0)))
    return dict(name="Retro", color=(0.17, 0.18, 0.20), ops=ops)


def keycaps():
    d = D
    T = d["T"]
    parts = []
    for k in KEYS:
        ops = []
        xc, yc = kx(k["x"]), ky(k["y"])
        w, h = k["w"], k["h"]
        zb = Z(P["plate"] + 0.1)     # il corpo entra nell'aletta
        if k["kind"] == "row":
            left = k["col"] < 5
            xl, xr = X(xc - w / 2), X(xc + w / 2)
            ztop_out, ztop_in = T + P["key_lo"], T + P["key_hi"]
            zl, zr = (ztop_out, ztop_in) if left else (ztop_in, ztop_out)
            cch = 0.25
            zat = lambda xx: zl + (zr - zl) * (xx - xl) / (xr - xl)
            pts = [(xl, zb), (xr, zb), (xr, zat(xr) - cch), (xr - cch, zat(xr - cch)),
                   (xl + cch, zat(xl + cch)), (xl, zat(xl) - cch)]
            ops.append(("add", pg("y", pts, Y(yc - h / 2), Y(yc + h / 2))))
            fx0, fx1 = xc - w / 2, xc + w / 2
            if k["col"] == 0:
                fx0 -= P["flange"]
            if k["col"] == 9:
                fx1 += P["flange"]
            ztxt = (zl + zr) / 2
        else:
            r = 0.6 if k["kind"] == "bottom" else 1.0
            ops.append(("add", rr("z", X(xc), Y(yc), w, h, r, zb, T + P["key_flat"])))
            fx0, fx1 = xc - w / 2, xc + w / 2
            if k["kind"] == "bottom" and k is KEYS[30]:
                fx0 -= P["flange"]
            if k["kind"] == "bottom" and k is KEYS[34]:
                fx1 += P["flange"]
            ztxt = T + P["key_flat"]
        ops.append(("add", box_p(fx0, yc - h / 2 - P["flange"], fx1, yc + h / 2 + P["flange"],
                                 P["plate"], d["fz_flange"], r=0.2)))
        ops.append(("add", cyl_p(kx(k["sx"]), ky(k["sy"]), P["nub_d"], d["fz_flange"] - 0.05, d["fz_sw"])))
        if P["legends"] and k["label"] and k["kind"] != "nav":
            size = 2.4 if len(k["label"]) == 1 else 1.5
            ops.append(("text", dict(txt=k["label"], u=X(xc), v=Y(yc), size=size, depth=0.3, face="front",
                                     z=ztxt, cut_to=ztxt - 0.3)))
        parts.append(dict(name="Tasto_" + k["id"], color=(0.10, 0.10, 0.11), ops=ops, group="tasti"))
    # cappuccio del navigatore
    nx, ny = kx(NAV_POS[0]), ky(NAV_POS[1])
    stem_top = d["fz_pcb2f"] - P["nav_total_h"]
    ops = [("add", rr("z", X(nx), Y(ny), P["nav_cap"], P["nav_cap"], 1.5, Z(1.0), T + P["nav_cap_proud"])),
           ("cut", cy("z", X(nx), Y(ny), P["nav_stem_d"] + 0.1, Z(1.1), Z(stem_top)))]
    parts.append(dict(name="Tasto_NAV", color=(0.55, 0.57, 0.60), ops=ops, group="tasti"))
    # tasti laterali
    for name, yc, L in BUTTONS:
        zc = Z(d["fz_act"])
        ops = [("add", rr("x", Y(yc), zc, L, 2.4, 1.0, X(70.75), X(72.6))),
               ("add", rr("x", Y(yc), zc, L + 1.4, 3.8, 0.8, X(70.75), X(71.15)))]
        col = (0.75, 0.12, 0.16) if name == "PTT" else (0.55, 0.57, 0.60)
        parts.append(dict(name="Laterale_" + name.replace("+", "P").replace("-", "M"), color=col, ops=ops,
                          group="tasti"))
    return parts


def kbd_support():
    d = D
    ops = [("add", box_p(kx(0.2), ky(0.2), kx(69.3), ky(39.3), d["fz_pcb2b"], d["fz_pcb1f"], r=0.5))]
    cut = lambda q: ops.append(("cut", q))
    cut(box_p(kx(7.5), ky(-1.0), kx(20.8), ky(17.5), d["fz_pcb2b"] - 1, d["fz_pcb1f"] + 1))
    cut(box_p(kx(21.3), ky(13.9), kx(32.6), ky(23.2), d["fz_pcb2b"] - 1, d["fz_pcb1f"] + 1, r=0.5))
    cut(box_p(C70_NEW[0] - 1.2, C70_NEW[1] - 2.0, C70_NEW[0] + 1.2, C70_NEW[1] + 2.0,
              d["fz_pcb2b"] - 1, d["fz_pcb1f"] + 1))
    for hx_, hy_ in KBD_HOLES:
        cut(cyl_p(kx(hx_), ky(hy_), 4.8, d["fz_pcb2b"] - 1, d["fz_pcb1f"] + 1))
    return dict(name="Supporto_tastiera", color=(0.30, 0.32, 0.35), ops=ops)


def lens():
    cyd = P["disp_top"] + P["disp_h"] / 2
    ops = [("add", rr("z", X(35), Y(cyd), P["lens"], P["lens"], P["lens_r"], Z(P["lens_t"]), D["T"]))]
    return dict(name="Lente", color=(0.55, 0.70, 0.80), ops=ops, alpha=0.35)


def parts():
    return [front_shell(), back_cover(), kbd_support(), lens()] + keycaps()


# ------------------------------------------------------------------ riferimento PCB rev. 1 (solo verifica)
def mocks(main_json=None, kbd_json=None):
    """Blocchi di ingombro per verificare la scocca: PCB, display, batteria, componenti."""
    import json
    d = D
    out = []

    def m(name, prim, color=(0.2, 0.6, 0.35)):
        out.append(dict(name=name, prim=prim, color=color))

    # PCB 1
    m("PCB1", box_p(0, 0, P["pcb1_w"], P["pcb1_l"], d["fz_pcb1f"], d["fz_pcb1b"], r=P["pcb1_r"]))
    m("PCB2", box_p(kx(0), ky(0), kx(P["pcb2_w"]), ky(P["pcb2_l"]), d["fz_pcb2f"], d["fz_pcb2b"], r=0.5))
    m("Display", box_p(35 - P["disp_w"] / 2, P["disp_top"], 35 + P["disp_w"] / 2, P["disp_top"] + P["disp_h"],
                       d["fz_disp_f"], d["fz_disp_b"]), (0.1, 0.1, 0.1))
    bx0, by0, bx1, by1 = BATT
    bcx, bcy = (bx0 + bx1) / 2, (by0 + by1) / 2
    m("Batteria", box_p(bcx - P["batt_w"] / 2, bcy - P["batt_l"] / 2, bcx + P["batt_w"] / 2, bcy + P["batt_l"] / 2,
                        d["fz_pcb1b"] + P["batt_tape"], d["fz_pcb1b"] + P["batt_tape"] + P["batt_t"]), (0.8, 0.7, 0.3))
    m("Speaker", box_p(SPK[0] - P["spk_w"] / 2, SPK[1] - P["spk_h"] / 2, SPK[0] + P["spk_w"] / 2, SPK[1] + P["spk_h"] / 2,
                       d["fz_inner"] - 0.05 - P["spk_t"], d["fz_inner"] - 0.05), (0.5, 0.4, 0.7))
    m("Motore", cyl_p(MOT[0], MOT[1], P["mot_d"], d["fz_inner"] - 0.05 - P["mot_t"], d["fz_inner"] - 0.05), (0.6, 0.6, 0.6))
    # SMA
    sz = P["sma_fz"]
    a = P["sma_hex"] / math.sqrt(3)
    hexpts = [(X(P["sma_x"]) + a * math.cos(math.radians(60 * i)), Z(sz) + a * math.sin(math.radians(60 * i)))
              for i in range(6)]
    m("SMA_esagono", pg("y", hexpts, Y(-0.25), Y(-0.25 + P["sma_hex_len"])), (0.85, 0.75, 0.3))
    m("SMA_filetto", cy("y", X(P["sma_x"]), Z(sz), 6.35, Y(-0.25), Y(-9.2)), (0.85, 0.75, 0.3))
    m("SMA_coda", cy("y", X(P["sma_x"]), Z(sz), 3.5, Y(P["sma_hex_len"] - 0.25), Y(6.8)), (0.85, 0.75, 0.3))
    # PCB 2 lato F: KMR2, 5 vie, LED
    for k in KEYS:
        m("KMR2", box_p(kx(k["sx"]) - 2.1, ky(k["sy"]) - 1.4, kx(k["sx"]) + 2.1, ky(k["sy"]) + 1.4,
                        d["fz_pcb2f"] - P["kmr2_h"], d["fz_pcb2f"]), (0.8, 0.8, 0.8))
    nx, ny = kx(NAV_POS[0]), ky(NAV_POS[1])
    m("NAV_corpo", box_p(nx - 3.7, ny - 3.7, nx + 3.7, ny + 3.7, d["fz_pcb2f"] - P["nav_body_h"], d["fz_pcb2f"]), (0.8, 0.8, 0.8))
    m("NAV_stelo", cyl_p(nx, ny, P["nav_stem_d"], d["fz_pcb2f"] - P["nav_total_h"], d["fz_pcb2f"] - P["nav_body_h"]), (0.3, 0.3, 0.3))
    for lx, ly in KBD_LEDS:
        m("LED_kbd", box_p(kx(lx) - 0.4, ky(ly) - 0.8, kx(lx) + 0.4, ky(ly) + 0.8, d["fz_pcb2f"] - 0.8, d["fz_pcb2f"]), (1, 1, 1))
    # componenti dai file KiCad (posizioni rev. 0 trasformate in rev. 1)
    H = [("SOIC", 2.2), ("QFN", 1.0), ("TDFN", 0.8), ("DRC0010J", 1.0), ("SOT-23", 1.2), ("SOT-353", 1.1),
         ("SOD-323", 1.1), ("Crystal", 0.9), ("Oscillator", 0.9), ("U.FL", 1.3), ("EVQP7C", P["evq_h"]),
         ("KMR2", 1.9), ("FH12", 2.0), ("JST_SH", 3.0), ("microSD", 1.5), ("USB_C", 3.3), ("LED_RGB", 1.1),
         ("LED_0603", 0.8), ("_0402", 0.55), ("_0603", 0.9), ("_0805", 1.3), ("FNR252012", 1.2),
         ("Johanson", 0.6), ("Infineon", 0.4), ("InvenSense", 1.0), ("Antenova", 1.0), ("Filter_1411", 0.6),
         ("Speaker_Pads", 1.0), ("Motor_Pads", 1.0), ("TestPoint", 0.05), ("SolderJumper", 0.05)]

    def height(fp):
        for key, hh in H:
            if key in fp:
                return hh
        return None

    if main_json:
        for o in json.load(open(main_json)):
            fp = o["fp"].split(":")[-1]
            if o["ref"] in ("AE2",) or "SWRA117D" in fp or "Mounting" in fp:
                continue
            hh = height(fp)
            if hh is None:
                print("altezza sconosciuta", o["ref"], fp)
                hh = 1.2
            x0, y0, x1, y1 = [v - 0.05 for v in o["bb"]]
            side = o["side"]
            if o["ref"] == "D3":
                x0, y0, x1, y1 = LED[0] - 1.6, LED[1] - 1.35, LED[0] + 1.6, LED[1] + 1.35
            elif o["ref"] == "J7":
                x0, y0, x1, y1 = J7_NEW[0] - 6.05, J7_NEW[1] - 3.95, J7_NEW[0] + 6.05, J7_NEW[1] + 3.95
            elif o["ref"] == "C70":
                x0, y0, x1, y1 = C70_NEW[0] - 0.7, C70_NEW[1] - 1.45, C70_NEW[0] + 0.7, C70_NEW[1] + 1.45
            elif y0 >= 70:
                y0, y1 = y0 + 6.3, y1 + 6.3
            if side == "B":
                fz0, fz1 = d["fz_pcb1b"], d["fz_pcb1b"] + hh
            else:
                fz0, fz1 = d["fz_pcb1f"] - hh, d["fz_pcb1f"]
            m("P1_" + o["ref"], box_p(x0, y0, x1, y1, fz0, fz1), (0.35, 0.35, 0.4))
    if kbd_json:
        for o in json.load(open(kbd_json)):
            if o["side"] != "B":
                continue
            fp = o["fp"].split(":")[-1]
            hh = height(fp) or 1.2
            x0, y0, x1, y1 = [v - 0.05 for v in o["bb"]]
            m("P2_" + o["ref"], box_p(kx(x0), ky(y0), kx(x1), ky(y1), d["fz_pcb2b"], d["fz_pcb2b"] + hh), (0.35, 0.35, 0.4))
    return out


def pcb1_features():
    """Contorno e fori della PCB 1 rev. 1 (coordinate KiCad, mm, y verso il basso)."""
    return dict(size=(P["pcb1_w"], P["pcb1_l"]), radius=P["pcb1_r"], notch=PCB1_NOTCH,
                holes=[(x, y, 2.2) for x, y in SCREWS], slot=FPC_SLOT)


# ------------------------------------------------------------------ texture come elenco di prismi (per Fusion)
def _inside_rrect(x, y, cx, cy_, w, h, r):
    dx, dy = abs(x - cx) - (w / 2 - r), abs(y - cy_) - (h / 2 - r)
    if abs(x - cx) > w / 2 or abs(y - cy_) > h / 2:
        return False
    if dx > 0 and dy > 0:
        return dx * dx + dy * dy <= r * r
    return True


def _intervals(p0, dvec, L, inside, step=0.05):
    """Intervalli di t in [0, L] dove inside(p0 + t*d) e' vero (campionamento + bisezione)."""
    out, t, prev, start = [], 0.0, False, None
    n = int(L / step) + 1
    for i in range(n + 1):
        t = min(i * step, L)
        cur = inside(p0[0] + t * dvec[0], p0[1] + t * dvec[1])
        if cur and not prev:
            start = t
        if not cur and prev:
            out.append((start, t - step / 2))
        prev = cur
    if prev:
        out.append((start, L))
    return out


def texture_prims(t):
    """Scanalature della texture come parallelogrammi (asse z), gia' ritagliati sul pannello."""
    x0, y0, x1, y1 = t["x0"], t["y0"], t["x1"], t["y1"]
    cx, cy_ = (x0 + x1) / 2, (y0 + y1) / 2
    w, h, r = x1 - x0, y1 - y0, t["r"]
    bx, by, bw, bh, br = t["badge"]
    diag = math.hypot(w, h) + 4
    n = int(diag / t["pitch"]) + 2
    inside = lambda x, y: _inside_rrect(x, y, cx, cy_, w, h, r) and not _inside_rrect(x, y, bx, by, bw, bh, br)
    prims = []
    for ang in (45, -45):
        ca, sa = math.cos(math.radians(ang)), math.sin(math.radians(ang))
        d = (-sa, ca)                     # direzione della scanalatura
        nrm = (ca, sa)                    # direzione del passo
        for i in range(n):
            off = (i - (n - 1) / 2) * t["pitch"]
            c = (cx + off * nrm[0], cy_ + off * nrm[1])
            p0 = (c[0] - d[0] * diag / 2, c[1] - d[1] * diag / 2)
            for ta, tb in _intervals(p0, d, diag, inside):
                if tb - ta < 0.3:
                    continue
                a = (p0[0] + d[0] * ta, p0[1] + d[1] * ta)
                b = (p0[0] + d[0] * tb, p0[1] + d[1] * tb)
                hw = t["w"] / 2
                pts = [(a[0] - nrm[0] * hw, a[1] - nrm[1] * hw), (b[0] - nrm[0] * hw, b[1] - nrm[1] * hw),
                       (b[0] + nrm[0] * hw, b[1] + nrm[1] * hw), (a[0] + nrm[0] * hw, a[1] + nrm[1] * hw)]
                prims.append(pg("z", pts, -1.0, t["depth"]))
    return prims
