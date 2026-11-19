# KOF VS Controller Selector en Steam Deck

Esto **no está probado en un Deck**. En Windows el selector ya funciona inyectando `KofVsSelector.dll` con el lanzador. En SteamOS/Proton el lanzador de Windows no aplica: hay que dejar que el propio juego cargue el mod.

Estos juegos **no tienen Steam Workshop**. No se puede “subir un mod” desde la tienda. La forma más simple es copiar unos archivos a la carpeta del juego.

## Qué copiar (no pisa el ejecutable ni Steam)

Desde `dist`, en **modo escritorio**, copia a la carpeta de cada juego (junto al `.exe`):

- `xinput1_4.dll` (cargador: el juego siempre importa XInput; en Proton suele funcionar mejor que dinput8)
- `dinput8.dll` (cargador de respaldo)
- `KofVsSelector.dll`
- `Kof98Bgm.dll` (selector de música; el mismo cargador lo abre junto al de controles)
- `kofvs.ini`

Carpetas típicas (pueden variar si el juego está en otra tarjeta):

- `~/.local/share/Steam/steamapps/common/The King of Fighters 2002 Unlimited Match/`
- `~/.local/share/Steam/steamapps/common/The King of Fighters'98 Ultimate Match/`

No copies `KofVsLauncher.exe`. No sustituyas `steam_api64.dll` ni el `.exe`.

## Opción de lanzamiento en Steam

En las propiedades del juego, opciones de lanzamiento:

```text
WINEDLLOVERRIDES="xinput1_4=n,b;dinput8=n,b" %command%
```

Eso obliga a Proton a usar los DLL de la carpeta del juego en vez de los suyos. Si solo pones `dinput8`, en el Deck a menudo **lo ignora** y no deja ni `kofvs.log`. Para **desactivar** el mod, quita esa opción y borra `xinput1_4.dll` y `dinput8.dll` de la carpeta.

## Qué puede fallar (sin evidencia de Deck)

- En Proton, D3D9 suele ser **DXVK**. El overlay de Windows parchea el código de `d3d9.dll`; en DXVK la vtable suele estar en la imagen del módulo. El mod primero intenta el hook inline y, si no, parchea el slot 17 de esa vtable. Si las dos fallan, el failsafe suelta la entrada y **no hay pantalla de selección**.
- Steam Input en el Deck envuelve casi todos los mandos como `28DE:11FF`. El teclado y los botones traseros del Deck ocupan slots. Con 3+ palancas USB más los controles del Deck es fácil llenar los 4 slots XInput.
- `CTRL+SHIFT+F9` / `F10` son incómodos en el Deck: asígnalos a un botón trasero en Steam Input.
- Un HORI en modo PS puede verse dos veces (Steam virtual + DirectInput). En Windows el mod oculta el duplicado si las pulsaciones coinciden; en Deck eso no está medido.
- Los seis perfiles de botones se aplican en memoria del proceso (punteros de tabla por puerto), igual que en Windows. No está comprobado en Deck que Proton/DXVK dejen escribir esas tablas, ni que Steam Input (mando virtual `28DE:11FF`) conserve las ocho posiciones físicas. Si el parche falla, el log dirá `KEYMAP PATCH FAILED` y se usará el remap XInput de respaldo.

## Cómo saber si cargó

Tras arrancar, junto al `.exe` debe aparecer:

- `kofvs-loader.log` — el cargador (`xinput1_4` o `dinput8`) sí se cargó;
- `kofvs.log` — el mod arrancó.

Si no hay **ninguno** de los dos, Proton sigue usando sus DLL y no los de la carpeta (revisa la opción de inicio y que los archivos estén al lado del `.exe`). Si hay `kofvs-loader.log` y no `kofvs.log`, el cargador funcionó pero no encontró `KofVsSelector.dll`. Si el log dice `Kof98Bgm.dll loaded`, la música también entró; su propio registro es `kof98bgm.log`.
