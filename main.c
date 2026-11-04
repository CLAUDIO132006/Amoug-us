// SOSPECHOSO - juego estilo "impostor" para PS2 (homebrew, PS2SDK + gsKit)
// Mapa 3D: The Skeld (AMONGUSSKELD_3D.obj -> skeld_map.h con tools/obj2h.py)
// Controles: D-pad / stick izq = mover | X (mantener) = usar tarea
//            O = reportar cuerpo | X en reunion = votar | START = iniciar
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <gsKit.h>
#include <dmaKit.h>
#include <libpad.h>

#include "skeld_map.h"   /* geometria 3D, pisos, colisiones y salas (generado) */

#define C(r,g,b) GS_SETREG_RGBAQ(r,g,b,0x80,0x00)
#define DT (1.0f/60.0f)

/* velocidades en metros por segundo (el mapa esta a escala real: 1 unidad = 1 m) */
#define PSPD   4.6f
#define AISPD  3.5f
#define IMPSPD 4.1f

static GSGLOBAL *gs;
static int SW = 640, SH = 448;

/* ---------- mini fuente 3x5 (ASCII 32..90, solo mayusculas) ---------- */
#define G(a,b,c,d,e) ((a<<12)|(b<<9)|(c<<6)|(d<<3)|e)
static const u16 FONT[59] = {
 G(0,0,0,0,0),G(2,2,2,0,2),G(5,5,0,0,0),G(5,7,5,7,5),G(3,6,7,3,6),G(5,1,2,4,5),G(2,5,2,5,3),G(2,2,0,0,0),
 G(1,2,2,2,1),G(4,2,2,2,4),G(0,5,2,5,0),G(0,2,7,2,0),G(0,0,0,2,4),G(0,0,7,0,0),G(0,0,0,0,2),G(1,1,2,4,4),
 G(7,5,5,5,7),G(2,6,2,2,7),G(7,1,7,4,7),G(7,1,7,1,7),G(5,5,7,1,1),G(7,4,7,1,7),G(7,4,7,5,7),G(7,1,1,1,1),G(7,5,7,5,7),G(7,5,7,1,7),
 G(0,2,0,2,0),G(0,2,0,2,4),G(1,2,4,2,1),G(0,7,0,7,0),G(4,2,1,2,4),G(7,1,3,0,2),G(7,5,7,4,7),
 G(2,5,7,5,5),G(6,5,6,5,6),G(3,4,4,4,3),G(6,5,5,5,6),G(7,4,6,4,7),G(7,4,6,4,4),G(3,4,5,5,3),G(5,5,7,5,5),G(7,2,2,2,7),
 G(1,1,1,5,2),G(5,5,6,5,5),G(4,4,4,4,7),G(5,7,7,5,5),G(6,5,5,5,5),G(2,5,5,5,2),G(6,5,6,4,4),G(2,5,5,3,1),G(6,5,6,5,5),
 G(3,4,2,1,6),G(7,2,2,2,2),G(5,5,5,5,7),G(5,5,5,5,2),G(5,5,7,7,5),G(5,5,2,5,5),G(5,5,2,2,2),G(7,1,2,4,7)
};

static void rect(float x1, float y1, float x2, float y2, u64 c) {
  if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
  if (x2 > SW) x2 = SW; if (y2 > SH) y2 = SH;
  if (x2 <= x1 || y2 <= y1) return;
  gsKit_prim_sprite(gs, x1, y1, x2, y2, 1, c);
}
static void text(float x, float y, int s, u64 col, const char *t) {
  for (; *t; t++, x += 4 * s) {
    int ch = *t; if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 90) continue;
    u16 g = FONT[ch - 32];
    for (int r = 0; r < 5; r++)
      for (int k = 0; k < 3; k++)
        if (g & (1 << (14 - r * 3 - k))) rect(x + k * s, y + r * s, x + (k + 1) * s, y + (r + 1) * s, col);
  }
}

/* ---------- mapa ----------
   Plano del suelo: x = este(+)/oeste(-), "y" = z del OBJ = sur(+)/norte(-).
   Salas (en el mismo orden que SK_ROOM / SK_STN de skeld_map.h):          */
#define NR SK_NROOM
static const char *NM[NR] = {
  "MOTOR SUPERIOR","MOTOR INFERIOR","REACTOR","SEGURIDAD","ENFERMERIA","CAFETERIA","ADMIN",
  "ALMACEN","ELECTRICO","COMUNICACION","ARMAS","OXIGENO","ESCUDOS","NAVEGACION"
};
static const char *TN[NR] = {
  "ALINEAR MOTOR","ALINEAR MOTOR","ARRANCAR REACTOR","REVISAR CAMARAS","ESCANEAR MUESTRA","DESCARGAR DATOS","ESCANEAR TARJETA",
  "ORDENAR CAJAS","CONECTAR CABLES","CALIBRAR SENAL","DISPARAR ASTEROIDES","LIMPIAR FILTRO","ACTIVAR ESCUDOS","CALIBRAR RUMBO"
};
#define CAFE 5
static const char *NAMES[6] = {"TU","ROJO","AZUL","VERDE","AMARILLO","NARANJA"};
static const u8 COL[6][3] = {{223,234,255},{224,51,74},{47,107,255},{47,210,122},{242,216,58},{255,138,42}};

/* colision: el cuerpo es un cuadrado de lado 2*r; debe estar sobre piso y fuera de props */
static int walk(float x, float z) {
  static const float ox[4] = {-1, 1, -1, 1}, oz[4] = {-1, -1, 1, 1};
  const float r = SK_PLAYER_R;
  for (int k = 0; k < 4; k++) {
    float px = x + ox[k] * r, pz = z + oz[k] * r; int in = 0;
    for (int i = 0; i < SK_NFLOOR; i++)
      if (px >= SK_FL[i][0] && px <= SK_FL[i][2] && pz >= SK_FL[i][1] && pz <= SK_FL[i][3]) { in = 1; break; }
    if (!in) return 0;
  }
  for (int i = 0; i < SK_NCOL; i++)
    if (x + r > SK_COL[i][0] - .05f && x - r < SK_COL[i][2] + .05f && z + r > SK_COL[i][1] - .05f && z - r < SK_COL[i][3] + .05f) return 0;
  return 1;
}
static int room_of(float x, float z) {
  for (int i = 0; i < NR; i++)
    if (x >= SK_ROOM[i][0] && x <= SK_ROOM[i][2] && z >= SK_ROOM[i][1] && z <= SK_ROOM[i][3]) return i;
  return -1;
}
static int line_clear(float x0, float z0, float x1, float z1) {
  float dx = x1 - x0, dz = z1 - z0, d = hypotf(dx, dz); int n = (int)(d / 0.3f) + 1;
  for (int i = 1; i <= n; i++) { float t = (float)i / n; if (!walk(x0 + dx * t, z0 + dz * t)) return 0; }
  return 1;
}
static void find_free(float *x, float *z) {
  if (walk(*x, *z)) return;
  for (int r = 1; r < 50; r++)
    for (int a = 0; a < 12; a++) {
      float px = *x + cosf(a * 0.5236f) * r * 0.3f, pz = *z + sinf(a * 0.5236f) * r * 0.3f;
      if (walk(px, pz)) { *x = px; *z = pz; return; }
    }
}

/* ---------- navegacion: rejilla de celdas + BFS ---------- */
#define NCELL (SK_GW * SK_GH)
#define MAXP 512
static u8 nav[SK_GH][SK_GW];
static u16 bfs_prev[NCELL], bfs_q[NCELL], bfs_stamp[NCELL], bfs_gen;

static void nav_init(void) {
  for (int j = 0; j < SK_GH; j++)
    for (int i = 0; i < SK_GW; i++) nav[j][i] = walk(SK_GX0 + (i + .5f) * SK_CELL, SK_GZ0 + (j + .5f) * SK_CELL);
}
static void wp(u16 n, float *x, float *z) { *x = SK_GX0 + (n % SK_GW + .5f) * SK_CELL; *z = SK_GZ0 + (n / SK_GW + .5f) * SK_CELL; }
static int cell_near(float x, float z) {
  int i = (int)floorf((x - SK_GX0) / SK_CELL), j = (int)floorf((z - SK_GZ0) / SK_CELL);
  if (i < 0) i = 0; if (j < 0) j = 0; if (i >= SK_GW) i = SK_GW - 1; if (j >= SK_GH) j = SK_GH - 1;
  if (nav[j][i]) return j * SK_GW + i;
  for (int r = 1; r <= 4; r++)
    for (int dj = -r; dj <= r; dj++)
      for (int di = -r; di <= r; di++) {
        int a = i + di, b = j + dj;
        if (a >= 0 && b >= 0 && a < SK_GW && b < SK_GH && nav[b][a]) return b * SK_GW + a;
      }
  return -1;
}
static int find_path(float sx, float sz, float tx, float tz, u16 *out) {
  static const int DI[8] = {1, -1, 0, 0, 1, 1, -1, -1}, DJ[8] = {0, 0, 1, -1, 1, -1, 1, -1};
  int s = cell_near(sx, sz), t = cell_near(tx, tz);
  if (s < 0 || t < 0) return 0;
  if (++bfs_gen == 0) { memset(bfs_stamp, 0, sizeof bfs_stamp); bfs_gen = 1; }
  int h = 0, tl = 0; bfs_q[tl++] = s; bfs_stamp[s] = bfs_gen; bfs_prev[s] = s;
  while (h < tl) {
    int n = bfs_q[h++]; if (n == t) break;
    int i = n % SK_GW, j = n / SK_GW;
    for (int k = 0; k < 8; k++) {
      int a = i + DI[k], b = j + DJ[k];
      if (a < 0 || b < 0 || a >= SK_GW || b >= SK_GH || !nav[b][a]) continue;
      if (k >= 4 && !(nav[j][a] && nav[b][i])) continue;      /* sin cortar esquinas */
      int m = b * SK_GW + a; if (bfs_stamp[m] == bfs_gen) continue;
      bfs_stamp[m] = bfs_gen; bfs_prev[m] = n; bfs_q[tl++] = m;
    }
  }
  if (bfs_stamp[t] != bfs_gen) return 0;
  u16 tmp[MAXP]; int l = 0;
  for (int n = t; n != s; n = bfs_prev[n]) { if (l >= MAXP) return 0; tmp[l++] = n; }
  for (int i = 0; i < l; i++) out[i] = tmp[l - 1 - i];
  return l;
}

/* ---------- estado ---------- */
typedef struct {
  float x, y, wait, rt;                 /* y = coordenada z del mundo */
  int f, alive, imp, goal, cur, rl, ri, tn[3], td[3];
  u16 route[MAXP];
} Ch;
typedef struct { float x, y; int i; } Body;
enum { TITLE, PLAY, MEET, MRES, END };
static Ch ch[6];
static Body bodies[6];
static int nb, state = TITLE, done, total = 15, endwin, witness, sus, cur, opts[6], nopts, mres_t, ejected;
static float cd, useT, gt, camx, camz;
static int nearTask, nearBody;
static char msg[80], msg2[80];
static u32 frames;

static float frand(void) { return rand() / (float)RAND_MAX; }

static void start(void) {
  srand(frames);
  int imp = 1 + rand() % 5;
  float cx = (SK_ROOM[CAFE][0] + SK_ROOM[CAFE][2]) / 2, cz = (SK_ROOM[CAFE][1] + SK_ROOM[CAFE][3]) / 2;
  for (int i = 0; i < 6; i++) {
    Ch *c = &ch[i]; memset(c, 0, sizeof(Ch));
    c->x = cx + cosf(i * 1.05f) * 2.6f; c->y = cz + sinf(i * 1.05f) * 2.2f; find_free(&c->x, &c->y);
    c->f = 1; c->alive = 1; c->imp = (i == imp);
    c->goal = -1; c->cur = -1; c->wait = frand() * 2;
    int a[NR]; for (int k = 0; k < NR; k++) a[k] = k;
    for (int k = NR - 1; k > 0; k--) { int j = rand() % (k + 1), t = a[k]; a[k] = a[j]; a[j] = t; }
    for (int k = 0; k < 3; k++) { c->tn[k] = a[k]; c->td[k] = 0; }
  }
  nb = 0; done = 0; cd = 20; useT = 0; witness = -1; gt = 0; state = PLAY;
  camx = ch[0].x; camz = ch[0].y;
}
static void finish(int win, const char *m) {
  if (state == END) return;
  endwin = win; state = END;
  snprintf(msg, sizeof msg, "%s", m);
  int im = 0; for (int i = 0; i < 6; i++) if (ch[i].imp) im = i;
  snprintf(msg2, sizeof msg2, "EL IMPOSTOR ERA %s", NAMES[im]);
}
static float steer(Ch *c, float tx, float tz, float sp) {
  float dx = tx - c->x, dz = tz - c->y, d = hypotf(dx, dz);
  if (d < .01f) return 0;
  float s = sp * DT; if (s > d) s = d;
  float mx = dx / d * s, mz = dz / d * s;
  if (walk(c->x + mx, c->y)) c->x += mx;
  if (walk(c->x, c->y + mz)) c->y += mz;
  if (fabsf(mx) > .002f) c->f = mx > 0 ? 1 : -1;
  return d;
}
static void follow(Ch *c, float sp) {
  if (c->ri >= c->rl) return;
  for (int look = 0; look < 8 && c->ri + 1 < c->rl; look++) {     /* atajo si hay linea libre */
    float nx, nz; wp(c->route[c->ri + 1], &nx, &nz);
    if (!line_clear(c->x, c->y, nx, nz)) break;
    c->ri++;
  }
  float tx, tz; wp(c->route[c->ri], &tx, &tz);
  if (steer(c, tx, tz, sp) < .3f) c->ri++;
}
static int same_area(float x1, float z1, float x2, float z2) {
  int a = room_of(x1, z1), b = room_of(x2, z2);
  return a == b && (a >= 0 || line_clear(x1, z1, x2, z2));
}
static void kill(Ch *k, Ch *t) {
  t->alive = 0; bodies[nb].x = t->x; bodies[nb].y = t->y; bodies[nb].i = (int)(t - ch); nb++;
  cd = 18; k->wait = 1.2f; k->goal = -1; k->cur = -1; k->rl = k->ri = 0;
  if (t == &ch[0]) { char b[80]; snprintf(b, sizeof b, "%s TE ELIMINO", NAMES[k - ch]); finish(0, b); }
  else if (ch[0].alive && hypotf(k->x - ch[0].x, k->y - ch[0].y) < 9.f && same_area(k->x, k->y, ch[0].x, ch[0].y)) witness = k - ch;
}
static void ai(Ch *c) {
  if (c->wait > 0) { c->wait -= DT; return; }
  if (c->imp && cd <= 0) {
    Ch *t = NULL; float b = 1e9f;
    for (int i = 0; i < 6; i++) if (ch[i].alive && !ch[i].imp) { float d = hypotf(c->x - ch[i].x, c->y - ch[i].y); if (d < b) { b = d; t = &ch[i]; } }
    if (t) {
      c->goal = -1; c->cur = -1;
      if (b < 1.8f) { kill(c, t); return; }
      if (b < 7.f && line_clear(c->x, c->y, t->x, t->y)) { steer(c, t->x, t->y, IMPSPD); c->rl = c->ri = 0; return; }
      c->rt -= DT;
      if (c->rt <= 0 || c->ri >= c->rl) { c->rt = .6f; c->rl = find_path(c->x, c->y, t->x, t->y, c->route); c->ri = 0; }
      if (c->ri < c->rl) follow(c, IMPSPD); else steer(c, t->x, t->y, IMPSPD);
      return;
    }
  }
  if (c->goal < 0) {
    c->cur = -1;
    for (int k = 0; k < 3; k++) if (!c->td[k]) { c->cur = k; break; }
    c->goal = c->cur >= 0 ? c->tn[c->cur] : rand() % NR;
    c->rl = find_path(c->x, c->y, SK_STN[c->goal][0], SK_STN[c->goal][1], c->route); c->ri = 0;
  }
  if (c->ri < c->rl) follow(c, AISPD);
  else if (steer(c, SK_STN[c->goal][0], SK_STN[c->goal][1], AISPD) < .4f) {
    c->wait = 3;
    if (c->cur >= 0) { c->td[c->cur] = 1; if (!c->imp) done++; }
    c->goal = -1; c->cur = -1;
  }
}

/* ---------- reunion ---------- */
static void meeting(int by, int victim) {
  if (state != PLAY) return;
  nb = 0; state = MEET; cur = 0; (void)by;
  int imp = 0, oth[5], no = 0;
  for (int i = 0; i < 6; i++) { if (ch[i].imp) imp = i; else if (i && ch[i].alive) oth[no++] = i; }
  sus = (frand() < .65f || !no) ? imp : oth[rand() % no];
  snprintf(msg, sizeof msg, "%s REPORTO A %s", by == 0 ? "TU" : NAMES[by], NAMES[victim]);
  if (witness >= 0) snprintf(msg2, sizeof msg2, "VISTE A %s ELIMINARLO", NAMES[witness]);
  else snprintf(msg2, sizeof msg2, "VIERON A %s CERCA DEL CUERPO", NAMES[sus]);
  nopts = 0; for (int i = 1; i < 6; i++) if (ch[i].alive) opts[nopts++] = i;
  opts[nopts++] = -1;
}
static void vote(int id) {
  int v[7] = {0};
  v[id + 1]++;
  for (int i = 1; i < 6; i++) {
    if (!ch[i].alive) continue;
    float r = frand(); int pick = -1;
    if (ch[i].imp) { int cr[6], n = 0; for (int j = 0; j < 6; j++) if (ch[j].alive && !ch[j].imp) cr[n++] = j; pick = r < .25f ? -1 : cr[rand() % n]; }
    else if (r < .55f && sus != i) pick = sus;
    else if (r < .8f) { int o[6], n = 0; for (int j = 0; j < 6; j++) if (ch[j].alive && j != i) o[n++] = j; pick = o[rand() % n]; }
    v[pick + 1]++;
  }
  int top = -1, mx = 0, tie = 0;
  for (int k = 0; k < 7; k++) { if (v[k] > mx) { mx = v[k]; top = k - 1; tie = 0; } else if (v[k] == mx) tie = 1; }
  ejected = (!tie && top >= 0) ? top : -1;
  if (ejected >= 0) snprintf(msg, sizeof msg, "%s FUE EXPULSADO", NAMES[ejected]), snprintf(msg2, sizeof msg2, ch[ejected].imp ? "ERA EL IMPOSTOR!" : "NO ERA EL IMPOSTOR");
  else snprintf(msg, sizeof msg, "NADIE FUE EXPULSADO"), msg2[0] = 0;
  mres_t = 200; state = MRES;
}
static void after_vote(void) {
  if (ejected >= 0) ch[ejected].alive = 0;
  if (ejected >= 0 && ch[ejected].imp) { finish(1, "EXPULSASTE AL IMPOSTOR"); return; }
  if (ejected == 0) { finish(0, "TE EXPULSARON"); return; }
  int crew = 0; for (int i = 0; i < 6; i++) if (ch[i].alive && !ch[i].imp) crew++;
  if (crew <= 1) { finish(0, "GANO EL IMPOSTOR"); return; }
  float cx = (SK_ROOM[CAFE][0] + SK_ROOM[CAFE][2]) / 2, cz = (SK_ROOM[CAFE][1] + SK_ROOM[CAFE][3]) / 2;
  int k = 0;
  for (int i = 0; i < 6; i++) if (ch[i].alive) {
    ch[i].x = cx + cosf(k * 1.2f) * 2.4f; ch[i].y = cz + sinf(k * 1.2f) * 1.8f; find_free(&ch[i].x, &ch[i].y);
    ch[i].goal = -1; ch[i].ri = ch[i].rl = 0; ch[i].wait = frand() * 2; k++;
  }
  cd = 15; witness = -1; state = PLAY;
  camx = ch[0].x; camz = ch[0].y;
}

/* ---------- entrada ---------- */
static char padBuf[256] __attribute__((aligned(64)));
static u32 btns, pressed, oldb;
static float ax, ay;
static void readpad(void) {
  struct padButtonStatus b; int st = padGetState(0, 0);
  btns = 0; ax = ay = 0;
  if (st == PAD_STATE_STABLE || st == PAD_STATE_FINDCTP1) {
    padRead(0, 0, &b); btns = 0xffff ^ b.btns;
    if ((b.mode >> 4) == 7) {
      ax = (b.ljoy_h - 128) / 128.0f; ay = (b.ljoy_v - 128) / 128.0f;
      if (fabsf(ax) < .25f) ax = 0; if (fabsf(ay) < .25f) ay = 0;
    }
  }
  pressed = btns & ~oldb; oldb = btns;
}

static void update(void) {
  Ch *p = &ch[0]; gt += DT; cd -= DT; nearTask = -1; nearBody = -1;
  if (p->alive) {
    float mx = ax + ((btns & PAD_RIGHT) ? 1 : 0) - ((btns & PAD_LEFT) ? 1 : 0);
    float my = ay + ((btns & PAD_DOWN) ? 1 : 0) - ((btns & PAD_UP) ? 1 : 0);
    float m = hypotf(mx, my); if (m > 1) { mx /= m; my /= m; }
    float nx = p->x + mx * PSPD * DT, ny = p->y + my * PSPD * DT;
    if (walk(nx, p->y)) p->x = nx; if (walk(p->x, ny)) p->y = ny;
    if (fabsf(mx) > .1f) p->f = mx > 0 ? 1 : -1;
    for (int k = 0; k < 3; k++)
      if (!p->td[k] && hypotf(p->x - SK_STN[p->tn[k]][0], p->y - SK_STN[p->tn[k]][1]) < 1.8f) nearTask = k;
    if (nearTask >= 0 && (btns & PAD_CROSS) && m < .25f) {
      useT += DT;
      if (useT >= 2) { p->td[nearTask] = 1; done++; useT = 0; }
    } else { useT -= DT * 2; if (useT < 0) useT = 0; }
    for (int i = 0; i < nb; i++) if (hypotf(p->x - bodies[i].x, p->y - bodies[i].y) < 3.2f) nearBody = i;
    if ((pressed & PAD_CIRCLE) && nearBody >= 0) { meeting(0, bodies[nearBody].i); return; }
  }
  for (int i = 1; i < 6; i++) if (ch[i].alive) ai(&ch[i]);
  if (state != PLAY) return;
  for (int i = 1; i < 6; i++)
    if (ch[i].alive && !ch[i].imp)
      for (int b = 0; b < nb; b++)
        if (state == PLAY && hypotf(ch[i].x - bodies[b].x, ch[i].y - bodies[b].y) < 5.5f && frand() < DT * 1.2f) { meeting(i, bodies[b].i); return; }
  int crew = 0; for (int i = 0; i < 6; i++) if (ch[i].alive && !ch[i].imp) crew++;
  if (done >= total) finish(1, "TAREAS COMPLETADAS");
  else if (crew <= 1) finish(0, "GANO EL IMPOSTOR");
}

/* ---------- camara 3D y proyeccion ---------- */
static float vf[3], vr[3], vu[3], vc[3];
#define FOC (SW * 0.81f)

static void set_view(float tx, float ty, float tz, float yaw, float pitch, float dist) {
  float cy = cosf(yaw), sy = sinf(yaw), cp = cosf(pitch), sp = sinf(pitch);
  vf[0] = sy * cp; vf[1] = -sp; vf[2] = -cy * cp;      /* hacia donde mira      */
  vu[0] = sy * sp; vu[1] = cp;  vu[2] = -cy * sp;      /* arriba de la camara   */
  vr[0] = cy;      vr[1] = 0;   vr[2] = sy;            /* derecha de la camara  */
  vc[0] = tx - vf[0] * dist; vc[1] = ty - vf[1] * dist; vc[2] = tz - vf[2] * dist;
}
static int proj(float X, float Y, float Z, float *sx, float *sy, float *zc) {
  float dx = X - vc[0], dy = Y - vc[1], dz = Z - vc[2];
  float z = dx * vf[0] + dy * vf[1] + dz * vf[2];
  if (z < 1.f) return 0;
  float xv = dx * vr[0] + dy * vr[1] + dz * vr[2], yv = dx * vu[0] + dy * vu[1] + dz * vu[2];
  float k = FOC / z;
  *sx = SW * .5f + xv * k; *sy = SH * .5f - yv * k; *zc = z;
  return 1;
}

/* ---------- dibujo ---------- */
/* personaje como "billboard": s = pixeles por unidad de sprite; (x,y) = pies */
static void capsule(float x, float y, int i, int f, int dead, float s) {
  u64 c = C(COL[i][0], COL[i][1], COL[i][2]);
  y -= 9 * s;
  if (dead) { rect(x - 16*s, y - 8*s, x + 14*s, y + 12*s, c); rect(x + (4*f - 5)*s, y - 6*s, x + (4*f + 5)*s, y + 2*s, C(190,230,255)); rect(x - 2*s, y - 16*s, x + 2*s, y - 8*s, C(255,255,255)); return; }
  rect(x - 14*s, y + 8*s, x + 14*s, y + 12*s, C(5,10,30));
  rect(x - f*17*s - 4*s, y - 18*s, x - f*17*s + 4*s, y - 2*s, c);
  rect(x - 11*s, y - 24*s, x + 11*s, y + 4*s, c);
  rect(x - 9*s, y + 4*s, x - 2*s, y + 11*s, c); rect(x + 2*s, y + 4*s, x + 9*s, y + 11*s, c);
  rect(x + (f*4 - 8)*s, y - 20*s, x + (f*4 + 8)*s, y - 10*s, C(190,230,255));
}

static void draw_char(int i) {
  Ch *c = &ch[i]; float x, y, z;
  if (!proj(c->x, 0, c->y, &x, &y, &z)) return;
  float s = FOC / z * .04f;
  capsule(x, y, i, c->f, 0, s);
  text(x - strlen(NAMES[i]) * 2, y - 33 * s - 16, 1, i ? C(255,210,58) : C(255,255,255), NAMES[i]);
}

#define MAXI 1600
enum { IT_FACE, IT_BODY, IT_CHAR, IT_TASK };
static float keyz[MAXI], ipos[MAXI][8];
static u16 ord[MAXI], iidx[MAXI];
static u8 ityp[MAXI];
static int cmp_far_first(const void *a, const void *b) {
  float d = keyz[*(const u16 *)b] - keyz[*(const u16 *)a];
  return d > 0 ? 1 : (d < 0 ? -1 : 0);
}
static const float NTAB[5][3] = {{0,1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};

/* dibuja el mapa 3D (pisos -> paredes/props/personajes ordenados de lejos a cerca) */
static void draw_world(float tx, float tz, float cull, int chars) {
  float sx[4], sy[4], zc[4];
  /* 1) pisos: nunca se solapan entre si, van primero */
  for (int t = 0; t < SK_NTILE; t++) {
    const SkTile *T = &SK_TILE[t];
    if (fabsf((T->x0 + T->x1) * .5f - tx) > cull || fabsf((T->z0 + T->z1) * .5f - tz) > cull) continue;
    int ok = proj(T->x0, 0, T->z0, &sx[0], &sy[0], &zc[0]) & proj(T->x1, 0, T->z0, &sx[1], &sy[1], &zc[1]) &
             proj(T->x0, 0, T->z1, &sx[2], &sy[2], &zc[2]) & proj(T->x1, 0, T->z1, &sx[3], &sy[3], &zc[3]);
    if (!ok) continue;
    float mnx = sx[0], mxx = sx[0], mny = sy[0], mxy = sy[0];
    for (int k = 1; k < 4; k++) { if (sx[k] < mnx) mnx = sx[k]; if (sx[k] > mxx) mxx = sx[k]; if (sy[k] < mny) mny = sy[k]; if (sy[k] > mxy) mxy = sy[k]; }
    if (mxx < -200 || mnx > SW + 200 || mxy < -200 || mny > SH + 200 || mxx - mnx > 2500 || mxy - mny > 2500) continue;
    gsKit_prim_quad(gs, sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], sx[3], sy[3], 1, C(T->r, T->g, T->b));
  }
  /* 2) lista de objetos a ordenar */
  int n = 0;
  for (int f = 0; f < SK_NFACE && n < MAXI - 20; f++) {
    const SkFace *F = &SK_FACE[f];
    float cx = (F->v[0][0] + F->v[1][0] + F->v[2][0] + F->v[3][0]) * .25f;
    float cy = (F->v[0][1] + F->v[1][1] + F->v[2][1] + F->v[3][1]) * .25f;
    float cz = (F->v[0][2] + F->v[1][2] + F->v[2][2] + F->v[3][2]) * .25f;
    if (fabsf(cx - tx) > cull || fabsf(cz - tz) > cull) continue;
    const float *nn = NTAB[F->n];
    if ((vc[0] - cx) * nn[0] + (vc[1] - cy) * nn[1] + (vc[2] - cz) * nn[2] <= 0) continue;   /* cara trasera */
    int ok = 1; float mnx = 1e9f, mxx = -1e9f, mny = 1e9f, mxy = -1e9f;
    for (int k = 0; k < 4; k++) {
      ok &= proj(F->v[k][0], F->v[k][1], F->v[k][2], &sx[k], &sy[k], &zc[k]);
      if (sx[k] < mnx) mnx = sx[k]; if (sx[k] > mxx) mxx = sx[k]; if (sy[k] < mny) mny = sy[k]; if (sy[k] > mxy) mxy = sy[k];
    }
    if (!ok || mxx < -200 || mnx > SW + 200 || mxy < -200 || mny > SH + 200 || mxx - mnx > 2500 || mxy - mny > 2500) continue;
    for (int k = 0; k < 4; k++) { ipos[n][k * 2] = sx[k]; ipos[n][k * 2 + 1] = sy[k]; }
    float dd = (cx - vc[0]) * vf[0] + (cy - vc[1]) * vf[1] + (cz - vc[2]) * vf[2];
    keyz[n] = dd; ityp[n] = IT_FACE; iidx[n] = f; n++;
  }
  if (chars) {
    float x, y, z;
    for (int i = 0; i < nb && n < MAXI; i++)
      if (proj(bodies[i].x, .3f, bodies[i].y, &x, &y, &z)) { keyz[n] = z; ityp[n] = IT_BODY; iidx[n] = i; n++; }
    for (int i = 1; i < 6 && n < MAXI; i++)      /* el jugador (0) se dibuja al final: siempre visible */
      if (ch[i].alive && proj(ch[i].x, .5f, ch[i].y, &x, &y, &z)) { keyz[n] = z; ityp[n] = IT_CHAR; iidx[n] = i; n++; }
    for (int k = 0; k < 3 && n < MAXI; k++)
      if (proj(SK_STN[ch[0].tn[k]][0], 1.3f, SK_STN[ch[0].tn[k]][1], &x, &y, &z)) { keyz[n] = z; ityp[n] = IT_TASK; iidx[n] = k; n++; }
  }
  for (int i = 0; i < n; i++) ord[i] = i;
  qsort(ord, n, sizeof(u16), cmp_far_first);
  /* 3) dibujar de lejos a cerca */
  for (int o = 0; o < n; o++) {
    int i = ord[o];
    switch (ityp[i]) {
    case IT_FACE: {
      const SkFace *F = &SK_FACE[iidx[i]]; const float *q = ipos[i];
      gsKit_prim_quad(gs, q[0], q[1], q[2], q[3], q[6], q[7], q[4], q[5], 1, C(F->r, F->g, F->b));
      break; }
    case IT_BODY: {
      Body *b = &bodies[iidx[i]]; float x, y, z;
      if (proj(b->x, 0, b->y, &x, &y, &z)) capsule(x, y, b->i, 1, 1, FOC / z * .04f);
      break; }
    case IT_CHAR: draw_char(iidx[i]); break;
    case IT_TASK: {
      int k = iidx[i]; float x, y, z, bob = .12f * sinf(gt * 4);
      if (proj(SK_STN[ch[0].tn[k]][0], 1.3f + bob, SK_STN[ch[0].tn[k]][1], &x, &y, &z)) {
        float h = FOC / z * .22f;
        if (ch[0].td[k]) rect(x - h, y - h, x + h, y + h, C(42,122,90));
        else { int g = 160 + (int)(95 * sinf(gt * 5)); if (g > 255) g = 255; rect(x - h, y - h, x + h, y + h, C(255, g, 58)); }
      }
      break; }
    }
  }
  if (chars && ch[0].alive) draw_char(0);
}

static void draw(void) {
  gsKit_clear(gs, C(2,6,23));
  if (state == TITLE) {
    set_view(0, 0, 0, gt * .25f, .8f, 70.f);                 /* camara orbitando el mapa */
    draw_world(0, 0, 80.f, 0);
    rect(90, 36, SW - 90, 150, C(2,6,23)); rect(120, 156, SW - 120, 186, C(2,6,23));
    text(110, 52, 8, C(150,190,255), "SOSPECHOSO");
    text(150, 164, 3, C(230,240,255), "ESTILO PS2 - HOMEBREW");
    rect(190, 330, SW - 190, 380, C(2,6,23));
    if (((int)(gt * 2)) & 1) text(215, 342, 4, C(255,210,58), "PULSA START");
    rect(50, 398, SW - 50, 424, C(2,6,23));
    text(70, 404, 2, C(160,190,255), "D-PAD MOVER  X USAR  O REPORTAR");
    return;
  }
  Ch *p = &ch[0];
  camx += (p->x - camx) * (DT * 8 > 1 ? 1 : DT * 8); camz += (p->y - camz) * (DT * 8 > 1 ? 1 : DT * 8);
  set_view(camx, .7f, camz, 0, 1.1f, 15.5f);                  /* ~63 grados, siguiendo al jugador */
  draw_world(camx, camz, 26.f, 1);

  float sx, sy, sz;
  if (nearTask >= 0 && proj(p->x, 0, p->y, &sx, &sy, &sz)) {
    float s = FOC / sz * .04f;
    text(sx - strlen(TN[p->tn[nearTask]]) * 2, sy - 33 * s - 30, 1, C(255,210,58), TN[p->tn[nearTask]]);
    if (useT > 0) rect(sx - 25, sy - 33 * s - 22, sx - 25 + 50 * useT / 2, sy - 33 * s - 18, C(255,210,58));
  }
  if (nearBody >= 0) text(240, SH - 30, 2, C(255,90,110), "O = REPORTAR");

  /* HUD: barra de progreso y tareas */
  rect(10, 10, 10 + 300, 24, C(91,151,255)); rect(12, 12, 12 + 296.0f * done / total, 22, C(109,255,176));
  for (int k = 0; k < 3; k++) { char b[48]; snprintf(b, sizeof b, "%s %s", p->td[k] ? "OK" : "--", TN[p->tn[k]]); text(10, 32 + k * 12, 1, p->td[k] ? C(109,255,176) : C(220,235,255), b); }
  { int r = room_of(p->x, p->y); const char *nm = r >= 0 ? NM[r] : "PASILLO"; text(SW - 10 - strlen(nm) * 4 * 2, SH - 24, 2, C(150,190,255), nm); }

  /* minimapa (esquina superior derecha, 2 px por metro) */
  { float mx0 = SW - 164, my0 = 10, sc = 2.f, ox = -37.f, oz = -17.f;
    rect(mx0 - 3, my0 - 3, mx0 + 156, my0 + 80, C(91,151,255)); rect(mx0 - 1, my0 - 1, mx0 + 154, my0 + 78, C(4,10,40));
    for (int i = 0; i < SK_NFLOOR; i++)
      rect(mx0 + (SK_FL[i][0] - ox) * sc, my0 + (SK_FL[i][1] - oz) * sc, mx0 + (SK_FL[i][2] - ox) * sc, my0 + (SK_FL[i][3] - oz) * sc, C(40,70,150));
    for (int k = 0; k < 3; k++) if (!p->td[k]) {
      float x = mx0 + (SK_STN[p->tn[k]][0] - ox) * sc, y = my0 + (SK_STN[p->tn[k]][1] - oz) * sc;
      rect(x - 2, y - 2, x + 2, y + 2, C(255,210,58));
    }
    for (int i = 0; i < nb; i++) { float x = mx0 + (bodies[i].x - ox) * sc, y = my0 + (bodies[i].y - oz) * sc; rect(x - 2, y - 2, x + 2, y + 2, C(255,60,80)); }
    { float x = mx0 + (p->x - ox) * sc, y = my0 + (p->y - oz) * sc; rect(x - 2, y - 2, x + 3, y + 3, C(255,255,255)); } }

  if (state == MEET || state == MRES) {
    rect(60, 40, SW - 60, SH - 40, C(10,25,90)); rect(64, 44, SW - 64, SH - 44, C(18,38,110));
    text(90, 62, 3, C(255,255,255), msg); text(90, 100, 2, C(190,215,255), msg2);
    if (state == MEET) {
      for (int i = 0; i < nopts; i++) {
        const char *nm = opts[i] < 0 ? "SALTAR VOTO" : NAMES[opts[i]];
        rect(110, 140 + i * 36, SW - 110, 170 + i * 36, i == cur ? C(255,210,58) : C(40,70,170));
        text(130, 148 + i * 36, 3, i == cur ? C(30,20,0) : C(255,255,255), nm);
      }
    }
  }
  if (state == END) {
    rect(60, 120, SW - 60, 330, C(10,25,90)); rect(64, 124, SW - 64, 326, C(18,38,110));
    text(110, 145, 6, endwin ? C(109,255,176) : C(255,90,110), endwin ? "VICTORIA" : "DERROTA");
    text(90, 215, 2, C(255,255,255), msg); text(90, 245, 2, C(190,215,255), msg2);
    text(150, 290, 3, C(255,210,58), "START PARA REINICIAR");
  }
}

int main(void) {
  SifInitRpc(0);
  SifLoadModule("rom0:SIO2MAN", 0, NULL);
  SifLoadModule("rom0:PADMAN", 0, NULL);
  gs = gsKit_init_global();
  gs->PSM = GS_PSM_CT32; gs->PSMZ = GS_PSMZ_16S;
  gs->ZBuffering = GS_SETTING_OFF; gs->DoubleBuffering = GS_SETTING_ON;
  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);
  gsKit_init_screen(gs);
  gsKit_mode_switch(gs, GS_ONESHOT);
  SW = gs->Width; SH = gs->Height;

  nav_init();
  padInit(0); padPortOpen(0, 0, padBuf);
  while (padGetState(0, 0) != PAD_STATE_STABLE) {}
  padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);

  for (;;) {
    frames++; readpad();
    switch (state) {
      case TITLE: gt += DT; if (pressed & PAD_START) start(); break;
      case PLAY: update(); break;
      case MEET:
        if (pressed & PAD_UP) cur = (cur + nopts - 1) % nopts;
        if (pressed & PAD_DOWN) cur = (cur + 1) % nopts;
        if (pressed & PAD_CROSS) vote(opts[cur]);
        break;
      case MRES: if (--mres_t <= 0) after_vote(); break;
      case END: if (pressed & PAD_START) start(); break;
    }
    draw();
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
  }
  return 0;
}
