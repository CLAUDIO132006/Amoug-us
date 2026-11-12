#!/usr/bin/env python3
"""fbx2h.py - convierte el FBX de skins (7 tripulantes) en crew_model.h para la PS2.

Uso:  python3 fbx2h.py "амонг_ас_главная.fbx" crew_model.h

Los 7 cuerpos del FBX tienen la MISMA geometria; solo cambia el color de la paleta
(textura АМОНГ.jpg). Por eso se guarda UNA malla y 7 colores de piel:
  clase 0 = piel (cambia por skin), 1 = mochila, 2 = borde del visor, 3 = visor.
Se descartan las caras internas (plano de simetria dentro de visor/mochila).
Espacio del modelo: +Y arriba, el personaje mira hacia -Z, pies en y=0, centrado en x,z.
"""
import sys, os, collections
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from fbxmodel import load

HEIGHT = 1.8                          # altura final del personaje en metros
VISOR = (176, 222, 246)               # el FBX trae el visor gris (Material.007); aqui azul claro
PACK = (66, 14, 0)
RIM = (0, 0, 0)
# nombre -> (color de referencia para identificar la skin)
NAMED = [("ROJO", (207, 29, 45)), ("NARANJA", (255, 109, 65)), ("AMARILLO", (255, 198, 10)),
         ("LIMA", (172, 198, 71)), ("CIAN", (23, 220, 211)), ("NEGRO", (0, 0, 0)), ("CAFE", (66, 14, 0))]
# colores muy oscuros no dejan ver el sombreado: se levantan un poco (solo para NEGRO y CAFE)
CUT_FRAC = 0.50                       # el cuerpo muerto se corta a esta fraccion de la altura
BONE = (232, 232, 238)
LIFT = {"NEGRO": (46, 48, 56), "CAFE": (112, 56, 28)}


def main(fbx, out):
    M = load(fbx)
    bodies = [m for m in M if len(m["F"]) == 432]
    visors = [m for m in M if len(m["F"]) == 32]
    assert len(bodies) == 7 and visors, "se esperaban 7 cuerpos y sus visores"
    assert all(np.allclose(b["V"], bodies[0]["V"]) and b["F"] == bodies[0]["F"] for b in bodies), "los cuerpos no son iguales"
    # cuerpo de referencia = el rojo
    ref = min(bodies, key=lambda b: sum(abs(np.array(collections.Counter(b["C"]).most_common(1)[0][0]) - (207, 29, 45))))
    skin_ref = collections.Counter(ref["C"]).most_common(1)[0][0]
    kind = {skin_ref: 0, PACK: 1, RIM: 2}
    keep = [i for i, c in enumerate(ref["C"]) if c in kind]
    print("caras del cuerpo:", len(ref["F"]), "-> utiles:", len(keep), "(internas descartadas:", len(ref["F"]) - len(keep), ")")

    # skins: color de las caras 'piel' en cada cuerpo
    skin_idx = [i for i in keep if kind[ref["C"][i]] == 0]
    skins = {}
    for b in bodies:
        c = collections.Counter(b["C"][i] for i in skin_idx).most_common(1)[0][0]
        d = min(NAMED, key=lambda n: sum((np.array(n[1]) - c) ** 2))[0]
        skins[d] = c
    assert len(skins) == 7, skins
    print("skins detectadas:", skins)

    # blender (bx,by,bz) -> (x=by, y=bz, z=bx): rotacion propia, mira hacia -Z (antes -X)
    def conv(V): return np.stack([V[:, 1], V[:, 2], V[:, 0]], 1)
    Vb, Vv = conv(ref["V"]), conv(visors[0]["V"])
    allv = np.vstack([Vb, Vv])
    lo, hi = allv.min(0), allv.max(0)
    s = HEIGHT / (hi[1] - lo[1])
    off = np.array([(lo[0] + hi[0]) / 2, lo[1], (lo[2] + hi[2]) / 2])
    Vb, Vv = (Vb - off) * s, (Vv - off) * s
    print("escala %.4f, ancho %.2f m, fondo %.2f m, alto %.2f m" % (s, (hi[0] - lo[0]) * s, (hi[2] - lo[2]) * s, HEIGHT))

    V = np.vstack([Vb, Vv]); nb = len(Vb)
    faces = [(ref["F"][i], kind[ref["C"][i]]) for i in keep] + [([nb + j for j in f], 3) for f in visors[0]["F"]]
    center = np.array([0, HEIGHT / 2, 0])
    normals, good = [], 0
    for f, k in faces:
        p = V[f]
        n = np.cross(p[2] - p[0], p[3] - p[1]) if len(f) == 4 else np.cross(p[1] - p[0], p[2] - p[0])
        n = n / (np.linalg.norm(n) + 1e-12)
        if np.dot(n, p.mean(0) - center) > 0: good += 1
        normals.append(n)
    print("normales hacia afuera: %.0f%%" % (100.0 * good / len(faces)))

    # ---------- cuerpo muerto: mitad inferior + tapa + hueso ----------
    cut = CUT_FRAC * HEIGHT
    dv, dvi, df = [], {}, []                       # vertices, indice por posicion, caras (idx[4], clase, normal)
    def addv(p):
        key = tuple(int(round(c * 10000)) for c in p)
        if key not in dvi: dvi[key] = len(dv); dv.append([float(c) for c in p])
        return dvi[key]
    def emit(pts, k, n):                            # poligono convexo -> quads/triangulos (abanico)
        ids = [addv(p) for p in pts]
        if len(ids) == 4: df.append((ids, k, n))
        else:
            for i in range(1, len(ids) - 1): df.append(([ids[0], ids[i], ids[i + 1], ids[i + 1]], k, n))
    def clip(poly):                                 # Sutherland-Hodgman contra y <= cut
        out = []
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            ina, inb = a[1] <= cut, b[1] <= cut
            if ina: out.append(a)
            if ina != inb:
                t = (cut - a[1]) / (b[1] - a[1]); out.append(a + (b - a) * t)
        return out
    sect = {0: [], 1: []}                           # puntos de corte por clase (piel / mochila)
    for (f, k), n in zip(faces[:len(keep)], normals[:len(keep)]):
        poly = [V[i] for i in f]
        if all(p[1] <= cut + 1e-6 for p in poly): emit(poly, k, n); continue
        if all(p[1] > cut for p in poly): continue
        c = clip(poly)
        if len(c) >= 3:
            emit(c, k, n)
            if k in sect: sect[k] += [(p[0], p[2]) for p in c if abs(p[1] - cut) < 1e-6]
    def hull(P):                                    # envolvente convexa 2D (cadena monotona)
        P = sorted(set((round(x, 5), round(z, 5)) for x, z in P))
        if len(P) < 3: return P
        cr = lambda o, a, b: (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
        lo, up = [], []
        for p in P:
            while len(lo) >= 2 and cr(lo[-2], lo[-1], p) <= 0: lo.pop()
            lo.append(p)
        for p in reversed(P):
            while len(up) >= 2 and cr(up[-2], up[-1], p) <= 0: up.pop()
            up.append(p)
        return lo[:-1] + up[:-1]
    cap_ctr = None
    for k, kk in ((0, 5), (1, 1)):                  # tapa de la piel (clase 5 = interior oscuro) y de la mochila
        h = hull(sect[k])
        if len(h) >= 3:
            pts = [np.array([x, cut, z]) for x, z in h]
            nrm = np.cross(pts[1] - pts[0], pts[2] - pts[0])
            if nrm[1] < 0: pts.reverse()            # normal hacia +Y
            for i in range(1, len(pts) - 1): emit([pts[0], pts[i], pts[i + 1]], kk, np.array([0, 1.0, 0]))
            if k == 0: cap_ctr = np.mean([(x, z) for x, z in h], axis=0)
    # hueso: eje + dos nudos arriba
    bx, bz = cap_ctr if cap_ctr is not None else (0.0, 0.0)
    def solid(poly_pts, ctr, k=4):                  # emite una cara con normal hacia afuera del centro 'ctr'
        p = [np.array(q) for q in poly_pts]
        nrm = np.cross(p[2] - p[0], p[3 if len(p) > 3 else 2] - p[1]) if len(p) > 3 else np.cross(p[1] - p[0], p[2] - p[0])
        L = np.linalg.norm(nrm)
        if L < 1e-9: return
        nrm = nrm / L
        if np.dot(nrm, np.mean(p, 0) - ctr) < 0: p.reverse(); nrm = -nrm
        emit(p, k, nrm)
    import math
    SEG, R_SHAFT, H_SHAFT = 8, 0.085, 0.50 * HEIGHT / 1.8
    top = cut + H_SHAFT
    ctr_s = np.array([bx, cut + H_SHAFT / 2, bz])
    for i in range(SEG):                            # eje: prisma octogonal (sin tapa inferior; arriba lo tapan los nudos)
        a0, a1 = 2 * math.pi * i / SEG, 2 * math.pi * (i + 1) / SEG
        c = lambda a, y: [bx + math.cos(a) * R_SHAFT, y, bz + math.sin(a) * R_SHAFT]
        solid([c(a0, cut - 0.02), c(a1, cut - 0.02), c(a1, top), c(a0, top)], np.array([bx, (cut + top) / 2, bz]))
    def knob(cx, cy, cz, r, seg=8, rings=4):        # esfera de pocos poligonos
        ctr = np.array([cx, cy, cz])
        def pt(i, j):
            th = math.pi * j / rings; ph = 2 * math.pi * i / seg
            return [cx + r * math.sin(th) * math.cos(ph), cy + r * math.cos(th), cz + r * math.sin(th) * math.sin(ph)]
        for j in range(rings):
            for i in range(seg):
                q = [pt(i, j), pt(i + 1, j), pt(i + 1, j + 1), pt(i, j + 1)]
                if j == 0: q = [q[0], q[2], q[3]]    # polo: triangulo
                elif j == rings - 1: q = [q[0], q[1], q[2]]
                solid(q, ctr)
    kr = 0.13 * HEIGHT / 1.8
    knob(bx - 0.10 * HEIGHT / 1.8, top + 0.03, bz, kr); knob(bx + 0.10 * HEIGHT / 1.8, top + 0.03, bz, kr)
    print("cuerpo muerto: corte a %.2f m, vertices %d, caras %d (hueso incluido)" % (cut, len(dv), len(df)))

    # ---------- mitad superior (para cortes a la mitad, lengua y cuello) ----------
    tv, tvi, tf = [], {}, []
    def taddv(p):
        key = tuple(int(round(c * 10000)) for c in p)
        if key not in tvi: tvi[key] = len(tv); tv.append([float(c) for c in p])
        return tvi[key]
    def temit(pts, k, n):
        ids = [taddv(p) for p in pts]
        if len(ids) == 4: tf.append((ids, k, n))
        else:
            for i in range(1, len(ids) - 1): tf.append(([ids[0], ids[i], ids[i + 1], ids[i + 1]], k, n))
    def clip_up(poly):                              # Sutherland-Hodgman contra y >= cut
        out = []
        for i in range(len(poly)):
            a, b = poly[i], poly[(i + 1) % len(poly)]
            ina, inb = a[1] >= cut, b[1] >= cut
            if ina: out.append(a)
            if ina != inb:
                t = (cut - a[1]) / (b[1] - a[1]); out.append(a + (b - a) * t)
        return out
    sect_u = {0: [], 1: []}
    for (f, k), n in zip(faces, normals):           # incluye el visor (esta por encima del corte)
        poly = [V[i] for i in f]
        if all(p[1] >= cut - 1e-6 for p in poly): temit(poly, k, n); continue
        if all(p[1] < cut for p in poly): continue
        c = clip_up(poly)
        if len(c) >= 3:
            temit(c, k, n)
            if k in sect_u: sect_u[k] += [(p[0], p[2]) for p in c if abs(p[1] - cut) < 1e-6]
    for k, kk in ((0, 5), (1, 1)):                  # tapa inferior de la mitad superior (mira hacia abajo)
        h = hull(sect_u[k])
        if len(h) >= 3:
            pts = [np.array([x, cut, z]) for x, z in h]
            nrm = np.cross(pts[1] - pts[0], pts[2] - pts[0])
            if nrm[1] > 0: pts.reverse()            # normal hacia -Y
            for i in range(1, len(pts) - 1): temit([pts[0], pts[i], pts[i + 1]], kk, np.array([0, -1.0, 0]))
    print("mitad superior: vertices %d, caras %d" % (len(tv), len(tf)))

    names = [n for n, _ in NAMED]
    f4 = lambda v: "%.4ff" % v
    o = ["/* GENERADO por tools/fbx2h.py - no editar a mano */\n#ifndef CREW_MODEL_H\n#define CREW_MODEL_H\n"]
    o.append("#define CM_NV %d\n#define CM_NF %d\n#define CM_NBODY %d\n#define CM_HEIGHT %s\n" % (len(V), len(faces), len(keep), f4(HEIGHT)))
    o.append("#define CM_HALFW %s   /* semiancho (eje X) para acostar al personaje */\n" % f4(float(max(abs(V[:, 0].min()), abs(V[:, 0].max())))))
    o.append("#define CM_NSKIN 7\n")
    o.append("static const char *CM_SKIN_NAME[CM_NSKIN] = {%s};\n" % ",".join('"%s"' % n for n in names))
    o.append("static const unsigned char CM_SKIN_RGB[CM_NSKIN][3] = {%s};\n" % ",".join(
        "{%d,%d,%d}" % tuple(LIFT.get(n, skins[n])) for n in names))
    o.append("static const unsigned char CM_KIND_RGB[6][3] = {{0,0,0},{%d,%d,%d},{%d,%d,%d},{%d,%d,%d},{%d,%d,%d},{0,0,0}}; /* 0 piel y 5 interior de corte = color de skin */\n" % (PACK + RIM + VISOR + BONE))
    o.append("static const float CM_V[CM_NV][3] = {\n" + ",".join("{%s,%s,%s}" % tuple(f4(x) for x in v) for v in V) + "};\n")
    o.append("static const unsigned short CM_F[CM_NF][4] = {\n" + ",".join(
        "{%d,%d,%d,%d}" % ((f[0], f[1], f[2], f[3]) if len(f) == 4 else (f[0], f[1], f[2], f[2])) for f, _ in faces) + "};\n")
    o.append("static const unsigned char CM_K[CM_NF] = {" + ",".join(str(k) for _, k in faces) + "};\n")
    o.append("static const signed char CM_N[CM_NF][3] = {\n" + ",".join(
        "{%d,%d,%d}" % tuple(int(round(x * 127)) for x in n) for n in normals) + "};\n")
    o.append("#define CD_NV %d\n#define CD_NF %d\n" % (len(dv), len(df)))
    o.append("static const float CD_V[CD_NV][3] = {\n" + ",".join("{%s,%s,%s}" % tuple(f4(x) for x in v) for v in dv) + "};\n")
    o.append("static const unsigned short CD_F[CD_NF][4] = {\n" + ",".join("{%d,%d,%d,%d}" % tuple(i) for i, _, _ in df) + "};\n")
    o.append("static const unsigned char CD_K[CD_NF] = {" + ",".join(str(k) for _, k, _ in df) + "};\n")
    o.append("static const signed char CD_N[CD_NF][3] = {\n" + ",".join(
        "{%d,%d,%d}" % tuple(int(round(float(x) * 127)) for x in n) for _, _, n in df) + "};\n")
    o.append("#define CM_CUT %s   /* altura del corte (m) */\n" % f4(cut))
    o.append("#define CT_NV %d\n#define CT_NF %d\n" % (len(tv), len(tf)))
    o.append("static const float CT_V[CT_NV][3] = {\n" + ",".join("{%s,%s,%s}" % tuple(f4(x) for x in v) for v in tv) + "};\n")
    o.append("static const unsigned short CT_F[CT_NF][4] = {\n" + ",".join("{%d,%d,%d,%d}" % tuple(i) for i, _, _ in tf) + "};\n")
    o.append("static const unsigned char CT_K[CT_NF] = {" + ",".join(str(k) for _, k, _ in tf) + "};\n")
    o.append("static const signed char CT_N[CT_NF][3] = {\n" + ",".join(
        "{%d,%d,%d}" % tuple(int(round(float(x) * 127)) for x in n) for _, _, n in tf) + "};\n#endif\n")
    open(out, "w").write("".join(o))
    print("->", out, "| vertices:", len(V), "caras:", len(faces), "(%d cuerpo + %d visor)" % (len(keep), len(visors[0]["F"])))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    main(*sys.argv[1:])
