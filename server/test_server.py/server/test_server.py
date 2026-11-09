#!/usr/bin/env python3
"""Pruebas del servidor con jugadores simulados:  python3 test_server.py"""
import asyncio, struct, sys, random
import sospechoso_server as S

PORT = 17777

class Bot:
    def __init__(self, skin=0): self.skin, self.q, self.r, self.w, self.pid, self.slot = skin, asyncio.Queue(), None, None, None, -1
    async def connect(self):
        self.r, self.w = await asyncio.open_connection("127.0.0.1", PORT)
        self.task = asyncio.create_task(self.reader())
        self.send(S.C_HELLO, bytes([S.VERSION, self.skin]))
        t, d = await self.expect(S.S_WELCOME); self.pid = d[0]; return self
    async def reader(self):
        buf = b""
        try:
            while True:
                c = await self.r.read(1024)
                if not c: break
                buf += c
                while len(buf) >= 2 and len(buf) >= 2 + buf[1]:
                    t, n = buf[0], buf[1]; self.q.put_nowait((t, buf[2:2+n])); buf = buf[2+n:]
        except Exception: pass
        self.q.put_nowait((None, b""))
    def send(self, t, p=b""): self.w.write(S.frame(t, p))
    async def expect(self, t, timeout=2.0):
        end = asyncio.get_event_loop().time() + timeout
        while True:
            left = end - asyncio.get_event_loop().time()
            assert left > 0, "timeout esperando 0x%02x" % t
            ty, d = await asyncio.wait_for(self.q.get(), left)
            assert ty is not None, "conexion cerrada esperando 0x%02x" % t
            if ty == t: return ty, d
    async def drain(self, wait=0.05):
        await asyncio.sleep(wait); out = []
        while not self.q.empty(): out.append(self.q.get_nowait())
        return out
    async def lobby(self):
        t, d = await self.expect(S.S_LOBBY); self.slot = d[1]; return dict(room=d[0], slot=d[1], host=d[2], mask=d[3], skins=list(d[4:10]))
    def close(self): self.w.close()

def pos(x, z, yaw=0, fl=0): return struct.pack("<hhhB", int(x*100), int(z*100), int(yaw*1000), fl)

async def new_game(n=4, min_players=3):
    srv, server = await S.serve(PORT, min_players, "127.0.0.1")
    return srv, server

async def make_room(n):
    bots = [await Bot(i).connect() for i in range(n)]
    bots[0].send(S.C_CREATE); lb = await bots[0].lobby(); rid = lb["room"]
    for b in bots[1:]:
        b.send(S.C_JOIN, bytes([rid])); await b.lobby()
    for b in bots: await b.drain(0.02)
    return bots, rid

async def start(bots):
    bots[0].send(S.C_START)
    roles = {}
    for b in bots:
        t, d = await b.expect(S.S_START); roles[b.slot] = d[0]; b.total = d[1]
    return roles

def by_role(bots, roles):
    imp = [b for b in bots if roles[b.slot] == 1]; crew = [b for b in bots if roles[b.slot] == 0]
    return imp[0], crew

async def t_lobby():
    srv, server = await new_game()
    a = await Bot(1).connect(); b = await Bot(2).connect()
    a.send(S.C_LIST); t, d = await a.expect(S.S_ROOMS); assert d == bytes([0]), "debe estar vacio"
    a.send(S.C_CREATE); lb = await a.lobby(); assert lb["slot"] == 0 and lb["host"] == 0 and lb["mask"] == 1 and lb["skins"][0] == 1
    b.send(S.C_LIST); t, d = await b.expect(S.S_ROOMS)
    assert d[0] == 1 and tuple(d[1:6]) == (lb["room"], 1, 6, 0, 1), list(d)
    b.send(S.C_JOIN, bytes([99])); t, d = await b.expect(S.S_ERROR); assert d[0] == S.E_NOROOM
    b.send(S.C_JOIN, bytes([lb["room"]])); lb2 = await b.lobby(); assert lb2["slot"] == 1 and lb2["mask"] == 3 and lb2["skins"][:2] == [1, 2]
    lb1 = await a.lobby(); assert lb1["mask"] == 3, "el anfitrion debe recibir la actualizacion"
    b.send(S.C_START); t, d = await b.expect(S.S_ERROR); assert d[0] == S.E_NOTHOST
    a.send(S.C_START); t, d = await a.expect(S.S_ERROR); assert d[0] == S.E_FEW, "con 2 jugadores no se puede empezar"
    # llenar la sala
    extra = [await Bot(k).connect() for k in range(4)]
    for e in extra: e.send(S.C_JOIN, bytes([lb["room"]])); await e.lobby()
    late = await Bot(0).connect(); late.send(S.C_JOIN, bytes([lb["room"]])); t, d = await late.expect(S.S_ERROR); assert d[0] == S.E_FULL
    # el anfitrion se va: el siguiente pasa a ser anfitrion
    a.close(); lbx = await b.lobby()
    while lbx["mask"] & 1: lbx = await b.lobby()
    assert lbx["host"] == 1, "el anfitrion debia pasar al slot 1, fue %d" % lbx["host"]
    for x in [b, late] + extra: x.close()
    await asyncio.sleep(0.1); assert not srv.rooms, "la sala vacia debe borrarse"
    server.close(); await server.wait_closed(); print("OK  lobby / salas / errores / migracion de anfitrion")

async def t_game_flow():
    srv, server = await new_game()
    bots, rid = await make_room(4)
    roles = await start(bots)
    assert sorted(roles.values()) == [0, 0, 0, 1], roles
    imp, crew = by_role(bots, roles)
    assert all(b.total == 9 for b in bots), "3 tareas x 3 tripulantes"
    c = bots[0].send(S.C_POS, pos(3.5, -2.25, 1.2, 1)); out = await bots[1].expect(S.S_POS)
    slot, x, z, yaw, fl = struct.unpack("<Bhhhb", out[1]); assert (slot, x, z, yaw, fl) == (0, 350, -225, 1200, 1), (slot, x, z, yaw, fl)
    assert not [1 for t, d in await bots[0].drain() if t == S.S_POS], "no debe devolverse al emisor"
    # asesinato fuera de reglas: un tripulante no puede matar
    crew[0].send(S.C_KILL, bytes([crew[1].slot]) + pos(0, 0)[:4]); assert not [1 for b in bots for t, d in await b.drain() if t == S.S_BODY]
    # el impostor mata
    v = crew[0]; imp.send(S.C_KILL, bytes([v.slot]) + struct.pack("<hh", 100, 200))
    for b in bots:
        t, d = await b.expect(S.S_BODY); assert d[0] == imp.slot and d[1] == v.slot and struct.unpack("<hh", d[2:6]) == (100, 200)
    imp.send(S.C_KILL, bytes([crew[1].slot]) + pos(0, 0)[:4]); assert not [1 for b in bots for t, d in await b.drain() if t == S.S_BODY], "enfriamiento del servidor"
    # un tripulante reporta
    rep = crew[1]; rep.send(S.C_REPORT, bytes([v.slot]))
    for b in bots:
        t, d = await b.expect(S.S_MEETING); assert d[0] == rep.slot and d[1] == v.slot and d[2] == sum(1 << x.slot for x in bots if x is not v)
    # votos: todos los vivos votan al impostor -> es expulsado -> gana la tripulacion
    for b in (imp, rep, crew[2]): b.send(S.C_VOTE, bytes([imp.slot]))
    for b in (imp, rep, crew[2], v):
        t, d = await b.expect(S.S_RESULT); assert d[0] == imp.slot and d[1] == 1
        t, d = await b.expect(S.S_END); assert d[0] == 1 and d[1] == 1 and d[2] == imp.slot, list(d)
    for b in bots: lb = await b.lobby()
    assert srv.rooms[rid].state == "lobby"
    for b in bots: b.close()
    server.close(); await server.wait_closed(); print("OK  roles / posiciones / asesinato / reunion / voto / fin (tripulacion gana)")

async def t_imp_wins():
    srv, server = await new_game()
    bots, rid = await make_room(4); roles = await start(bots); imp, crew = by_role(bots, roles)
    imp.send(S.C_KILL, bytes([crew[0].slot]) + struct.pack("<hh", 0, 0))
    for b in bots: await b.expect(S.S_BODY)
    crew[1].send(S.C_REPORT, bytes([crew[0].slot]))
    for b in bots: await b.expect(S.S_MEETING)
    # empate / voto saltado: nadie es expulsado
    imp.send(S.C_VOTE, bytes([crew[1].slot])); crew[1].send(S.C_VOTE, bytes([imp.slot])); crew[2].send(S.C_VOTE, bytes([S.SKIP]))
    for b in (imp, crew[1], crew[2]):
        t, d = await b.expect(S.S_RESULT); assert d[0] == 255, "empate 1-1: no se expulsa a nadie"
    await asyncio.sleep(0.6); imp.send(S.C_KILL, bytes([crew[1].slot]) + struct.pack("<hh", 0, 0))
    await asyncio.sleep(0.1)
    # primer kill fue hace < 8 s: el servidor lo rechaza (anti-abuso); forzamos el tiempo
    srv.rooms[rid].last_kill -= 10; imp.send(S.C_KILL, bytes([crew[1].slot]) + struct.pack("<hh", 0, 0))
    for b in (imp, crew[1], crew[2]):
        t, d = await b.expect(S.S_BODY, 3.0) if b is not crew[1] else (None, None)
    t, d = await imp.expect(S.S_END); assert d[0] == 0 and d[1] == 2 and d[2] == imp.slot, list(d)
    for b in bots: b.close()
    server.close(); await server.wait_closed(); print("OK  empate sin expulsion / enfriamiento / gana el impostor (queda 1 tripulante)")

async def t_tasks_win():
    srv, server = await new_game()
    bots, rid = await make_room(4); roles = await start(bots); imp, crew = by_role(bots, roles)
    imp.send(S.C_TASK); assert not [1 for b in bots for t, d in await b.drain() if t == S.S_TASKS], "el impostor no suma tareas"
    n = 0
    for k in range(9):
        crew[k % 3].send(S.C_TASK); n += 1
        for b in bots:
            t, d = await b.expect(S.S_TASKS); assert d[0] == n and d[1] == 9
    for b in bots:
        t, d = await b.expect(S.S_END); assert d[0] == 1 and d[1] == 0
    for b in bots: b.close()
    server.close(); await server.wait_closed(); print("OK  tareas completas -> gana la tripulacion")

async def t_disconnects():
    srv, server = await new_game()
    # el impostor se desconecta en pleno juego -> gana la tripulacion
    bots, rid = await make_room(4); roles = await start(bots); imp, crew = by_role(bots, roles)
    imp.close()
    for b in crew:
        t, d = await b.expect(S.S_LEFT); assert d[0] == imp.slot
        t, d = await b.expect(S.S_END); assert d[0] == 1 and d[1] == 3, list(d)
    for b in crew: b.close()
    await asyncio.sleep(0.1)
    # un tripulante se va en una reunion: si todos los demas ya votaron, la votacion se resuelve
    bots, rid = await make_room(4); roles = await start(bots); imp, crew = by_role(bots, roles)
    imp.send(S.C_KILL, bytes([crew[0].slot]) + struct.pack("<hh", 0, 0))
    for b in bots: await b.expect(S.S_BODY)
    crew[1].send(S.C_REPORT, bytes([crew[0].slot]))
    for b in bots: await b.expect(S.S_MEETING)
    imp.send(S.C_VOTE, bytes([S.SKIP])); crew[1].send(S.C_VOTE, bytes([S.SKIP]))
    await asyncio.sleep(0.05); crew[2].close()
    for b in (imp, crew[1]):
        t, d = await b.expect(S.S_LEFT); t, d = await b.expect(S.S_RESULT); assert d[0] == 255
    for b in bots: b.close()
    server.close(); await server.wait_closed(); print("OK  desconexiones en juego y en reunion")

async def t_robust():
    srv, server = await new_game()
    # mensajes basura / sin HELLO / version mala no tumban el servidor
    r, w = await asyncio.open_connection("127.0.0.1", PORT); w.write(bytes([0x03, 0, 0x04, 1, 9, 0xFF, 200] + [1]*10)); await w.drain(); await asyncio.sleep(0.1); w.close()
    r, w = await asyncio.open_connection("127.0.0.1", PORT); w.write(S.frame(S.C_HELLO, bytes([99, 0]))); await w.drain(); await asyncio.sleep(0.1)
    assert await r.read(10) == b"", "version incorrecta: el servidor cierra"
    # cliente "pegado": mensajes partidos en trozos
    a = await Bot().connect(); data = S.frame(S.C_CREATE); a.w.write(data[:1]); await a.w.drain(); await asyncio.sleep(0.05); a.w.write(data[1:]); await a.lobby()
    # varios clientes a la vez
    many = await asyncio.gather(*[Bot(i).connect() for i in range(30)])
    for m in many: m.send(S.C_LIST)
    for m in many: await m.expect(S.S_ROOMS)
    for m in many + [a]: m.close()
    server.close(); await server.wait_closed(); print("OK  mensajes basura / version / paquetes partidos / 30 clientes")

async def main():
    for t in (t_lobby, t_game_flow, t_imp_wins, t_tasks_win, t_disconnects, t_robust):
        await t()
    print("\nTODAS LAS PRUEBAS DEL SERVIDOR PASARON")

asyncio.run(main())
