# Servicio de conversación de IA2A

El AU espera un modelo para atender peticiones de mezcla. El intérprete de reglas de los primeros prototipos se usa en las pruebas automatizadas.

## Probar con Claude desde Logic

Con Node 22.18 o posterior disponible en Terminal, ejecuta [Conectar-IA2A-Claude.command](Conectar-IA2A-Claude.command) con `/bin/bash` y pega tu clave de Claude Console en el campo oculto. El iniciador arranca el servicio en el Mac y abre Logic. Si Logic ya tiene cargada esta versión del AU, puede encontrar el servicio al enviar una petición sin reiniciar el host. Deja Terminal abierta durante la prueba. El frontal muestra **Claude conectado** tras recibir una respuesta válida. Cada envío usa el saldo de API de tu organización.

El iniciador usa `claude-haiku-5-5`. Se puede cambiar el modelo mediante `IA2A_MODEL`. Una clave de API creada para un espacio de trabajo basta. Para una clave sin espacio asignado, define `ANTHROPIC_WORKSPACE_ID` antes de arrancar. La clave se mantiene en el proceso del servicio y las peticiones a Claude incluyen texto, historial reciente, tipo de fuente y mediciones resumidas. El nombre de pista, la grabación y el render permanecen en el Mac. Si es la primera instalación de esta versión del AU, reinicia Logic una vez para cargar el componente nuevo.


Si una consulta falla, **Reintentar** vuelve a enviar la última petición con el mismo fragmento y la misma conversación. La Terminal muestra la categoría del error del servicio. Si quedaba un ajuste pendiente, el frontal lo identifica como **Propuesta anterior** y permite seguir comparándolo.

## Configurar el servicio manualmente

Con Node 22.18 o posterior, `npm --prefix service start` inicia el servicio en el equipo. Para usarlo, el host del plugin debe recibir `IA2A_TURN_URL=http://127.0.0.1:48220/v1/turn`. Se puede configurar el mismo `IA2A_SERVICE_TOKEN` en servicio y host para autenticar al cliente.

Para conectar otro modelo compatible con Chat Completions, definir **en el servicio** `IA2A_MODEL_URL` (URL completa de Chat Completions), `IA2A_MODEL` y, si el proveedor lo requiere, `IA2A_MODEL_KEY`. Usar HTTPS para proveedores externos. Las peticiones contienen conversación y mediciones resumidas. La captura y el audio renderizado permanecen en el plugin.

Ejecutar las pruebas con `npm --prefix service test`.

Con el ejecutable ClientTest compilado, `node service/test/client-http.ts build/ClientTest` prueba el recorrido cliente → servicio → adaptador HTTP en bucle local. Añadir `--claude` para probar el recorrido de la API Messages con una respuesta simulada.

Ejecutar `node service/test/processor-dialogue.ts build/ProcessorTest` para probar una aclaración de fuente y la continuidad del diálogo a través de Aplicar y una nueva captura.
