# Servidor online de SOSPECHOSO

Solo necesita **Python 3.8+** (sin librerias externas).

    python3 sospechoso_server.py              # puerto 7777, minimo 3 jugadores por partida
    python3 sospechoso_server.py --port 9000 --min-players 4
    python3 test_server.py                    # pruebas automaticas del servidor

## Como conectarte desde el juego
1. En el servidor, abre el puerto TCP (7777 por defecto) en el firewall.
2. En el juego: **ONLINE** -> escribe la IP del servidor (izq-der elige el numero, arriba-abajo lo cambia) -> **X**.
3. Veras la lista de salas: **+ CREAR SALA** o elige una **EN ESPERA** y pulsa X.
4. Estaras en la nave (lobby). El anfitrion pulsa **START** (o **X** en la consola verde) cuando haya 3 o mas jugadores.

## Donde alojarlo
- **En tu casa / red local:** corre el servidor en tu PC y usa su IP local (por ejemplo 192.168.1.50).
- **Por internet:** necesitas un puerto TCP publico: un VPS, o reenviar el puerto en tu router.
  La PS2 no soporta HTTPS ni WebSockets: el juego habla TCP simple, asi que sirve cualquier maquina con IP alcanzable.

## Que decide el servidor
Roles (1 impostor), quien esta vivo, reuniones, votos y victoria (tareas completas, impostor expulsado,
queda 1 tripulante, o el impostor se va). Cada jugador manda solo su propia posicion; el servidor la reparte.
El protocolo esta documentado al inicio de `sospechoso_server.py`.
