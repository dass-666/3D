# Spatial Echo Studio — Guía de uso (beta)

Spatial Echo Studio crea **repeticiones** (ecos) de una voz o fragmento de audio. Cada repetición es un **punto** en el mapa: su posición decide el paneo (izquierda/derecha) y la profundidad aproximada (delante/detrás: solo cambia nivel y brillo, no es audio 3D). Cada repetición tiene su propio momento, tono, cortes y efectos.

## Parte 1. Primeros pasos

**A. Importar una voz o fragmento WAV.** Pulsa **Importar audio ▾ → Cargar archivo WAV…** (o arrastra un .wav a la forma de onda). Puedes seleccionar una parte arrastrando sobre la forma de onda: esa parte es el «fragmento» que se repetirá.

**B. Escuchar el original.** Reproduce el proyecto en FL Studio: el audio del canal del Mixer pasa sin cambios. Para oír las repeticiones pulsa **Disparar** (cabecera) o, con un punto seleccionado, clic derecho → **Disparar esta**.

**C. Crear repeticiones.** Doble clic en el mapa añade una repetición. Arrástrala para cambiar su posición. Shift+clic selecciona varias. Clic derecho sobre un punto: disparar, silenciar o borrar.

**D. Presets.** Selecciona una repetición y elige un preset en el panel derecho: **Preset vocal** (por ejemplo Robot, Monster, Fantasma) o **Preset creativo** (Glitch Vocal, Trance Gate, Vocal Chop, Echo Throw, Wide Vocal…). Si seleccionas varias repeticiones, el preset se aplica a todas.

**E. Intensidad, velocidad y mezcla.** En Modo Pro, la columna izquierda tiene tres mandos: **Velocidad** = número de cortes, **Intensidad** = profundidad de los cortes, **Mezcla** = cuánto se oyen las repeticiones frente al original (es el mismo «Mix» de la cabecera).

**F. Modo Fácil.** Muestra el mapa, la forma de onda y los controles esenciales. Ideal para empezar.

**G. Modo Pro.** Botón **Pro** en la cabecera. Añade: columna de presets y mandos rápidos a la izquierda, pestañas **Cortes / Mapa espacial / Audio** en el centro, panel de parámetros con secciones plegables a la derecha y una barra inferior (Escuchar este punto, Solo, A/B curva, medidor de salida). Cambiar de modo no cambia el sonido.

## Parte 2. Sonido

**H. Cortes perfectos.** En Pro, pestaña **Cortes**: arrastra sobre la regla para elegir una zona (opcional) y pulsa **Generar cortes perfectos**. Ajusta el número de cortes con **Velocidad** (2–256).

**I. Editar curvas a mano.** En el editor de cortes (botón **Ampliar** para pantalla completa):
- **Editar puntos**: arrastra puntos (momento y volumen). Doble clic añade, Supr borra. Mayús = ajuste fino.
- **Editar cortes**: mueve, estira o acorta bloques con el ratón; clic derecho para duplicar o eliminar.
- **Rejilla**: activa para ajustar al BPM (1/4…1/64) o desactívala para posición libre en ms.
- **Espacio** o **▶ Escuchar** para oír lo dibujado. Ctrl+Z deshace.

**J. Pitch y formantes.** Sección **PITCH Y FORMANTES** de la repetición: Pitch en semitonos, Formantes para cambiar el «tamaño» de la voz. Tras cambiar el tono aparece «procesando tono…» un momento; mientras tanto suena el tono original.

**K. Reverb, Delay y otros efectos.** Cada repetición tiene su cadena: carácter (Overdrive, Bitcrusher…), modulación (Chorus, Flanger…), **Delay** y **Reverb** (Hall, Plate…), además de filtros paso alto/bajo, fades y Reverse. El orden de la cadena se cambia en la categoría «Cadena de efectos (orden)».

**L. Mapa espacial.** Izquierda/derecha = paneo; arriba/abajo = delante/detrás (nivel y brillo). En Pro, pestaña **Mapa espacial**.

## Parte 3. Guardar, capturar y exportar

**M. Tres formas de guardar (son distintas):**

| Qué | Dónde | Qué guarda |
|---|---|---|
| **Proyecto de FL Studio** (.flp) | Guardar en FL Studio | Todo el plugin, automáticamente |
| **Escena** (.sesscene) | Cabecera: **Escena ▾ → Guardar escena** | Todo el plugin (repeticiones + audio) en un archivo, para llevarlo a otro proyecto. Carpeta: `Documentos\Spatial Echo Studio\Escenas` |
| **Preset propio** | **+ Guardar preset** | Solo el sonido, cortes y efectos de una repetición. Aparece en **Mis presets ▾** en todos tus proyectos |

**N. Capturar audio desde el Mixer.** Con audio sonando en el canal del Mixer: **Importar audio ▾ → Capturar últimos N s** (lo que acaba de sonar) o **Empezar a grabar** / pulsar de nuevo para parar. La captura pasa a ser el fragmento y se guarda con el proyecto.

**O. Exportar audio procesado.** **⇪ Exportar a Playlist** → elige contenido, formato (WAV 24 bits recomendado) y pulsa **Exportar**. El WAV va a `Documentos\Spatial Echo Studio\Exportaciones`. Después puedes arrastrar el recuadro del clip a la Playlist; si FL Studio no lo acepta, pulsa **Abrir carpeta del audio** y arrastra el .wav desde el Explorador. *Nota: la exportación incluye las repeticiones del fragmento importado; no incluye el modo en vivo.*

**P. Procesar el Mixer en vivo (experimental, sin validar todavía en FL Studio).** **● Procesar Mixer en vivo** aplica filtros, cortes, efectos y paneo al audio que pasa por el canal, sin latencia. La casilla **Ecos en vivo** hace que las repeticiones repitan también ese audio. Úsalo y cuéntanos qué pasa, pero no lo des por terminado.

---

# Pruebas para el probador (15–20 minutos)

Prepara: un proyecto nuevo en FL Studio a 120 BPM, un WAV corto de voz (2–5 s) y el plugin en un canal del Mixer. Marca una casilla por prueba y escribe un comentario si algo te sorprende.

| # | Prueba | Qué debería pasar | FUNCIONA | FALLA | Comentarios |
|---|---|---|---|---|---|
| 1 | Cargar el plugin en el Mixer | La ventana se abre sin errores | ☐ | ☐ | |
| 2 | Importar audio → Cargar archivo WAV | Se ve la forma de onda | ☐ | ☐ | |
| 3 | Crear 2–3 repeticiones (doble clic en el mapa) y pulsar **Disparar** | Suenan las repeticiones, cada una en su lado | ☐ | ☐ | |
| 4 | Cambiar presets en una repetición: Robot, Monster, Fantasma, Glitch Vocal, Trance Gate | Cada uno suena claramente distinto | ☐ | ☐ | |
| 5 | Modo Pro → Cortes → Velocidad en 8, luego 32, luego 64 y escuchar | Se oyen 8 / 32 / 64 cortes, sin perder volumen de forma brusca | ☐ | ☐ | |
| 6 | Ajustar Ganancia de una repetición y **Mix** | El volumen cambia; no satura con «Protección» activa | ☐ | ☐ | |
| 7 | Pitch +7 y luego −5 en una repetición | Suena más agudo / más grave, misma duración | ☐ | ☐ | |
| 8 | Activar **Reverb** (Hall) | Cola de reverberación audible | ☐ | ☐ | |
| 9 | Activar **Delay** | Ecos repetidos detrás de la repetición | ☐ | ☐ | |
| 10 | Activar **Stutter** (4 repeticiones) | La repetición tartamudea | ☐ | ☐ | |
| 11 | Cambiar Fácil ↔ Pro varias veces | Nada cambia en el sonido ni se pierde | ☐ | ☐ | |
| 12 | Guardar el proyecto, cerrar FL Studio, volver a abrirlo | Mismo audio, repeticiones y ajustes | ☐ | ☐ | |
| 13 | CPU: 16 repeticiones con 64 cortes y Reverb; disparar | Anota el % de CPU de FL Studio y si hay cortes de audio | ☐ | ☐ | CPU: ___ %  Buffer: ___ |
| 14 (opcional) | Exportar a Playlist (WAV 24 bits) | Aparece el WAV en Documentos\Spatial Echo Studio\Exportaciones y suena igual | ☐ | ☐ | |
| 15 (opcional) | Procesar Mixer en vivo con un loop sonando | El efecto se oye sobre el canal; Bypass devuelve el original | ☐ | ☐ | |

Cuando termines, rellena **REPORTE_DE_ERRORES.md** y envíalo con la tabla anterior.
