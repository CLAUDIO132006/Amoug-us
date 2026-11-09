#!/usr/bin/env python3
"""Servidor online de SOSPECHOSO (juego estilo Among Us para PS2).

Uso:    python3 sospechoso_server.py [--port 7777] [--min-players 3]
Requiere solo Python 3.8+ (sin librerias externas). Escucha TCP en todas las interfaces.

Arquitectura: el servidor es la AUTORIDAD de las reglas (roles, quien esta vivo, reuniones,
votos, tareas totales y condiciones de victoria). Cada cliente es dueno de su propio movimiento
y posicion (el servidor solo los retransmite). Es la forma mas simple de tener partidas online
en una consola con poca capacidad.

PROTOCOLO (binario, little-endian). Cada mensaje:  [tipo u8][largo u8][carga: largo bytes]

  Cliente -> Servidor
    0x01 HELLO   [version u8][skin u8]        -> 0x81 WELCOME
    0x02 LIST                                 -> 0x82 ROOMS
    0x03 CREATE                               -> 0x83 LOBBY (eres el anfitrion)
    0x04 JOIN    [room_id u8]                 -> 0x83 LOBBY | 0x8E ERROR
    0x05 LEAVE
    0x06 START                                -> 0x84 START (solo el anfitrion)
    0x07 PING                                 -> 0x8D PONG
    0x10 POS     [x s16 cm][z s16 cm][yaw s16 mrad][flags u8]
    0x11 KILL    [victima u8][x s16][z s16]
    0x12 REPORT  [victima u8]
    0x13 TASK
    0x14 VOTE    [objetivo u8]  (255 = saltar)

  Servidor -> Cliente
    0x81 WELCOME [player_id u8]
    0x82 ROOMS   [n u8] n x [room_id][jugadores][max][estado 0=espera 1=jugando][skin_anfitrion]
    0x83 LOBBY   [room_id][mi_slot][slot_anfitrion][mascara_presentes][skin0..skin5]
    0x84 START   [mi_rol 0=tripulante 1=impostor][tareas_totales][mascara_presentes]
    0x8D PONG
    0x8E ERROR   [codigo]  1 sala llena, 2 no existe, 3 ya en juego, 4 no eres anfitrion,
                           5 faltan jugadores, 6 estado invalido
    0x90 POS     [slot][x s16][z s16][yaw s16][flags u8]
    0x91 BODY    [asesino][victima][x s16][z s16]
    0x92 MEETING [reporta][victima][mascara_vivos]
    0x93 RESULT  [expulsado (255 = nadie)][era_impostor][mascara_vivos]
    0x94 TASKS   [hechas][totales]
    0x95 END     [gana_tripulacion][motivo][slot_impostor]
                 motivo: 0 tareas, 1 impostor expulsado, 2 impostor gana (queda 1 tripulante), 3 el impostor se fue
    0x96 LEFT    [slot]
"""
import argparse, asyncio, random, struct, time

VERSION = 1
MAX_PLAYERS = 6
TASKS_PER_CREW = 3
MEETING_TIME = 45.0
IDLE_TIMEOUT = 45.0
MIN_KILL_GAP = 8.0       # el cliente usa 18 s; el servidor solo evita abusos

C_HELLO, C_LIST, C_CREATE, C_JOIN, C_LEAVE, C_START, C_PING = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07
C_POS, C_KILL, C_REPORT, C_TASK, C_VOTE = 0x10, 0x11, 0x12, 0x13, 0x14
S_WELCOME, S_ROOMS, S_LOBBY, S_START, S_PONG, S_ERROR = 0x81, 0x82, 0x83, 0x84, 0x8D, 0x8E
S_POS, S_BODY, S_MEETING, S_RESULT, S_TASKS, S_END, S_LEFT = 0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96
E_FULL, E_NOROOM, E_INGAME, E_NOTHOST, E_FEW, E_STATE = 1, 2, 3, 4, 5, 6
SKIP = 255


def frame(t, payload=b""):
    return bytes([t, len(payload)]) + payload


def mask_of(slots):
    m = 0
    for s in slots:
        m |= 1 << s
    return m


class Player:
    def __init__(self, pid, writer):
        self.pid, self.writer, self.skin = pid, writer, 0
        self.room, self.slot = None, -1
        self.last = time.monotonic()
        self.hello = False

    def send(self, t, payload=b""):
        try:
            if self.writer.is_closing():
                return
            if self.writer.transport.get_write_buffer_size() > 65536:   # cliente demasiado lento: se corta
                self.writer.close()
                return
            self.writer.write(frame(t, payload))
        except Exception:
            pass


class Room:
    def __init__(self, rid):
        self.id = rid
        self.players = {}          # slot -> Player
        self.host = 0
        self.state = "lobby"       # "lobby" | "play"
        self.reset_game()

    def reset_game(self):
        self.roles = {}            # slot -> 0 tripulante / 1 impostor
        self.alive = set()
        self.bodies = set()
        self.done = self.total = 0
        self.meeting = None
        self.last_kill = 0.0

    def slots(self):
        return sorted(self.players)

    def broadcast(self, t, payload=b"", skip=None):
        for s, p in self.players.items():
            if s != skip:
                p.send(t, payload)

    def lobby_msg(self, slot):
        skins = bytes(self.players[s].skin if s in self.players else 0 for s in range(MAX_PLAYERS))
        return bytes([self.id, slot, self.host, mask_of(self.players)]) + skins

    def send_lobby(self):
        for s, p in self.players.items():
            p.send(S_LOBBY, self.lobby_msg(s))

    def crew_alive(self):
        return [s for s in self.alive if self.roles.get(s) == 0]

    def imp_slots(self):
        return [s for s, r in self.roles.items() if r == 1]


class Server:
    def __init__(self, min_players=3):
        self.rooms, self.players, self.next_pid = {}, {}, 1
        self.min_players = min_players

    # ---------- utilidades ----------
    def new_room_id(self):
        for i in range(1, 100):
            if i not in self.rooms:
                return i
        return None

    def leave_room(self, pl, reason_gone=True):
        room = pl.room
        if room is None:
            return
        slot = pl.slot
        room.players.pop(slot, None)
        pl.room, pl.slot = None, -1
        if not room.players:
            self.rooms.pop(room.id, None)
            return
        if room.state == "lobby":
            if room.host == slot:
                room.host = min(room.players)             # el siguiente jugador pasa a ser anfitrion
            room.send_lobby()
        else:
            room.alive.discard(slot)
            room.bodies.discard(slot)
            room.broadcast(S_LEFT, bytes([slot]))
            if room.meeting is not None:
                room.meeting["votes"].pop(slot, None)
                self.try_resolve(room)
            self.check_win(room)                                  # si se fue el impostor o casi toda la tripulacion

    # ---------- reglas ----------
    def check_win(self, room, ejected_imp=False):
        if room.state != "play":
            return False
        imps = [s for s in room.imp_slots() if s in room.alive]
        if not imps:
            self.end_game(room, 1, 3 if not ejected_imp else 1)
            return True
        if len(room.crew_alive()) <= 1:
            self.end_game(room, 0, 2)
            return True
        if room.total and room.done >= room.total:
            self.end_game(room, 1, 0)
            return True
        return False

    def end_game(self, room, crew_wins, reason):
        if room.state != "play":
            return
        imp = (room.imp_slots() or [255])[0]
        room.broadcast(S_END, bytes([crew_wins, reason, imp]))
        room.state = "lobby"
        room.reset_game()
        room.send_lobby()

    def start_game(self, room):
        slots = room.slots()
        imp = random.choice(slots)
        room.roles = {s: (1 if s == imp else 0) for s in slots}
        room.alive = set(slots)
        room.total = TASKS_PER_CREW * (len(slots) - 1)
        room.done, room.bodies, room.meeting = 0, set(), None
        room.last_kill = 0.0
        room.state = "play"
        m = mask_of(slots)
        for s, p in room.players.items():
            p.send(S_START, bytes([room.roles[s], room.total, m]))

    def try_resolve(self, room):
        mt = room.meeting
        if mt is None:
            return
        need = [s for s in room.alive]
        if any(s not in mt["votes"] for s in need) and time.monotonic() < mt["deadline"]:
            return
        tally = {}
        for s, tgt in mt["votes"].items():
            if s in room.alive and tgt != SKIP and tgt in room.alive:
                tally[tgt] = tally.get(tgt, 0) + 1
        ejected, was_imp = SKIP, 0
        if tally:
            top = max(tally.values())
            winners = [s for s, v in tally.items() if v == top]
            if len(winners) == 1:
                ejected = winners[0]
        if ejected != SKIP:
            room.alive.discard(ejected)
            was_imp = room.roles.get(ejected, 0)
        room.meeting = None
        room.broadcast(S_RESULT, bytes([ejected, was_imp, mask_of(room.alive)]))
        self.check_win(room, ejected_imp=bool(was_imp))

    # ---------- mensajes del cliente ----------
    def handle(self, pl, t, data):
        pl.last = time.monotonic()
        if t == C_HELLO:
            if len(data) >= 2 and data[0] == VERSION:
                pl.skin, pl.hello = data[1] % 7, True
                pl.send(S_WELCOME, bytes([pl.pid]))
            else:
                pl.writer.close()
            return
        if not pl.hello:
            return
        room = pl.room
        if t == C_PING:
            pl.send(S_PONG)
        elif t == C_LIST:
            rs = sorted(self.rooms.values(), key=lambda r: r.id)[:40]
            body = bytes([len(rs)]) + b"".join(
                bytes([r.id, len(r.players), MAX_PLAYERS, 0 if r.state == "lobby" else 1, r.players[r.host].skin]) for r in rs)
            pl.send(S_ROOMS, body)
        elif t == C_CREATE and room is None:
            rid = self.new_room_id()
            if rid is None:
                pl.send(S_ERROR, bytes([E_FULL]))
                return
            r = Room(rid)
            self.rooms[rid] = r
            r.players[0], pl.room, pl.slot, r.host = pl, r, 0, 0
            r.send_lobby()
        elif t == C_JOIN and room is None and len(data) >= 1:
            r = self.rooms.get(data[0])
            if r is None:
                pl.send(S_ERROR, bytes([E_NOROOM]))
            elif r.state != "lobby":
                pl.send(S_ERROR, bytes([E_INGAME]))
            elif len(r.players) >= MAX_PLAYERS:
                pl.send(S_ERROR, bytes([E_FULL]))
            else:
                slot = min(set(range(MAX_PLAYERS)) - set(r.players))
                r.players[slot], pl.room, pl.slot = pl, r, slot
                r.send_lobby()
        elif t == C_LEAVE:
            self.leave_room(pl)
        elif room is None:
            return
        elif t == C_START:
            if room.state != "lobby":
                pl.send(S_ERROR, bytes([E_STATE]))
            elif room.host != pl.slot:
                pl.send(S_ERROR, bytes([E_NOTHOST]))
            elif len(room.players) < self.min_players:
                pl.send(S_ERROR, bytes([E_FEW]))
            else:
                self.start_game(room)
        elif t == C_POS and len(data) >= 7:
            room.broadcast(S_POS, bytes([pl.slot]) + data[:7], skip=pl.slot)
        elif room.state != "play":
            return
        elif t == C_KILL and len(data) >= 5:
            victim, now = data[0], time.monotonic()
            if (room.meeting is None and room.roles.get(pl.slot) == 1 and pl.slot in room.alive and
                    victim in room.alive and room.roles.get(victim) == 0 and now - room.last_kill >= MIN_KILL_GAP):
                room.last_kill = now
                room.alive.discard(victim)
                room.bodies.add(victim)
                room.broadcast(S_BODY, bytes([pl.slot, victim]) + data[1:5])
                self.check_win(room)
        elif t == C_REPORT and len(data) >= 1:
            victim = data[0]
            if room.meeting is None and pl.slot in room.alive and victim in room.bodies:
                room.bodies.clear()
                room.meeting = {"votes": {}, "deadline": time.monotonic() + MEETING_TIME}
                room.broadcast(S_MEETING, bytes([pl.slot, victim, mask_of(room.alive)]))
        elif t == C_TASK:
            if room.meeting is None and room.roles.get(pl.slot) == 0 and pl.slot in room.alive and room.done < room.total:
                room.done += 1
                room.broadcast(S_TASKS, bytes([room.done, room.total]))
                self.check_win(room)
        elif t == C_VOTE and len(data) >= 1:
            mt = room.meeting
            if mt is not None and pl.slot in room.alive and pl.slot not in mt["votes"]:
                tgt = data[0]
                mt["votes"][pl.slot] = tgt if (tgt == SKIP or tgt in room.alive) else SKIP
                self.try_resolve(room)

    # ---------- red ----------
    async def client(self, reader, writer):
        pl = Player(self.next_pid, writer)
        self.next_pid = self.next_pid % 250 + 1
        self.players[id(pl)] = pl
        peer = writer.get_extra_info("peername")
        print("[+] conexion de", peer)
        buf = b""
        try:
            while True:
                chunk = await reader.read(1024)
                if not chunk:
                    break
                buf += chunk
                while len(buf) >= 2 and len(buf) >= 2 + buf[1]:
                    t, n = buf[0], buf[1]
                    data, buf = buf[2:2 + n], buf[2 + n:]
                    self.handle(pl, t, data)
                if len(buf) > 600:
                    break
        except (ConnectionError, asyncio.IncompleteReadError):
            pass
        finally:
            self.leave_room(pl)
            self.players.pop(id(pl), None)
            writer.close()
            print("[-] desconexion de", peer)

    async def ticker(self):
        while True:
            await asyncio.sleep(0.5)
            now = time.monotonic()
            for room in list(self.rooms.values()):
                if room.meeting is not None and now >= room.meeting["deadline"]:
                    self.try_resolve(room)
            for pl in list(self.players.values()):
                if now - pl.last > IDLE_TIMEOUT:
                    print("[!] sin actividad, se cierra", pl.pid)
                    pl.writer.close()


async def serve(port, min_players, host="0.0.0.0"):
    srv = Server(min_players)
    server = await asyncio.start_server(srv.client, host, port)
    print("SOSPECHOSO servidor escuchando en %s:%d (minimo %d jugadores por partida)" % (host, port, min_players))
    asyncio.create_task(srv.ticker())
    return srv, server


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", type=int, default=7777)
    ap.add_argument("--min-players", type=int, default=3)
    a = ap.parse_args()

    async def main():
        srv, server = await serve(a.port, a.min_players)
        async with server:
            await server.serve_forever()
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        pass
