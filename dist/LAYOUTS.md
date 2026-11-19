# Seis perfiles de botones (KOF VS Controller Selector)

Cada perfil coloca A, B, C y D **una sola vez** sobre las ocho posiciones físicas del palo:

```text
  [ 0 Cuadrado/X ] [ 1 Triángulo/Y ] [ 2 R1/RB ] [ 3 L1/LB ]
  [ 4 Cruz/A     ] [ 5 Círculo/B    ] [ 6 R2/RT ] [ 7 L2/LT ]
```

A = puño débil (LP), B = patada débil (LK), C = puño fuerte (HP), D = patada fuerte (HK).
Los guiones no tienen ataque de combate. START, SELECT/Share y Options no se reasignan a A/B/C/D.

No se afirma que los perfiles 3–6 sean “los cuatro más usados del mundo”. Se indica la fuente de cada uno.

## 1. CLÁSICA (ya existía; no se ha cambiado)

```text
 [ A ] [ C ] [ - ] [ - ]
 [ B ] [ D ] [ - ] [ - ]
```

Es el diamante de fábrica del juego (X=LP, A=LK, Y=HP, B=HK). El mod deja el puntero de tabla del puerto
en NULL para no tocar las opciones del juego.

## 2. SAN FELIPE (ya existía; no se ha cambiado)

```text
 [ A ] [ B ] [ C ] [ - ]
 [ D ] [ - ] [ - ] [ - ]
```

Definida en este proyecto para el uso local de Juan. En '98, L1 puede actuar como SELECT (versiones EX)
si `san_felipe_l1_select=1` (por defecto 0: SELECT solo sale del botón SELECT físico). START sigue siendo START.

## 3. NEO GEO LINEAL

```text
 [ A ] [ B ] [ C ] [ D ]
 [ - ] [ - ] [ - ] [ - ]
```

Fila AES/MVS de cuatro botones. Referencias: palos AES de SNK, Neo Geo Mini, esquemas Type B de
adaptadores USB→Neo Geo y manuales que muestran A-B-C-D en una sola fila.

## 4. NEO GEO PULGAR

```text
 [ B ] [ C ] [ D ] [ - ]
 [ A ] [ - ] [ - ] [ - ]
```

A al pulgar (primera posición inferior), B-C-D arriba. Aparece en montajes de comunidad (por ejemplo
hilos de r/kof sobre palos Neo Geo). No se presenta como estándar oficial de SNK.

## 5. CUADRADO AB/CD

```text
 [ A ] [ B ] [ - ] [ - ]
 [ C ] [ D ] [ - ] [ - ]
```

Débiles arriba, fuertes abajo. Variante habitual de palos de 4 botones en cuadrado; no hay ranking
mundial asociado.

## 6. MÉXICO ALTERNATIVO

```text
 [ A ] [ B ] [ C ] [ - ]
 [ - ] [ - ] [ D ] [ - ]
```

A-B-C en la fila superior y D en R2 (tercera posición inferior). Variante regional pedida para este
mod; no se atribuye a un ranking internacional.

## Cómo se aplica

`ButtonLayoutManager` (`layout.c` / `layout.h`) registra los seis perfiles, los valida y construye una
tabla `{outbits, rawbit}` por jugador. En combate, el mod escribe los punteros de puerto 1 y 2 del
proceso (no modifica el `.exe` en disco): CLÁSICA = NULL (fábrica); los demás = tabla propia. Si el
parche de tabla no está activo, `layout_apply` traduce la misma rejilla a bits XInput de fábrica.

La prueba de botones de la pantalla LAYOUT usa `layout_action_from_pad` sobre el mismo mapa físico.
Mientras el selector está visible, la entrada al juego sigue bloqueada (`ACCESS_BLOCK_ALL`).
