/* Small offline locale switch for the badge portal. Spanish is the default. */
(function () {
  const esEn = {
    'Santa Muerte // Inicio': 'Santa Muerte // Home',
    'Santa Muerte // Ofrendas': 'Santa Muerte // Offerings',
    'Santa Muerte // Las Ofrendas': 'Santa Muerte // Offerings',
    'Santa Muerte // Herramientas LED': 'Santa Muerte // LED Tools',
    'Santa Muerte // Herramientas NFC': 'Santa Muerte // NFC Tools',
    'Santa Muerte // Ajustes': 'Santa Muerte // Settings',
    'Ajustes': 'Settings',
    'Santa Muerte // Luces': 'Santa Muerte // Lights',
    'Santa Muerte // NFC': 'Santa Muerte // NFC',
    'Control local // ofrendas anónimas // sin internet': 'Local control // anonymous offerings // no internet',
    'ofrendas anónimas // solo local': 'anonymous offerings // local only',
    'Inicio': 'Home', 'Luces': 'Lights', 'Ofrendas': 'Offerings',
    'Herramientas LED': 'LED Tools', 'Herramientas NFC': 'NFC Tools',
    'Ofrendas // Tablero': 'Offerings // Board',
    'Menú': 'Menu', 'Abrir menú': 'Open menu', 'Navegación del badge': 'Badge navigation',
    'Deja una ofrenda': 'Leave an offering',
    'Dibuja, escribe o pega una foto. Como salga.': 'Draw, write, or attach a photo. Keep it real.',
    'Tipo de ofrenda': 'Offering type', 'Dibuja': 'Draw', 'Escribe': 'Write', 'Sube': 'Upload', 'Archivo': 'File',
    'Seleccionar archivo': 'Select file', 'Archivo adjunto': 'Attached file', 'descargar': 'download',
    'Foto': 'Photo', 'Seleccionar foto': 'Select photo', 'Foto para publicar': 'Photo to post',
    'Banda // mensaje': 'Strip // message', 'BANDA // MENSAJE': 'STRIP // MESSAGE',
    'Área para dibujar': 'Drawing area', 'Mensaje en la banda': 'Message on the strip',
    'Escribe aquí…': 'Write here…', 'Escribe y dibuja aquí.': 'Write and draw here.',
    'Sin nombres. Sin firma.': 'No names. No signature.', 'Deshacer': 'Undo', 'Limpiar': 'Clear',
    'Quitarla': 'Remove it', 'Quitar foto': 'Remove photo', 'La foto va aparte, como nota pegada.': 'The photo stays separate, like a taped note.',
    'Dejar ofrenda': 'Leave offering', 'Las Ofrendas': 'Offerings',
    'Las palabras duran más que los dibujos.': 'Words last longer than drawings.',
    'VER TODAS': 'SEE ALL', 'Ver todas': 'See all', 'vacía': 'empty',
    'Viendo el pasillo…': 'Checking the hallway…', 'El badge no contesta.': 'The badge is not answering.',
    'Pasillo vacío. Deja la primera ofrenda.': 'Empty hall. Leave the first offering.',
    'Dibuja, escribe o haz las dos.': 'Draw, write, or do both.', 'Enviando…': 'Sending…',
    'No pasó la ofrenda.': 'The offering did not go through.', 'Ofrenda enviada.': 'Offering sent.',
    'No cargó el tablero.': 'The board did not load.', 'No se pudo llegar al badge.': 'Could not reach the badge.',
    'hora desconocida': 'unknown time', 'ahora': 'now', 'Reduciendo la foto…': 'Shrinking photo…',
    'La foto no quedó lo bastante pequeña.': 'The photo is still too big.',
    'No se pudo leer la foto.': 'Could not read the photo.', 'El archivo supera el límite de': 'The file exceeds the limit of', 'No disponible aquí': 'Not available here',
    'El navegador no pudo leer el tamaño de la foto.': 'The browser could not read the photo size.',
    'Dibujo publicado en el tablero': 'Drawing posted on the board',
    'demasiado detalle — simplifica el dibujo': 'too much detail — simplify the drawing',
    'no se pudo compactar el dibujo': 'could not compact the drawing',
    'El dibujo no se pudo compactar. Intenta de nuevo.': 'The drawing could not be compacted. Try again.',
    'Luz del badge // control local': 'Badge light // local control', 'Revisando…': 'Checking…',
    'Conectado': 'Connected', 'Desconectado': 'Disconnected', '← Inicio': '← Home',
    'Ola morada': 'Purple wave', 'Patrones': 'Patterns', 'Fijo': 'Solid', 'Arcoíris': 'Rainbow',
    'Carrera': 'Chase', 'Pulso': 'Pulse', 'Destello': 'Twinkle', 'Teatro': 'Theater',
    'Apagado': 'Off', 'Animación': 'Animation', 'Brillo': 'Brightness', 'Velocidad': 'Speed',
    'Rueda de color': 'Color wheel',
    'Color hexadecimal': 'Hex color', 'Ajustes guardados': 'Settings saved',
    'El ESP32 no respondió.': 'The ESP32 did not respond.', 'Se perdió la conexión.': 'Connection lost.',
    'Los controles van directo al ESP32 por su Wi-Fi local.': 'Controls go straight to the ESP32 over its local Wi-Fi.',
    'Acceso al badge // Wi-Fi': 'Badge access // Wi-Fi', 'Cargando red…': 'Loading network…',
    'Contraseña Wi-Fi': 'Wi-Fi password', 'Ocultar': 'Hide', 'Ver': 'Show',
    'Se muestra completa para leerla y pasarla. El badge crea tres palabras con tema; acepta 8–63 caracteres ASCII.': 'Shown in full so you can read and share it. The badge makes three themed words; it accepts 8–63 ASCII characters.',
    'Ocultar SSID': 'Hide SSID', 'Las redes ocultas se escriben a mano.': 'Hidden networks must be entered by hand.',
    'Guardar Wi-Fi': 'Save Wi-Fi', 'Contraseña válida.': 'Valid password.',
    'Usa ASCII visible. Letras con acento y emoji no caben en la clave WPA2.': 'Use visible ASCII. Accents and emoji do not fit in a WPA2 password.',
    'La contraseña debe tener 8 a 63 caracteres.': 'The password must be 8 to 63 characters.',
    'Los ajustes de Wi-Fi solo salen al abrir esto desde el badge.': 'Wi-Fi settings only work when this page is opened from the badge.',
    'No cargaron los ajustes.': 'Settings did not load.', 'No cargaron los ajustes de Wi-Fi.': 'Wi-Fi settings did not load.',
    'Guardando…': 'Saving…', 'No se guardaron los ajustes.': 'Settings were not saved.',
    'Reconectar al badge': 'Reconnect to badge',
    'Conectar al Wi-Fi de casa': 'Connect to home Wi-Fi',
    'Red de casa sin configurar.': 'Home network not configured.',
    'Nombre de red (SSID)': 'Network name (SSID)',
    'Contraseña de red': 'Network password',
    'La contraseña se guarda en el badge y no vuelve a mostrarse. Escríbela cada vez que cambies esta conexión.': 'The password is saved on the badge and is not shown again. Enter it whenever you change this connection.',
    'Guardar y conectar': 'Save and connect',
    'Abre el badge en': 'Open the badge at', 'o': 'or',
    'Nombre enviado al router:': 'Name sent to the router:',
    'Conectado a la red de casa.': 'Connected to the home network.',
    'Conectando a la red de casa…': 'Connecting to the home network…',
    'La red de casa no está conectada.': 'The home network is not connected.',
    'Guardado. El badge está conectando a tu red de casa.': 'Saved. The badge is connecting to your home network.',
    'Guardando y conectando…': 'Saving and connecting…',
    'El nombre de red debe tener 1 a 32 bytes.': 'The network name must be 1 to 32 bytes.',
    'El SSID de casa debe tener 1 a 32 bytes sin controles.': 'The home SSID must be 1 to 32 bytes with no control characters.',
    'Faltan el nombre y la contraseña de la red de casa.': 'The home network name and password are required.',
    'No cargó la red de casa.': 'The home network did not load.',
    'No se guardó la red de casa.': 'The home network was not saved.',
    'La red de casa no se pudo guardar.': 'The home network could not be saved.',
    'No se pudo abrir NVS para guardar el Wi-Fi de casa.': 'Could not open NVS to save the home Wi-Fi.',
    'Abre esta página desde el badge para conectar una red de casa.': 'Open this page from the badge to connect a home network.',
    'Punto de acceso del badge': 'Badge access point',
    'Deja encendida la red Santa Muerte para visitas y recuperación.': 'Keep the Santa Muerte network on for visitors and recovery.',
    'Modo Santa Muerte. Apágalo para cambiar a la red de casa guardada.': 'Santa Muerte mode. Turn it off to switch to the saved home network.',
    'Ojo: apagarlo desconecta esta página.': 'Heads up: turning it off disconnects this page.',
    'Vuelve por tu red de casa en SantaMuerte.local, o conecta USB y presiona': 'Return through your home network at SantaMuerte.local, or connect USB and press',
    'para encenderlo otra vez.': 'to turn it back on.',
    'El badge cambiará a la red de casa guardada. Si no llega, conecta USB y presiona': 'The badge will switch to the saved home network. If it cannot connect, use USB and press',
    'para volver al modo Santa Muerte.': 'to return to Santa Muerte mode.',
    'Encendiendo el punto de acceso…': 'Turning on the access point…',
    'Apagando el punto de acceso…': 'Turning off the access point…',
    'Falta el ajuste del punto de acceso.': 'The access-point setting is missing.',
    'No cambió el punto de acceso.': 'The access point did not change.',
    'Guardado. Encendiendo el punto de acceso del badge.': 'Saved. Turning on the badge access point.',
    'Guardado. El punto de acceso se apagará; usa la red de casa o USB para volver a encenderlo.': 'Saved. The access point will turn off; use the home network or USB to turn it back on.',
    'Guardado. Cambiando a tu red de casa.': 'Saved. Switching to your home network.',
    'Guardando y cambiando de red…': 'Saving and switching networks…',
    'Guardado. Cambiando de red…': 'Saved. Switching networks…',
    'Guardado. El punto de acceso sigue apagado hasta que lo enciendas.': 'Saved. The access point stays off until you turn it on.',
    'No se pudo abrir NVS para guardar el ajuste del punto de acceso.': 'Could not open NVS to save the access-point setting.',
    'No se pudo guardar el ajuste del punto de acceso.': 'Could not save the access-point setting.',
    'Cambiar al Wi-Fi de casa': 'Switch to home Wi-Fi',
    'Guardar y cambiar de red': 'Save and switch networks',
    'Comunión del badge // tags y emulación': 'Badge communion // tags and emulation',
    'Lector': 'Reader', 'Lee un tag': 'Read a tag', 'Lector sin datos': 'Reader idle',
    'PN532 fuera': 'PN532 offline', 'PN532 listo': 'PN532 ready', 'Listo para leer': 'Ready to read',
    'Listo': 'Ready', 'Lector fuera': 'Reader offline',
    'Lector NFC': 'NFC reader', 'Lector NFC conectado': 'NFC reader connected',
    'Elige una acción y acerca un tag compatible al lector del PCB.': 'Pick an action and bring a compatible tag to the PCB reader.',
    'Leer tag': 'Read tag', 'Tipo de tag': 'Tag type', 'Capacidad': 'Capacity', 'Resultado': 'Result',
    'Sin registro': 'No record', 'Lee un tag para ver su texto o URL.': 'Read a tag to see its text or URL.',
    'Bytes NDEF': 'NDEF bytes', 'Escritura': 'Writing', 'Crea un registro NDEF': 'Create an NDEF record',
    'Sitio o deep link': 'Site or deep link', 'Los prefijos URI se comprimen solos.': 'URI prefixes compress automatically.',
    'Texto UTF-8': 'UTF-8 text', 'Texto': 'Text', 'Escribe el texto del tag': 'Write the tag text',
    'Se guarda como texto NDEF UTF-8.': 'Saved as UTF-8 NDEF text.', 'Escribir URL': 'Write URL',
    'Ojo:': 'Heads up:', 'Acción actual': 'Current action',
    'El estado sale aquí cuando responda el controlador.': 'The controller status appears here when it responds.',
    'Emulación de tag': 'Tag emulation',
    'Presenta un registro NFC desde el badge. El Wi-Fi se comparte al encender.': 'Present an NFC record from the badge. Wi-Fi is shared at startup.',
    'URL que presenta el tag': 'URL presented by the tag', 'Texto que presenta el tag': 'Text presented by the tag',
    'Sale como texto NDEF UTF-8.': 'It appears as UTF-8 NDEF text.', 'Iniciar emulación': 'Start emulation',
    'Compartir Wi-Fi por NFC': 'Share Wi-Fi over NFC', 'Parar emulación': 'Stop emulation',
    'Un modo NFC a la vez:': 'One NFC mode at a time:', 'Estado': 'Status', 'Parada': 'Stopped',
    'Emulación parada': 'Emulation stopped', 'Lecturas': 'Reads',
    'El badge inicia con su registro Wi-Fi. Puedes cambiarlo por texto o URL.': 'The badge starts with its Wi-Fi record. You can change it to text or a URL.',
    'El badge se verá como tag NFC de solo lectura hasta que pares la emulación.': 'The badge appears as a read-only NFC tag until you stop emulation.',
    'El badge comparte su Wi-Fi al encender. Cambia esto por texto o URL, o usa Compartir Wi-Fi por NFC.': 'The badge shares Wi-Fi at startup. Change this to text or a URL, or use Share Wi-Fi over NFC.',
    'Conéctate al Wi-Fi del ESP32 y abre': 'Connect to the ESP32 Wi-Fi and open',
    'Todo pasa local, en el PCB.': 'Everything stays local, on the PCB.',
    'Elige una acción y acerca un tag compatible al lector del PCB.': 'Pick an action and bring a compatible tag to the PCB reader.',
    'Borrar': 'Erase', 'Tamaño de pincel': 'Brush size', 'Dibujar en': 'Draw in',
    'Rojo': 'Red', 'Naranja': 'Orange', 'Amarillo': 'Yellow', 'Verde': 'Green',
    '⌁ Leer tag': '⌁ Read tag',
    'Tag de configuración NFC del ESP32': 'ESP32 NFC setup tag',
    'escribe en tags NFC Forum Type 2, como NTAG2xx y MIFARE Ultralight. No lo muevas hasta que termine.': 'writes to NFC Forum Type 2 tags, such as NTAG2xx and MIFARE Ultralight. Do not move it until it finishes.',
    'Registro NFC del badge Sneakreaper': 'Sneakreaper badge NFC record',
    'con emulación activa, el PN532 no lee ni escribe tags externos. Desbloquea el teléfono, prende NFC y acerca sus antenas.': 'with emulation running, the PN532 will not read or write external tags. Unlock the phone, turn NFC on, and line up the antennas.',
    '. Todo pasa local, en el PCB.': '. Everything stays local, on the PCB.',
    'Escribe texto para teléfonos cerca': 'Text for nearby phones',
    'Sale como URL NFC Forum Type 4, solo lectura.': 'Appears as a read-only NFC Forum Type 4 URL.',
    'Tag leído.': 'Tag read.',
    'Acerca un tag': 'Present a tag',
    'Leyendo tag…': 'Reading tag…',
    'Emulación activa': 'Emulating',
    'No pasó': 'Failed',
    'Esperando tag': 'Waiting for a tag',
    'Elige una acción NFC.': 'Pick an NFC action.',
    'No hay acción activa.': 'No action running.',
    'El lector NFC está leyendo el badge': 'An NFC reader is reading the badge',
    'El badge será un tag NFC de solo lectura hasta que pares la emulación.': 'The badge acts as a read-only NFC tag until you stop emulation.',
    'El controlador no contesta.': 'The controller is not answering.',
    'La página no llega al NFC del ESP32.': 'This page cannot reach the ESP32 NFC controller.',
    'Vista previa: no se hizo nada en NFC.': 'Preview only: no NFC action was taken.',
    'No pasó la acción.': 'The action did not go through.',
    'Se perdió el ESP32.': 'Lost the ESP32.',
    'Escribir texto': 'Write text',
    'Emular tag URL': 'Emulate URL tag',
    'Emular tag de texto': 'Emulate text tag',
    'Escribe algo antes de guardar.': 'Write something before saving.',
    'Escribe algo para emular.': 'Write something to emulate.',
    'El PN532 no ha iniciado.': 'The PN532 has not started.',
    'La emulación está parada.': 'Emulation is stopped.',
    'Falló la configuración SAM del PN532.': 'PN532 SAM configuration failed.',
    'Falló la configuración de reintentos del PN532.': 'PN532 retry configuration failed.',
    'El lector PN532 no está disponible.': 'The PN532 reader is unavailable.',
    'Para la emulación antes de leer o escribir otro tag.': 'Stop emulation before reading or writing another tag.',
    'Ya hay otra acción NFC esperando un tag.': 'Another NFC action is already waiting for a tag.',
    'La página Type 2 queda fuera del rango NTAG2xx.': 'That Type 2 page is outside the NTAG2xx range.',
    'No se pudo leer la página Type 2': 'Could not read Type 2 page',
    'No se escribe fuera del rango de usuario NTAG2xx.': 'Refusing to write outside the NTAG2xx user range.',
    'No se pudo escribir y verificar la página Type 2': 'Could not write and verify Type 2 page',
    'El largo TLV Type 2 pasa la capacidad del tag.': 'The Type 2 TLV length exceeds the tag capacity.',
    'El mensaje NDEF es muy corto para leerlo.': 'The NDEF message is too short to read.',
    'Se detectó texto UTF-16; aquí solo se muestra UTF-8.': 'UTF-16 text detected; only UTF-8 is shown here.',
    'Tipo de registro NDEF no compatible. Abajo salen los bytes crudos.': 'Unsupported NDEF record type. The raw bytes are shown below.',
    'El tag responde como memoria Type 2, pero no está en formato NDEF.': 'The tag answers as Type 2 memory but is not NDEF formatted.',
    'Se vio memoria Type 2, pero sin contenedor válido.': 'Type 2 memory seen, but no valid container.',
    'El tag no tiene memoria de usuario.': 'The tag has no user memory.',
    'El tag trae un largo TLV mayor que su área de datos.': 'The tag reports a TLV longer than its data area.',
    'Este tag trae un mensaje NDEF vacío.': 'This tag carries an empty NDEF message.',
    'Hay datos NDEF, pero no se pudieron leer.': 'There is NDEF data, but it could not be read.',
    'No se encontró mensaje NDEF en el tag.': 'No NDEF message was found on the tag.',
    'Escribe texto o una URL antes de guardar.': 'Enter text or a URL before saving.',
    'El contenido pasa el límite de 700 bytes.': 'The content exceeds the 700 byte limit.',
    'El registro NDEF pesa mucho para este tag.': 'The NDEF record is too big for this tag.',
    'No se pudo leer la página de capacidad Type 2.': 'Could not read the Type 2 capability page.',
    'Este tag dice que su NDEF es solo lectura.': 'This tag reports its NDEF as read-only.',
    'Este tag no tiene memoria para escribir.': 'This tag has no writable memory.',
    'El SSID está vacío o pasa de 32 bytes.': 'The SSID is empty or longer than 32 bytes.',
    'Los datos de Wi-Fi no caben en el tag emulado.': 'The Wi-Fi data does not fit in the emulated tag.',
    'No se pudo armar el registro WSC de Wi-Fi.': 'Could not build the Wi-Fi WSC record.',
    'El mensaje NDEF de Wi-Fi y texto pesa mucho.': 'The Wi-Fi plus text NDEF message is too big.',
    'El largo del mensaje NDEF de Wi-Fi no cuadra.': 'The Wi-Fi NDEF message length does not add up.',
    'La emulación solo acepta texto o URL.': 'Emulation accepts text or a URL only.',
    'Escribe texto o una URL antes de emular.': 'Enter text or a URL before emulating.',
    'La emulación acepta hasta 220 bytes.': 'Emulation accepts up to 220 bytes.',
    'El registro NDEF pesa mucho para emularlo.': 'The NDEF record is too big to emulate.',
    'Registro NDEF leído del tag emulado.': 'NDEF record read from the emulated tag.',
    'Lector NFC detectado. Sirviendo el registro NDEF…': 'NFC reader detected. Serving the NDEF record…',
    'Listo. Acerca un lector NFC o teléfono a la antena del PN532.': 'Ready. Bring an NFC reader or phone to the PN532 antenna.',
    'El lector NFC se fue antes de leer todo el registro.': 'The NFC reader left before reading the whole record.',
    'Se detectó el tag, pero no se pudo leer su memoria Type 2.': 'The tag was detected, but its Type 2 memory could not be read.',
    'UID del tag leído.': 'Tag UID read.',
    'Falló la escritura antes de guardar todo el registro NDEF.': 'The write failed before the whole NDEF record was saved.',
    'El tag se escribió, pero falló la lectura de prueba.': 'The tag was written, but the verify read failed.',
    'Acerca un tag Type 2 que se pueda escribir para guardar la URL.': 'Present a writable Type 2 tag to save the URL.',
    'Acerca un tag Type 2 que se pueda escribir para guardar el texto.': 'Present a writable Type 2 tag to save the text.',
    'Espera a que termine la acción del tag.': 'Wait for the tag action to finish.',
    'El PN532 no está disponible para pasar Wi-Fi.': 'The PN532 is unavailable for Wi-Fi sharing.',
    'Espera a que termine la acción NFC.': 'Wait for the NFC action to finish.',
    'Wi-Fi listo. Lee el badge para conectarte a su red.': 'Wi-Fi ready. Scan the badge to join its network.',
    'Lector listo. Elige una acción y acerca un tag.': 'Reader ready. Pick an action and present a tag.',
    'El PN532 no volvió al modo lector.': 'The PN532 did not return to reader mode.',
    'No se detectó ningún tag en 15 segundos.': 'No tag was detected within 15 seconds.',
    'Tag detectado. Procesando…': 'Tag detected. Processing…',
    'No arrancó la sincronización NFC.': 'NFC synchronisation did not start.',
    'Falló el inicio del PN532.': 'The PN532 failed to start.',
    'No se encontró el PN532. Revisa corriente, SPI y cables.': 'PN532 not found. Check power, SPI and wiring.',
    'No arrancó la tarea NFC.': 'The NFC task did not start.',
    'Acerca un tag NFC al lector del PCB para leerlo.': 'Hold an NFC tag over the PCB reader to scan it.',
    'La cola de acciones NFC está llena.': 'The NFC action queue is full.',
    'El tipo debe ser texto o URL.': 'The type must be text or url.',
    'No se pudo poner la acción NFC en cola.': 'Could not queue the NFC action.',
    'Acerca un tag Type 2 para guardar la URL.': 'Present a Type 2 tag to save the URL.',
    'Acerca un tag Type 2 para guardar el texto.': 'Present a Type 2 tag to save the text.',
    'No se pudo poner la emulación en cola.': 'Could not queue emulation.',
    'Preparando el registro NDEF…': 'Preparing the NDEF record…',
    'Los datos de Wi-Fi no son válidos.': 'The Wi-Fi data is not valid.',
    'No se pudo poner Wi-Fi en cola.': 'Could not queue Wi-Fi sharing.',
    'Preparando los registros de Wi-Fi…': 'Preparing the Wi-Fi records…',
    'Todavía se está poniendo otra acción NFC en cola.': 'Another NFC action is still being queued.',
    'Parando la emulación…': 'Stopping emulation…',
    'El tablero no está disponible.': 'The board is unavailable.',
    'Ofrenda NFC': 'NFC Offering',
    'Manda cada tag a las ofrendas': 'Send every tag to the offerings',
    'Ofrenda NFC apagada': 'NFC Offering off',
    'Ofrenda NFC encendida': 'NFC Offering on',
    'Tags ofrendados': 'Tags offered',
    'La Ofrenda NFC está apagada.': 'NFC Offering is off.',
    'Ofrenda NFC encendida. Cada tag que se lea se va a las ofrendas.': 'NFC Offering on. Every tag read joins the offerings.',
    'Tag ofrendado. Va a las ofrendas.': 'Tag offered. On its way to the offerings.',
    'Tag ofrendado y publicado.': 'Tag offered and posted.',
    'Se leyó un tag sin texto ni URL. No se subió nada.': 'Read a tag with no text or URL. Nothing was uploaded.',
    'Falta el ajuste de Ofrenda NFC.': 'The NFC Offering setting is missing.',
    'Las once luces del badge sobre la figura': 'The badge\'s eleven lights on the figure',
    'halo // hombro izquierdo': 'halo // left shoulder',
    'halo // izquierda baja': 'halo // lower left',
    'halo // izquierda alta': 'halo // upper left',
    'halo // corona izquierda': 'halo // crown left',
    'halo // corona derecha': 'halo // crown right',
    'halo // derecha alta': 'halo // upper right',
    'halo // derecha baja': 'halo // lower right',
    'halo // hombro derecho': 'halo // right shoulder',
    'manos // arriba': 'hands // top',
    'manos // abajo izquierda': 'hands // lower left',
    'manos // abajo derecha': 'hands // lower right',
    'Animaciones': 'Animations',
    'Despertar': 'Wake',
    'Color propio': 'Custom colour',
    'Multicolor': 'Multicolour',
    'Silencio': 'Silence',
    'Ofrenda': 'Offering',
    'Aureola': 'Aperture',
    'Corona': 'Crown',
    'Encuentro': 'Collide',
    'Escáner': 'Scanner',
    'Manos': 'Hands',
    'Deriva': 'Drift',
    'aro': 'ring',
    'manos': 'hands',
    'todo': 'all',
    'Respuesta inválida.': 'Invalid response.',
    'El ESP32 no aceptó los ajustes.': 'The ESP32 did not accept the settings.',
    'Sin cambios. El badge sigue igual.': 'No changes. The badge is unchanged.',
    'Toca «Ver» para leerla y pasarla. El badge crea tres palabras con tema; acepta 8–63 caracteres ASCII.': 'Tap "Show" to read it and pass it on. The badge makes three themed words; it accepts 8–63 ASCII characters.',
    'Ojo: guarda el SSID tal cual aparece arriba.': 'Heads up: save the SSID exactly as it appears above.',
    'Si tu teléfono olvida esta red y ya no sabes su SSID, no podrás volver a entrar por Wi-Fi. Toca usar serial o borrar y cargar el badge otra vez.': 'If your phone forgets this network and you no longer know its SSID, you will not be able to get back in over Wi-Fi. You would have to use serial, or erase and flash the badge again.',
    'El badge mandó datos de Wi-Fi inválidos.': 'The badge sent invalid Wi-Fi data.',
    'Abre esta página desde el badge en 10.69.4.20 para ver o cambiar el Wi-Fi.': 'Open this page from the badge at 10.69.4.20 to see or change the Wi-Fi.',
    'No se guardaron los ajustes de Wi-Fi.': 'The Wi-Fi settings were not saved.',
    'El badge respondió raro': 'The badge answered oddly',
    'Escaneo constante': 'Constant scanning',
    'El lector se queda buscando. Cada tag que aparece se publica solo: su texto o URL, o su UID si no trae nada. Apaga la emulación.': 'The reader keeps looking. Every tag that turns up posts itself: its text or URL, or its UID when it carries nothing. Turns emulation off.',
    'Tag sin datos. Se ofrendó su UID.': 'Tag had no data. Its UID was offered.',
    'La Ofrenda NFC se apagó para emular.': 'NFC Offering switched off to emulate.',
    'Cian': 'Cyan', 'Azul': 'Blue', 'Morado': 'Purple', 'Rosa': 'Pink', 'Blanco': 'White'
  };
  const enEs = Object.fromEntries(Object.entries(esEn).map(([es, en]) => [en, es]));
  let locale = 'es-MX';
  try { locale = localStorage.getItem('santa-muerte-locale') === 'en-US' ? 'en-US' : 'es-MX'; } catch (_) {}

  const plain = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz';
  const double = Array.from('𝔸𝔹ℂ𝔻𝔼𝔽𝔾ℍ𝕀𝕁𝕂𝕃𝕄ℕ𝕆ℙℚℝ𝕊𝕋𝕌𝕍𝕎𝕏𝕐ℤ𝕒𝕓𝕔𝕕𝕖𝕗𝕘𝕙𝕚𝕛𝕜𝕝𝕞𝕟𝕠𝕡𝕢𝕣𝕤𝕥𝕦𝕧𝕨𝕩𝕪𝕫');
  function undecorate(value) { return Array.from(value).map(c => double.includes(c) ? plain[double.indexOf(c)] : c).join(''); }
  function decorate(value) { return Array.from(value).map(c => plain.includes(c) ? double[plain.indexOf(c)] : c).join(''); }
  function normalize(value) { return undecorate(value).replace(/\s+/g, ' ').trim(); }
  function convert(value, target) {
    if (!value || !value.trim()) return value;
    const table = target === 'en-US' ? esEn : enEs;
    const key = normalize(value);
    let converted = table[key];
    if (!converted) {
      // else-if, not two independent ifs: `match = match || ...` left the
      // minutes match truthy, so the hours branch overwrote it and every
      // "hace N min" rendered as "Nh ago" -- an offering from 10 minutes ago
      // read as 10 hours old.
      let match = key.match(/^hace (\d+) min$/);
      if (match) {
        converted = target === 'en-US' ? `${match[1]} min ago` : `hace ${match[1]} min`;
      } else if ((match = key.match(/^hace (\d+) h$/))) {
        converted = target === 'en-US' ? `${match[1]}h ago` : `hace ${match[1]} h`;
      }
      if (/^Los dibujos ocupan más\./.test(key)) converted = target === 'en-US' ? key.replace('Los dibujos ocupan más. El texto se queda; solo caben los ', 'Drawings take more room. Text stays; only the newest ') .replace(' dibujos más nuevos.', ' drawings fit.') : key;
      if (/^Drawings take more room\./.test(key)) converted = target === 'en-US' ? key : key.replace('Drawings take more room. Text stays; only the newest ', 'Los dibujos ocupan más. El texto se queda; solo caben los ').replace(' drawings fit.', ' dibujos más nuevos.');
    }
    if (!converted || converted === key) return value;
    const start = value.search(/\S/); const end = value.search(/\s*$/);
    return `${value.slice(0, start)}${converted}${value.slice(end)}`;
  }
  function skipped(element) {
    // .type-letter holds exactly one character: the board's typewriter effect
    // splits a string into one span per letter. Walking those as text nodes
    // asks the dictionary to translate single letters, and Spanish "o" is
    // English "or" -- which turned Anonymous into Anornymorus. They need no
    // translating here anyway; portal-locale-change rebuilds them through
    // convert() with the whole string intact.
    return element && element.closest('textarea, input, script, style, .type-letter, .post-text, .payload, .raw, .emulation-preview, [data-locale-control]');
  }
  function translateText(root) {
    const walker = document.createTreeWalker(root || document.body, NodeFilter.SHOW_TEXT);
    const nodes = []; let node;
    while ((node = walker.nextNode())) nodes.push(node);
    nodes.forEach(textNode => {
      if (skipped(textNode.parentElement)) return;
      let converted = convert(undecorate(textNode.nodeValue), locale);
      if (textNode.parentElement.closest('h1, h2, .saint-title, .settings-title')) converted = decorate(converted);
      if (converted !== textNode.nodeValue) textNode.nodeValue = converted;
    });
  }
  function translateAttrs(root) {
    (root || document).querySelectorAll('*').forEach(element => {
      if (element.matches('[data-locale-control]')) return;
      ['aria-label', 'title', 'placeholder', 'alt'].forEach(attribute => {
        if (!element.hasAttribute(attribute)) return;
        const converted = convert(element.getAttribute(attribute), locale);
        if (converted !== element.getAttribute(attribute)) element.setAttribute(attribute, converted);
      });
    });
  }
  function updateControls() {
    document.querySelectorAll('.language-toggle button').forEach(button => {
      button.textContent = locale === 'es-MX' ? '🇲🇽' : '🇺🇸';
      button.setAttribute('aria-label', locale === 'es-MX' ? 'Switch to English' : 'Cambiar a español');
      button.title = locale === 'es-MX' ? 'Switch to English' : 'Cambiar a español';
    });
  }
  function mountControl() {
    if (document.querySelector('.language-toggle')) return;
    const host = document.querySelector('.footer-language') || document.querySelector('.titlebar, .portal-header, .header-actions');
    if (!host) return;
    const control = document.createElement('div');
    control.className = 'language-toggle';
    control.dataset.localeControl = 'true';
    control.setAttribute('aria-label', 'Idioma / Language');
    control.innerHTML = '<button type="button">🇲🇽</button>';
    control.querySelector('button').addEventListener('click', () => setLocale(locale === 'es-MX' ? 'en-US' : 'es-MX'));
    host.appendChild(control);
  }
  function saveLocale() {
    fetch('/api/ui/language', {
      method: 'POST', headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: `locale=${encodeURIComponent(locale)}`, cache: 'no-store'
    }).catch(() => {});
  }
  function loadBadgeLocale() {
    fetch('/api/ui/language', {cache: 'no-store'})
      .then(response => response.ok ? response.json() : null)
      .then(state => { if (state && state.locale) setLocale(state.locale, false); })
      .catch(() => {});
  }
  function setLocale(next, persist = true) {
    locale = next === 'en-US' ? 'en-US' : 'es-MX';
    document.documentElement.lang = locale;
    document.title = convert(document.title, locale);
    translateText(document.body); translateAttrs(document);
    updateControls();
    try { localStorage.setItem('santa-muerte-locale', locale); } catch (_) {}
    if (persist) saveLocale();
    document.dispatchEvent(new CustomEvent('portal-locale-change', {detail:{locale}}));
  }
  window.PortalLocale = { get locale() { return locale; }, setLocale, convert };
  document.addEventListener('DOMContentLoaded', () => {
    mountControl(); setLocale(locale, false); loadBadgeLocale();
    new MutationObserver(records => records.forEach(record => {
      if (record.type === 'characterData' && !skipped(record.target.parentElement)) {
        let converted = convert(undecorate(record.target.nodeValue), locale);
        if (record.target.parentElement.closest('h1, h2, .saint-title, .settings-title')) converted = decorate(converted);
        if (converted !== record.target.nodeValue) record.target.nodeValue = converted;
      }
      if (record.type === 'childList') { translateText(record.target); translateAttrs(record.target); }
    })).observe(document.body, {subtree:true, childList:true, characterData:true});
  });
})();
