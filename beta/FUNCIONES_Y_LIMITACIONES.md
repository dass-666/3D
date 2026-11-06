# Funciones y limitaciones (beta)

Esta es una **beta de prueba**. «Validado en FL Studio» significa que el autor lo comprobó en su PC; nada se ha probado todavía en otros equipos.

## Funciones incluidas

| Función | Estado |
|---|---|
| Importar WAV y elegir un fragmento | Validado en FL Studio |
| Repeticiones en el mapa (hasta 16): paneo, delante/detrás aproximado, momento en tiempos del BPM o en ms | Validado en FL Studio |
| Disparar todas o una sola repetición | Validado en FL Studio |
| Pitch por repetición (±24 st) y formantes (±12 st) | Validado en FL Studio (validación general) |
| Stutter / Beat Repeat, con aceleración | Validado en FL Studio |
| Volume Shaper: 2–256 cortes, distribuciones, patrones, nivel automático | Validado en FL Studio |
| Editor de curva y de cortes con el ratón, rejilla BPM o libre en ms, zoom | Validado en FL Studio (validación general) |
| Cadena de efectos por repetición (carácter, modulación, Delay, Reverb), orden configurable | Validado en FL Studio (validación general) |
| Presets de voz (Robot, Monster, Fantasma…) y 13 presets creativos (Glitch Vocal, Trance Gate…) | Validado en FL Studio (validación general) |
| Modo Fácil y Modo Pro, tamaño 100/125/150 % | Validado en FL Studio |
| Deshacer / rehacer (Ctrl+Z / Ctrl+Y) | Validado en FL Studio |
| Captura del audio del canal del Mixer (últimos N s o grabación) | Validado en FL Studio |
| Guardado en el proyecto, escenas (.sesscene), presets propios | Validado en FL Studio (validación general) |
| Exportar WAV procesado (16/24/32 bits) | Validado en FL Studio (validación general) |

## Funciones experimentales (incluidas, pendientes de validar)

| Función | Notas |
|---|---|
| Arrastrar el WAV exportado directamente a la Playlist | No confirmado en FL Studio. Alternativa: «Abrir carpeta del audio» y arrastrar desde el Explorador |
| Procesar Mixer en vivo | Filtros, cortes, efectos y paneo sobre el audio del canal; sin pitch, formantes ni Stutter sobre la señal principal |
| Ecos en vivo | Las repeticiones repiten el audio del canal. Hasta 16 ecos, máximo 8 s. Con tono o formantes el eco más cercano posible es ~140 ms |

## Cambios de esta versión (pendientes de validar en FL Studio)

- **Presets propios**: dos copias del plugin (otro canal, una versión anterior instalada, un plugin «puenteado») ya no se borran los presets entre sí; «Mis presets» muestra al abrirse los guardados desde otra copia; si el archivo de presets no se puede escribir, el plugin lo dice en vez de anunciar «guardado»; la ventana para escribir el nombre se abre dentro de la ventana del plugin.
- **Editor de cortes**: con los cortes generados, el editor indica que para moverlos con el mouse hay que pulsar «Editar cortes».
- Un preset propio guarda sonido, pitch/formantes, cortes y efectos; **no guarda el Stutter** (igual que los presets integrados).

Guía de comprobación: **VALIDACION_PENDIENTE.md**.

## No disponible en esta beta

- Capas vocales y armonización.
- Audio 3D / binaural: «delante/detrás» solo cambia nivel y brillo.
- La exportación WAV no incluye el modo en vivo ni los ecos en vivo.
- Un cambio de tempo durante la exportación no se refleja (se usa el BPM del momento de exportar).
- Solo Windows x64, formato VST3. No hay versión para macOS, AU ni 32 bits.

## Datos que el plugin guarda en tu PC

- `Documentos\Spatial Echo Studio\diagnostico.log` — registro técnico para diagnosticar fallos.
- `Documentos\Spatial Echo Studio\Escenas` y `\Exportaciones` — solo si los usas.
- `%APPDATA%\SpatialEchoStudio\presets.json` — tus presets propios.

El plugin no se conecta a Internet.
