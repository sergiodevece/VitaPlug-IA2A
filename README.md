# VitaPlug IA2A

Prototipo de compresor óptico conversacional para macOS. El frontal A reúne la conversación, la escucha Antes/Después y un historial de reducción desplegable. La aprobación guarda la propuesta en la sesión del DAW.

## Escuchar en Logic

1. Insertar IA2A desde **Audio Units → VitaPlug → IA2A**. Dejar **Fuente** en **Automática** o elegir el material. El modo automático usa el nombre de pista que facilita Logic como indicio. Reproducir un pasaje representativo.
2. Pulsar **Escuchar** y describir el cambio. También se puede escribir primero y pulsar **Enviar** para armar la captura. Para usar una referencia, seleccionar el acompañamiento en **Cadena lateral**, activar **Referencia de mezcla** y capturar de nuevo.
3. Alternar **Antes** y **Después**, primero a volumen igualado y luego a nivel final. Escribir otra petición para refinar. **Aplicar** guarda el ajuste a nivel final. **Deshacer** recupera el anterior.

Conectar [el servicio de conversación](service/README.md) para interpretar peticiones libres. El modelo recibe un resumen de la pista con nivel, transitorios, colas y balance estéreo. Si identificar la fuente cambiaría la propuesta, puede preguntarlo. Las respuestas y peticiones sucesivas conservan el contexto reciente mientras el calibrador prueba los ajustes con el DSP óptico sobre el audio real.

## Compilar y probar

En macOS con CMake, compilador C++20 y JUCE 9.0.2 (revisión `72782788ce18c2d4d760b28e0921d6ffc6431102`):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DJUCE_DIR=/ruta/a/JUCE
cmake --build build --parallel
ctest --test-dir build --output-on-failure
npm --prefix service test
```

El componente se genera en `build/IA2A_artefacts/Release/AU/IA2A.component`. Una vez instalado, ejecutar `auval -v aufx Ia2a Vtpl` y `build/InstalledAU` para comprobar el registro y el aislamiento de la referencia.

El [informe de pruebas](docs/acceptance/2026-11-25.md) recoge la paridad con el original, los resultados con Apple Loops y el recorrido en Logic. El [evaluador local](tools/ia2a_eval.cpp) admite `--help` y exporta el nivel final, una versión igualada para comparar y un informe JSON.
