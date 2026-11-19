# Registro de pruebas

Se distinguen tres niveles, y ninguno se da por superado con las pruebas de un nivel inferior:

1. **Simulación:** núcleo en C con controles simulados (`build\SelectorTests.exe`).
2. **Prototipo:** ventana D3D9 real con los dispositivos reales del PC, fuera del juego (`dist\SelectorPrototype.exe`).
3. **Juego:** la DLL dentro de KOF 2002 UM y KOF '98 UM FE, lanzados desde Steam con el lanzador.

## 1. Simulación — 286 comprobaciones correctas, 0 fallidas (17-10-2026)

| Prueba | Resultado |
|---|---|
| A: 3 controles; C3 toma P1, C1 toma P2, C2 no interfiere | OK (simulada) |
| B: 4 controles; C4 toma P1, C2 toma P2, C1 y C3 bloqueados | OK (simulada) |
| C: liberación y nueva selección tras el enfrentamiento, con otros jugadores | OK (simulada) |
| D: sin robo de lado; sin repetición al mantener pulsado | OK (simulada) |
| E: P2 se desconecta; recuperación sin asignar otro control | OK (simulada) |
| F: la asignación se mantiene entre rounds y cambios de personaje | OK (simulada) |
| G: 5+ dispositivos detectados y diferenciados (incluye duplicados XInput/Steam) | OK (simulada) |
| Extra: 2 controles = juego normal; tercer control a mitad de VS; teclas de admin; mapa de puertos | OK (simulada) |
| Extra: clasificación de BGM y límite entre enfrentamientos | OK (simulada) |
| Extra: decodificador de prólogo del hook de Present (bytes reales de `d3d9.dll`) | OK (simulada) |
| Extra: CLASICA y SAN FELIPE por posición física (HORI PS DirectInput = HORI Xbox XInput); L1 = SELECT en '98 | OK (simulada) |
| Extra: seis perfiles distintos con A/B/C/D una vez; tablas de juego por puerto; CLÁSICA = fábrica; persistencia VID:PID | OK (simulada) |
| Extra: SELECT YOUR BUTTON LAYOUT tras ambos lados; P1 y P2 independientes; memoria por control; controles bloqueados | OK (simulada) |
| Extra: duplicado por entradas reales (HORI PS envuelto por Steam Input se oculta) | OK (simulada) |

Capturas de la interfaz generadas por las pruebas: `build\ui\*.png`.

## 2. Prototipo (dispositivos reales, ventana D3D9 real)

- **Dispositivos detectados:**
  - `XINPUT 1 HORI 0F0D:01FB` y `XINPUT 2 HORI 0F0D:01B5`;
  - `DINPUT DualSense 054C:0CE6`;
  - tres mandos simulados con el teclado.
- **Interfaz de duplicados de la HORI:** la interfaz DirectInput `mi_01` de la HORI `0F0D:01FB` se descarta
  como duplicado.
- **Overlay:** se dibuja mediante el mismo hook en línea de `Present` que se usa en el juego.
- **Ejecución automática** (`--auto`, `docs\evidencias\proto_snapshot_1..5.png`):
  - un control bloqueado no llega a los slots 1–2;
  - la pulsación de P1 sí llega al slot 1.
- **Prueba manual con los mandos físicos:** pendiente.

## 3. Juego — pruebas de humo realizadas por el agente (17-10-2026)

Lanzadas con el lanzador y con `debug_force_vs=1` / `debug_snapshot=1`, es decir, el selector forzado en el
título, sin entrar en VS. Nadie pulsó botones durante estas pruebas.

| Comprobación | KOF 2002 UM | KOF '98 UM FE |
|---|---|---|
| Versión del ejecutable reconocida | Sí | Sí |
| Hooks XInput, CreateFileW y DirectInput instalados | Sí | Sí |
| Mapa slot→puerto leído del juego | `0->1 1->2 2->3 3->4` | `0->1 1->2 2->2 3->2` |
| Hook de Present encadenado tras el hook previo (overlay de Steam) | Sí | Sí |
| Overlay visible dentro del juego | Sí (`docs\evidencias\kof2002_pantalla_overlay.png`) | Sí (`docs\evidencias\kof98_backbuffer_overlay.png`) |
| Dispositivos | 2 HORI + `XINPUT 3 STEAM VIRTUAL 28DE:11FF` | igual |
| El juego sigue funcionando con la entrada bloqueada | Sí, más de 1 min | Sí, más de 40 s |

### Errores encontrados y corregidos en el juego

1. **Crash al instalar el overlay** (`0xC0000005` en `KofVsSelector.dll+0x4120`).
   - **Causa:** la vtable del dispositivo D3D9 temporal está en memoria privada de ese dispositivo y se libera
     con él; no la comparte el dispositivo del juego.
   - **Solución:** hook en línea de la función `Present` de `d3d9.dll`, que comparten todos los dispositivos.
2. **`Present` ya estaba enganchado por otro módulo** (salto `E9` en sus primeros bytes, presumiblemente el
   overlay de Steam, que no aparece fuera de Steam).
   - **Solución:** se reemplazan solo esos 5 bytes por un salto a un puente cercano y se encadena con el
     hook previo.
3. **El mando virtual de Steam aparecía sin VID:PID.**
   - **Solución:** se toma de la ruta DirectInput `ig_NN` de Valve.
4. **El failsafe solo actuaba si el overlay no había dibujado nunca.**
   - **Solución:** ahora también actúa si el overlay deja de presentar frames.

## 4. Partidas reales de Juan

### KOF '98 UM FE, 17-10-2026, 16:16–16:21, modo 3, 3 controles (2 HORI + DualSense vía Steam)

**Funcionó:**
- La pantalla apareció al entrar en VS.
- Reclamar, cambiar de lado y soltar (B/BACK) funcionaron.
- El control 2 quedó bloqueado mientras los dos lados estaban ocupados ("BOTH SIDES TAKEN").
- Asignación final: P1 = control 3 y P2 = control 1. El combate empezó con esa asignación (ACCESS ASSIGNED).

**Falló:** al terminar el enfrentamiento la pantalla no volvió.
- **Causa:** no había ninguna pista configurada como señal de vuelta a la selección en '98.
- **Lo que mostró el log:** `PS_N.ogg` se abre al entrar en la selección de personajes y otra vez tras `WIN_N.ogg`.
- **Corrección:** `select_bgm=bgm/PS_N.` para '98, anclado a la carpeta para no confundirla con `R_PS_N.ogg`.
- **Estado:** pendiente de volver a probarlo en una partida real.

### KOF '98 UM FE, 17-10-2026, 16:25–16:36, nueva sesión tras la corrección

Tres enfrentamientos seguidos:
1. P1 = control 3, P2 = control 1.
2. P1 = control 1, P2 = control 3.
3. P1 = control 2, P2 = control 3.

Tras cada victoria se abre `PS_N.ogg`; el selector pasa por MATCH_FINISHED y RESETTING y vuelve a la
selección. **Prueba C superada en '98.**

### KOF 2002 UM, 17-10-2026, 16:38–16:41, modo 3, banco de música arrange

- Al entrar en VS se abre `BGM03.ogg` y aparece la selección.
- Primer enfrentamiento: P1 = control 1, P2 = control 2. Música de combate `BGM15`, victoria `BGM04`.
- Al volver `BGM03.ogg`, el selector pasa por MATCH_FINISHED y RESETTING y vuelve a la selección.
- En la nueva selección, el control 3 queda bloqueado ("BOTH SIDES TAKEN").
- **Queda confirmado que `BGM03` es la señal correcta en el banco arrange.**
- **Pendiente:** el banco original (`48000Hz`). Su pista de selección sigue sin identificar.

Juan informa de que en el juego todo funcionó bien. El log no puede demostrar por sí solo que un control
bloqueado no movió nada en combate, así que la prueba H queda como observación de Juan, no medida.

## 5. Pruebas reales pendientes (con jugadores y mandos físicos)

Ninguna de estas pruebas está superada todavía. Para cada una:
- Usa la configuración normal (`debug_force_vs=0`).
- Entra en VS desde el menú del juego.
- Al terminar, guarda `dist\kofvs.log`.

| Prueba | Procedimiento | Qué comprobar | Estado |
|---|---|---|---|
| A | 3 controles: C3 pulsa izquierda y A; C1 pulsa derecha y A; C2 pulsa de todo | C2 no mueve nada; en combate P1 = C3 y P2 = C1 | PENDIENTE |
| B | 4 controles (hacen falta 4 dispositivos): C4 toma P1, C2 toma P2 | C1 y C3 no actúan ni en menús ni en combate | PENDIENTE |
| C | Terminar un enfrentamiento completo | Vuelve la pantalla; otros jugadores pueden tomar P1/P2 (en '98 o con la banda original de 2002: `CTRL+SHIFT+F9`) | PENDIENTE |
| D | Con P1 tomado, otro control pulsa izquierda | P1 no cambia de dueño | PENDIENTE |
| E | Desconectar el control de P2 durante el combate | El lado P2 queda AVAILABLE y no se asigna otro control solo | PENDIENTE |
| F | Combate a 3 rounds o por equipos | No vuelve la pantalla entre rounds ni entre miembros del equipo | PENDIENTE |
| G | 5 o más dispositivos (al menos uno DirectInput) | Todos aparecen una vez, numerados | PENDIENTE |
| H | En combate, el control bloqueado pulsa movimientos y START | El personaje no reacciona y no se pausa | PENDIENTE |
| I | P1 elige SAN FELIPE, P2 CLÁSICA. En combate, P1 pulsa Cuadrado, R1, Triángulo y Cruz | Puño débil, puño fuerte, patada débil y patada fuerte; P2 sigue con la configuración del juego | PENDIENTE |
| J | '98, SAN FELIPE: mantener L1 sobre un personaje con versión EX y confirmar | Retrato EX/Orochi (SELECT). START sigue haciendo RANDOM, como el juego original | PENDIENTE |
| K | HORI Mini o Alpha en modo PS y el mismo modelo en modo Xbox, SAN FELIPE | A B C D caen en las mismas posiciones físicas | PENDIENTE |
| L | P1 LINEAL, P2 PULGAR; luego AB/CD contra MÉXICO; prueba de rejilla antes de confirmar | Cada palo usa solo su perfil; al pulsar un botón físico se ilumina A/B/C/D correcto; START no se convierte en ataque | PENDIENTE |
| M | Steam Deck / Proton | El overlay, los seis perfiles y el parche de tablas funcionan | PENDIENTE (sin hardware) |

La tabla CLÁSICA de la prueba I se obtuvo leyendo las tablas por defecto de los ejecutables (análisis
estático; el juego no se ejecutó para esto): 2002 `0x1407CE0B0` y '98 x64 `0x14053BB50`, donde
`opt_lp` = X, `opt_sp` = Y, `opt_lk` = A, `opt_sk` = B y `opt_start` = START.
