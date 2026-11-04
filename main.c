// SOSPECHOSO - juego estilo "impostor" para PS2 (homebrew, PS2SDK + gsKit)
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

#define C(r,g,b) GS_SETREG_RGBAQ(r,g,b,0x80,0x00)
#define DT (1.0f/60.0f)

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

/* ---------- mapa ---------- */
static const int R[15][4] = {
 {40,40,260,200},{450,40,300,200},{900,40,260,200},{450,300,300,160},{450,520,300,200},{40,520,260,200},{900,520,260,200},
 {260,110,230,60},{710,110,230,60},{560,200,80,140},{560,420,80,140},{120,200,100,360},{980,200,100,360},{260,590,230,60},{710,590,230,60}
};
static const int NX[7] = {170,600,1030,600,600,170,1030};
static const int NY[7] = {140,140,140,380,620,620,620};
static const char *NM[7] = {"MOTORES","CAFETERIA","REACTOR","ALMACEN","SEGURIDAD","ELECTRICO","NAVEGACION"};
static const char *TN[7] = {"ALINEAR MOTOR","SERVIR COMBUSTIBLE","ESCANEAR TARJETA","ORDENAR CAJAS","REVISAR CAMARAS","CONECTAR CABLES","CALIBRAR RUMBO"};
static const int EG[8][2] = {{0,1},{1,2},{1,3},{3,4},{0,5},{2,6},{5,4},{4,6}};
static const char *NAMES[6] = {"TU","ROJO","AZUL","VERDE","AMARILLO","NARANJA"};
static const u8 COL[6][3] = {{223,234,255},{224,51,74},{47,107,255},{47,210,122},{242,216,58},{255,138,42}};

static int walk(float x, float y) {
  for (int i = 0; i < 15; i++)
    if (x > R[i][0] + 14 && x < R[i][0] + R[i][2] - 14 && y > R[i][1] + 14 && y < R[i][1] + R[i][3] - 14) return 1;
  return 0;
}
static int nn(float x, float y) {
  int b = 0; float d = 1e9f;
  for (int i = 0; i < 7; i++) { float k = hypotf(NX[i] - x, NY[i] - y); if (k < d) { d = k; b = i; } }
  return b;
}
static int path(int a, int b, int *out) {
  int prev[7], q[7], h = 0, t = 0, seen[7] = {0};
  q[t++] = a; seen[a] = 1; prev[a] = -1;
  while (h < t) {
    int n = q[h++]; if (n == b) break;
    for (int e = 0; e < 8; e++) {
      int m = -1;
      if (EG[e][0] == n) m = EG[e][1]; else if (EG[e][1] == n) m = EG[e][0];
      if (m >= 0 && !seen[m]) { seen[m] = 1; prev[m] = n; q[t++] = m; }
    }
  }
  int tmp[8], l = 0;
  for (int n = b; n != a && n >= 0; n = prev[n]) tmp[l++] = n;
  for (int i = 0; i < l; i++) out[i] = tmp[l - 1 - i];
  return l;
}

/* ---------- estado ---------- */
typedef struct {
  float x, y, wait, rt;
  int f, alive, imp, goal, cur, rl, ri, route[8], tn[3], td[3];
} Ch;
typedef struct { float x, y; int i; } Body;
enum { TITLE, PLAY, MEET, MRES, END };
static Ch ch[6];
static Body bodies[6];
static int nb, state = TITLE, done, total = 15, endwin, witness, sus, cur, opts[6], nopts, mres_t, ejected;
static float cd, useT, gt;
static int nearTask, nearBody;
static char msg[80], msg2[80];
static u32 frames;

static float frand(void) { return rand() / (float)RAND_MAX; }

static void start(void) {
  srand(frames);
  int imp = 1 + rand() % 5;
  for (int i = 0; i < 6; i++) {
    Ch *c = &ch[i]; memset(c, 0, sizeof(Ch));
    c->x = 600 + (i - 2.5f) * 26; c->y = 380; c->f = 1; c->alive = 1; c->imp = (i == imp);
    c->goal = -1; c->cur = -1; c->wait = frand() * 2;
    int a[7] = {0,1,2,3,4,5,6};
    for (int k = 6; k > 0; k--) { int j = rand() % (k + 1), t = a[k]; a[k] = a[j]; a[j] = t; }
    for (int k = 0; k < 3; k++) { c->tn[k] = a[k]; c->td[k] = 0; }
  }
  nb = 0; done = 0; cd = 20; useT = 0; witness = -1; gt = 0; state = PLAY;
}
static void finish(int win, const char *m) {
  if (state == END) return;
  endwin = win; state = END;
  snprintf(msg, sizeof msg, "%s", m);
  int im = 0; for (int i = 0; i < 6; i++) if (ch[i].imp) im = i;
  snprintf(msg2, sizeof msg2, "EL IMPOSTOR ERA %s", NAMES[im]);
}
static float steer(Ch *c, float tx, float ty, float sp) {
  float dx = tx - c->x, dy = ty - c->y, d = hypotf(dx, dy);
  if (d < 1) return 0;
  float s = sp * DT; if (s > d) s = d;
  float mx = dx / d * s, my = dy / d * s;
  if (walk(c->x + mx, c->y)) c->x += mx;
  if (walk(c->x, c->y + my)) c->y += my;
  if (fabsf(mx) > .1f) c->f = mx > 0 ? 1 : -1;
  return d;
}
static void follow(Ch *c, float sp) {
  int n = c->route[c->ri];
  if (steer(c, NX[n], NY[n], sp) < 8) c->ri++;
}
static void kill(Ch *k, Ch *t) {
  t->alive = 0; bodies[nb].x = t->x; bodies[nb].y = t->y; bodies[nb].i = (int)(t - ch); nb++;
  cd = 18; k->wait = 1.2f; k->goal = -1; k->cur = -1; k->rl = k->ri = 0;
  if (t == &ch[0]) { char b[80]; snprintf(b, sizeof b, "%s TE ELIMINO", NAMES[k - ch]); finish(0, b); }
  else if (ch[0].alive && hypotf(k->x - ch[0].x, k->y - ch[0].y) < 220 && nn(k->x, k->y) == nn(ch[0].x, ch[0].y)) witness = k - ch;
}
static void ai(Ch *c) {
  if (c->wait > 0) { c->wait -= DT; return; }
  if (c->imp && cd <= 0) {
    Ch *t = NULL; float b = 1e9f;
    for (int i = 0; i < 6; i++) if (ch[i].alive && !ch[i].imp) { float d = hypotf(c->x - ch[i].x, c->y - ch[i].y); if (d < b) { b = d; t = &ch[i]; } }
    if (t) {
      c->goal = -1; c->cur = -1;
      if (b < 40) { kill(c, t); return; }
      if (nn(c->x, c->y) == nn(t->x, t->y) || b < 150) { steer(c, t->x, t->y, 102); return; }
      c->rt -= DT; if (c->rt <= 0) { c->rt = 1; c->rl = path(nn(c->x, c->y), nn(t->x, t->y), c->route); c->ri = 0; }
      if (c->ri < c->rl) follow(c, 102); else steer(c, t->x, t->y, 102);
      return;
    }
  }
  if (c->goal < 0) {
    c->cur = -1;
    for (int k = 0; k < 3; k++) if (!c->td[k]) { c->cur = k; break; }
    c->goal = c->cur >= 0 ? c->tn[c->cur] : rand() % 7;
    c->rl = path(nn(c->x, c->y), c->goal, c->route); c->ri = 0;
  }
  if (c->ri < c->rl) follow(c, 95);
  else if (steer(c, NX[c->goal] + 55, NY[c->goal], 95) < 6) {
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
  int k = 0;
  for (int i = 0; i < 6; i++) if (ch[i].alive) {
    ch[i].x = 600 + cosf(k * 1.2f) * 55; ch[i].y = 380 + sinf(k * 1.2f) * 38;
    ch[i].goal = -1; ch[i].ri = ch[i].rl = 0; ch[i].wait = frand() * 2; k++;
  }
  cd = 15; witness = -1; state = PLAY;
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
    float nx = p->x + mx * 140 * DT, ny = p->y + my * 140 * DT;
    if (walk(nx, p->y)) p->x = nx; if (walk(p->x, ny)) p->y = ny;
    if (fabsf(mx) > .1f) p->f = mx > 0 ? 1 : -1;
    for (int k = 0; k < 3; k++)
      if (!p->td[k] && hypotf(p->x - (NX[p->tn[k]] + 55), p->y - NY[p->tn[k]]) < 48) nearTask = k;
    if (nearTask >= 0 && (btns & PAD_CROSS) && m < .25f) {
      useT += DT;
      if (useT >= 2) { p->td[nearTask] = 1; done++; useT = 0; }
    } else { useT -= DT * 2; if (useT < 0) useT = 0; }
    for (int i = 0; i < nb; i++) if (hypotf(p->x - bodies[i].x, p->y - bodies[i].y) < 80) nearBody = i;
    if ((pressed & PAD_CIRCLE) && nearBody >= 0) { meeting(0, bodies[nearBody].i); return; }
  }
  for (int i = 1; i < 6; i++) if (ch[i].alive) ai(&ch[i]);
  if (state != PLAY) return;
  for (int i = 1; i < 6; i++)
    if (ch[i].alive && !ch[i].imp)
      for (int b = 0; b < nb; b++)
        if (state == PLAY && hypotf(ch[i].x - bodies[b].x, ch[i].y - bodies[b].y) < 130 && frand() < DT * 1.2f) { meeting(i, bodies[b].i); return; }
  int crew = 0; for (int i = 0; i < 6; i++) if (ch[i].alive && !ch[i].imp) crew++;
  if (done >= total) finish(1, "TAREAS COMPLETADAS");
  else if (crew <= 1) finish(0, "GANO EL IMPOSTOR");
}

/* ---------- dibujo ---------- */
static void capsule(float x, float y, int i, int f, int dead) {
  u64 c = C(COL[i][0], COL[i][1], COL[i][2]);
  if (dead) { rect(x - 16, y - 8, x + 14, y + 12, c); rect(x + 4 * f - 5, y - 6, x + 4 * f + 5, y + 2, C(190,230,255)); rect(x - 2, y - 16, x + 2, y - 8, C(255,255,255)); return; }
  rect(x - 14, y + 8, x + 14, y + 12, C(5,10,30));
  rect(x - f * 17 - 4, y - 18, x - f * 17 + 4, y - 2, c);
  rect(x - 11, y - 24, x + 11, y + 4, c);
  rect(x - 9, y + 4, x - 2, y + 11, c); rect(x + 2, y + 4, x + 9, y + 11, c);
  rect(x + f * 4 - 8, y - 20, x + f * 4 + 8, y - 10, C(190,230,255));
}
static void draw(void) {
  gsKit_clear(gs, C(2,6,23));
  if (state == TITLE) {
    for (int i = 0; i < 6; i++) { float h = 120 + 60 * sinf(gt * .6f + i * 1.7f); rect(30 + i * 105, SH - h, 56 + i * 105, SH, C(40,70,170)); }
    text(110, 130, 8, C(150,190,255), "SOSPECHOSO");
    text(150, 215, 3, C(230,240,255), "ESTILO PS2 - HOMEBREW");
    if (((int)(gt * 2)) & 1) text(215, 300, 4, C(255,210,58), "PULSA START");
    text(70, 380, 2, C(160,190,255), "D-PAD MOVER  X USAR  O REPORTAR");
    return;
  }
  Ch *p = &ch[0];
  float cx = p->x - SW / 2, cy = p->y - SH / 2;
  if (cx < 0) cx = 0; if (cx > 1200 - SW) cx = 1200 - SW;
  if (cy < 0) cy = 0; if (cy > 760 - SH) cy = 760 - SH;
  for (int i = 0; i < 15; i++) rect(R[i][0] - 4 - cx, R[i][1] - 4 - cy, R[i][0] + R[i][2] + 4 - cx, R[i][1] + R[i][3] + 4 - cy, C(91,151,255));
  for (int i = 0; i < 15; i++) rect(R[i][0] - cx, R[i][1] - cy, R[i][0] + R[i][2] - cx, R[i][1] + R[i][3] - cy, C(16,37,106));
  for (int i = 0; i < 7; i++) text(R[i][0] + R[i][2] / 2 - strlen(NM[i]) * 4 - cx, R[i][1] + 10 - cy, 2, C(130,160,215), NM[i]);
  for (int k = 0; k < 3; k++) {
    float x = NX[p->tn[k]] + 55 - cx, y = NY[p->tn[k]] - cy;
    if (p->td[k]) rect(x - 10, y - 10, x + 10, y + 10, C(42,122,90));
    else { int g = 160 + (int)(95 * sinf(gt * 5)); if (g > 255) g = 255; rect(x - 10, y - 10, x + 10, y + 10, C(255, g, 58)); }
  }
  for (int i = 0; i < nb; i++) capsule(bodies[i].x - cx, bodies[i].y - cy, bodies[i].i, 1, 1);
  int ord[6], n = 0;
  for (int i = 0; i < 6; i++) if (ch[i].alive) ord[n++] = i;
  for (int a = 0; a < n; a++) for (int b = a + 1; b < n; b++) if (ch[ord[b]].y < ch[ord[a]].y) { int t = ord[a]; ord[a] = ord[b]; ord[b] = t; }
  for (int k = 0; k < n; k++) {
    Ch *c = &ch[ord[k]]; capsule(c->x - cx, c->y - cy, ord[k], c->f, 0);
    text(c->x - cx - strlen(NAMES[ord[k]]) * 2, c->y - cy - 36, 1, ord[k] ? C(255,210,58) : C(255,255,255), NAMES[ord[k]]);
  }
  if (nearTask >= 0) {
    text(p->x - cx - 50, p->y - cy - 56, 1, C(255,210,58), TN[p->tn[nearTask]]);
    if (useT > 0) rect(p->x - cx - 25, p->y - cy - 46, p->x - cx - 25 + 50 * useT / 2, p->y - cy - 42, C(255,210,58));
  }
  if (nearBody >= 0) text(240, SH - 30, 2, C(255,90,110), "O = REPORTAR");
  rect(10, 10, 10 + 300, 24, C(91,151,255)); rect(12, 12, 12 + 296.0f * done / total, 22, C(109,255,176));
  for (int k = 0; k < 3; k++) { char b[48]; snprintf(b, sizeof b, "%s %s", p->td[k] ? "OK" : "--", TN[p->tn[k]]); text(10, 32 + k * 12, 1, p->td[k] ? C(109,255,176) : C(220,235,255), b); }

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
  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);
  gsKit_init_screen(gs);
  gsKit_mode_switch(gs, GS_ONESHOT);
  SW = gs->Width; SH = gs->Height;

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
