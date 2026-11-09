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

#include "skeld_map.h"   /* geometria 3D, pisos, colisiones y salas (generado por tools/obj2h.py) */
#include "crew_model.h"   /* tripulante 3D + 7 skins (generado por tools/fbx2h.py) */
#ifdef USE_AUDIO          /* sonido de pasos: make AUDIO=1 (por defecto) | make AUDIO=0 para compilar sin audio */
#include <audsrv.h>
#include "sfx_step.h"     /* clips de pasos (generado por tools/audio2h.py) */
extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;
#ifdef HAVE_FREESD
extern unsigned char freesd_irx[];
extern unsigned int size_freesd_irx;
#endif
#endif
#ifdef USE_ONLINE         /* online: make ONLINE=1 (por defecto) | make ONLINE=0 para compilar sin red */
#ifdef NET_HOST           /* solo para pruebas en PC */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <unistd.h>
#else
#include <ps2ip.h>        /* lwIP en el EE */
#endif
#endif

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
  "ALINEAR MOTOR","CARGAR COMBUSTIBLE","ARRANCAR REACTOR","REVISAR CAMARAS","ESCANEAR MUESTRA","DESCARGAR DATOS","ESCANEAR TARJETA",
  "ORDENAR CAJAS","CONECTAR CABLES","CALIBRAR SENAL","DISPARAR ASTEROIDES","LIMPIAR FILTRO","ACTIVAR ESCUDOS","CALIBRAR RUMBO"
};
#define CAFE 5

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
  float x, y, wait, rt, yaw, tyaw;      /* y = coordenada z del mundo; yaw = hacia donde mira */
  float ph, run, ox, oz;                /* animacion de correr: fase, mezcla 0..1, posicion del cuadro anterior */
  int lstep;
  int skin, alive, imp, goal, cur, rl, ri, tn[3], td[3];
  u16 route[MAXP];
} Ch;
typedef struct { float x, y, yaw; int i; } Body;
enum { TITLE, INTRO, PLAY, MEET, MRES, END, LOBBY };
static Ch ch[6];
static Body bodies[6];
static int nb, state = TITLE, done, total = 15, endwin, witness, sus, cur, opts[6], nopts, mres_t, ejected;
static float cd, gt, camx, camz;
static int mini = -1;                         /* sala del minijuego abierto, o -1 */
static int prole, witP, nearKill;             /* prole: rol elegido (0 azar, 1 tripulante, 2 impostor); witP: te vieron matar */
static float intro_t;                         /* tiempo de la animacion de entrada */
static int tmenu, topt;
/* ---- online ---- */
#ifndef NET_PORT
#define NET_PORT 7777
#endif
static u8 net_ip[4] = {192, 168, 1, 100};     /* direccion del servidor: se cambia en el menu ONLINE */
static int online, net_ok, my_slot = -1, my_pid, room_id, host_slot, lmask, lskin[6], game_live, lobby_mode;
static int slot2li[6] = {-1,-1,-1,-1,-1,-1}, li2slot[6] = {-1,-1,-1,-1,-1,-1};   /* slot del servidor <-> indice local (yo siempre soy 0) */
static struct { u8 id, n, mx, st, hs; } rooms[40];
static int rooms_n, rooms_sel, rooms_top, voted, rep_pending, conn_stage, ipsel, rep_t;
static char net_msg[56];
static float net_msg_t, pos_t, list_t, ping_t, join_t, rtx[6], rtz[6];                       /* menu del titulo: pantalla (0 menu, 1 skin, 2 controles) y opcion */
static int nearTask, nearBody, pskin;   /* pskin = skin elegida por el jugador */
static char msg[80], msg2[80];
static u32 frames;

static float frand(void) { return rand() / (float)RAND_MAX; }
/* nombre visible: el jugador es "TU", los demas llevan el nombre de su color */
static const char *nm_of(int i) { return i == 0 ? "TU" : CM_SKIN_NAME[ch[i].skin]; }
static void turn(Ch *c) {                     /* gira suavemente hacia tyaw */
  float d = c->tyaw - c->yaw;
  while (d > 3.14159f) d -= 6.28318f;
  while (d < -3.14159f) d += 6.28318f;
  float k = DT * 14; c->yaw += d * (k > 1 ? 1 : k);
}

/* ---------- correr: animacion + sonido de pasos ----------
   Como en Among Us la cadencia es fija: ~4 pasos por segundo (igual que la grabacion), un pie toca el suelo
   cada media vuelta de fase (ph = pi/2 + k*pi) y en ese instante suena un clip de paso.                      */
#define STEP_RATE 3.99f
static int audio_ok, audio_err = 1;               /* audio_err: 0 bien | 1 no compilado (Makefile) | 2 modulo audsrv | 3 audsrv_init | 4 formato */
#ifdef USE_AUDIO
static void audio_init(void) {
  int ret = 0, id;
  audio_err = 2;
  id = SifLoadModule("rom0:LIBSD", 0, NULL);                         /* audsrv necesita libsd: normalmente viene en la BIOS */
#ifdef HAVE_FREESD
  if (id < 0) SifExecModuleBuffer(freesd_irx, size_freesd_irx, 0, NULL, &ret);   /* respaldo: libsd libre incrustado */
#endif
  id = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &ret);          /* audsrv.irx va incrustado en el ELF */
  if (id < 0) return;
  audio_err = 3;
  if (audsrv_init() != 0) return;
  audio_err = 4;
  struct audsrv_fmt_t fmt; fmt.freq = SFX_RATE; fmt.bits = 16; fmt.channels = 1;
  if (audsrv_set_format(&fmt) != 0) return;
#ifdef MAX_VOLUME
  audsrv_set_volume(MAX_VOLUME);
#endif
  audio_err = 0; audio_ok = 1;
}
static void sfx_step(void) {
  static u32 rs = 12345; static int last = -1;                       /* azar propio: no altera el de la partida */
  if (!audio_ok) return;
  rs = rs * 1664525u + 1013904223u;
  int k = (int)((rs >> 16) % SFX_NSTEP); if (k == last) k = (k + 1) % SFX_NSTEP; last = k;
  audsrv_play_audio((const char *)(SFX_PCM + k * SFX_LEN), SFX_LEN * 2);
}
#else
static void audio_init(void) {}                                      /* sin -DUSE_AUDIO: audio_err queda en 1 */
static void sfx_step(void) {}
#endif
static void anim(Ch *c, int snd) {                /* snd=1: este personaje hace sonar sus pasos (el jugador) */
  float spd = hypotf(c->x - c->ox, c->y - c->oz) / DT;
  int moving = c->alive && spd > 1.0f;            /* si choca con una pared no "corre" */
  if (moving && c->run < .05f) { c->ph = 1.07f; c->lstep = -1; }   /* arranque: el primer pie toca en ~40 ms */
  float k = DT * 14; if (k > 1) k = 1;
  c->run += ((moving ? 1.f : 0.f) - c->run) * k;
  if (!moving) return;
  c->ph += DT * STEP_RATE * 3.14159f;
  int st = (int)floorf((c->ph - 1.5708f) / 3.14159f);
  if (st != c->lstep) { c->lstep = st; if (snd) sfx_step(); }
  if (c->ph > 628.f) { c->ph -= 628.318f; c->lstep -= 200; }      /* evita perder precision */
}

static void start(void) {
  srand(frames);
  int pimp = (prole == 2) || (prole == 0 && rand() % 6 == 0);   /* 1 impostor entre 6: igual de probable para ti que para cualquiera */
  int imp = pimp ? 0 : 1 + rand() % 5;
  float cx = (SK_ROOM[CAFE][0] + SK_ROOM[CAFE][2]) / 2, cz = (SK_ROOM[CAFE][1] + SK_ROOM[CAFE][3]) / 2;
  for (int i = 0; i < 6; i++) {
    Ch *c = &ch[i]; memset(c, 0, sizeof(Ch));
    c->x = cx + cosf(i * 1.05f) * 2.6f; c->y = cz + sinf(i * 1.05f) * 2.2f; find_free(&c->x, &c->y);
    c->yaw = c->tyaw = 3.14159f; c->alive = 1; c->imp = (i == imp);   /* mira a la camara */
    c->goal = -1; c->cur = -1; c->wait = frand() * 2;
    int a[NR]; for (int k = 0; k < NR; k++) a[k] = k;
    for (int k = NR - 1; k > 0; k--) { int j = rand() % (k + 1), t = a[k]; a[k] = a[j]; a[j] = t; }
    for (int k = 0; k < 3; k++) { c->tn[k] = a[k]; c->td[k] = 0; }
  }
  { /* skins: el jugador usa la elegida; los otros 5 reciben al azar entre las 6 restantes */
    int rest[CM_NSKIN], nr = 0;
    for (int k = 0; k < CM_NSKIN; k++) if (k != pskin) rest[nr++] = k;
    for (int k = nr - 1; k > 0; k--) { int j = rand() % (k + 1), t = rest[k]; rest[k] = rest[j]; rest[j] = t; }
    ch[0].skin = pskin; for (int i = 1; i < 6; i++) ch[i].skin = rest[i - 1];
  }
  nb = 0; done = 0; cd = 20; mini = -1; witness = -1; witP = 0; gt = 0; state = INTRO; intro_t = 0;
  camx = ch[0].x; camz = ch[0].y;
}
static void finish(int win, const char *m) {
  if (state == END) return;
  endwin = win; state = END;
  snprintf(msg, sizeof msg, "%s", m);
  int im = 0; for (int i = 0; i < 6; i++) if (ch[i].imp) im = i;
  if (online) snprintf(msg2, sizeof msg2, ch[0].imp ? "ERAS EL IMPOSTOR" : "LA PARTIDA SIGUE SIN TI");
  else if (im == 0) snprintf(msg2, sizeof msg2, "ERAS EL IMPOSTOR"); else snprintf(msg2, sizeof msg2, "EL IMPOSTOR ERA %s", nm_of(im));
}
static void finish_team(int crew_wins, const char *m) { finish(ch[0].imp ? !crew_wins : crew_wins, m); }   /* el "ganas" depende de tu rol */
static float steer(Ch *c, float tx, float tz, float sp) {
  float dx = tx - c->x, dz = tz - c->y, d = hypotf(dx, dz);
  if (d < .01f) return 0;
  float s = sp * DT; if (s > d) s = d;
  float mx = dx / d * s, mz = dz / d * s;
  if (walk(c->x + mx, c->y)) c->x += mx;
  if (walk(c->x, c->y + mz)) c->y += mz;
  if (s > .0005f) c->tyaw = atan2f(-mx, -mz);
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
  t->alive = 0; bodies[nb].x = t->x; bodies[nb].y = t->y; bodies[nb].i = (int)(t - ch); bodies[nb].yaw = t->yaw; nb++;
  cd = 18; k->wait = 1.2f; k->goal = -1; k->cur = -1; k->rl = k->ri = 0;
  if (t == &ch[0]) { char b[80]; snprintf(b, sizeof b, "%s TE ELIMINO", nm_of(k - ch)); finish(0, b); }
  else if (k == &ch[0]) {                      /* mataste tu: si algun tripulante te vio, sospechara de ti en la reunion */
    for (int i = 1; i < 6; i++)
      if (ch[i].alive && ch + i != t && hypotf(k->x - ch[i].x, k->y - ch[i].y) < 9.f && same_area(k->x, k->y, ch[i].x, ch[i].y)) witP = 1;
  }
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
  nb = 0; state = MEET; cur = 0; mini = -1; (void)by;
  int imp = 0, oth[5], no = 0;
  for (int i = 0; i < 6; i++) { if (ch[i].imp) imp = i; else if (i && ch[i].alive) oth[no++] = i; }
  if (ch[0].imp) sus = (witP || !no) ? 0 : oth[rand() % no];          /* si eres el impostor: sospechan de ti solo si te vieron */
  else sus = (frand() < .65f || !no) ? imp : oth[rand() % no];
  snprintf(msg, sizeof msg, "%s REPORTO A %s", nm_of(by), nm_of(victim));
  if (ch[0].imp) snprintf(msg2, sizeof msg2, witP ? "ALGUIEN TE VIO CERCA DEL CUERPO" : "NADIE SABE QUIEN FUE");
  else if (witness >= 0) snprintf(msg2, sizeof msg2, "VISTE A %s ELIMINARLO", nm_of(witness));
  else snprintf(msg2, sizeof msg2, "VIERON A %s CERCA DEL CUERPO", nm_of(sus));
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
  if (ejected >= 0) snprintf(msg, sizeof msg, "%s FUE EXPULSADO", nm_of(ejected)), snprintf(msg2, sizeof msg2, ch[ejected].imp ? "ERA EL IMPOSTOR!" : "NO ERA EL IMPOSTOR");
  else snprintf(msg, sizeof msg, "NADIE FUE EXPULSADO"), msg2[0] = 0;
  mres_t = 200; state = MRES;
}
static void after_vote(void) {
  if (online) {                                                  /* online: el servidor decide si hay ganador (llega como mensaje END) */
    if (ejected >= 0) ch[ejected].alive = 0;
    if (ejected == 0) { finish(0, "TE EXPULSARON"); return; }
    float cx = (SK_ROOM[CAFE][0] + SK_ROOM[CAFE][2]) / 2, cz = (SK_ROOM[CAFE][1] + SK_ROOM[CAFE][3]) / 2;
    for (int i = 0; i < 6; i++) if (ch[i].alive) {
      int sp = li2slot[i] >= 0 ? li2slot[i] : i;
      ch[i].x = cx + cosf(sp * 1.2f) * 2.4f; ch[i].y = cz + sinf(sp * 1.2f) * 1.8f; find_free(&ch[i].x, &ch[i].y);
      rtx[i] = ch[i].x; rtz[i] = ch[i].y; ch[i].yaw = ch[i].tyaw = 3.14159f; ch[i].run = 0;
    }
    cd = 15; state = PLAY; camx = ch[0].x; camz = ch[0].y; pos_t = 1.f;
    return;
  }
  if (ejected >= 0) ch[ejected].alive = 0;
  if (ejected >= 0 && ch[ejected].imp) { finish_team(1, ch[0].imp ? "TE DESCUBRIERON" : "EXPULSASTE AL IMPOSTOR"); return; }
  if (ejected == 0) { finish(0, "TE EXPULSARON"); return; }
  int crew = 0; for (int i = 0; i < 6; i++) if (ch[i].alive && !ch[i].imp) crew++;
  if (crew <= 1) { finish_team(0, "GANO EL IMPOSTOR"); return; }
  float cx = (SK_ROOM[CAFE][0] + SK_ROOM[CAFE][2]) / 2, cz = (SK_ROOM[CAFE][1] + SK_ROOM[CAFE][3]) / 2;
  int k = 0;
  for (int i = 0; i < 6; i++) if (ch[i].alive) {
    ch[i].x = cx + cosf(k * 1.2f) * 2.4f; ch[i].y = cz + sinf(k * 1.2f) * 1.8f; find_free(&ch[i].x, &ch[i].y);
    ch[i].yaw = ch[i].tyaw = 3.14159f; ch[i].run = 0; ch[i].goal = -1; ch[i].ri = ch[i].rl = 0; ch[i].wait = frand() * 2; k++;
  }
  cd = 15; witness = -1; witP = 0; state = PLAY;
  camx = ch[0].x; camz = ch[0].y;
}

/* =====================================================================
   ONLINE (servidor: server/sospechoso_server.py)
   Mensaje = [tipo u8][largo u8][carga]. El servidor decide roles, muertes, reuniones, votos y
   victoria; cada cliente manda solo su propia posicion. (Lista de mensajes: encabezado del servidor.)
   ===================================================================== */
static void lobby_enter(void);
static void lobby_sync(int fresh);
static void net_down(void);
static void net_say(const char *s) { snprintf(net_msg, sizeof net_msg, "%s", s); net_msg_t = 4.f; }
static void put16(u8 *p, int v) { p[0] = v & 255; p[1] = (v >> 8) & 255; }
static int get16(const u8 *p) { int v = p[0] | (p[1] << 8); return v >= 32768 ? v - 65536 : v; }
static void map_reset(void) { for (int i = 0; i < 6; i++) slot2li[i] = li2slot[i] = -1; my_slot = -1; room_id = 0; lmask = 0; }

static int net_fd = -1;
static u8 rxb[1100] __attribute__((unused));
static int rxn __attribute__((unused)), net_phase, net_timer;      /* net_phase (PS2): 0 sin iniciar, 2 esperando IP, 3 lista */

#ifdef USE_ONLINE
#ifdef NET_HOST                              /* pruebas en PC: sockets normales */
#define N_SOCKET(a, b, c)  socket(a, b, c)
#define N_CONNECT(f, a, l) connect(f, a, l)
#define N_SEND(f, b, n)    send(f, b, n, 0)
#define N_RECV(f, b, n)    recv(f, b, n, 0)
#define N_CLOSE(f)         close(f)
#define N_SELECT           select
static int net_platform_poll(void) { return 1; }
#else                                        /* PS2: lwIP en el EE (ps2ip) sobre el adaptador de red (smap) */
#define N_SOCKET(a, b, c)  lwip_socket(a, b, c)
#define N_CONNECT(f, a, l) lwip_connect(f, a, l)
#define N_SEND(f, b, n)    lwip_send(f, b, n, 0)
#define N_RECV(f, b, n)    lwip_recv(f, b, n, 0)
#define N_CLOSE(f)         lwip_close(f)
#define N_SELECT           lwip_select
extern unsigned char ps2dev9_irx[], netman_irx[], smap_irx[];
extern unsigned int size_ps2dev9_irx, size_netman_irx, size_smap_irx;
#ifndef DHCP_STATE_BOUND
#define DHCP_STATE_BOUND 10
#endif
/* >0 lista, 0 trabajando (se llama cada cuadro), <0 error. Carga los modulos de red y pide IP por DHCP. */
static int net_platform_poll(void) {
  if (net_phase == 3) return 1;
  if (net_phase == 0) {
    int ret = 0; struct ip4_addr ip, nm, gw;
    SifExecModuleBuffer(ps2dev9_irx, size_ps2dev9_irx, 0, NULL, &ret);
    SifExecModuleBuffer(netman_irx, size_netman_irx, 0, NULL, &ret);
    SifExecModuleBuffer(smap_irx, size_smap_irx, 0, NULL, &ret);
    ip4_addr_set_zero(&ip); ip4_addr_set_zero(&nm); ip4_addr_set_zero(&gw);
    if (ps2ipInit(&ip, &nm, &gw) < 0) return -1;
    t_ip_info info;
    if (ps2ip_getconfig("sm0", &info) >= 0) { info.dhcp_enabled = 1; ps2ip_setconfig(&info); }
    net_phase = 2; net_timer = 0; return 0;
  }
  t_ip_info info;
  if (ps2ip_getconfig("sm0", &info) >= 0 && info.dhcp_enabled && info.dhcp_status == DHCP_STATE_BOUND) { net_phase = 3; return 1; }
  return ++net_timer > 60 * 20 ? -1 : 0;     /* 20 s para conseguir IP */
}
#endif
static int net_open(void) {
  struct sockaddr_in sa; memset(&sa, 0, sizeof sa);
  sa.sin_family = AF_INET;
  sa.sin_port = (u16)(((NET_PORT & 0xff) << 8) | (NET_PORT >> 8));                  /* orden de red */
  sa.sin_addr.s_addr = net_ip[0] | (net_ip[1] << 8) | ((u32)net_ip[2] << 16) | ((u32)net_ip[3] << 24);
  net_fd = N_SOCKET(AF_INET, SOCK_STREAM, 0);
  if (net_fd < 0) return -1;
  if (N_CONNECT(net_fd, (struct sockaddr *)&sa, sizeof sa) < 0) { N_CLOSE(net_fd); net_fd = -1; return -1; }
  rxn = 0; return 0;
}
static void net_close(void) { if (net_fd >= 0) N_CLOSE(net_fd); net_fd = -1; rxn = 0; net_ok = 0; }
static void net_tx(int type, const u8 *p, int n) {
  if (net_fd < 0) return;
  u8 b[260]; b[0] = type; b[1] = n; if (n) memcpy(b + 2, p, n);
  int off = 0, tot = n + 2;
  while (off < tot) { int r = N_SEND(net_fd, b + off, tot - off); if (r <= 0) { net_down(); return; } off += r; }
}
static void net_msg_in(int t, const u8 *d, int n);
static void net_poll(void) {
  if (net_fd < 0) return;
  for (int guard = 0; guard < 8; guard++) {
    fd_set rs; FD_ZERO(&rs); FD_SET(net_fd, &rs);
    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = 0;
    if (N_SELECT(net_fd + 1, &rs, NULL, NULL, &tv) <= 0) break;
    int r = N_RECV(net_fd, rxb + rxn, (int)sizeof rxb - rxn);
    if (r <= 0) { net_down(); return; }
    rxn += r;
    int off = 0;
    while (rxn - off >= 2 && rxn - off >= 2 + rxb[off + 1]) {
      int t = rxb[off], n = rxb[off + 1]; net_msg_in(t, rxb + off + 2, n); off += 2 + n;
      if (net_fd < 0) return;
    }
    if (off) { memmove(rxb, rxb + off, rxn - off); rxn -= off; }
    if (rxn >= (int)sizeof rxb) { net_down(); return; }
  }
}
#else                                        /* sin online (make ONLINE=0): el menu avisa que no esta disponible */
static int net_platform_poll(void) { return -1; }
static int net_open(void) { return -1; }
static void net_close(void) { net_fd = -1; net_ok = 0; }
static void net_tx(int type, const u8 *p, int n) { (void)type; (void)p; (void)n; }
static void net_poll(void) {}
#endif
static __attribute__((unused)) void net_down(void) {   /* se perdio la conexion */
  net_close(); online = 0; game_live = 0; mini = -1; map_reset();
  state = TITLE; tmenu = 3; conn_stage = 0; net_say("SE PERDIO LA CONEXION");
}

/* ---- partida online ---- */
static void start_online(int role, int tot, int mask) {
  srand(frames);
  int sk[6]; for (int i = 0; i < 6; i++) sk[i] = ch[i].skin;
  float cx = (SK_ROOM[CAFE][0] + SK_ROOM[CAFE][2]) / 2, cz = (SK_ROOM[CAFE][1] + SK_ROOM[CAFE][3]) / 2;
  for (int i = 0; i < 6; i++) {
    Ch *c = &ch[i]; memset(c, 0, sizeof(Ch));
    int s = li2slot[i], present = s >= 0 && ((mask >> s) & 1);
    c->skin = sk[i]; c->alive = present; c->yaw = c->tyaw = 3.14159f; c->goal = -1; c->cur = -1;
    int sp = present ? s : i;
    c->x = cx + cosf(sp * 1.05f) * 2.6f; c->y = cz + sinf(sp * 1.05f) * 2.2f; find_free(&c->x, &c->y);
    rtx[i] = c->x; rtz[i] = c->y;
    int a[NR]; for (int k = 0; k < NR; k++) a[k] = k;
    for (int k = NR - 1; k > 0; k--) { int j = rand() % (k + 1), t = a[k]; a[k] = a[j]; a[j] = t; }
    for (int k = 0; k < 3; k++) { c->tn[k] = a[k]; c->td[k] = 0; }
    c->imp = (i == 0 && role == 1);
  }
  online = 1; game_live = 1; total = tot; nb = 0; done = 0; cd = 20; mini = -1; witness = -1; witP = 0; gt = 0;
  voted = 0; rep_pending = 0; pos_t = 0; lobby_mode = 0;
  state = INTRO; intro_t = 0; camx = ch[0].x; camz = ch[0].y;
}
static void remote_update(void) {            /* suaviza a los demas jugadores hacia la ultima posicion recibida */
  for (int i = 1; i < 6; i++) if (ch[i].alive) {
    float dx = rtx[i] - ch[i].x, dz = rtz[i] - ch[i].y;
    if (hypotf(dx, dz) > 4.f) { ch[i].x = rtx[i]; ch[i].y = rtz[i]; } else { ch[i].x += dx * .3f; ch[i].y += dz * .3f; }
  }
}
static void kill_online(Ch *t) {             /* soy el impostor: aplico la muerte ya y aviso al servidor */
  int li = (int)(t - ch); u8 p[5];
  t->alive = 0; bodies[nb].x = t->x; bodies[nb].y = t->y; bodies[nb].i = li; bodies[nb].yaw = t->yaw; nb++;
  cd = 18;
  p[0] = (u8)li2slot[li]; put16(p + 1, (int)(t->x * 100)); put16(p + 3, (int)(t->y * 100)); net_tx(0x11, p, 5);
}
static void online_report(int bi) {
  if (rep_pending > 0) return;
  u8 p = (u8)li2slot[bodies[bi].i]; net_tx(0x12, &p, 1); rep_pending = 60;
}
static void online_vote(int li) { if (voted) return; voted = 1; u8 p = li < 0 ? 255 : (u8)li2slot[li]; net_tx(0x14, &p, 1); }
static void online_tick(void) {              /* cada cuadro: recibir, enviar posicion y mantener viva la conexion */
  if (net_msg_t > 0) net_msg_t -= DT;
  if (join_t > 0) join_t -= DT;
  if (rep_pending > 0) rep_pending--;
  if (net_fd < 0) return;
  net_poll();
  if (net_fd < 0) return;
  if ((state == PLAY || state == LOBBY) && ch[0].alive) {
    if ((pos_t += DT) >= .08f) {
      u8 p[7]; pos_t = 0;
      put16(p, (int)(ch[0].x * 100)); put16(p + 2, (int)(ch[0].y * 100)); put16(p + 4, (int)(ch[0].yaw * 1000)); p[6] = 0;
      net_tx(0x10, p, 7);
    }
  } else if ((ping_t += DT) >= 3.f) { ping_t = 0; net_tx(0x07, NULL, 0); }
  if (state == TITLE && tmenu == 4 && (list_t -= DT) <= 0) { list_t = 2.f; net_tx(0x02, NULL, 0); }
}
static void apply_alive(int amask) {
  for (int s = 0; s < 6; s++) { int li = slot2li[s]; if (li > 0) ch[li].alive = (amask >> s) & 1; }
}
static __attribute__((unused)) void net_msg_in(int t, const u8 *d, int n) {
  switch (t) {
  case 0x81: if (n >= 1) { my_pid = d[0]; net_ok = 1; } break;
  case 0x82:
    if (n >= 1) { rooms_n = d[0] > 40 ? 40 : d[0]; if (n < 1 + rooms_n * 5) rooms_n = (n - 1) / 5;
      for (int k = 0; k < rooms_n; k++) { rooms[k].id = d[1 + k * 5]; rooms[k].n = d[2 + k * 5]; rooms[k].mx = d[3 + k * 5]; rooms[k].st = d[4 + k * 5]; rooms[k].hs = d[5 + k * 5] % CM_NSKIN; }
      if (rooms_sel > rooms_n) rooms_sel = rooms_n; }
    break;
  case 0x83:
    if (n < 10) break;
    room_id = d[0]; my_slot = d[1]; host_slot = d[2]; lmask = d[3]; for (int i = 0; i < 6; i++) lskin[i] = d[4 + i] % CM_NSKIN;
    if (state == TITLE && tmenu == 4) lobby_enter(); else if (state == LOBBY) lobby_sync(0);
    break;
  case 0x84: if (n >= 3) start_online(d[0], d[1], d[2]); break;
  case 0x8e: {
    static const char *em[7] = {"ERROR", "LA SALA ESTA LLENA", "ESA SALA YA NO EXISTE", "LA PARTIDA YA EMPEZO", "SOLO EL ANFITRION PUEDE EMPEZAR", "FALTAN JUGADORES (MINIMO 3)", "NO SE PUEDE AHORA"};
    net_say(em[(n >= 1 && d[0] < 7) ? d[0] : 0]); join_t = 0; break; }
  case 0x90:
    if (n >= 8 && d[0] < 6 && slot2li[d[0]] > 0) { int li = slot2li[d[0]]; rtx[li] = get16(d + 1) / 100.f; rtz[li] = get16(d + 3) / 100.f; ch[li].tyaw = get16(d + 5) / 1000.f; }
    break;
  case 0x91:
    if (n >= 6 && d[0] < 6 && d[1] < 6 && slot2li[d[1]] >= 0) {
      int vl = slot2li[d[1]], kl = slot2li[d[0]];
      if (vl == 0) { if (ch[0].alive) { char b[80]; ch[0].alive = 0; snprintf(b, sizeof b, "%s TE ELIMINO", kl >= 0 ? nm_of(kl) : "ALGUIEN"); finish(0, b); } }
      else if (ch[vl].alive) { ch[vl].alive = 0; bodies[nb].x = get16(d + 2) / 100.f; bodies[nb].y = get16(d + 4) / 100.f; bodies[nb].i = vl; bodies[nb].yaw = ch[vl].yaw; nb++; }
    }
    break;
  case 0x92:
    if (n >= 3 && state == PLAY && d[0] < 6 && d[1] < 6) {
      int bl = slot2li[d[0]], vl = slot2li[d[1]];
      apply_alive(d[2]); nb = 0; state = MEET; cur = 0; mini = -1; voted = 0; rep_pending = 0;
      snprintf(msg, sizeof msg, "%s REPORTO A %s", bl >= 0 ? nm_of(bl) : "ALGUIEN", vl >= 0 ? nm_of(vl) : "ALGUIEN");
      snprintf(msg2, sizeof msg2, "VOTA A QUIEN CREAS QUE ES EL IMPOSTOR");
      nopts = 0; for (int i = 1; i < 6; i++) if (ch[i].alive) opts[nopts++] = i;
      opts[nopts++] = -1;
    }
    break;
  case 0x93:
    if (n >= 3 && state == MEET) {
      apply_alive(d[2]); ejected = (d[0] < 6 && slot2li[d[0]] >= 0) ? slot2li[d[0]] : -1;
      if (ejected >= 0) { snprintf(msg, sizeof msg, "%s FUE EXPULSADO", nm_of(ejected)); snprintf(msg2, sizeof msg2, d[1] ? "ERA EL IMPOSTOR!" : "NO ERA EL IMPOSTOR"); }
      else { snprintf(msg, sizeof msg, "NADIE FUE EXPULSADO"); msg2[0] = 0; }
      mres_t = 200; state = MRES;
    }
    break;
  case 0x94: if (n >= 2) { done = d[0]; total = d[1]; } break;
  case 0x95:
    if (n >= 3) {
      static const char *rs[4] = {"TAREAS COMPLETADAS", "EXPULSARON AL IMPOSTOR", "GANO EL IMPOSTOR", "EL IMPOSTOR SE FUE"};
      int crew_wins = d[0], reason = d[1] & 3, il = d[2] < 6 ? slot2li[d[2]] : -1;
      char who[48];
      if (il == 0) snprintf(who, sizeof who, "ERAS EL IMPOSTOR"); else if (il > 0) snprintf(who, sizeof who, "EL IMPOSTOR ERA %s", nm_of(il)); else snprintf(who, sizeof who, "LA PARTIDA TERMINO");
      game_live = 0; mini = -1;
      if (state == END) snprintf(msg2, sizeof msg2, "TERMINO: %s", who);       /* ya habias caido: solo se actualiza el aviso */
      else {
        endwin = ch[0].imp ? !crew_wins : crew_wins; state = END; snprintf(msg, sizeof msg, "%s", rs[reason]); snprintf(msg2, sizeof msg2, "%s", who);
      }
    }
    break;
  case 0x96: if (n >= 1 && d[0] < 6 && slot2li[d[0]] > 0) ch[slot2li[d[0]]].alive = 0; break;
  default: break;
  }
}

/* ---------- entrada ---------- */
static char padBuf[256] __attribute__((aligned(64)));
static u32 btns, pressed, oldb, mbtn, mpress, oldm;
static float ax, ay, mvx, mvy;      /* mbtn/mpress/mvx/mvy: entrada combinada D-pad + stick (minijuegos) */
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
  mvx = ax + ((btns & PAD_RIGHT) ? 1 : 0) - ((btns & PAD_LEFT) ? 1 : 0); if (mvx > 1) mvx = 1; if (mvx < -1) mvx = -1;
  mvy = ay + ((btns & PAD_DOWN) ? 1 : 0) - ((btns & PAD_UP) ? 1 : 0);    if (mvy > 1) mvy = 1; if (mvy < -1) mvy = -1;
  u32 sb = btns;
  if (mvx > .5f) sb |= PAD_RIGHT; if (mvx < -.5f) sb |= PAD_LEFT; if (mvy > .5f) sb |= PAD_DOWN; if (mvy < -.5f) sb |= PAD_UP;
  mpress = sb & ~oldm; oldm = sb; mbtn = sb;
}

/* ---------- minijuegos: uno por zona (indice = sala) ----------
   X = abrir/accion, O = salir (el progreso se pierde). El resto del mundo sigue
   funcionando mientras juegas: ojo con el impostor.                              */
#define PX 100
#define PY 56
#define PW 440
#define PH 300
static int mslot, mstate, mtimer;
static float mt, mp;                             /* tiempo y progreso generico (0..1) */
static char mmsg[32]; static int mmsgt;
static const char *HINT[NR] = {
  "ARRIBA-ABAJO MOVER  X FIJAR  IZQ-DER CAMBIAR MOTOR", "MANTEN X PARA LLENAR EL TANQUE",
  "REPITE LA SECUENCIA CON EL D-PAD", "D-PAD ELEGIR  X SELECCIONAR AL INTRUSO",
  "QUIETO: ESCANEANDO MUESTRA", "X INICIAR LA DESCARGA",
  "MANTEN DERECHA Y DESLIZA A BUEN RITMO", "IZQ-DER ELEGIR  X INTERCAMBIAR  ORDENA 1-5",
  "ARRIBA-ABAJO ELEGIR  X CONECTAR EL MISMO COLOR", "IZQ-DER SINTONIZA HASTA IGUALAR LA ONDA",
  "MOVER MIRA  X DISPARAR  8 ASTEROIDES", "X AGARRAR HOJA  LLEVALA A LA DERECHA",
  "D-PAD ELEGIR  X ACTIVAR PANELES ROJOS", "ARRIBA-ABAJO PARA SEGUIR LA RUTA"
};
static const u8 WC[4][3] = {{230,40,40},{50,100,255},{250,220,40},{240,80,200}};   /* colores de cables */
static const int SHP[7][2] = {{0,0},{2,0},{0,1},{1,1},{2,1},{0,2},{2,2}};          /* paneles de escudos (col,fila) */

static float en_v[2]; static int en_lock[2], en_sel;                                 /* 0 motor */
static int si_seq[5], si_len, si_round, si_ph, si_i, si_t, si_lit;                    /* 2 reactor */
static int ca_cur, ca_bad, ca_round, ca_lock;                                         /* 3 camaras */
static int dl_started;                                                                /* 5 descarga */
static float sw_x, sw_t;                                                              /* 6 tarjeta */
static int bx_n[5], bx_cur, bx_sel;                                                   /* 7 cajas */
static int wr_l[4], wr_r[4], wr_link[4], wr_li, wr_ri, wr_ph;                         /* 8 cables */
static float cm_f, cm_tf;                                                             /* 9 senal */
static float as_x[6], as_y[6], as_vx[6], as_vy[6], as_cx, as_cy; static int as_kills, as_boom;   /* 10 asteroides */
static float lf_x[6], lf_y[6], lf_cx, lf_cy; static int lf_alive[6], lf_grab, lf_out;            /* 11 filtro */
static int sh_on[7], sh_cur;                                                          /* 12 escudos */
static float nv_p, nv_y;                                                              /* 13 rumbo */

static float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static void say(const char *s) { snprintf(mmsg, sizeof mmsg, "%s", s); mmsgt = 90; }
static void mwin(void) { if (!mstate) { mstate = 1; mtimer = 70; } }
static float nv_path(float p) { return .5f + .27f * sinf(p * 9.f) * (1.f - .3f * p); }
static void spawn_ast(int k) {
  if (k & 1) { as_x[k] = PX + 30 + frand() * 380; as_y[k] = PY + 52; as_vx[k] = (frand() - .5f) * 50; as_vy[k] = 30 + frand() * 45; }
  else       { as_x[k] = PX + 22; as_y[k] = PY + 60 + frand() * 180; as_vx[k] = 35 + frand() * 55; as_vy[k] = (frand() - .5f) * 50; }
}
static int wr_used(int r) { for (int k = 0; k < 4; k++) if (wr_link[k] == r) return 1; return 0; }
static int wr_step(int cur, int dir, int ph) {
  for (int n = 0; n < 4; n++) { cur = (cur + dir + 4) % 4; if (ph == 0 ? wr_link[cur] < 0 : !wr_used(cur)) return cur; }
  return cur;
}
static int crates_sorted(void) { for (int k = 0; k < 5; k++) if (bx_n[k] != k + 1) return 0; return 1; }

static void mini_open(int slot) {
  mini = ch[0].tn[slot]; mslot = slot; mstate = 0; mt = 0; mp = 0; mmsgt = 0;
  switch (mini) {
  case 0: for (int k = 0; k < 2; k++) { do en_v[k] = frand(); while (fabsf(en_v[k] - .5f) < .25f); en_lock[k] = 0; } en_sel = 0; break;
  case 2: si_round = 0; si_len = 3; si_ph = 0; si_i = 0; si_t = -30; si_lit = -1; for (int k = 0; k < 5; k++) si_seq[k] = rand() % 4; break;
  case 3: ca_round = 0; ca_cur = 4; ca_bad = rand() % 9; ca_lock = 0; break;
  case 5: dl_started = 0; break;
  case 6: sw_x = 0; sw_t = 0; break;
  case 7:
    for (int k = 0; k < 5; k++) bx_n[k] = k + 1;
    do for (int k = 4; k > 0; k--) { int j = rand() % (k + 1), t = bx_n[k]; bx_n[k] = bx_n[j]; bx_n[j] = t; } while (crates_sorted());
    bx_cur = 0; bx_sel = -1; break;
  case 8: {
    int a[4] = {0,1,2,3}, b[4] = {0,1,2,3};
    for (int k = 3; k > 0; k--) { int j = rand() % (k + 1), t = a[k]; a[k] = a[j]; a[j] = t; j = rand() % (k + 1); t = b[k]; b[k] = b[j]; b[j] = t; }
    for (int k = 0; k < 4; k++) { wr_l[k] = a[k]; wr_r[k] = b[k]; wr_link[k] = -1; }
    wr_li = 0; wr_ri = 0; wr_ph = 0; break; }
  case 9: cm_f = frand(); do cm_tf = frand(); while (fabsf(cm_tf - cm_f) < .3f); break;
  case 10: for (int k = 0; k < 6; k++) spawn_ast(k); as_cx = PX + PW / 2; as_cy = PY + PH / 2; as_kills = 0; as_boom = 0; break;
  case 11:
    for (int k = 0; k < 6; k++) { lf_x[k] = PX + 40 + frand() * 270; lf_y[k] = PY + 70 + frand() * 150; lf_alive[k] = 1; }
    lf_grab = -1; lf_out = 0; lf_cx = PX + 100; lf_cy = PY + 150; break;
  case 12: { int off = 0; for (int k = 0; k < 7; k++) { sh_on[k] = rand() & 1; if (!sh_on[k]) off++; } if (off < 3) sh_on[0] = sh_on[3] = sh_on[6] = 0; sh_cur = 3; break; }
  case 13: nv_p = 0; nv_y = clamp01(nv_path(0) + .3f); break;
  }
}

static void mini_update(void) {
  if (mpress & PAD_CIRCLE) { mini = -1; return; }
  mt += DT; if (mmsgt > 0) mmsgt--;
  if (mstate) { if (--mtimer <= 0) { ch[0].td[mslot] = 1; if (online) net_tx(0x13, NULL, 0); else done++; mini = -1; } return; }
  switch (mini) {
  case 0:
    if (mpress & PAD_LEFT) en_sel = 0;
    if (mpress & PAD_RIGHT) en_sel = 1;
    if (!en_lock[en_sel]) en_v[en_sel] = clamp01(en_v[en_sel] + mvy * DT * .8f);
    if ((mpress & PAD_CROSS) && !en_lock[en_sel]) {
      if (fabsf(en_v[en_sel] - .5f) < .04f) { en_lock[en_sel] = 1; en_v[en_sel] = .5f; } else say("DESALINEADO");
    }
    if (en_lock[0] && en_lock[1]) mwin();
    break;
  case 1:
    if (mbtn & PAD_CROSS) mp += DT * .22f; else { mp -= DT * .3f; if (mp < 0) mp = 0; }
    if (mp >= 1) mwin();
    break;
  case 2:
    if (si_t < 0) { si_t++; si_lit = -1; }
    else if (si_ph == 0) {                                   /* mostrar secuencia */
      si_t++; int step = si_t / 36;
      if (step >= si_len) { si_ph = 1; si_i = 0; si_lit = -1; si_t = 0; }
      else si_lit = (si_t % 36) < 24 ? si_seq[step] : -1;
    } else {                                                 /* repetirla */
      if (si_t > 0) { if (--si_t == 0) si_lit = -1; }
      int d = (mpress & PAD_UP) ? 0 : (mpress & PAD_RIGHT) ? 1 : (mpress & PAD_DOWN) ? 2 : (mpress & PAD_LEFT) ? 3 : -1;
      if (d >= 0) {
        si_lit = d; si_t = 12;
        if (d == si_seq[si_i]) {
          if (++si_i == si_len) { if (++si_round == 3) mwin(); else { si_len++; si_ph = 0; si_t = -45; } }
        } else { say("ERROR - OTRA VEZ"); si_ph = 0; si_t = -40; }
      }
    }
    break;
  case 3:
    if (ca_lock > 0) ca_lock--;
    if ((mpress & PAD_LEFT) && ca_cur % 3 > 0) ca_cur--;
    if ((mpress & PAD_RIGHT) && ca_cur % 3 < 2) ca_cur++;
    if ((mpress & PAD_UP) && ca_cur >= 3) ca_cur -= 3;
    if ((mpress & PAD_DOWN) && ca_cur < 6) ca_cur += 3;
    if ((mpress & PAD_CROSS) && !ca_lock) {
      if (ca_cur == ca_bad) { if (++ca_round == 3) mwin(); else { ca_bad = rand() % 9; say("CORRECTO"); } }
      else { say("NO ES ESA"); ca_lock = 40; }
    }
    break;
  case 4: mp += DT / 5.f; if (mp >= 1) mwin(); break;
  case 5:
    if (!dl_started) { if (mpress & PAD_CROSS) dl_started = 1; }
    else { mp += DT / 6.f; if (mp >= 1) mwin(); }
    break;
  case 6: {
    float spd = mvx > .2f ? (fabsf(ax) > .2f ? ax * 2.4f : 1.3f) : 0;
    if (spd > 0) { sw_x += spd * DT; sw_t += DT; }
    else { sw_x -= 3 * DT; if (sw_x <= 0) { sw_x = 0; sw_t = 0; } }
    if (sw_x >= 1) {
      if (sw_t < .5f) say("MUY RAPIDO"); else if (sw_t > 1.1f) say("MUY LENTO"); else mwin();
      if (!mstate) { sw_x = 0; sw_t = 0; }
    }
    break; }
  case 7:
    if ((mpress & PAD_LEFT) && bx_cur > 0) bx_cur--;
    if ((mpress & PAD_RIGHT) && bx_cur < 4) bx_cur++;
    if (mpress & PAD_CROSS) {
      if (bx_sel < 0) bx_sel = bx_cur;
      else { int t = bx_n[bx_sel]; bx_n[bx_sel] = bx_n[bx_cur]; bx_n[bx_cur] = t; bx_sel = -1; if (crates_sorted()) mwin(); }
    }
    break;
  case 8:
    if (mpress & PAD_UP) { if (wr_ph == 0) wr_li = wr_step(wr_li, -1, 0); else wr_ri = wr_step(wr_ri, -1, 1); }
    if (mpress & PAD_DOWN) { if (wr_ph == 0) wr_li = wr_step(wr_li, 1, 0); else wr_ri = wr_step(wr_ri, 1, 1); }
    if ((mpress & PAD_LEFT) && wr_ph == 1) wr_ph = 0;
    if (mpress & PAD_CROSS) {
      if (wr_ph == 0) { wr_ph = 1; wr_ri = wr_step(-1, 1, 1); }
      else if (wr_r[wr_ri] == wr_l[wr_li]) {
        wr_link[wr_li] = wr_ri; wr_ph = 0;
        int all = 1; for (int k = 0; k < 4; k++) if (wr_link[k] < 0) all = 0;
        if (all) mwin(); else wr_li = wr_step(wr_li, 1, 0);
      } else { say("COLOR NO COINCIDE"); wr_ph = 0; }
    }
    break;
  case 9:
    cm_f = clamp01(cm_f + mvx * DT * .35f);
    if (1 - fabsf(cm_f - cm_tf) * 6 > .93f) mp += DT * .5f; else { mp -= DT * .4f; if (mp < 0) mp = 0; }
    if (mp >= 1) mwin();
    break;
  case 10:
    as_cx += mvx * DT * 230; as_cy += mvy * DT * 230;
    if (as_cx < PX + 20) as_cx = PX + 20; if (as_cx > PX + PW - 20) as_cx = PX + PW - 20;
    if (as_cy < PY + 50) as_cy = PY + 50; if (as_cy > PY + PH - 50) as_cy = PY + PH - 50;
    for (int k = 0; k < 6; k++) {
      as_x[k] += as_vx[k] * DT; as_y[k] += as_vy[k] * DT;
      if (as_x[k] < PX + 10 || as_x[k] > PX + PW - 10 || as_y[k] < PY + 40 || as_y[k] > PY + PH - 40) spawn_ast(k);
    }
    if (as_boom > 0) as_boom--;
    if (mpress & PAD_CROSS) {
      as_boom = 6;
      for (int k = 0; k < 6; k++) if (hypotf(as_x[k] - as_cx, as_y[k] - as_cy) < 20) { as_kills++; spawn_ast(k); break; }
    }
    if (as_kills >= 8) mwin();
    break;
  case 11:
    lf_cx += mvx * DT * 200; lf_cy += mvy * DT * 200;
    if (lf_cx < PX + 20) lf_cx = PX + 20; if (lf_cx > PX + PW - 20) lf_cx = PX + PW - 20;
    if (lf_cy < PY + 55) lf_cy = PY + 55; if (lf_cy > PY + PH - 45) lf_cy = PY + PH - 45;
    if (lf_grab >= 0) {
      lf_x[lf_grab] = lf_cx; lf_y[lf_grab] = lf_cy;
      if (lf_cx > PX + PW - 70) { lf_alive[lf_grab] = 0; lf_grab = -1; lf_out++; }
    }
    if (mpress & PAD_CROSS) {
      if (lf_grab >= 0) lf_grab = -1;
      else for (int k = 0; k < 6; k++) if (lf_alive[k] && hypotf(lf_x[k] - lf_cx, lf_y[k] - lf_cy) < 18) { lf_grab = k; break; }
    }
    if (lf_out >= 6) mwin();
    break;
  case 12: {
    int dx = ((mpress & PAD_RIGHT) ? 1 : 0) - ((mpress & PAD_LEFT) ? 1 : 0), dy = ((mpress & PAD_DOWN) ? 1 : 0) - ((mpress & PAD_UP) ? 1 : 0);
    if (dx || dy) {
      int best = -1; float bc = 1e9f;
      for (int k = 0; k < 7; k++) {
        if (k == sh_cur) continue;
        float rx = SHP[k][0] - SHP[sh_cur][0], ry = SHP[k][1] - SHP[sh_cur][1];
        float al = rx * dx + ry * dy; if (al <= 0) continue;
        float c = al + fabsf(rx * dy - ry * dx) * 1.5f;
        if (c < bc) { bc = c; best = k; }
      }
      if (best >= 0) sh_cur = best;
    }
    if (mpress & PAD_CROSS) sh_on[sh_cur] = 1;
    int all = 1; for (int k = 0; k < 7; k++) if (!sh_on[k]) all = 0;
    if (all) mwin();
    break; }
  case 13:
    nv_y = clamp01(nv_y + mvy * DT * .7f);
    if (fabsf(nv_y - nv_path(nv_p)) < .07f) nv_p += DT * .17f; else { nv_p -= DT * .25f; if (nv_p < 0) nv_p = 0; }
    if (nv_p >= 1) mwin();
    break;
  }
}

/* --- dibujo de minijuegos --- */
static void bar(float x, float y, float w, float h, float v, u64 col) {
  rect(x - 2, y - 2, x + w + 2, y + h + 2, C(91,151,255)); rect(x, y, x + w, y + h, C(4,10,40));
  rect(x, y, x + w * clamp01(v), y + h, col);
}
static void wire(float x1, float y1, float x2, float y2, u64 c) {
  for (int i = 0; i <= 24; i++) { float t = i / 24.f, x = x1 + (x2 - x1) * t, y = y1 + (y2 - y1) * t; rect(x - 2, y - 2, x + 2, y + 2, c); }
}
static void mini_draw(void) {
  u64 yel = C(255,210,58), grn = C(109,255,176), wht = C(255,255,255);
  rect(PX - 4, PY - 4, PX + PW + 4, PY + PH + 4, C(91,151,255)); rect(PX, PY, PX + PW, PY + PH, C(10,25,70));
  text(PX + 14, PY + 12, 2, wht, TN[mini]);
  text(PX + 14, PY + PH - 16, 1, C(160,190,255), HINT[mini]); text(PX + PW - 50, PY + PH - 16, 1, C(255,120,130), "O SALIR");
  switch (mini) {
  case 0:
    for (int k = 0; k < 2; k++) {
      float x = PX + 120 + k * 200, y = PY + 60 + en_v[k] * 190;
      rect(x - 8, PY + 60, x + 8, PY + 250, C(30,50,110));
      rect(x - 34, PY + 154, x + 34, PY + 156, grn);
      rect(x - 26, y - 8, x + 26, y + 8, en_lock[k] ? grn : (k == en_sel ? yel : C(200,210,240)));
      text(x - 28, PY + 40, 2, en_lock[k] ? grn : wht, k ? "MOTOR 2" : "MOTOR 1");
    }
    break;
  case 1:
    rect(PX + PW / 2 - 52, PY + 48, PX + PW / 2 + 52, PY + 252, C(91,151,255)); rect(PX + PW / 2 - 50, PY + 50, PX + PW / 2 + 50, PY + 250, C(4,10,40));
    rect(PX + PW / 2 - 50, PY + 250 - 200 * clamp01(mp), PX + PW / 2 + 50, PY + 250, C(255,150,40));
    { char b[16]; snprintf(b, sizeof b, "%d%%", (int)(clamp01(mp) * 100)); text(PX + PW / 2 - strlen(b) * 6, PY + 140, 3, wht, b); }
    break;
  case 2: {
    static const u8 pc[4][3] = {{60,220,100},{230,60,70},{250,220,60},{60,120,255}};
    static const int ox[4] = {0, 80, 0, -80}, oy[4] = {-76, 0, 76, 0};
    for (int k = 0; k < 4; k++) {
      float x = PX + PW / 2 + ox[k], y = PY + 150 + oy[k]; int lit = (si_lit == k);
      rect(x - 34, y - 28, x + 34, y + 28, lit ? wht : C(20,30,60));
      rect(x - 30, y - 24, x + 30, y + 24, lit ? C(pc[k][0], pc[k][1], pc[k][2]) : C(pc[k][0] / 3, pc[k][1] / 3, pc[k][2] / 3));
    }
    { char b[24]; snprintf(b, sizeof b, "RONDA %d/3", si_round < 2 ? si_round + 1 : 3); text(PX + 14, PY + 40, 2, C(190,215,255), b);
      text(PX + PW - 130, PY + 40, 2, si_ph ? grn : yel, si_ph ? "TU TURNO" : "MEMORIZA"); }
    break; }
  case 3: {
    for (int k = 0; k < 9; k++) {
      float x = PX + 47 + (k % 3) * 118, y = PY + 50 + (k / 3) * 72;
      rect(x, y, x + 110, y + 64, k == ca_cur ? yel : C(40,70,150)); rect(x + 3, y + 3, x + 107, y + 61, C(10,18,40));
      for (int n = 0; n < 4; n++) { float nx = x + 4 + (rand() % 100), ny = y + 4 + (rand() % 54); rect(nx, ny, nx + 6, ny + 1, C(40,60,100)); }
      int hx = (k * 37) % 70 + 20, hy = (k * 53) % 24 + 12;
      u64 fc = (k == ca_bad && (((int)(gt * 6)) & 1)) ? C(255,50,60) : C(120,135,160);
      rect(x + hx, y + hy, x + hx + 12, y + hy + 26, fc);
    }
    { char b[24]; snprintf(b, sizeof b, "RONDA %d/3", ca_round < 2 ? ca_round + 1 : 3); text(PX + PW - 100, PY + 36, 1, C(190,215,255), b); }
    break; }
  case 4: {
    float sy = PY + 70 + (sinf(mt * 3) * .5f + .5f) * 130;
    rect(PX + PW / 2 - 26, PY + 62, PX + PW / 2 + 26, PY + 112, C(60,110,200));
    rect(PX + PW / 2 - 44, PY + 114, PX + PW / 2 + 44, PY + 200, C(60,110,200));
    rect(PX + 60, sy, PX + PW - 60, sy + 3, grn); rect(PX + 60, sy + 3, PX + PW - 60, sy + 12, C(30,120,90));
    bar(PX + 60, PY + 228, PW - 120, 14, mp, grn);
    break; }
  case 5: {
    static const char *fl[6] = {"RUTA.DAT","MAPA.BIN","TAREAS.LOG","CAMARAS.VID","ESCUDOS.CFG","NAV.SYS"};
    if (!dl_started) { rect(PX + 140, PY + 110, PX + 300, PY + 160, yel); text(PX + 160, PY + 126, 3, C(30,20,0), "INICIAR"); }
    else for (int k = 0; k < 4; k++) { int i = ((int)(mp * 30) + k) % 6; text(PX + 60, PY + 70 + k * 22, 2, C(190,215,255), fl[i]); }
    bar(PX + 60, PY + 228, PW - 120, 14, mp, C(90,170,255));
    break; }
  case 6: {
    rect(PX + 30, PY + 110, PX + 410, PY + 190, C(30,50,110)); rect(PX + 40, PY + 138, PX + 400, PY + 162, C(5,10,30));
    rect(PX + 360, PY + 70, PX + 400, PY + 100, sw_x > 0 ? C(255,200,60) : C(200,50,60));
    float cx = PX + 50 + sw_x * 260;
    rect(cx, PY + 120, cx + 70, PY + 168, C(240,240,250)); rect(cx, PY + 140, cx + 70, PY + 150, C(60,60,90)); rect(cx + 6, PY + 126, cx + 22, PY + 134, yel);
    bar(PX + 60, PY + 228, PW - 120, 14, sw_x, grn);
    break; }
  case 7:
    for (int k = 0; k < 5; k++) {
      float x = PX + 30 + k * 80, y = PY + 100 + (bx_sel == k ? -16 : 0);
      rect(x, y, x + 64, y + 64, k == bx_cur ? yel : C(120,80,40)); rect(x + 4, y + 4, x + 60, y + 60, C(170,120,60)); rect(x + 4, y + 28, x + 60, y + 34, C(130,90,45));
      char b[4]; snprintf(b, sizeof b, "%d", bx_n[k]); text(x + 22, y + 20, 4, C(40,20,0), b);
    }
    rect(PX + 30 + bx_cur * 80, PY + 176, PX + 94 + bx_cur * 80, PY + 180, yel);
    break;
  case 8:
    for (int k = 0; k < 4; k++) {
      float y = PY + 70 + k * 48, lx = PX + 60, rx = PX + PW - 60;
      rect(lx - 26, y - 8, lx, y + 8, C(WC[wr_l[k]][0], WC[wr_l[k]][1], WC[wr_l[k]][2]));
      rect(rx, y - 8, rx + 26, y + 8, C(WC[wr_r[k]][0], WC[wr_r[k]][1], WC[wr_r[k]][2]));
      if (wr_ph == 0 && k == wr_li) rect(lx - 32, y - 12, lx - 28, y + 12, yel);
      if (wr_ph == 1 && k == wr_ri) rect(rx + 30, y - 12, rx + 34, y + 12, yel);
    }
    for (int k = 0; k < 4; k++) if (wr_link[k] >= 0) {
      const u8 *c = WC[wr_l[k]]; wire(PX + 60, PY + 70 + k * 48, PX + PW - 60, PY + 70 + wr_link[k] * 48, C(c[0], c[1], c[2]));
    }
    if (wr_ph == 1) { const u8 *c = WC[wr_l[wr_li]]; wire(PX + 60, PY + 70 + wr_li * 48, PX + PW - 60, PY + 70 + wr_ri * 48, C(c[0] / 2, c[1] / 2, c[2] / 2)); }
    break;
  case 9: {
    float sig = 1 - fabsf(cm_f - cm_tf) * 6; if (sig < 0) sig = 0;
    for (int i = 0; i < 76; i++) {
      float x = PX + 30 + i * 5;
      float y1 = PY + 110 - sinf(i * .045f * (1 + cm_tf * 5)) * 30, y2 = PY + 185 - sinf(i * .045f * (1 + cm_f * 5)) * 30;
      rect(x, y1, x + 3, y1 + 3, C(60,220,230)); rect(x, y2, x + 3, y2 + 3, yel);
    }
    text(PX + 30, PY + 46, 1, C(60,220,230), "SENAL OBJETIVO"); text(PX + 30, PY + 140, 1, yel, "TU SINTONIA");
    bar(PX + 60, PY + 238, PW - 120, 8, sig, sig > .93f ? grn : C(255,120,90)); bar(PX + 60, PY + 256, PW - 120, 6, mp, grn);
    break; }
  case 10: {
    rect(PX + 18, PY + 48, PX + PW - 18, PY + PH - 40, C(80,110,190)); rect(PX + 20, PY + 50, PX + PW - 20, PY + PH - 42, C(3,6,24));
    for (int k = 0; k < 6; k++) { rect(as_x[k] - 12, as_y[k] - 12, as_x[k] + 12, as_y[k] + 12, C(150,120,90)); rect(as_x[k] - 6, as_y[k] - 6, as_x[k] + 6, as_y[k] + 6, C(100,80,60)); }
    u64 cc = as_boom ? C(255,90,60) : grn;
    rect(as_cx - 14, as_cy - 1, as_cx - 4, as_cy + 1, cc); rect(as_cx + 4, as_cy - 1, as_cx + 14, as_cy + 1, cc);
    rect(as_cx - 1, as_cy - 14, as_cx + 1, as_cy - 4, cc); rect(as_cx - 1, as_cy + 4, as_cx + 1, as_cy + 14, cc);
    if (as_boom) rect(as_cx - 3, as_cy - 3, as_cx + 3, as_cy + 3, C(255,240,200));
    { char b[16]; snprintf(b, sizeof b, "%d/8", as_kills); text(PX + PW - 60, PY + 30, 2, yel, b); }
    break; }
  case 11: {
    rect(PX + 18, PY + 48, PX + PW - 18, PY + PH - 40, C(90,100,120)); rect(PX + 20, PY + 50, PX + PW - 70, PY + PH - 42, C(30,38,55));
    for (int i = 0; i < 12; i++) { rect(PX + 20 + i * 30, PY + 50, PX + 21 + i * 30, PY + PH - 42, C(45,55,80)); }
    rect(PX + PW - 68, PY + 50, PX + PW - 20, PY + PH - 42, C(20,70,60)); text(PX + PW - 56, PY + 140, 3, grn, ">>");
    for (int k = 0; k < 6; k++) if (lf_alive[k]) { rect(lf_x[k] - 12, lf_y[k] - 6, lf_x[k] + 12, lf_y[k] + 6, C(70,170,60)); rect(lf_x[k] - 10, lf_y[k] - 1, lf_x[k] + 10, lf_y[k] + 1, C(40,110,40)); }
    rect(lf_cx - 6, lf_cy - 6, lf_cx + 6, lf_cy + 6, lf_grab >= 0 ? yel : wht);
    { char b[16]; snprintf(b, sizeof b, "%d/6", lf_out); text(PX + PW - 60, PY + 30, 2, yel, b); }
    break; }
  case 12:
    for (int k = 0; k < 7; k++) {
      float x = PX + 70 + SHP[k][0] * 120, y = PY + 62 + SHP[k][1] * 72;
      rect(x - 4, y - 4, x + 104, y + 54, k == sh_cur ? yel : C(30,50,110));
      rect(x, y, x + 100, y + 50, sh_on[k] ? C(225,235,255) : C(205,50,70));
    }
    break;
  case 13: {
    float x0 = PX + 30, y0 = PY + 60;
    for (int i = 0; i <= 76; i++) { float p = i / 76.f; rect(x0 + p * 380 - 1, y0 + nv_path(p) * 190 - 1, x0 + p * 380 + 2, y0 + nv_path(p) * 190 + 2, C(60,220,230)); }
    int on = fabsf(nv_y - nv_path(nv_p)) < .07f;
    rect(x0 + nv_p * 380 - 10, y0 + nv_y * 190 - 6, x0 + nv_p * 380 + 10, y0 + nv_y * 190 + 6, on ? grn : C(255,90,90));
    rect(x0 + nv_p * 380 + 10, y0 + nv_y * 190 - 2, x0 + nv_p * 380 + 16, y0 + nv_y * 190 + 2, on ? grn : C(255,90,90));
    rect(x0 + 378, y0 - 10, x0 + 384, y0 + 200, yel);
    bar(PX + 60, PY + 262, PW - 120, 8, nv_p, grn);
    break; }
  }
  if (mmsgt > 0 && !mstate) text(PX + PW / 2 - strlen(mmsg) * 4, PY + 40, 2, C(255,120,130), mmsg);
  if (mstate) { rect(PX + 40, PY + 120, PX + PW - 40, PY + 180, C(18,90,60)); text(PX + PW / 2 - 60 * 1.5f, PY + 138, 3, wht, "COMPLETADO"); }
}



static void update(void) {
  Ch *p = &ch[0]; gt += DT; cd -= DT; nearTask = -1; nearBody = -1; nearKill = -1;
  for (int i = 0; i < 6; i++) { ch[i].ox = ch[i].x; ch[i].oz = ch[i].y; }
  if (p->alive && mini >= 0) mini_update();             /* jugando: el mundo sigue, tu personaje no se mueve */
  else if (p->alive) {
    float mx = ax + ((btns & PAD_RIGHT) ? 1 : 0) - ((btns & PAD_LEFT) ? 1 : 0);
    float my = ay + ((btns & PAD_DOWN) ? 1 : 0) - ((btns & PAD_UP) ? 1 : 0);
    float m = hypotf(mx, my); if (m > 1) { mx /= m; my /= m; }
    float nx = p->x + mx * PSPD * DT, ny = p->y + my * PSPD * DT;
    if (walk(nx, p->y)) p->x = nx; if (walk(p->x, ny)) p->y = ny;
    if (m > .1f) p->tyaw = atan2f(-mx, -my);
    if (!p->imp) {
      for (int k = 0; k < 3; k++)
        if (!p->td[k] && hypotf(p->x - SK_STN[p->tn[k]][0], p->y - SK_STN[p->tn[k]][1]) < 1.8f) nearTask = k;
      if (nearTask >= 0 && (pressed & PAD_CROSS)) mini_open(nearTask);
    } else {                                              /* impostor: sin tareas; X mata al tripulante mas cercano */
      float bd = 1.9f;
      for (int i = 1; i < 6; i++)
        if (ch[i].alive && !ch[i].imp) { float d = hypotf(p->x - ch[i].x, p->y - ch[i].y); if (d < bd) { bd = d; nearKill = i; } }
      if (nearKill >= 0 && cd <= 0 && (pressed & PAD_CROSS)) { if (online) kill_online(&ch[nearKill]); else kill(p, &ch[nearKill]); }
    }
    for (int i = 0; i < nb; i++) if (hypotf(p->x - bodies[i].x, p->y - bodies[i].y) < 3.2f) nearBody = i;
    if ((pressed & PAD_CIRCLE) && nearBody >= 0) { if (online) online_report(nearBody); else { meeting(0, bodies[nearBody].i); return; } }
  }
  if (online) remote_update(); else for (int i = 1; i < 6; i++) if (ch[i].alive) ai(&ch[i]);
  for (int i = 0; i < 6; i++) if (ch[i].alive) { turn(&ch[i]); anim(&ch[i], i == 0); }
  if (state != PLAY) return;
  if (online) return;                                           /* online: reportes, victoria y derrota las decide el servidor */
  for (int i = 1; i < 6; i++)
    if (ch[i].alive && !ch[i].imp)
      for (int b = 0; b < nb; b++)
        if (state == PLAY && hypotf(ch[i].x - bodies[b].x, ch[i].y - bodies[b].y) < 5.5f && frand() < DT * 1.2f) { meeting(i, bodies[b].i); return; }
  int crew = 0; for (int i = 0; i < 6; i++) if (ch[i].alive && !ch[i].imp) crew++;
  if (done >= total) finish_team(1, "TAREAS COMPLETADAS");
  else if (crew <= 1) finish_team(0, "GANO EL IMPOSTOR");
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
/* ---------- personaje 3D (malla de crew_model.h) ----------
   Cada personaje se dibuja como UN objeto: se proyectan sus vertices, se descartan
   las caras traseras (orientacion en pantalla), se ordenan de lejos a cerca y se pintan. */
/* ---------- niebla de vision ----------
   Solo ves lo que tienes a la vista: se lanzan rayos desde tu personaje y las paredes los cortan.
   Lo que queda fuera (otros cuartos, personajes, cuerpos) no se dibuja, asi no ves quien mata a quien. */
#define VIS_R 9.5f
static u8 los[SK_GH][SK_GW];                  /* 1 = hay piso (la vista pasa), 0 = vacio/pared (la vista se corta) */
static u16 vseen[SK_GH][SK_GW], vgen;         /* celdas vistas en este cuadro */
static float vdx[180], vdz[180];
static int fog_on;
static float fogx, fogz;
static float char_lit = 1.f;                  /* oscurece personajes lejanos (lo usa draw_crew) */
static void vis_init(void) {
  for (int j = 0; j < SK_GH; j++)
    for (int i = 0; i < SK_GW; i++) {
      float x = SK_GX0 + (i + .5f) * SK_CELL, z = SK_GZ0 + (j + .5f) * SK_CELL; u8 in = 0;
      for (int f = 0; f < SK_NFLOOR; f++)
        if (x >= SK_FL[f][0] && x <= SK_FL[f][2] && z >= SK_FL[f][1] && z <= SK_FL[f][3]) { in = 1; break; }
      los[j][i] = in;
    }
  for (int k = 0; k < 180; k++) { vdx[k] = cosf(k * 0.034907f); vdz[k] = sinf(k * 0.034907f); }   /* un rayo cada 2 grados */
}
static void vis_update(float px, float pz) {
  fogx = px; fogz = pz;
  if (++vgen == 0) { memset(vseen, 0, sizeof vseen); vgen = 1; }
  for (int k = 0; k < 180; k++)
    for (float d = 0; d <= VIS_R; d += .35f) {
      int i = (int)floorf((px + vdx[k] * d - SK_GX0) / SK_CELL), j = (int)floorf((pz + vdz[k] * d - SK_GZ0) / SK_CELL);
      if (i < 0 || j < 0 || i >= SK_GW || j >= SK_GH) break;
      vseen[j][i] = vgen;
      if (!los[j][i]) break;                  /* pared: el rayo se detiene (la pared misma si se ve) */
    }
}
static int vis_cell(int i, int j) { return i >= 0 && j >= 0 && i < SK_GW && j < SK_GH && vseen[j][i] == vgen; }
static int vis_at(float x, float z) {         /* este punto esta a la vista? */
  if (!fog_on || hypotf(x - fogx, z - fogz) < 1.3f) return 1;
  return vis_cell((int)floorf((x - SK_GX0) / SK_CELL), (int)floorf((z - SK_GZ0) / SK_CELL));
}
static int vis_near(float x, float z) {       /* idem, tolerando 1 celda de margen (para paredes y baldosas) */
  int i = (int)floorf((x - SK_GX0) / SK_CELL), j = (int)floorf((z - SK_GZ0) / SK_CELL);
  for (int dj = -1; dj <= 1; dj++) for (int di = -1; di <= 1; di++) if (vis_cell(i + di, j + dj)) return 1;
  return 0;
}
static float fogk(float x, float z) {         /* 1 cerca de ti, se oscurece hacia el borde de tu vision */
  if (!fog_on) return 1.f;
  float k = (VIS_R - hypotf(x - fogx, z - fogz)) / (VIS_R * .45f);
  return k < .18f ? .18f : (k > 1.f ? 1.f : k);
}

#define MAXV (CM_NV > CD_NV ? CM_NV : CD_NV)
#define MAXF (CM_NF > CD_NF ? CM_NF : CD_NF)
static float cvx[MAXV], cvy[MAXV], cvz[MAXV];
static u8 cvok[MAXV];
static float cfk[MAXF];
static u16 cfo[MAXF];
static int cmp_cf(const void *a, const void *b) {
  float d = cfk[*(const u16 *)b] - cfk[*(const u16 *)a];
  return d > 0 ? 1 : (d < 0 ? -1 : 0);
}
#define RUN_LEGH  .42f   /* altura (m) hasta donde llegan las piernas */
#define RUN_SWING .22f   /* cuanto avanzan/retroceden los pies (m) */
#define RUN_LIFT  .12f   /* cuanto se levanta el pie (m) */
#define CM_CULL >   /* una cara es visible si su area en pantalla es negativa (y hacia abajo) */
#define LX .30f
#define LY .80f
#define LZ .52f
/* dead=1: cuerpo sin vida = mitad inferior cortada, con el hueso saliendo (malla CD_*) */
static void draw_crew(float wx, float wz, float yaw, int dead, int skin, float ph, float run) {
  const float (*V)[3] = dead ? CD_V : CM_V;
  const u16 (*F)[4] = dead ? CD_F : CM_F;
  const u8 *K = dead ? CD_K : CM_K;
  const signed char (*N)[3] = dead ? CD_N : CM_N;
  int nv = dead ? CD_NV : CM_NV, nf = dead ? CD_NF : CM_NF;
  float cy = cosf(yaw), sy = sinf(yaw), zc;
  /* animacion de correr (solo vivo): piernas alternadas (balanceo + levantar), rebote, inclinacion y balanceo del cuerpo */
  float rk = dead ? 0.f : run, s = 0, c = 1, cl = 1, sl = 0, cr = 1, sr = 0, bob = 0;
  if (rk > .01f) {
    s = sinf(ph); c = cosf(ph);
    float lean = .13f * rk, roll = .10f * s * rk;
    cl = cosf(lean); sl = sinf(lean); cr = cosf(roll); sr = sinf(roll); bob = fabsf(c) * .07f * rk;
  }
  for (int v = 0; v < nv; v++) {
    float px = V[v][0], py = V[v][1], pz = V[v][2];
    if (rk > .01f) {
      float w = 1.f - py / RUN_LEGH;                                     /* 1 en los pies, 0 en la entrepierna */
      if (w > 0) {
        float side = px / .12f; side = side < -1 ? -1 : (side > 1 ? 1 : side);   /* -1 pierna izquierda, +1 derecha */
        pz += RUN_SWING * side * s * w * rk;                                /* adelante/atras (el frente es -Z) */
        float lift = -side * c; if (lift > 0) py += RUN_LIFT * lift * w * rk;    /* levanta el pie al avanzar */
      }
      float z2 = pz * cl - py * sl, y2 = py * cl + pz * sl; pz = z2; py = y2;    /* inclinacion hacia adelante */
      float x2 = px * cr - py * sr; y2 = px * sr + py * cr; px = x2; py = y2 + bob;   /* balanceo lateral + rebote */
    }
    cvok[v] = proj(wx + px * cy + pz * sy, py, wz - px * sy + pz * cy, &cvx[v], &cvy[v], &zc);
    cvz[v] = zc;
  }
  int n = 0;
  for (int f = 0; f < nf; f++) {
    int a = F[f][0], b = F[f][1], c = F[f][2], d = F[f][3];
    if (!(cvok[a] & cvok[b] & cvok[c] & cvok[d])) continue;
    if ((K[f] == 3 || K[f] == 2) && N[f][2] > 10) continue;       /* caras traseras del visor: van por dentro del cuerpo, nunca se ven */
    float cr = (cvx[b] - cvx[a]) * (cvy[c] - cvy[a]) - (cvy[b] - cvy[a]) * (cvx[c] - cvx[a]);
    if (cr CM_CULL 0) continue;                                   /* cara trasera */
    cfk[n] = (cvz[a] + cvz[b] + cvz[c] + cvz[d]) * .25f; cfo[n++] = f;
  }
  qsort(cfo, n, sizeof(u16), cmp_cf);
  for (int i = 0; i < n; i++) {
    int f = cfo[i], a = F[f][0], b = F[f][1], c = F[f][2], d = F[f][3];
    float nx = N[f][0] / 127.f, ny = N[f][1] / 127.f, nz = N[f][2] / 127.f;
    float wnx = nx * cy + nz * sy, wnz = -nx * sy + nz * cy;
    float dl = wnx * LX + ny * LY + wnz * LZ; if (dl < 0) dl = 0;
    int k = K[f];
    float sh = k == 3 ? .70f + .30f * dl : (k == 4 ? .60f + .40f * dl : (k == 5 ? .50f : .42f + .58f * dl));
    sh *= char_lit;
    const u8 *rgb = (k == 0 || k == 5) ? CM_SKIN_RGB[skin] : CM_KIND_RGB[k];
    int r = (int)(rgb[0] * sh), g = (int)(rgb[1] * sh), bl = (int)(rgb[2] * sh);
    if (r > 255) r = 255; if (g > 255) g = 255; if (bl > 255) bl = 255;
    gsKit_prim_quad(gs, cvx[a], cvy[a], cvx[b], cvy[b], cvx[d], cvy[d], cvx[c], cvy[c], 1, C(r, g, bl));
  }
}
/* true si el punto (x, 0.75 m, z) cae dentro (o cerca) de la pantalla */
static int on_screen(float x, float z, float *sx, float *sy) {
  float zc;
  if (!proj(x, .75f, z, sx, sy, &zc)) return 0;
  return *sx > -150 && *sx < SW + 150 && *sy > -150 && *sy < SH + 150;
}

static void draw_char(int i) {
  Ch *c = &ch[i]; float x, y, z;
  if (!on_screen(c->x, c->y, &x, &y)) return;
  char_lit = i ? fogk(c->x, c->y) : 1.f;
  draw_crew(c->x, c->y, c->yaw, 0, c->skin, c->ph, c->run);
  char_lit = 1.f;
  if (proj(c->x, 1.65f, c->y, &x, &y, &z))
    text(x - strlen(nm_of(i)) * 2, y - 10, 1, i ? C(255,210,58) : C(255,255,255), nm_of(i));
}

#define MAXI 1600
enum { IT_FACE, IT_BODY, IT_CHAR, IT_TASK };
static float keyz[MAXI], ipos[MAXI][8], ifk[MAXI];
static u16 ord[MAXI], iidx[MAXI];
static u8 ityp[MAXI];
static int cmp_far_first(const void *a, const void *b) {
  float d = keyz[*(const u16 *)b] - keyz[*(const u16 *)a];
  return d > 0 ? 1 : (d < 0 ? -1 : 0);
}
static const float NTAB[5][3] = {{0,1,0},{0,0,1},{0,0,-1},{1,0,0},{-1,0,0}};

/* dibuja el mapa 3D (pisos -> paredes/props/personajes ordenados de lejos a cerca) */
static const SkTile *g_tile = SK_TILE; static int g_ntile = SK_NTILE;      /* geometria activa: el mapa o la nave del lobby */
static const SkFace *g_face = SK_FACE; static int g_nface = SK_NFACE;
static void draw_world(float tx, float tz, float cull, int chars) {
  float sx[4], sy[4], zc[4];
  /* 1) pisos: nunca se solapan entre si, van primero */
  for (int t = 0; t < g_ntile; t++) {
    const SkTile *T = &g_tile[t];
    float tcx = (T->x0 + T->x1) * .5f, tcz = (T->z0 + T->z1) * .5f, tk = 1.f;
    if (fabsf(tcx - tx) > cull || fabsf(tcz - tz) > cull) continue;
    if (fog_on) { if (hypotf(tcx - fogx, tcz - fogz) > VIS_R + .8f || !vis_near(tcx, tcz)) continue; tk = fogk(tcx, tcz); }
    int ok = proj(T->x0, 0, T->z0, &sx[0], &sy[0], &zc[0]) & proj(T->x1, 0, T->z0, &sx[1], &sy[1], &zc[1]) &
             proj(T->x0, 0, T->z1, &sx[2], &sy[2], &zc[2]) & proj(T->x1, 0, T->z1, &sx[3], &sy[3], &zc[3]);
    if (!ok) continue;
    float mnx = sx[0], mxx = sx[0], mny = sy[0], mxy = sy[0];
    for (int k = 1; k < 4; k++) { if (sx[k] < mnx) mnx = sx[k]; if (sx[k] > mxx) mxx = sx[k]; if (sy[k] < mny) mny = sy[k]; if (sy[k] > mxy) mxy = sy[k]; }
    if (mxx < -200 || mnx > SW + 200 || mxy < -200 || mny > SH + 200 || mxx - mnx > 2500 || mxy - mny > 2500) continue;
    gsKit_prim_quad(gs, sx[0], sy[0], sx[1], sy[1], sx[2], sy[2], sx[3], sy[3], 1, C((int)(T->r * tk), (int)(T->g * tk), (int)(T->b * tk)));
  }
  /* 2) lista de objetos a ordenar */
  int n = 0;
  for (int f = 0; f < g_nface && n < MAXI - 20; f++) {
    const SkFace *F = &g_face[f];
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
    if (fog_on && (hypotf(cx - fogx, cz - fogz) > VIS_R + 1.2f || !vis_near(cx, cz))) continue;   /* fuera de tu vision */
    ifk[n] = fogk(cx, cz);
    for (int k = 0; k < 4; k++) { ipos[n][k * 2] = sx[k]; ipos[n][k * 2 + 1] = sy[k]; }
    float dd = (cx - vc[0]) * vf[0] + (cy - vc[1]) * vf[1] + (cz - vc[2]) * vf[2];
    keyz[n] = dd; ityp[n] = IT_FACE; iidx[n] = f; n++;
  }
  if (chars) {
    float x, y, z;
    for (int i = 0; i < nb && n < MAXI; i++)
      if (vis_at(bodies[i].x, bodies[i].y) && proj(bodies[i].x, .3f, bodies[i].y, &x, &y, &z)) { keyz[n] = z; ityp[n] = IT_BODY; iidx[n] = i; n++; }
    for (int i = 1; i < 6 && n < MAXI; i++)      /* el jugador (0) se dibuja al final: siempre visible */
      if (ch[i].alive && vis_at(ch[i].x, ch[i].y) && proj(ch[i].x, .5f, ch[i].y, &x, &y, &z)) { keyz[n] = z; ityp[n] = IT_CHAR; iidx[n] = i; n++; }
    for (int k = 0; k < 3 && n < MAXI && !ch[0].imp && !lobby_mode; k++)
      if (vis_at(SK_STN[ch[0].tn[k]][0], SK_STN[ch[0].tn[k]][1]) && proj(SK_STN[ch[0].tn[k]][0], 1.3f, SK_STN[ch[0].tn[k]][1], &x, &y, &z)) { keyz[n] = z; ityp[n] = IT_TASK; iidx[n] = k; n++; }
  }
  for (int i = 0; i < n; i++) ord[i] = i;
  qsort(ord, n, sizeof(u16), cmp_far_first);
  /* 3) dibujar de lejos a cerca */
  for (int o = 0; o < n; o++) {
    int i = ord[o];
    switch (ityp[i]) {
    case IT_FACE: {
      const SkFace *F = &g_face[iidx[i]]; const float *q = ipos[i];
      float fk = ifk[i];
      gsKit_prim_quad(gs, q[0], q[1], q[2], q[3], q[6], q[7], q[4], q[5], 1, C((int)(F->r * fk), (int)(F->g * fk), (int)(F->b * fk)));
      break; }
    case IT_BODY: {
      Body *b = &bodies[iidx[i]]; float x, y;
      if (on_screen(b->x, b->y, &x, &y)) { char_lit = fogk(b->x, b->y); draw_crew(b->x, b->y, b->yaw, 1, ch[b->i].skin, 0, 0); char_lit = 1.f; }
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

static void ctext(float y, int sc, u64 col, const char *t) {      /* texto centrado en pantalla */
  text((SW - (int)strlen(t) * 4 * sc + sc) / 2, y, sc, col, t);
}

/* ---------- sala de espera: la "nave" (el "carro" donde esperas a los demas) ---------- */
static SkFace lbf[96];
static SkTile lbt[200];
static int nlbf, nlbt, nlbc;
static float lbc[6][4];
static const float LBX = 8.f, LBZ = 5.f;                 /* mitad del largo y del ancho de la nave */
static void lb_box(float x0, float z0, float x1, float z1, float h, const u8 *top, const u8 *side, int solid) {
  static const float sh[5] = {1.f, .82f, .58f, .72f, .64f};
  float V[5][4][3] = {
    {{x0,h,z0},{x1,h,z0},{x1,h,z1},{x0,h,z1}},
    {{x0,0,z1},{x1,0,z1},{x1,h,z1},{x0,h,z1}},
    {{x0,0,z0},{x1,0,z0},{x1,h,z0},{x0,h,z0}},
    {{x1,0,z0},{x1,0,z1},{x1,h,z1},{x1,h,z0}},
    {{x0,0,z0},{x0,0,z1},{x0,h,z1},{x0,h,z0}} };
  for (int k = 0; k < 5 && nlbf < 96; k++) {
    SkFace *f = &lbf[nlbf++]; const u8 *c = k == 0 ? top : side;
    memcpy(f->v, V[k], sizeof f->v);
    f->r = (u8)(c[0] * sh[k]); f->g = (u8)(c[1] * sh[k]); f->b = (u8)(c[2] * sh[k]); f->n = (u8)k;
  }
  if (solid && nlbc < 6) { lbc[nlbc][0] = x0; lbc[nlbc][1] = z0; lbc[nlbc][2] = x1; lbc[nlbc][3] = z1; nlbc++; }
}
static void lobby_build(void) {
  static const u8 wall[3] = {150,155,172}, wallt[3] = {190,196,214}, cons[3] = {60,100,200}, consT[3] = {80,230,150}, bench[3] = {150,155,172}, benchT[3] = {200,206,222};
  nlbf = nlbt = nlbc = 0;
  for (int i = -8; i < 8; i++) for (int j = -5; j < 5; j++) {
    int stripe = (j == -1 || j == 0); float k = ((i + j) & 1) ? 1.05f : .95f; SkTile *t = &lbt[nlbt++];
    t->x0 = (float)i; t->z0 = (float)j; t->x1 = i + 1.f; t->z1 = j + 1.f;
    t->r = (u8)((stripe ? 128 : 96) * k); t->g = (u8)((stripe ? 144 : 112) * k); t->b = (u8)((stripe ? 172 : 140) * k);
  }
  for (int i = -2; i < 2; i++) for (int j = 5; j < 7; j++) {                /* rampa de la nave (decorativa) */
    SkTile *t = &lbt[nlbt++]; t->x0 = (float)i; t->z0 = (float)j; t->x1 = i + 1.f; t->z1 = j + 1.f; t->r = 110; t->g = 110; t->b = 122;
  }
  lb_box(-8.5f, -5.5f, 8.5f, -5.f, 1.3f, wallt, wall, 0);                    /* paredes */
  lb_box(-8.5f, 5.f, -1.6f, 5.5f, 1.3f, wallt, wall, 0); lb_box(1.6f, 5.f, 8.5f, 5.5f, 1.3f, wallt, wall, 0);
  lb_box(-8.5f, -5.f, -8.f, 5.f, 1.3f, wallt, wall, 0);  lb_box(8.f, -5.f, 8.5f, 5.f, 1.3f, wallt, wall, 0);
  lb_box(-1.4f, -4.7f, 1.4f, -3.6f, .95f, consT, cons, 1);                   /* consola: el anfitrion empieza aqui */
  lb_box(-6.5f, -2.4f, -3.f, -1.6f, .5f, benchT, bench, 1); lb_box(3.f, -2.4f, 6.5f, -1.6f, .5f, benchT, bench, 1);   /* bancas */
  lb_box(-6.5f, 1.6f, -3.f, 2.4f, .5f, benchT, bench, 1);   lb_box(3.f, 1.6f, 6.5f, 2.4f, .5f, benchT, bench, 1);
}
static int lobby_walk(float x, float z) {
  if (fabsf(x) > LBX - .85f || fabsf(z) > LBZ - .85f) return 0;
  for (int i = 0; i < nlbc; i++)
    if (x + .35f > lbc[i][0] && x - .35f < lbc[i][2] && z + .35f > lbc[i][1] && z - .35f < lbc[i][3]) return 0;
  return 1;
}
static void lobby_sync(int fresh) {              /* altas y bajas segun la mascara de presentes que manda el servidor */
  if (my_slot < 0) return;
  slot2li[my_slot] = 0; li2slot[0] = my_slot;
  for (int s = 0; s < 6; s++) {
    if ((lmask >> s) & 1) {
      if (slot2li[s] < 0) { int li = 1; while (li < 6 && li2slot[li] >= 0) li++; if (li >= 6) continue; slot2li[s] = li; li2slot[li] = s; }
      int li = slot2li[s]; Ch *c = &ch[li];
      if (fresh || !c->alive) {
        memset(c, 0, sizeof(Ch)); c->alive = 1; c->yaw = c->tyaw = 3.14159f; c->goal = -1; c->cur = -1;
        c->x = (s - 2.5f) * 1.4f; c->y = 3.3f; rtx[li] = c->x; rtz[li] = c->y;
      }
      c->skin = lskin[s];
    } else if (slot2li[s] >= 0 && s != my_slot) { ch[slot2li[s]].alive = 0; li2slot[slot2li[s]] = -1; slot2li[s] = -1; }
  }
}
static void lobby_enter(void) {
  lobby_build(); lobby_mode = 1; online = 0; nb = 0; mini = -1; fog_on = 0; ping_t = 0; pos_t = 0;
  for (int i = 0; i < 6; i++) ch[i].alive = 0;
  lobby_sync(1);
  state = LOBBY; camx = ch[0].x; camz = ch[0].y;
}
static void lobby_leave(void) {
  net_tx(0x05, NULL, 0); lobby_mode = 0; map_reset(); state = TITLE; tmenu = 4; list_t = 0; join_t = 0; rooms_sel = 0;
  for (int i = 0; i < 6; i++) ch[i].alive = 0;
}
static void lobby_update(void) {
  Ch *p = &ch[0]; gt += DT;
  for (int i = 0; i < 6; i++) { ch[i].ox = ch[i].x; ch[i].oz = ch[i].y; }
  float mx = mvx, my = mvy, m = hypotf(mx, my); if (m > 1) { mx /= m; my /= m; }
  float nx = p->x + mx * PSPD * DT, ny = p->y + my * PSPD * DT;
  if (lobby_walk(nx, p->y)) p->x = nx; if (lobby_walk(p->x, ny)) p->y = ny;
  if (m > .1f) p->tyaw = atan2f(-mx, -my);
  remote_update();
  for (int i = 0; i < 6; i++) if (ch[i].alive) { turn(&ch[i]); anim(&ch[i], i == 0); }
  int host = (my_slot == host_slot), near = hypotf(p->x, p->y + 3.1f) < 2.6f;
  if (host && ((pressed & PAD_START) || (near && (pressed & PAD_CROSS)))) net_tx(0x06, NULL, 0);
  if (pressed & PAD_CIRCLE) lobby_leave();
}
static int popcnt6(int m) { int c = 0; for (int i = 0; i < 6; i++) c += (m >> i) & 1; return c; }

static void draw_lobby(void) {
  Ch *p = &ch[0];
  for (int i = 0; i < 60; i++) { int x = (i * 97 + 31) % SW, y = (i * 53 + (int)(gt * (10 + (i % 5) * 7))) % SH; rect(x, y, x + 2, y + 2, C(90,100,140)); }
  camx += (p->x - camx) * (DT * 8 > 1 ? 1 : DT * 8); camz += (p->y - camz) * (DT * 8 > 1 ? 1 : DT * 8);
  set_view(camx, .8f, camz, 0, 1.1f, 13.5f);
  fog_on = 0; g_tile = lbt; g_ntile = nlbt; g_face = lbf; g_nface = nlbf;
  draw_world(camx, camz, 40.f, 1);
  g_tile = SK_TILE; g_ntile = SK_NTILE; g_face = SK_FACE; g_nface = SK_NFACE;
  char b[64]; int host = (my_slot == host_slot), n = popcnt6(lmask);
  rect(120, 8, SW - 120, 36, C(2,6,23)); snprintf(b, sizeof b, "SALA %d    JUGADORES %d/6", room_id, n); ctext(14, 2, C(190,215,255), b);
  float x, y, z;
  for (int i = 1; i < 6; i++) if (ch[i].alive && li2slot[i] == host_slot && proj(ch[i].x, 2.15f, ch[i].y, &x, &y, &z)) text(x - 12, y - 18, 1, C(255,210,58), "HOST");
  if (proj(p->x, 2.15f, p->y, &x, &y, &z) && host) text(x - 12, y - 18, 1, C(255,210,58), "HOST");
  rect(30, 392, SW - 30, 436, C(2,6,23));
  if (host) ctext(398, 2, n >= 3 ? C(109,255,176) : C(255,210,58), n >= 3 ? "START O X EN LA CONSOLA: EMPEZAR" : "NECESITAS 3 JUGADORES PARA EMPEZAR");
  else ctext(398, 2, C(190,215,255), "ESPERANDO AL ANFITRION...");
  ctext(416, 1, C(160,190,255), "D-PAD MOVER   O SALIR DE LA SALA");
  if (net_msg_t > 0) { rect(100, 44, SW - 100, 72, C(90,20,30)); ctext(52, 2, C(255,255,255), net_msg); }
}

/* ---------- animacion de entrada: tu rol (y solo el tuyo) ----------
   Tripulante: se ve a los 6 en fila (nadie queda "omitido", asi no se delata al impostor).
   Impostor: apareces tu solo. En ningun caso se muestra el rol de los demas. */
#define INTRO_LEN 5.4f
static float ease3(float u) { u = clamp01(u); return 1.f - (1.f - u) * (1.f - u) * (1.f - u); }
static void draw_intro(void) {
  int imp = ch[0].imp; float t = intro_t;
  rect(0, 0, SW, SH, imp ? C(24,3,8) : C(3,8,30));
  for (int i = 0; i < 70; i++) {                                         /* estrellas que van cayendo */
    int x = (i * 97 + 31) % SW, y = (int)(i * 53 + t * (14 + (i % 5) * 9)) % SH;
    rect(x, y, x + 2, y + 2, i % 7 ? C(90,100,140) : C(200,210,255));
  }
  rect(0, 20, SW, 126, imp ? C(70,6,14) : C(8,28,70));                   /* banda del titulo */
  set_view(0, .95f, 0, 0, .15f, 7.5f);
  int lst[6], n = 0;                                                     /* quien aparece y en que orden (el jugador entra al centro-izquierda) */
  if (imp) lst[n++] = 0;
  else {
    int oth[5], no = 0; for (int c = 1; c < 6; c++) if (ch[c].alive) oth[no++] = c;
    int pp = no > 2 ? 2 : 0;
    for (int k = 0; k <= no; k++) { if (k == pp) lst[n++] = 0; if (k < no) lst[n++] = oth[k]; }
  }
  int ord[6]; for (int k = 0; k < n; k++) ord[k] = k;                    /* de atras (laterales) hacia adelante (centro) */
  for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++)
    if (fabsf(ord[j] - (n - 1) * .5f) > fabsf(ord[i] - (n - 1) * .5f)) { int tt = ord[i]; ord[i] = ord[j]; ord[j] = tt; }
  for (int oi = 0; oi < n; oi++) {
    int slot = ord[oi], who = lst[slot];
    float off = slot - (n - 1) * .5f;
    float xt = imp ? 0.f : off * 1.45f, zt = imp ? .6f : -fabsf(off) * .3f;
    float t0 = .25f + (imp ? 0.f : slot * .13f), u = (t - t0) / 1.3f;
    if (t < t0) continue;                                                /* aun no entra */
    float z = zt - 13.f * (1.f - ease3(u)), ph = 0, run = 0;
    if (u < 1.f) { run = 1.f; ph = (t - t0) * STEP_RATE * 3.14159f; }   /* entra corriendo */
    draw_crew(xt, z, 3.14159f, 0, ch[who].skin, ph, run);
    if (who == 0) { float x, y, zc; if (proj(xt, 1.95f, z, &x, &y, &zc)) text(x - 4, y - 8, 1, C(255,255,255), "TU"); }
  }
  if (t > .5f) {
    float e1 = ease3((t - .5f) / .45f);
    ctext(28.f - (1.f - e1) * 90.f, 7, imp ? C(255,60,70) : C(108,225,255), imp ? "IMPOSTOR" : "TRIPULANTE");
  }
  if (t > 1.1f) ctext(96, 3, C(235,240,255), imp ? "ELIMINA A LA TRIPULACION" : "HAY 1 IMPOSTOR ENTRE NOSOTROS");
  if (t > 1.0f && t < INTRO_LEN - .5f && (((int)(t * 2)) & 1)) ctext(410, 2, C(160,190,255), "START PARA SALTAR");
  float k = t < .5f ? t / .5f : (t > INTRO_LEN - .45f ? (INTRO_LEN - t) / .45f : 1.f); k = clamp01(k);   /* cortina */
  float hh = (1.f - k) * SH * .5f;
  if (hh > 0) { rect(0, 0, SW, hh, C(0,0,0)); rect(0, SH - hh, SW, SH, C(0,0,0)); }
}

static void draw(void) {
  gsKit_clear(gs, C(2,6,23));
  if (state == INTRO) { draw_intro(); return; }
  if (state == LOBBY) { draw_lobby(); return; }
  if (state == TITLE) {
    fog_on = 0;
    u64 bg = C(2,6,23), yel = C(255,210,58), wht = C(230,240,255);
    set_view(0, 0, 0, gt * .25f, .8f, 70.f);                 /* camara orbitando el mapa */
    draw_world(0, 0, 80.f, 0);
    const u8 *sk = CM_SKIN_RGB[pskin];
    if (tmenu == 0) {                                         /* ---- menu principal ---- */
      static const char *opt[5] = {"SKIN", "", "JUGAR SOLO", "ONLINE", "CONTROLES"};
      static const char *rn[3] = {"AL AZAR", "TRIPULANTE", "IMPOSTOR"};
      rect(100, 30, SW - 100, 112, bg); ctext(52, 8, C(150,190,255), "SOSPECHOSO");
      rect(150, 118, SW - 150, 148, bg); ctext(125, 3, wht, "ESTILO PS2 - HOMEBREW");
      for (int i = 0; i < 5; i++) {
        float y = 156 + i * 44; char b[40]; const char *lb = opt[i];
        if (i == 1) { snprintf(b, sizeof b, "ROL: %s", rn[prole]); lb = b; }
        rect(170, y, SW - 170, y + 36, i == topt ? yel : C(40,70,170));
        ctext(y + 11, 3, i == topt ? C(30,20,0) : C(255,255,255), lb);
      }
      rect(SW - 170 - 44, 156 + 5, SW - 170 - 12, 156 + 31, C(sk[0], sk[1], sk[2]));   /* muestra de tu skin */
      rect(40, 402, SW - 40, 428, bg); ctext(408, 2, C(160,190,255), "ARRIBA-ABAJO ELEGIR  X ACEPTAR  START JUGAR");
    } else if (tmenu == 1) {                                  /* ---- skin ---- */
      rect(100, 24, SW - 100, 88, bg); ctext(40, 5, C(150,190,255), "ELIGE TU SKIN");
      rect(190, 96, SW - 190, 300, bg);
      set_view(0, .65f, 0, 0, .2f, 6.5f);
      draw_crew(0, 0, gt * 1.2f, 0, pskin, gt * STEP_RATE * 3.14159f, 1.f);   /* en el menu de skin corre en el lugar */
      { char b[40]; snprintf(b, sizeof b, "< %s >", CM_SKIN_NAME[pskin]);
        rect(170, 306, SW - 170, 340, bg); ctext(314, 3, wht, b); }
      { float x0 = (SW - CM_NSKIN * 40 + 12) / 2.f;
        for (int i = 0; i < CM_NSKIN; i++) {
          const u8 *c = CM_SKIN_RGB[i]; float x = x0 + i * 40;
          if (i == pskin) rect(x - 4, 346, x + 32, 376, yel); else rect(x - 2, 348, x + 30, 374, bg);
          rect(x, 350, x + 28, 372, C(c[0], c[1], c[2]));
        } }
      rect(40, 402, SW - 40, 428, bg); ctext(408, 2, C(160,190,255), "IZQ-DER CAMBIAR  X ACEPTAR");
    } else if (tmenu == 3) {                                  /* ---- online: direccion del servidor ---- */
      rect(100, 30, SW - 100, 100, bg); ctext(46, 6, C(150,190,255), "ONLINE");
      rect(70, 112, SW - 70, 340, bg);
      ctext(124, 2, C(190,215,255), "DIRECCION DEL SERVIDOR (IP)");
      for (int k = 0; k < 4; k++) {
        float x = 118 + k * 108; char b[8]; snprintf(b, sizeof b, "%d", net_ip[k]);
        rect(x, 170, x + 88, 218, k == ipsel ? yel : C(40,70,170));
        text(x + (88 - (int)strlen(b) * 12) / 2, 184, 3, k == ipsel ? C(30,20,0) : C(255,255,255), b);
        if (k < 3) rect(x + 94, 210, x + 102, 218, wht);
      }
      if (conn_stage) ctext(246, 2, yel, net_msg);
      else if (net_msg_t > 0) ctext(246, 2, C(255,110,120), net_msg);
      else ctext(246, 2, C(190,215,255), "X = CONECTAR");
      ctext(284, 1, C(160,190,255), "IZQ-DER ELEGIR NUMERO   ARRIBA-ABAJO CAMBIARLO");
#ifndef USE_ONLINE
      ctext(306, 2, C(255,110,120), "ESTA VERSION NO TRAE ONLINE");
#endif
      rect(40, 402, SW - 40, 428, bg); ctext(408, 2, C(160,190,255), "X CONECTAR   O VOLVER");
    } else if (tmenu == 4) {                                  /* ---- online: lista de salas ---- */
      rect(100, 18, SW - 100, 78, bg); ctext(32, 5, C(150,190,255), "SALAS");
      rect(50, 86, SW - 50, 392, bg);
      for (int r = 0; r < 6; r++) {
        int idx = rooms_top + r; if (idx > rooms_n) break;
        float y = 94 + r * 48; int sel = (idx == rooms_sel); char b[64];
        if (idx == 0) { snprintf(b, sizeof b, "+ CREAR SALA"); }
        else { const char *hs = CM_SKIN_NAME[rooms[idx - 1].hs]; snprintf(b, sizeof b, "SALA %d  %s  %d/%d", rooms[idx - 1].id, hs, rooms[idx - 1].n, rooms[idx - 1].mx); }
        int playing = idx > 0 && rooms[idx - 1].st;
        rect(60, y, SW - 60, y + 40, sel ? yel : (idx == 0 ? C(30,110,80) : C(40,70,170)));
        u64 tc = sel ? C(30,20,0) : (playing ? C(130,140,170) : C(255,255,255));
        text(76, y + 12, 3, tc, b);
        if (idx > 0) { const char *st = playing ? "JUGANDO" : "EN ESPERA"; text(SW - 60 - 12 - (int)strlen(st) * 8, y + 14, 2, sel ? C(30,20,0) : (playing ? C(255,120,120) : C(109,255,176)), st); }
      }
      if (rooms_n == 0) ctext(300, 2, C(190,215,255), "NO HAY SALAS: CREA LA PRIMERA");
      if (net_msg_t > 0) { rect(70, 352, SW - 70, 384, C(90,20,30)); ctext(360, 2, C(255,255,255), net_msg); }
      rect(20, 402, SW - 20, 428, bg); ctext(408, 1, C(160,190,255), "ARRIBA-ABAJO ELEGIR  X ENTRAR  TRIANGULO ACTUALIZAR  O SALIR");
    } else {                                                  /* ---- controles ---- */
      static const char *ln[6] = {"D-PAD O STICK  MOVER", "X  USAR TAREA O VOTAR", "X  MATAR (SI ERES IMPOSTOR)", "O  REPORTAR CUERPO", "O  SALIR DE UNA TAREA", "START  JUGAR"};
      rect(90, 40, SW - 90, 390, bg); ctext(56, 5, C(150,190,255), "CONTROLES");
      for (int i = 0; i < 6; i++) text(110, 118 + i * 34, 2, i % 2 ? wht : C(190,215,255), ln[i]);
      { char b[56];
        if (audio_ok) ctext(336, 2, C(109,255,176), "AUDIO ACTIVO - TRIANGULO PARA PROBAR");
        else { static const char *why[5] = {"", "NO COMPILADO (REVISA EL MAKEFILE)", "FALLO EL MODULO DE AUDIO", "FALLO AUDSRV_INIT", "FALLO EL FORMATO"};
               snprintf(b, sizeof b, "AUDIO: %s", why[audio_err > 4 ? 4 : audio_err]); ctext(336, 2, C(255,110,120), b); } }
      rect(40, 402, SW - 40, 428, bg); ctext(408, 2, C(160,190,255), "X VOLVER");
    }
    return;
  }
  Ch *p = &ch[0];
  camx += (p->x - camx) * (DT * 8 > 1 ? 1 : DT * 8); camz += (p->y - camz) * (DT * 8 > 1 ? 1 : DT * 8);
  set_view(camx, .8f, camz, 0, 1.1f, 14.5f);                  /* ~63 grados, siguiendo al jugador */
  fog_on = 1; vis_update(p->x, p->y);                         /* niebla: solo lo que tienes a la vista */
  draw_world(camx, camz, VIS_R + 3.5f, 1);

  float sx, sy, sz;
  if (nearTask >= 0 && proj(p->x, 1.65f, p->y, &sx, &sy, &sz)) {
    text(sx - strlen(TN[p->tn[nearTask]]) * 2, sy - 40, 1, C(255,210,58), TN[p->tn[nearTask]]);
    text(sx - 16, sy - 30, 1, C(255,255,255), "X = USAR");
  }
  if (nearBody >= 0 && state == PLAY) text(240, SH - 30, 2, C(255,90,110), "O = REPORTAR");
  if (nearKill >= 0 && cd <= 0 && state == PLAY) text(250, SH - 54, 2, C(255,70,80), "X = MATAR");

  /* HUD: barra de progreso y tareas */
  rect(10, 10, 10 + 300, 24, C(91,151,255)); rect(12, 12, 12 + 296.0f * done / total, 22, C(109,255,176));
  if (p->imp) {
    char b[40];
    text(10, 32, 1, C(255,70,80), "ERES EL IMPOSTOR");
    if (cd > 0) snprintf(b, sizeof b, "MATAR EN %d", (int)ceilf(cd)); else snprintf(b, sizeof b, "X = MATAR");
    text(10, 44, 1, cd > 0 ? C(220,235,255) : C(255,210,58), b);
  } else for (int k = 0; k < 3; k++) { char b[48]; snprintf(b, sizeof b, "%s %s", p->td[k] ? "OK" : "--", TN[p->tn[k]]); text(10, 32 + k * 12, 1, p->td[k] ? C(109,255,176) : C(220,235,255), b); }
  { int r = room_of(p->x, p->y); const char *nm = r >= 0 ? NM[r] : "PASILLO"; text(SW - 10 - strlen(nm) * 4 * 2, SH - 24, 2, C(150,190,255), nm); }

  /* minimapa (esquina superior derecha, 2 px por metro) */
  { float mx0 = SW - 164, my0 = 10, sc = 2.f, ox = -37.f, oz = -17.f;
    rect(mx0 - 3, my0 - 3, mx0 + 156, my0 + 80, C(91,151,255)); rect(mx0 - 1, my0 - 1, mx0 + 154, my0 + 78, C(4,10,40));
    for (int i = 0; i < SK_NFLOOR; i++)
      rect(mx0 + (SK_FL[i][0] - ox) * sc, my0 + (SK_FL[i][1] - oz) * sc, mx0 + (SK_FL[i][2] - ox) * sc, my0 + (SK_FL[i][3] - oz) * sc, C(40,70,150));
    for (int k = 0; k < 3 && !p->imp; k++) if (!p->td[k]) {
      float x = mx0 + (SK_STN[p->tn[k]][0] - ox) * sc, y = my0 + (SK_STN[p->tn[k]][1] - oz) * sc;
      rect(x - 2, y - 2, x + 2, y + 2, C(255,210,58));
    }
    for (int i = 0; i < nb; i++) { if (!vis_at(bodies[i].x, bodies[i].y)) continue; float x = mx0 + (bodies[i].x - ox) * sc, y = my0 + (bodies[i].y - oz) * sc; rect(x - 2, y - 2, x + 2, y + 2, C(255,60,80)); }
    { float x = mx0 + (p->x - ox) * sc, y = my0 + (p->y - oz) * sc; rect(x - 2, y - 2, x + 3, y + 3, C(255,255,255)); } }

  if (mini >= 0 && state == PLAY) mini_draw();

  if (state == MEET || state == MRES) {
    rect(60, 40, SW - 60, SH - 40, C(10,25,90)); rect(64, 44, SW - 64, SH - 44, C(18,38,110));
    text(90, 62, 3, C(255,255,255), msg); text(90, 100, 2, C(190,215,255), msg2);
    if (state == MEET && online && voted) ctext(372, 2, C(255,210,58), "ESPERANDO A LOS DEMAS VOTOS...");
    if (state == MEET) {
      for (int i = 0; i < nopts; i++) {
        const char *nm = opts[i] < 0 ? "SALTAR VOTO" : nm_of(opts[i]);
        rect(110, 140 + i * 36, SW - 110, 170 + i * 36, i == cur ? C(255,210,58) : C(40,70,170));
        if (opts[i] >= 0) { const u8 *k = CM_SKIN_RGB[ch[opts[i]].skin]; rect(120, 146 + i * 36, 148, 164 + i * 36, C(k[0], k[1], k[2])); }
        text(opts[i] >= 0 ? 160 : 130, 148 + i * 36, 3, i == cur ? C(30,20,0) : C(255,255,255), nm);
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

/* ---------- logica por estado (titulo, menus online, lobby, partida) ---------- */
static void bump_ip(int dir) { net_ip[ipsel] = (u8)(net_ip[ipsel] + dir); }
static void title_update(void) {
  gt += DT;
  if (tmenu <= 2 && (pressed & PAD_START)) { tmenu = 0; start(); return; }
  if (tmenu == 0) {
    if (mpress & PAD_UP) topt = (topt + 4) % 5;
    if (mpress & PAD_DOWN) topt = (topt + 1) % 5;
    if (topt == 1 && (mpress & PAD_RIGHT)) prole = (prole + 1) % 3;
    if (topt == 1 && (mpress & PAD_LEFT)) prole = (prole + 2) % 3;
    if (mpress & PAD_CROSS) {
      if (topt == 0) tmenu = 1; else if (topt == 1) prole = (prole + 1) % 3; else if (topt == 2) start();
      else if (topt == 3) { tmenu = 3; conn_stage = 0; net_msg_t = 0; } else tmenu = 2;
    }
  } else if (tmenu == 1) {
    if (mpress & PAD_RIGHT) pskin = (pskin + 1) % CM_NSKIN;
    if (mpress & PAD_LEFT) pskin = (pskin + CM_NSKIN - 1) % CM_NSKIN;
    if (mpress & (PAD_CROSS | PAD_CIRCLE)) tmenu = 0;
  } else if (tmenu == 3) {                                       /* direccion del servidor + conexion */
    if (conn_stage == 0) {
      if (mpress & PAD_LEFT) ipsel = (ipsel + 3) % 4;
      if (mpress & PAD_RIGHT) ipsel = (ipsel + 1) % 4;
      if (mbtn & (PAD_UP | PAD_DOWN)) {                          /* mantener = cambia rapido */
        int dir = (mbtn & PAD_UP) ? 1 : -1; rep_t++;
        if (rep_t == 1 || (rep_t > 20 && rep_t % 2 == 0)) bump_ip(dir);
      } else rep_t = 0;
      if (mpress & PAD_CROSS) { conn_stage = 1; net_timer = 0; net_say("INICIANDO RED..."); net_msg_t = 1e9f; }
      if (mpress & PAD_CIRCLE) tmenu = 0;
    } else {
      if (mpress & PAD_CIRCLE) { net_close(); conn_stage = 0; net_msg_t = 0; return; }
      if (conn_stage == 1) {                                     /* preparar la red (en PS2: modulos + DHCP) */
        int r = net_platform_poll();
        if (r < 0) { conn_stage = 0; net_say(net_phase ? "NO SE CONSIGUIO IP (REVISA LA RED)" : "NO HAY RED EN ESTA VERSION"); }
        else if (r > 0) { conn_stage = 2; net_say("CONECTANDO AL SERVIDOR..."); net_msg_t = 1e9f; }
      } else if (conn_stage == 2) {                              /* (conectar bloquea unos instantes) */
        if (net_open() < 0) { conn_stage = 0; net_say("NO SE PUDO CONECTAR AL SERVIDOR"); }
        else { u8 h[2] = {1, (u8)pskin}; net_tx(0x01, h, 2); conn_stage = 3; net_timer = 0; net_say("ESPERANDO AL SERVIDOR..."); net_msg_t = 1e9f; }
      } else if (conn_stage == 3) {
        if (net_ok) { conn_stage = 0; net_msg_t = 0; tmenu = 4; rooms_sel = rooms_top = 0; rooms_n = 0; list_t = 0; net_tx(0x02, NULL, 0); }
        else if (++net_timer > 60 * 5) { net_close(); conn_stage = 0; net_say("EL SERVIDOR NO RESPONDE"); }
      }
    }
  } else if (tmenu == 4) {                                       /* lista de salas */
    if (mpress & PAD_UP) rooms_sel = (rooms_sel + rooms_n) % (rooms_n + 1);
    if (mpress & PAD_DOWN) rooms_sel = (rooms_sel + 1) % (rooms_n + 1);
    if (rooms_sel < rooms_top) rooms_top = rooms_sel;
    if (rooms_sel > rooms_top + 5) rooms_top = rooms_sel - 5;
    if (mpress & PAD_TRIANGLE) { net_tx(0x02, NULL, 0); list_t = 2.f; }
    if ((mpress & PAD_CROSS) && join_t <= 0) {
      if (rooms_sel == 0) { net_tx(0x03, NULL, 0); join_t = 1.5f; }
      else if (rooms_sel <= rooms_n) {
        if (rooms[rooms_sel - 1].st) net_say("ESA PARTIDA YA EMPEZO");
        else if (rooms[rooms_sel - 1].n >= rooms[rooms_sel - 1].mx) net_say("LA SALA ESTA LLENA");
        else { u8 id = rooms[rooms_sel - 1].id; net_tx(0x04, &id, 1); join_t = 1.5f; }
      }
    }
    if (mpress & PAD_CIRCLE) { net_close(); tmenu = 3; conn_stage = 0; net_msg_t = 0; }
  } else {                                                       /* controles: TRIANGULO prueba el sonido de pasos */
    if (mpress & PAD_TRIANGLE) sfx_step();
    if (mpress & (PAD_CROSS | PAD_CIRCLE)) tmenu = 0;
  }
}
static void end_exit(void) {                                     /* START en la pantalla de fin */
  if (!online) { start(); return; }
  if (game_live) {                                               /* caiste pero la partida sigue: sales de la sala */
    net_tx(0x05, NULL, 0); online = 0; game_live = 0; map_reset(); state = TITLE; tmenu = 4; list_t = 0; rooms_sel = 0; join_t = 0;
    for (int i = 0; i < 6; i++) ch[i].alive = 0;
  } else lobby_enter();                                          /* la partida termino: todos vuelven a la nave */
}
static void step_state(void) {
  online_tick();
  switch (state) {
    case TITLE: title_update(); break;
    case LOBBY: lobby_update(); break;
    case INTRO:
      intro_t += DT;
      if (intro_t > 1.0f && intro_t < INTRO_LEN - .45f && (pressed & (PAD_START | PAD_CROSS))) intro_t = INTRO_LEN - .45f;   /* saltar */
      if (intro_t >= INTRO_LEN) { state = PLAY; camx = ch[0].x; camz = ch[0].y; }
      break;
    case PLAY: update(); break;
    case MEET:
      if (pressed & PAD_UP) cur = (cur + nopts - 1) % nopts;
      if (pressed & PAD_DOWN) cur = (cur + 1) % nopts;
      if (pressed & PAD_CROSS) { if (online) online_vote(opts[cur]); else vote(opts[cur]); }
      break;
    case MRES: if (--mres_t <= 0) after_vote(); break;
    case END: if (pressed & PAD_START) end_exit(); break;
  }
}

int main(void) {
  SifInitRpc(0);
  SifLoadModule("rom0:SIO2MAN", 0, NULL);
  SifLoadModule("rom0:PADMAN", 0, NULL);
  /* cola de dibujo mas grande (personajes 3D = muchos poligonos por cuadro) */
  gs = gsKit_init_global_custom(1024 * 1024, 1024 * 256);
  gs->PSM = GS_PSM_CT32; gs->PSMZ = GS_PSMZ_16S;
  gs->ZBuffering = GS_SETTING_OFF; gs->DoubleBuffering = GS_SETTING_ON;
  dmaKit_init(D_CTRL_RELE_OFF, D_CTRL_MFD_OFF, D_CTRL_STS_UNSPEC, D_CTRL_STD_OFF, D_CTRL_RCYC_8, 1 << DMA_CHANNEL_GIF);
  dmaKit_chan_init(DMA_CHANNEL_GIF);
  gsKit_init_screen(gs);
  gsKit_mode_switch(gs, GS_ONESHOT);
  SW = gs->Width; SH = gs->Height;

  nav_init();
  vis_init();
  audio_init();
  padInit(0); padPortOpen(0, 0, padBuf);
  while (padGetState(0, 0) != PAD_STATE_STABLE) {}
  padSetMainMode(0, 0, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);

  for (;;) {
    frames++; readpad(); step_state();
    draw();
    gsKit_queue_exec(gs);
    gsKit_sync_flip(gs);
  }
  return 0;
}
