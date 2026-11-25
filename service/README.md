# Servicio de conversación de IA2A

El AU emplea un intérprete de prueba integrado al abrirlo con la configuración inicial. Reconoce las peticiones de ejemplo en español y algunas correcciones como «menos aire».

## Probar con Claude desde Logic

Con Node 22.18 o posterior disponible en Terminal, guarda el proyecto y sal de Logic. Abre [Conectar-IA2A-Claude.command](Conectar-IA2A-Claude.command) y pega tu clave de Claude Console en el campo oculto de Terminal. El iniciador arranca el servicio en el Mac y abre Logic con el endpoint local. Deja Terminal abierta durante la prueba. El frontal muestra **Claude conectado** tras recibir una respuesta válida. Cada envío usa el saldo de API de tu organización.

El iniciador usa `claude-haiku-5-5`. Se puede cambiar el modelo mediante `IA2A_MODEL`. Una clave de API creada para un espacio de trabajo basta. Para una clave sin espacio asignado, define `ANTHROPIC_WORKSPACE_ID` antes de arrancar. La clave se mantiene en el proceso del servicio y las peticiones a Claude incluyen texto, historial reciente y mediciones resumidas. Las grabaciones y el render permanecen en el Mac.

## Configurar el servicio manualmente

Con Node 22.18 o posterior, `npm --prefix service start` inicia el servicio en el equipo. Para usarlo, el host del plugin debe recibir `IA2A_TURN_URL=http://127.0.0.1:48220/v1/turn`. Se puede configurar el mismo `IA2A_SERVICE_TOKEN` en servicio y host para autenticar al cliente.

Para conectar otro modelo compatible con Chat Completions, definir **en el servicio** `IA2A_MODEL_URL` (URL completa de Chat Completions), `IA2A_MODEL` y, si el proveedor lo requiere, `IA2A_MODEL_KEY`. Usar HTTPS para proveedores externos. Las peticiones contienen conversación y mediciones resumidas. La captura y el audio renderizado permanecen en el plugin.

Ejecutar las pruebas con `npm --prefix service test`.

Con el ejecutable ClientTest compilado, `node service/test/client-http.ts build/ClientTest` prueba el recorrido cliente → servicio → adaptador HTTP en bucle local. Añadir `--claude` para probar el recorrido de la API Messages con una respuesta simulada.
