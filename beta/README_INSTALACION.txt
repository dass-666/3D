SPATIAL ECHO STUDIO - BETA DE PRUEBA (Windows x64, VST3)
=========================================================

Es una version BETA: puede tener errores. Usala en proyectos de prueba,
no en trabajos importantes sin copia de seguridad.

Requisitos
- Windows 10 u 11 de 64 bits.
- FL Studio de 64 bits con soporte VST3 (FL Studio 20 o posterior).

INSTALACION PASO A PASO
-----------------------
1. Descarga el archivo SpatialEchoStudio-Beta-Windows-x64.zip.
2. Haz clic derecho sobre el ZIP > "Extraer todo..." > Extraer.
   No abras el plugin desde dentro del ZIP sin extraerlo.
3. Cierra FL Studio por completo.
4. Copia la carpeta "Spatial Echo Studio.vst3" (entera, sin abrirla ni
   cambiarle el nombre) dentro de:
       C:\Program Files\Common Files\VST3
   Windows pedira permiso de administrador: pulsa "Continuar".
   Si ya existia una version anterior, elige "Reemplazar".
5. Abre FL Studio.
6. Menu Options > Manage plugins (Plugin Manager) > "Find more plugins"
   (o "Find installed plugins"). Espera a que termine.
   Busca "Spatial Echo Studio" en la lista y marca la estrella (favorito)
   para que aparezca en los menus.
7. Abre el Mixer (F9), elige un canal (Insert) que reciba audio, haz clic en
   una ranura vacia (Slot) y selecciona Spatial Echo Studio.
8. Comprueba que la ventana del plugin se abre y que el audio del canal se
   sigue oyendo al reproducir.

Tambien funciona como efecto en el Channel Rack, pero las pruebas de esta
beta se pensaron para el Mixer.

SOLUCION DE PROBLEMAS
---------------------
* El plugin no aparece
  - Comprueba la ruta exacta: C:\Program Files\Common Files\VST3\Spatial Echo Studio.vst3
    (es una carpeta; dentro hay Contents\x86_64-win\Spatial Echo Studio.vst3).
  - En Plugin Manager, comprueba que esa carpeta VST3 esta en "Plugin search
    paths" y vuelve a pulsar "Find more plugins".
  - El plugin es solo de 64 bits: no aparece en FL Studio de 32 bits.

* El plugin no abre o FL Studio muestra un error al cargarlo
  - Asegurate de haber copiado la carpeta .vst3 completa (no solo una parte).
  - Cierra FL Studio, borra la carpeta del plugin, vuelve a copiarla desde el
    ZIP recien extraido y repite el escaneo.
  - Anota el mensaje exacto y envia el reporte (REPORTE_DE_ERRORES.md).

* No hay sonido
  - Comprueba que el canal del Mixer recibe audio (sus medidores se mueven).
  - El plugin no genera sonido por si solo: hay que importar un WAV o capturar
    el audio del canal (ver GUIA_DE_USO.md) y pulsar "Disparar".
  - Revisa que "Mix" y "Salida" no esten al minimo y que el plugin no este
    en bypass (boton de la ranura del Mixer).

* Problemas de permisos al copiar
  - Copiar en "C:\Program Files\Common Files\VST3" necesita permiso de
    administrador. Usa una cuenta con permisos de administrador o pide a quien
    administre el equipo que copie la carpeta. No hace falta desactivar el
    antivirus ni otras protecciones de Windows.
  - Si Windows marca el ZIP como descargado de Internet: clic derecho en el ZIP
    > Propiedades > si aparece la casilla "Desbloquear", marcala > Aceptar, y
    extrae de nuevo.

* Se carga una version antigua
  - Cierra FL Studio, borra C:\Program Files\Common Files\VST3\Spatial Echo Studio.vst3,
    copia la nueva y vuelve a escanear en Plugin Manager.
  - Busca otras copias (por ejemplo en C:\Program Files\VSTPlugins o en
    carpetas personalizadas) y borralas.
  - Compara con VERSION.txt: el commit de esta beta figura ahi.

Archivos de este paquete
- Spatial Echo Studio.vst3      el plugin
- README_INSTALACION.txt        este archivo
- GUIA_DE_USO.md                guia para empezar + pruebas de 15-20 minutos
- FUNCIONES_Y_LIMITACIONES.md   que hace y que no hace esta beta
- REPORTE_DE_ERRORES.md         plantilla para enviar resultados y fallos
- VALIDACION_PENDIENTE.md       pruebas de oido y vista de los cambios recientes
- VERSION.txt                   version, commit y pruebas realizadas

Los archivos .md se pueden abrir con el Bloc de notas.
