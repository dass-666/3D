# Prueba manual en FL Studio (B2 corregido + B3 + interfaz)

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

## Hipótesis abiertas (sin confirmar en FL Studio)
- Sincronización/BPM: la causa encontrada (umbral de 1e-3 beats que cancelaba la
  repetición ante pequeñas diferencias de posición del host) se reprodujo en
  simulación; falta confirmar que era la causa en FL Studio.
- Persistencia: la ruta de guardado/restauración pasa las pruebas en ambos órdenes
  (estado antes o después de prepareToPlay, otra frecuencia de muestreo). No se
  ha reproducido la falla: el registro de diagnóstico dirá qué hace FL Studio.
