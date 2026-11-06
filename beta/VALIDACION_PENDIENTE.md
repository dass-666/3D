# Validación pendiente en FL Studio (oído y vista)

Versión: commit indicado en `VERSION.txt`. Estas pruebas cubren lo que las pruebas automáticas **no pueden** confirmar: cómo suena, cómo se ve y cómo se comporta dentro de FL Studio. Cada prueba dice qué hacer, qué debería pasar y qué anotar si falla.

Preparación (una vez): proyecto nuevo a **120 BPM**, metrónomo de FL Studio activado, el plugin en un canal del Mixer, un WAV de voz de 2–5 s importado (**Importar audio ▾ → Cargar archivo WAV…**), Modo **Pro**.

## A. Presets propios (corregido en esta versión)

Antes: con dos copias del plugin (por ejemplo una versión vieja y una nueva, o un plugin «puenteado»), los presets guardados en una se borraban al guardar en la otra; y si el archivo no se podía escribir, el plugin decía «guardado» igualmente.

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| A1 | Selecciona la repetición 1, actívale Reverb y 16 cortes. **+ Guardar preset** → nombre «Prueba A» → Guardar | La ventana para escribir el nombre aparece **dentro** de la ventana del plugin (no detrás). Aviso verde «Preset «Prueba A» guardado» |
| A2 | Inserta el plugin en **otro** canal del Mixer. En esa segunda copia: **Mis presets ▾ → Mis presets** | Aparece «Prueba A» |
| A3 | En la segunda copia guarda «Prueba B». Vuelve a la primera y abre **Mis presets ▾** | Aparecen «Prueba A» y «Prueba B» |
| A4 | Cierra FL Studio por completo, vuelve a abrirlo, abre cualquier proyecto con el plugin → **Mis presets ▾** | Siguen «Prueba A» y «Prueba B» |
| A5 | Aplica «Prueba A» a otra repetición y escucha | Suena con la misma Reverb y los mismos cortes; su posición y su momento NO cambian |
| A6 | Con la ventana de nombre abierta, cierra la ventana del plugin y vuelve a abrirla | El plugin responde con normalidad (nada queda bloqueado) |

Si falla: indica qué número, y adjunta `%APPDATA%\SpatialEchoStudio\presets.json` (pega esa ruta en el Explorador de Windows).

Límite conocido (no es un fallo): un preset guarda sonido, pitch/formantes, cortes y cadena de efectos. **No guarda el Stutter**, ni la posición, el momento o la ganancia de la repetición.

## B. Cortes: mover con el mouse

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| B1 | Pestaña **Cortes** con el Volume Shaper activado (sin editar) | Abajo a la izquierda del editor se lee «Cortes generados: para moverlos con el mouse pulsa «Editar cortes»…» |
| B2 | Pulsa **Editar cortes**. Arrastra el **cuerpo** de un corte a la derecha y luego a la izquierda | El corte se desplaza entero en horizontal, mantiene su duración y no pisa a sus vecinos. La etiqueta muestra los ms |
| B3 | Arrastra un **borde** y luego el **borde superior** | El borde cambia la duración (cursor ↔); el borde superior cambia la intensidad (cursor ↕) |
| B4 | **Espacio** para escuchar | Suena exactamente lo dibujado, sin clics en los bordes |

## C. Subdivisiones, modo libre en ms y Volume Shaper

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| C1 | **Rejilla** activada en 1/4, 1/8, 1/16 y 1/32; mueve un corte en cada caso | Los bordes saltan a pasos de 500, 250, 125 y 62,5 ms (a 120 BPM), contados desde el inicio de la repetición |
| C2 | Rejilla apagada; mueve un corte; luego Mayús + arrastrar | Posición libre en ms; con Mayús el movimiento es 10 veces más fino |
| C3 | Cambia el BPM del proyecto a 90 | Los cortes dibujados NO se mueven (se guardan en ms) |
| C4 | Generador: 8, 32 y 64 cortes; Profundidad 100 % y luego 50 % | Se oyen 8/32/64 cortes; con 50 % los cortes no llegan al silencio. Sin pérdida brusca de volumen |
| C5 | Ataque 0,1 ms frente a 10 ms; Liberación 3 ms frente a 50 ms | Ataque corto = cortes secos (sin clic audible); largos = cortes suaves |
| C6 | Activa **Nivel automático** con apertura 30 % | El volumen percibido se parece al de sin cortes (hasta +6 dB de compensación) |
| C7 | Modo de tiempo **ms** en una repetición: 500 ms; cambia el BPM | La repetición sigue sonando a 500 ms del disparo |

## D. Independencia por repetición

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| D1 | Repetición 1: Pitch +7, Ganancia −6 dB, Reverb. Repetición 2: Pitch −5, Delay. **Disparar** | Cada una suena con lo suyo: la 1 aguda con reverb, la 2 grave con delay |
| D2 | Cambia el Pitch y los efectos de la 1 mientras escuchas la 2 en **Solo** | La 2 no cambia en absoluto |
| D3 | Silencia la 1 | La 2 suena igual que antes |

## E. Sincronía y efectos

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| E1 | Repetición a 1 tiempo; reproduce con el metrónomo a 90, 120 y 140 BPM | La repetición cae siempre sobre el clic del tiempo |
| E2 | Bucle de 1 compás en la Playlist durante 1 minuto | No se pierde ninguna repetición ni se desplaza |
| E3 | Stutter 4 y 8 repeticiones, división 1/16, aceleración 0 y +50 % | Tartamudeo regular; con +50 % las porciones se aceleran; duración total igual |
| E4 | Reverse | La repetición suena al revés |
| E5 | Filtro paso bajo 1 kHz y paso alto 2 kHz | Más oscuro / más fino |
| E6 | Delay sincronizado 1/8 con realimentación 50 %; luego ping-pong | Ecos a corcheas que se apagan; ping-pong alterna izquierda/derecha |
| E7 | Reverb Room, Hall, Plate | Tres colas distintas; terminan solas |
| E8 | Formantes +6 y −6 con Pitch 0 | Mismo tono, voz «más pequeña» / «más grande» |

## F. Guardado y recuperación

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| F1 | Con todo lo anterior ajustado, guarda el proyecto (.flp), cierra FL Studio y ábrelo | Mismo audio, repeticiones, cortes dibujados, efectos, Modo Pro y tamaño de ventana |
| F2 | Guarda el proyecto otra vez sin tocar nada y vuelve a abrirlo | Todo igual (nada cambia con cada guardado) |

## G. Estabilidad

| # | Qué hacer | Qué debería pasar |
|---|---|---|
| G1 | 16 repeticiones con 64 cortes y Reverb; dispara varias veces durante 2 minutos | Anota % de CPU de FL Studio y tamaño de buffer; sin cortes ni chasquidos |
| G2 | Abre y cierra la ventana del plugin 10 veces mientras suena | Sin cuelgues ni cortes de audio |

## Cómo informar

Para cada fila: **OK**, **FALLA** (qué oíste o viste) o **NO PROBADO**. Si algo falla, adjunta el archivo de diagnóstico (`Documentos\Spatial Echo Studio\diagnostico.log`, si existe) y una captura de pantalla.
