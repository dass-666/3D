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
5. Lo que **no** existe todavía y la interfaz marca como «próximamente»:
   aceleración progresiva, «carácter» (Robot/Fantasma) y «velocidad».

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
   bloque. Dispara: 8 cortes **uniformes**, sin clics (fundido 3 ms).
2. «Nº de cortes» 16, profundidad 60 %, apertura 30 %: los cortes se acortan y
   la parte cerrada no llega al silencio.
3. Ritmo «Milisegundos» 100 ms: 10 cortes por segundo, independiente del BPM.
   Ritmo «División del BPM» 1/16: sigue al tempo del proyecto.
4. Presets «Tremolo» (triangular), «Cortes acelerados» (Pro: aceleración) y
   «Fade final» (solo la segunda mitad).
5. En Pro: «Toda la repetición» desactivado + «Inicio/Fin zona» aplica la curva
   solo a esa parte. Con Stutter activo, la curva se aplica a cada trozo.
6. Lo que no existe: edición manual de puntos de la curva (la interfaz lo dice).

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
4. Delay, reverb, modulación, formantes y capas vocales: **no implementados**
   (la interfaz lo indica; no hay controles).

## Hipótesis abiertas (sin confirmar en FL Studio)
- Sincronización/BPM: la causa encontrada (umbral de 1e-3 beats que cancelaba la
  repetición ante pequeñas diferencias de posición del host) se reprodujo en
  simulación; falta confirmar que era la causa en FL Studio.
- Persistencia: la ruta de guardado/restauración pasa las pruebas en ambos órdenes
  (estado antes o después de prepareToPlay, otra frecuencia de muestreo). No se
  ha reproducido la falla: el registro de diagnóstico dirá qué hace FL Studio.
