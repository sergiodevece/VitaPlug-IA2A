# Servicio de conversación de IA2A

El AU emplea un intérprete de prueba integrado al abrirlo con la configuración inicial. Reconoce las peticiones de ejemplo en español y algunas correcciones como «menos aire».

Con Node 22.18 o posterior, `npm --prefix service start` inicia el servicio en el equipo. Para usarlo, el host del plugin debe recibir `IA2A_TURN_URL=http://127.0.0.1:48220/v1/turn`. Se puede configurar el mismo `IA2A_SERVICE_TOKEN` en servicio y host para autenticar al cliente.

Para conectar un modelo, definir **en el servicio** `IA2A_MODEL_URL` (URL completa de Chat Completions), `IA2A_MODEL` y, si el proveedor lo requiere, `IA2A_MODEL_KEY`. Usar HTTPS para proveedores externos. Las peticiones contienen conversación y mediciones resumidas. La captura y el audio renderizado permanecen en el plugin.

Ejecutar las pruebas con `npm --prefix service test`.

Con el ejecutable ClientTest compilado, `node service/test/client-http.ts build/ClientTest` prueba el recorrido cliente → servicio → adaptador HTTP en bucle local.
