# Prueba manual en FL Studio (B2–B4 + tiempo libre, Volume Shaper, entrada del host)

Las pruebas automáticas simulan un host; **no sustituyen esta prueba**. Anota
OK/FALLA en cada paso y adjunta `Documentos/Spatial Echo Studio/diagnostico.log`.

## 0. Preparación
1. Cierra FL Studio. Copia `Spatial Echo Studio.vst3` (artefacto de la CI) en
   `C:\Program Files\Common Files\VST3`, reemplazando el anterior.
2. Abre FL Studio, refresca la lista de plugins y carga Spatial Echo Studio en
   una pista del mezclador que tenga una voz o un loop.
3. Pasa a **Modo Pro** (cabecera): abajo verás la línea de diagnóstico y el
   interruptor «Registro de diagnóstico» (debe estar activo).

## 1. Sincronización (antes FALLABA)
1. Proyecto a 120 BPM, metrónomo activo. Carga un WAV corto (una palabra).
2. Deja solo el punto 1 activo (silencia los demás con «Silenciar»).
3. Pulsa Play en FL y, mientras suena, pulsa **Disparar** varias veces.
   - Esperado: cada clic suena 1 tiempo después del siguiente pulso de la rejilla
     (rejilla 1/4). Cada clic dispara: ya no hace falta apagar y encender.
   - En la línea de diagnóstico, «sonaron» sube con cada disparo y «perdidas» queda en 0.
4. Repite con rejilla 1/8 y 1/16.

## 2. Cambio de BPM (antes FALLABA)
1. Con el proyecto sonando, cambia el tempo a 90 y luego a 140.
2. Dispara en cada tempo. Esperado: la repetición sigue cayendo en la rejilla.
3. Si el tempo cambia **mientras** una repetición está pendiente, debe sonar igual
   (antes se cancelaba).

## 3. Transporte detenido, saltos y bucles
1. Con FL detenido, pulsa Disparar: suena enseguida, con el tempo del proyecto.
2. Haz un bucle corto de patrón y dispara justo antes del final: si la reproducción
   vuelve al inicio, la repetición pendiente se cancela (es lo previsto).

## 4. Persistencia (antes FALLABA)
1. Carga un WAV, selecciona un fragmento en la forma de onda, mueve puntos y
   cambia Mix. Guarda el proyecto (.flp). Cierra FL Studio por completo.
2. Abre el proyecto. Esperado: mismo audio, fragmento, puntos, Mix y modo.
3. Si falla, envíame el `diagnostico.log`: registra cada guardado (`getState`) y
   cada restauración (`setState`) con tamaños y si el audio se recuperó.

## 5. Varias repeticiones (B3)
1. Activa los 4 puntos de la demo. Dispara: deben sonar 4 repeticiones en los
   tiempos 1, 2, 3 y 4, la 1 a la izquierda y la 2 a la derecha.
2. Arrastra un punto mientras suena: el paneo cambia sin clics.
3. «Delante/Detrás» solo cambia nivel y brillo (profundidad aproximada). No es 3D.
4. Sube Mix al 100 % con 16 puntos a la vez: con «Protección» activa no debe saturar.

## 6. Pitch y ganancia por repetición (B4)
1. Selecciona la repetición 2. Preset «Agudo»: la barra Pitch marca +7 st.
   Justo después aparece «procesando tono…» (naranja) y un punto ámbar en la
   línea temporal; en una voz de 1–2 s debe desaparecer en menos de un segundo.
   Mientras se procesa, esa repetición suena con el tono original (no se simula).
2. Pulsa «Disparar esta» (panel) o clic derecho en el punto → «Disparar esta»:
   suena **solo** esa repetición, una quinta más aguda y con la **misma
   duración** que el original.
3. Pon la repetición 3 en −5 st y la 4 en +12 st. Dispara (global): cada una
   suena con su tono y las demás no cambian.
4. Ganancia −36 dB en la 1: casi inaudible; +12 dB con «Protección» activa: no
   satura (línea de diagnóstico «limitador» muestra la reducción).
5. Selecciona varias repeticiones (Shift+clic) y elige un preset: cambia el
   tono de todas, sin mover su posición ni su momento.
6. Guarda el proyecto, cierra y reabre FL Studio: tono, ganancia y Stutter de
   cada repetición deben volver exactamente.

## 7. Stutter / Beat Repeat (B4)
1. Repetición 3: activa Stutter, «Qué repetir: Inicio», 1/16, 8 repeticiones,
   50 % de duración. La línea temporal muestra 8 trozos. Dispara: oyes
   ta-ta-ta-ta-ta-ta-ta-ta a 1/16 del tempo del proyecto, sin clics.
2. Cambia el BPM del proyecto: la separación entre trozos sigue al tempo.
3. 1/32 con 32 repeticiones en los 16 puntos a la vez: no debe haber cortes ni
   saturación (con «Protección»).
4. «Copiar Stutter» en un punto y «Pegar Stutter» con varios seleccionados:
   todos reciben la misma configuración.
5. «Aceleración» (−100…100 %): > 0 los cortes se acercan progresivamente, < 0
   se separan; la duración total no cambia y la línea temporal dibuja los
   cortes donde suenan (sección 14). «Carácter» (Robot/Monster…) está en la
   cadena de efectos (sección 12).

## 8. Tiempo libre (ms)
1. En la línea temporal cambia «Sincronizado al BPM» por «Tiempo libre (ms)»:
   los momentos se convierten al tempo actual (nada se mueve). El eje pasa a
   segundos y «Momento (libre)» del panel se mide en ms.
2. Pon la repetición 2 en 500 ms. Dispara a 90 BPM y luego a 150 BPM: debe
   sonar siempre 0,5 s después del disparo (en modo BPM se movería).
3. Cambia el BPM del proyecto **mientras** una repetición está pendiente: su
   momento en ms no cambia.
4. «Ajustar a la rejilla» (opcional) en modo libre: el arrastre salta a la
   división rítmica al tempo actual.
5. Guarda y reabre: el modo y los ms se conservan.

## 9. Volume Shaper / cortes
1. Repetición 4, sección «Volume Shaper / cortes»: Activar, preset «Gate»
   (8 cortes cuadrados, 100 %). La línea temporal dibuja la curva sobre el
   bloque. Dispara: 8 cortes **uniformes**, sin clics (ataque 1 ms, liberación 5 ms).
2. «Nº de cortes» 16, profundidad 60 %, apertura 30 %: los cortes se acortan y
   la parte cerrada no llega al silencio.
3. Ritmo «Milisegundos» 100 ms: 10 cortes por segundo, independiente del BPM.
   Ritmo «División del BPM» 1/16: sigue al tempo del proyecto.
4. Presets «Tremolo» (triangular), «Cortes acelerados» (Pro: aceleración) y
   «Fade final» (solo la segunda mitad).
5. En Pro: «Toda la repetición» desactivado + «Inicio/Fin zona» aplica la curva
   solo a esa parte. Con Stutter activo, la curva se aplica a cada trozo.

## 9b. Cortes avanzados (nueva fase)
1. **Nivel con muchos cortes.** Repetición con el preset «Gate» y 32 cortes:
   compara con el mismo fragmento sin curva. Esperado: los picos se conservan
   (no hay rampa que «no llegue»); el nivel medio baja porque la mitad del
   tiempo es silencio. Activa «Nivel automático (hasta +6 dB)»: el volumen
   percibido vuelve a acercarse al original. «Ganancia post» añade hasta ±12 dB
   y el limitador de salida sigue protegiendo (no debe distorsionar).
2. «Cortes» hasta 256. La pista «Máx. útil aquí: N cortes» (Pro) dice cuántos
   caben con el ataque/liberación actuales en ese fragmento; por encima sólo
   quedan chasquidos (y la pista lo avisa).
3. Pro: «Ataque» 0,1–50 ms y «Liberación» 0,1–200 ms. Ataque corto = transitorio
   intacto; liberación larga = cortes más suaves. Comprueba que no hay clics
   con ataque 0,1 ms (es lo más brusco posible).
4. «Distribución» Acelerando / Desacelerando + «Intensidad»: los cortes se
   acortan o alargan progresivamente (sólo con «Nº de cortes»).
5. Pro: «Patrón» `x.xx.x..` (x suena, . mudo): cortes mudos exactamente donde
   hay punto. «Duración» 500 ms: la zona cortada acaba a los 500 ms y el resto
   suena entero.
6. Modo Libre (ms): cambia el BPM del proyecto y los cortes por «Nº» y por
   «Milisegundos» no cambian; sólo «División del BPM» sigue al tempo.
7. Presets nuevos: Fast Gate, Machine Gun, Vocal Chop (patrón), Glitch Vocal,
   Cortes desacelerados, Fade Stutter.

## 9c. Editor de curva (Pro)
1. Botón «Editor de curva…»: se abre sobre la ventana con la onda de esa
   repetición, la curva encima (blanca) y la regla en ms. Las partes mudas se
   ven oscuras en la onda; lo que suena, brillante.
2. Ctrl+rueda: zoom horizontal; rueda: desplazar; «↕+/↕−»: zoom vertical de la
   onda; «Todo»: ver entera.
3. «Manual»: la forma generada pasa a puntos. Doble clic añade un punto;
   arrastrar lo mueve (sin cruzar a los vecinos); clic derecho o Supr lo quita.
   «▶ Escuchar» dispara sólo esa repetición con la curva actual: lo que se ve
   es lo que suena.
4. Deshacer/Rehacer (Ctrl+Z / Ctrl+Y). Copiar en una repetición, Pegar en
   otra. «Duplicar» repite la curva dos veces en el mismo tiempo. «Espejo» la
   invierte en el tiempo.
5. «Guardar B», cambia algo, «A > B» / «B > A» alterna entre las dos versiones.
6. Arrastrar en la regla selecciona un rango; «Selección → zona» limita la
   curva a ese rango (el resto suena sin tocar).
7. Guarda, cierra y reabre el proyecto: los puntos manuales vuelven.

## 10. Entrada del host (Mixer)
1. Con audio sonando en la pista del Mixer donde está el plugin, el botón
   «Entrada del host» muestra el nivel de entrada en dB (sin audio: «--»).
2. Menú «Últimos 2 s»: el fragmento pasa a ser lo que acaba de sonar
   («Entrada del host (captura)» en la línea de audio, guardado en el proyecto).
   Las repeticiones usan ese fragmento igual que un WAV.
3. «Empezar a grabar», canta 3 s, pulsa de nuevo: el fragmento es la grabación.
4. Con la pista en silencio, «Últimos 2 s» debe rechazarse con un aviso, no
   cargar silencio.
5. Guarda, cierra y reabre: la captura vuelve (va embebida como WAV).
6. Detener y reiniciar el transporte no afecta a la captura (sigue
   registrando). El plugin no añade latencia.

## 11. Efectos por repetición
1. Fade in 300 ms / Fade out 300 ms: entrada y salida suaves.
2. Reverse: la repetición suena al revés (solo esa).
3. Paso bajo 800 Hz: más oscura. Paso alto 1500 Hz: más fina. Doble clic en el
   control lo apaga.

## 12. Cadena de efectos por repetición (nueva fase)
Orden por defecto: carácter → modulación → delay → reverb; se cambia en la
sección «Cadena de efectos (orden)» con ▲▼ (sección 14). Cada bloque tiene su
«Activar» y su «Mezcla» (Pro). Hasta 16 repeticiones con cadena a la vez; la
17.ª suena sin efectos (la interfaz lo dice).
1. **Delay.** Activar, 250 ms, feedback 50 %, mezcla 40 %: ecos cada 250 ms
   que se apagan. «Sincronizar al BPM» + 1/8: a 120 BPM los ecos caen cada
   250 ms y cambian al cambiar el tempo. «Ping-pong»: el primer eco va a la
   izquierda, el segundo a la derecha. Cambiar el tiempo mientras suena desliza
   el eco (sin clics).
2. **Reverb.** Hall, tamaño 80, decay 85, predelay 30 ms, mezcla 60: cola
   larga que sigue sonando después de la repetición y se apaga sola. Room con
   decay 10: cola corta. Plate: más brillante. «Salida» baja la reverb sin
   tocar la voz.
3. **Colas.** Con delay o reverb sonando, cambia otro parámetro o elimina la
   repetición: la cola debe seguir hasta apagarse, no cortarse.
4. **Carácter.** Saturación suave 80 %: más cuerpo/armónicos, sin subir de
   volumen de forma peligrosa. Overdrive: más agresivo. Bitcrusher 100 %:
   granulado digital. Teléfono: banda estrecha. Lo-fi: apagado y sucio. Robot
   (anillo) 50 %: voz metálica sin tono reconocible.
5. **Modulación.** Trémolo 5 Hz profundidad 100 %: pulsa 5 veces por segundo.
   Chorus 1 Hz: ensancha. Flanger: barrido metálico.
6. **Formantes.** «Formantes» −6 st con pitch 0: la voz suena «más grande» sin
   cambiar la nota. +4 st: más fina. Mientras se pre-renderiza aparece
   «procesando tono…» y suena el original.
7. **Presets de voz.** Monster (−9 st, formantes −6, saturación), Robot
   (anillo), Fantasma (+5 st, formantes +3, chorus + hall): deben sonar
   claramente distintos entre sí y del Original, no sólo por el tono.
8. **Presets creativos** (desplegable «Preset creativo»): Vocal Chop, Fast
   Gate, Trance Gate, Machine Gun, Glitch Vocal, Robot, Fantasma, Monster,
   Radio/Teléfono, Wide Vocal, Echo Throw, Fade Stutter, Reverse Tail. Cada uno
   cambia cortes, efectos y/o voz de las repeticiones seleccionadas.
9. **CPU.** 16 repeticiones con las cuatro secciones activas a 44,1/48/96 kHz:
   sin cortes de audio en FL Studio (indica el tamaño de búfer usado).
10. **Persistencia.** Guarda, cierra y reabre: cadena, formantes y curva
    manual vuelven idénticos.
11. No implementado (la interfaz no lo ofrece): reordenar la cadena, capas
    vocales, armonización, «aceleración progresiva» del Stutter.

## 13. Interfaz Modo Pro (nueva distribución)

Objetivo: comprobar que la nueva distribución del Modo Pro funciona dentro de FL Studio y que nada del sonido cambia al cambiar de modo. Ventana mínima 1080×790; prueba también a redimensionar.

1. Carga un WAV o captura la entrada, crea 3–4 repeticiones y pulsa **Pro**. Debe aparecer: columna izquierda (preset activo, Velocidad / Intensidad / Mezcla, lista de categorías), centro con pestañas **Cortes / Mapa espacial / Audio-fragmento**, panel de parámetros a la derecha con secciones plegables, línea temporal y barra inferior (Escuchar este punto · Solo · A/B curva · ◀ Punto n/N ▶ · medidor de salida).
2. **Velocidad**: gira el mando y pulsa «Escuchar este punto». El número de cortes de la curva central y lo que suena deben cambiar a la vez (2–256; el editor limita al máximo útil según las rampas). **Intensidad** = profundidad de los cortes. **Mezcla** = el mismo parámetro Mezcla de la cabecera (comprueba que los dos mandos se mueven juntos).
3. Ajustes avanzados no relacionados (p. ej. Ataque, Patrón, Reverb activada) deben conservarse al mover Velocidad o Intensidad.
4. **Generar cortes perfectos**: arrastra en la regla del editor central para seleccionar una zona y pulsa el botón (cambia a «Generar cortes en la selección»). La curva aparece solo en esa zona; fuera, el audio pasa al 100 %. Después pulsa **Manual** y arrastra un punto: lo que se dibuja es lo que suena.
5. Con una curva manual activa, la columna izquierda avisa «Curva manual activa: Velocidad e Intensidad vuelven al generador». Mover Velocidad vuelve al generador; **Manual** recupera los puntos guardados.
6. **Ampliar** abre el editor a toda la ventana; **Cerrar** vuelve al centro con la misma curva y zoom.
7. Categorías: cada botón abre su sección en el panel derecho y hace scroll hasta ella; «Espacio / Paneo» muestra el mapa en el centro y «Cortes / Volume Shaper» vuelve al editor.
8. Barra inferior: ◀ ▶ cambian la repetición seleccionada; Solo marca solo las seleccionadas; A/B curva se activa tras «Guardar B»; el medidor de salida debe moverse al sonar las repeticiones y marcar en rojo si toca 0 dBFS.
9. Cambia Fácil ↔ Pro varias veces, guarda el proyecto, ciérralo y vuelve a abrirlo: el preset activo, las curvas y las secciones deben conservarse (las secciones plegadas no se guardan; es solo visual).
10. Rendimiento: con 16 repeticiones y 256 cortes, arrastra puntos en el editor; la interfaz no debe dar tirones y el audio no debe cortarse (activa el Registro de diagnóstico y mira «perdidas» y «limitador»).

Lo que NO existe todavía (no se anuncia en la interfaz): capas vocales, armonización, audio 3D/binaural.

## 14. B8: deshacer, aceleración del Stutter, orden de la cadena, presets propios, tamaño

1. **Deshacer/rehacer** (↶ ↷ en la cabecera, Ctrl+Z / Ctrl+Y): mueve un punto, cambia un mando y borra un punto; cada Ctrl+Z deshace una acción. Un arrastre largo de un mando es un solo paso. Cargar un WAV o abrir un proyecto no se deshace y vacía el historial (es intencionado). El audio cargado nunca cambia con deshacer.
2. **Aceleración del Stutter** (panel Stutter): con 16 repeticiones y +100 %, los cortes se amontonan al final como una pelota que bota; con −100 % se separan. La duración total es la misma que con 0 %. Comprueba que la línea temporal coincide con lo que oyes.
3. **Orden de la cadena** (categoría «Cadena de efectos (orden)»): con Bitcrusher + Delay, sube el delay por encima del carácter (▲): los ecos dejan de estar triturados uno a uno y se tritura la mezcla. Guarda y reabre: el orden se conserva.
4. **Presets propios**: ▾ en la tarjeta del preset → «Guardar la repetición como preset propio…», nombre, Guardar. Aparece en «Propios», en ◀ ▶ y en «Preset creativo» del panel. ☆ marca favorito (sección «Favoritos» del menú). Los presets se guardan en `%APPDATA%\SpatialEchoStudio\presets.json` y se comparten entre instancias y proyectos. «Eliminar este preset propio» lo borra.
5. **Tamaño de la interfaz** (Escena ▾ → Tamaño): 125 % y 150 % escalan toda la ventana y se guardan con el proyecto. Comprueba que FL Studio redimensiona la ventana del plugin sin recortes.
6. **CPU**: 16 repeticiones, 256 cortes, cadena completa en todas (presets «Echo Throw»/«Fantasma»/«Wide» + Velocidad 256). En Linux Release el procesador usa ~10 % de tiempo real a 48 kHz y ~21 % a 96 kHz con cualquier tamaño de bloque; anota el % de CPU de FL Studio y el tamaño de buffer.

## 15. B9: presets, exportar a la Playlist y Mixer en vivo

Barra de acciones (debajo de la cabecera, igual en Fácil y Pro): **Importar audio ▾ · Procesar Mixer en vivo · Exportar a Playlist · Mis presets ▾ · Guardar preset**.

1. **Tres cosas distintas que se guardan**:
   - *Escena* (cabecera, «Escena ▾ → Guardar escena (.sesscene)»): todo el plugin (repeticiones + fragmento). Carpeta por defecto `Documentos\Spatial Echo Studio\Escenas`.
   - *Preset* («Guardar preset»): solo el sonido, los cortes y los efectos de una repetición (sin posición ni momento). Se guarda en `%APPDATA%\SpatialEchoStudio\presets.json` y aparece en «Mis presets» en todos los proyectos. Sin repetición seleccionada se usa la primera.
   - *Audio* («Exportar a Playlist»): un archivo .wav.
   Prueba en **Fácil**: Guardar preset → nombre → aparece el aviso verde; «Mis presets ▾ → Mis presets (n)» lo lista; aplícalo a otra repetición; «Eliminar un preset propio» lo borra; «Abrir la carpeta de presets» abre la carpeta. Abre una escena guardada con B8 o antes: debe cargar igual.
2. **Exportar audio**: «Exportar a Playlist» → Contenido (mezcla completa o solo la repetición seleccionada), Formato (24 bits PCM, 16 bits PCM con dither, 32 bits float), Frecuencia (Proyecto, 44.1–96 kHz), «Incluir el fragmento original» → **Exportar**. Barra de progreso; **Cancelar** detiene. Comprueba: suena igual que «Probar» (mismo Mix, Salida y Protección, mismo BPM del proyecto); las colas de Delay/Reverb terminan solas (no se cortan); el archivo empieza en el momento del disparo; con 16 bits/24 bits no aparece aviso de recorte si Protección está activa. Los archivos van a `Documentos\Spatial Echo Studio\Exportaciones` (no se sobrescriben).
3. **Arrastrar a la Playlist (sin verificar)**: tras exportar, arrastra el recuadro «Arrastra este clip a la Playlist» hasta una pista de la Playlist. Anota si FL Studio crea el clip. Si no lo acepta: **Abrir carpeta del audio** y arrastra el .wav desde el Explorador de Windows (esto sí es el flujo estándar de FL Studio). No borres la carpeta: FL Studio enlaza el archivo.
4. **Procesar Mixer en vivo**: pon el plugin en una pista del Mixer que reciba un loop o una voz, reproduce y pulsa «Procesar Mixer en vivo» (pasa a verde «ACTIVO»; el panel derecho muestra «Mixer en vivo»). Prueba en este orden: Dry/Wet 0 % (suena el original) y 100 %; Filtros (paso alto/bajo); Volume Shaper activado con 4–16 cortes y «Ciclo de cortes» 1 compás: los cortes deben caer siempre en los mismos tiempos aunque empieces la reproducción en otro compás; con FL Studio parado los cortes siguen en bucle a su tempo; Carácter, Modulación, Delay, Reverb y su orden; Espacio/Paneo. Detén la reproducción: la cola de reverb/delay sigue sonando (si FL Studio corta la cola, revisa «Smart disable» del plugin). Bypass devuelve el original exacto. Las repeticiones disparadas siguen sonando encima. «Capturar últimos N s» sigue capturando la entrada original (sin procesar).
5. **Latencia y CPU**: el modo en vivo no añade latencia (0 muestras: no hace falta compensación). Anota el % de CPU de FL Studio con el modo en vivo y cadena completa, y el tamaño de buffer.
6. **Persistencia**: con el modo en vivo activo y ajustado, guarda el proyecto de FL Studio, ciérralo y ábrelo: el modo en vivo y sus ajustes deben volver. Los presets propios no van en el proyecto (están en tu usuario).
7. **Regresión B8**: repite rápidamente los puntos de la sección 14 (deshacer, aceleración, orden de cadena, tamaño).

Límites conocidos de B9: en vivo no hay pitch, formantes ni Stutter (necesitan el fragmento pre-renderizado: usa Capturar + repeticiones); el ciclo de cortes en vivo usa el tempo del proyecto y la exportación se renderiza al BPM que tenía el proyecto al exportar (un cambio de tempo durante las repeticiones no se refleja); bloques del host mayores de 4096 muestras (o del doble del anunciado) pasan sin procesar en vivo.

## 16. B10: edición libre de cortes con el ratón (editor de curva)

Selecciona una repetición, pestaña **Cortes** (Pro) o «Editor de curva…», y pulsa **Ampliar** para trabajar a pantalla completa.

1. **Editar puntos** (envolvente continua): arrastra un punto en diagonal: cambia a la vez el momento (X) y el volumen (Y); la etiqueta muestra ms y %. Un punto se detiene en su vecino (no lo cruza). Mayús mientras arrastras = ajuste fino (1/10). ← → mueven el punto seleccionado 1 ms (Mayús: 0,1 ms).
2. **Editar cortes** (bloques): la primera vez se crean a partir de la forma que suena ahora. Arrastra el **cuerpo** de un corte para moverlo entero, sus **bordes** para estirarlo o acortarlo (cursor ↔) y su **borde superior** para la intensidad (cursor ↕). Arrastrar en un hueco vacío crea un corte nuevo; doble clic crea uno de un paso de rejilla (50 ms sin rejilla). Clic derecho en un corte: Duplicar, Eliminar, Intensidad. Ctrl+D duplica, Supr borra. Prueba el patrón «TA — TA-TA — TAAA — TA» y escúchalo con **Espacio** o «▶ Escuchar»: debe sonar exactamente lo dibujado.
3. **Rejilla**: activa «Rejilla» y elige 1/4…1/64 o tresillos. Los bordes caen en la rejilla del BPM, contada desde el inicio de la repetición. Con la rejilla apagada la posición es libre en ms. Cambia el BPM de FL Studio: los cortes no se mueven (se guardan en ms).
4. **Zoom y desplazamiento**: Ctrl+rueda o + / − hasta ver milisegundos sueltos; rueda o la barra inferior para desplazarte.
5. **Cambiar de herramienta no pierde nada**: los puntos y los cortes se guardan por separado. Clic derecho en un hueco: «Convertir cortes → puntos» o «Crear cortes desde la curva actual» (estas dos sí reemplazan, y lo dicen). Con más de 127 cortes la conversión a puntos se rechaza con un aviso.
6. **Deshacer**: cada arrastre es un paso (Ctrl+Z / Ctrl+Y). Guarda el proyecto, ciérralo y ábrelo: los cortes vuelven en las mismas posiciones. Las demás repeticiones no cambian.
7. **Rendimiento**: con 256 cortes el editor debe seguir fluido y el audio sin cortes (diagnóstico «perdidas» = 0).

Límite: dentro de cada corte el ataque y la liberación se aplican hacia dentro (el corte suena exactamente entre sus bordes). En cortes más cortos que ataque + liberación, las rampas se acortan a la mitad del corte.

## Hipótesis abiertas (sin confirmar en FL Studio)
- Sincronización/BPM: la causa encontrada (umbral de 1e-3 beats que cancelaba la
  repetición ante pequeñas diferencias de posición del host) se reprodujo en
  simulación; falta confirmar que era la causa en FL Studio.
- Persistencia: la ruta de guardado/restauración pasa las pruebas en ambos órdenes
  (estado antes o después de prepareToPlay, otra frecuencia de muestreo). No se
  ha reproducido la falla: el registro de diagnóstico dirá qué hace FL Studio.
