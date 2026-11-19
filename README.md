# KOF VS Controller Selector

Mod para el VS local de **KOF 2002 Unlimited Match** y **KOF '98 Ultimate Match Final Edition** (Steam, x64).
Cuando hay más de dos controles conectados, antes del enfrentamiento aparece dentro del juego la pantalla
**SELECT YOUR CONTROLLER**. Cada jugador reclama P1 o P2 con su propio control. Cuando ambos lados están
ocupados aparece **SELECT YOUR BUTTON LAYOUT**: cada uno elige una de seis configuraciones (con la
rejilla 2×4 del palo) y confirma. Durante el enfrentamiento solo esos dos controles llegan al juego, cada
uno con su propio perfil, y el resto queda bloqueado.

No se modifica ningún archivo del juego ni de Steam. El mod es una DLL que el lanzador carga en el proceso
del juego solo cuando se inicia desde el lanzador.

## Versiones soportadas

| Juego | Ejecutable | Timestamp PE | SizeOfImage |
|---|---|---|---|
| KOF 2002 UM | `KingOfFighters2002UM_x64.exe` | `0x6243D069` | `0x41AE000` |
| KOF '98 UM FE | `KingOfFighters98UM_x64.exe` | `0x62033053` | `0x1B31000` |

El lanzador y la DLL comprueban la versión antes de hacer nada: la identidad PE y dos firmas de código, la
escritura del modo VS y la lectura del mapa de puertos. Con cualquier otra versión no se inyecta el mod, o la
DLL queda inactiva y lo registra como `UNSUPPORTED EXECUTABLE`.

## Uso

1. Abre Steam con la sesión iniciada.
2. Ejecuta `Jugar KOF2002 con selector.bat` o `Jugar KOF98 con selector.bat` desde la carpeta `dist`.
   También puedes usar `KofVsLauncher.exe kof2002` o `KofVsLauncher.exe kof98`.
   Si el juego ya está abierto, el lanzador inyecta el mod en ese proceso.
3. Entra en el modo VS local. Si hay más de dos controles, aparece la pantalla de selección.

### Pantalla de selección

| Acción | Botón |
|---|---|
| Reclamar P1 | Izquierda (cruceta o stick) |
| Reclamar P2 | Derecha (cruceta o stick) |
| Cambiar el perfil de botones (pantalla LAYOUT, antes de READY) | Arriba / Abajo |
| Probar A/B/C/D en la rejilla | Pulsa el botón físico; se ilumina la letra |
| Confirmar (READY) | A o START |
| Cancelar / soltar el lado | B o BACK |

### Configuración de botones

P1 y P2 eligen por separado en **SELECT YOUR BUTTON LAYOUT**. El perfil se recuerda por VID:PID en
`kofvs-layouts.ini` y se sugiere al volver a reclamar ese control; el otro jugador no se altera.

Las seis rejillas (A=puño débil, B=patada débil, C=puño fuerte, D=patada fuerte). Los guiones no tienen
ataque. START y SELECT conservan su papel.

```text
CLASICA          SAN FELIPE        LINEAL
[ A ][ C ][-][-]  [ A ][ B ][ C ][-]  [ A ][ B ][ C ][ D ]
[ B ][ D ][-][-]  [ D ][-][-][-]      [-][-][-][-]

PULGAR           AB/CD             MEXICO
[ B ][ C ][ D ][-]  [ A ][ B ][-][-]  [ A ][ B ][ C ][-]
[ A ][-][-][-]      [ C ][ D ][-][-]  [-][-][ D ][-]
```

Origen de cada perfil: `LAYOUTS.md`. CLÁSICA y SAN FELIPE no se han cambiado respecto a las versiones
anteriores de este mod.

- CLÁSICA no reasigna nada: es lo que el juego usa al conectar un control (tablas por defecto 2002 `0x1407CE0B0` / DirectInput `0x1407CE210`, '98 `0x14053BB50` / DirectInput `0x14053BBE0`).
- SAN FELIPE deja vacíos Círculo, R2, L2 y L1. En '98 hay que **mantener SELECT** sobre el retrato para la versión EX/Orochi; SELECT sale solo del botón SELECT/SHARE/BACK físico (L1 ya no manda SELECT; `san_felipe_l1_select=1` recupera el comportamiento anterior). Mantener START en esa pantalla es el RANDOM del propio juego; el mod no lo desactiva.
- Un HORI en modo PlayStation (DirectInput) y uno en modo Xbox/PC (XInput) usan las mismas posiciones físicas. El mapa DirectInput no es el orden Xbox `A B X Y`. No se confunde la X de PlayStation con la X de Xbox.
- Si un mando concreto manda otro orden, mira `kofvs.log` (`INPUT CONTROL … DI buttons=`) y añade `di_map_VVVV_PPPP=` en `kofvs.ini`.
- Solo se aplica cuando la pantalla de selección está activa (más de dos controles, o `always_show=1`).

Reglas de la pantalla:
- Solo se puede reclamar un lado libre. No se puede robar un lado ocupado ni ocupar los dos con el mismo control.
- Mantener un botón pulsado no repite la acción.
- Cuando los dos lados están en READY se muestra LOCKED y, tras `lock_delay_ms`, empieza el enfrentamiento
  solo con los controles asignados.
- Al terminar el enfrentamiento se libera la asignación y se vuelve a pedir la selección. Los rounds y los
  cambios de personaje del equipo no cuentan como enfrentamiento nuevo.
- Si un control asignado se desconecta, su lado queda libre y se vuelve a pedir un control. Nunca se asigna
  otro dispositivo automáticamente.
- Con exactamente dos controles no aparece la pantalla y el juego funciona como siempre.

### Teclas de administración

Solo funcionan con la ventana del juego en primer plano.

- `CTRL+SHIFT+F9`: repetir la selección; úsalo también para forzar la reselección entre enfrentamientos.
- `CTRL+SHIFT+F10`: entrada normal del juego hasta el siguiente enfrentamiento VS.

### START y SELECT físicos

START y SELECT llegan al juego solo desde sus botones físicos (START/OPTIONS/MENU y
SELECT/SHARE/BACK), en XInput y en DirectInput, en todos los perfiles. Steam Input a veces
manda START o BACK en la misma lectura que un gatillo o un hombro (L1, R1, L2, R2); el mod
descarta ese START/BACK "colgado" del gatillo, por control y también en modo normal. Un
START o SELECT pulsado por sí mismo pasa siempre.

### Selector de música (KOF 98 y KOF 2002)

Si `Kof98Bgm.dll` o `Kof2002Bgm.dll` están junto a `KofVsLauncher.exe`, el lanzador las carga
en el mismo proceso. La ventana SELECT MUSIC usa los mismos dos mandos que eligieron P1 y P2
aquí, con su perfil de botones: no hay que configurar nada más. Mientras esa ventana está
abierta, el selector no deja pasar botones al juego. Si un mod falta, el otro funciona solo.

## Desactivar y desinstalar

- **Jugar sin el mod:** abre el juego desde Steam como siempre. El mod solo existe en el proceso que
  inicia el lanzador.
- **Desactivar sin borrar nada:** pon `enabled=0` en `dist\kofvs.ini`. La DLL se carga pero no instala ningún hook.
- **Desinstalar:** borra la carpeta `D:\KOF-VS-Controller-Selector`. No hay nada que restaurar en el juego
  ni en Steam.

## Configuración (`kofvs.ini`)

Cada clave lleva un comentario en el propio archivo. Las más importantes:

- `always_show=1`: muestra la pantalla aunque solo haya dos controles (necesario para elegir un perfil
  distinto de CLÁSICA jugando con dos).
- `default_layout=classic|san_felipe|lineal|pulgar|abcd|mexico`: perfil inicial si ese control no tiene
  uno guardado en `kofvs-layouts.ini`.
- `san_felipe_l1_select` (en `[kof98]`): 0 (por defecto) = START y SELECT solo salen de los botones físicos START/OPTIONS/MENU y SELECT/SHARE/BACK. 1 = en SAN FELIPE, L1 también actúa como SELECT (comportamiento anterior).
- `log_buttons=1`: anota cada pulsación (índice DirectInput, posición y acción que ve el juego).
- `block_keyboard=1`: el teclado no llega al juego mientras el selector gestiona el VS.
- `use_dinput`, `di_button_N`: controles DirectInput y su mapeo a botones XInput.
- `steam_dedupe=auto`: si existe un mando virtual de Steam, se ocultan los dispositivos DirectInput Sony,
  Nintendo y Valve para que no aparezcan duplicados.
- `select_bgm`: la pista que el juego abre en la selección de personajes, que marca el final del
  enfrentamiento (ver Limitaciones).
- `failsafe_ms`: si el overlay no presenta frames durante ese tiempo mientras la entrada está bloqueada,
  se devuelve la entrada normal al juego.
- `debug_force_vs` y `debug_snapshot`: solo para depuración; activan el selector fuera del VS y escriben
  `kofvs_overlay.png` desde el backbuffer.

## Registro

`dist\kofvs.log` registra:
- la versión detectada y los hooks instalados;
- cada dispositivo, con su ruta DirectInput y su VID:PID, y los descartados por duplicados;
- los cambios de modo del juego, los estados (IDLE, DETECTING, SELECTING, CONFIRMING, LOCKED, PLAYING,
  MATCH_FINISHED, RESETTING) y las asignaciones;
- las aperturas de BGM y una línea `STATS` cada 10 s.

## Cómo funciona

- **Entrada XInput:** se interceptan `XInputGetState` y `XInputSetState` en la tabla de importaciones del juego.
  El slot que el juego asocia al puerto 1 recibe el control de P1 y el del puerto 2 el de P2. Los slots no
  asignados devuelven un estado neutro conservando su estado real de conexión, y durante la selección todos
  van en neutro. El mapa slot→puerto se lee de la configuración de controles del propio juego.
- **Entrada DirectInput:** se interceptan `Poll` y `GetDeviceState` de los dispositivos DirectInput del juego.
  Mientras el selector gestiona el VS, los joysticks y el teclado del juego no aportan entrada.
- **Dispositivos físicos:** el mod los lee con su propia instancia: XInput, más DirectInput para los mandos
  que no son XInput. Cada dispositivo tiene una clave estable y un número de control. Las interfaces
  DirectInput de mandos XInput (`IG_` o mismo VID:PID) se descartan, así que nada aparece dos veces.
  Los controles DirectInput se adaptan a formato XInput antes de entregarse al juego.
- **Overlay:** se engancha `IDirect3DDevice9::Present` en `d3d9.dll`, encadenado detrás del hook del overlay
  de Steam si existe. La pantalla se dibuja con rectángulos `Clear` en el backbuffer del juego; no es una
  ventana externa.
- **Detección del VS:** se lee la variable de modo del juego: 2 o 3 es el VS de 2 jugadores, según el
  análisis estático de cada ejecutable (ver `analysis\`).

## Compilar

Requisitos: Visual Studio 2022 (MSVC 14.44) y .NET 9 SDK.

```powershell
powershell -ExecutionPolicy Bypass -File .\build.ps1
```

`build_native.bat` compila la DLL, el prototipo y las pruebas, y después ejecuta las pruebas.

## Limitaciones conocidas

Lee también `PRUEBAS.md`: indica qué se ha verificado de verdad y qué no.

1. **Fin de enfrentamiento:** se detecta por la pista de BGM de la selección de personajes, que el juego
   abre al volver a ella:
   - **2002, banda arrange:** `UM48000Hz/BGM03.`, log real.
   - **'98:** `bgm/PS_N.`, log real (3 enfrentamientos).
   - **2002, banda original:** pista sin identificar.

   Sin esa pista hay que usar `CTRL+SHIFT+F9` entre enfrentamientos. El mod Random BGM, si está instalado
   en la carpeta del juego, puede cambiar los archivos que se abren; su interacción no está verificada.
2. **XInput admite como máximo 4 mandos en Windows.** Con 5 o más dispositivos, los que pasen de 4 tienen que
   ser DirectInput. Los mandos virtuales de Steam Input también ocupan slots XInput.
3. **Steam Input:** cuando Steam gestiona un mando (por ejemplo un DualSense), el juego no ve el dispositivo
   físico sino un mando virtual `28DE:11FF`, que aparece como `STEAM VIRTUAL`. Si Steam lo recrea, cuenta
   como un dispositivo nuevo y, si estaba asignado, su lado queda libre.
4. **DirectInput:** el mapeo de botones es configurable, pero LT/RT son digitales y no se usan ejes
   adicionales. Por defecto los mandos Xbox no se leen por DirectInput (`xbox_via_dinput=0`).
5. **Teclado:** con `block_keyboard=1` nadie puede jugar con teclado en un VS gestionado por el selector.
6. **Temporizadores:** el juego sigue funcionando detrás del overlay. Si la pantalla siguiente tiene cuenta
   atrás, avanza mientras se eligen los controles (sin verificar).
7. **Prueba H** (exclusividad real durante un combate): pendiente de verificación con jugadores reales.
8. **Steam Deck:** hay un cargador `dinput8.dll` para Proton (ver `LEEME_STEAMDECK.md`). No está probado en un Deck. Estos juegos no tienen Steam Workshop. El paquete para compartir está en `comunidad\` (ver `comunidad\LEEME.md`). El paquete para compartir está en `comunidad\` (ver `comunidad\LEEME.md`). El paquete para compartir está en `comunidad\` (ver `comunidad\LEEME.md`).
