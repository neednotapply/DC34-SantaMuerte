/* Small offline locale switch for the badge portal. Spanish is the default. */
(function () {
  const esEn = {
    'Santa Muerte // Inicio': 'Santa Muerte // Home',
    'Santa Muerte // Ofrendas': 'Santa Muerte // Field Notes',
    'Santa Muerte // Las Ofrendas': 'Santa Muerte // Field Notes',
    'Santa Muerte // Herramientas LED': 'Santa Muerte // LED Tools',
    'Santa Muerte // Herramientas NFC': 'Santa Muerte // NFC Tools',
    // Keep the legacy strings so a browser with an older cached document
    // still receives the current name after it loads this locale file.
    'Santa Muerte // Ajustes': 'Santa Muerte // Network',
    'Ajustes': 'Network',
    'Santa Muerte // Red': 'Santa Muerte // Network',
    'Red': 'Network',
    'Santa Muerte // Herramientas USB': 'Santa Muerte // USB Tools',
    'Herramientas USB': 'USB Tools',
    'Santa Muerte // Scripts': 'Santa Muerte // Scripting',
    'Scripts': 'Scripting',
    'SANTA MUERTE // ESP32 // SOLO LOCAL': 'SANTA MUERTE // ESP32 // LOCAL ONLY',
    'Controles': 'Controls', 'Consola de cargas': 'Payload console',
    'Teclea en la computadora conectada por USB. Úsalo solo en la tuya.': 'Types into the computer attached over USB. Only use it on your own.',
    'Script estilo Ducky': 'Ducky-style script',
    'Enviar al equipo': 'Send to device', 'Subir': 'Upload',
    'Guardadas': 'Saved', 'Cargas subidas': 'Uploaded payloads',
    'Control del host': 'Host controls', 'Control remoto USB': 'USB remote',
    'Envía controles estándar al equipo conectado. Serial USB sigue disponible mientras usas estos controles.': 'Sends standard controls to the connected computer. USB Serial stays available while you use these controls.',
    'Anterior': 'Previous', 'Reproducir / Pausar': 'Play / Pause', 'Siguiente': 'Next',
    'Silenciar': 'Mute', 'Vol −': 'Vol −', 'Vol +': 'Vol +',
    'Presentación anterior': 'Previous slide', 'Presentación siguiente': 'Next slide',
    'Controles del sistema': 'System controls', 'Suspender': 'Sleep',
    'Despertar': 'Wake', 'Apagar equipo': 'Power off computer',
    'Botón del badge': 'Badge button', 'Acciones físicas': 'Physical actions',
    'El menú serial permanece disponible. Estas acciones solo cambian lo que hace BOOT cuando el firmware está corriendo.': 'The serial menu remains available. These actions only change what BOOT does while firmware is running.',
    'Pulsación': 'Press', 'Mantener': 'Hold', 'Guardar acciones del botón': 'Save button actions',
    'Controles LED actuales': 'Current LED controls', 'Sin acción': 'No action',
    'Subir volumen': 'Volume up', 'Bajar volumen': 'Volume down',
    'Pista siguiente': 'Next track', 'Pista anterior': 'Previous track',
    'Suspender equipo': 'Sleep computer', 'Despertar equipo': 'Wake computer',
    'Guardando acciones del botón…': 'Saving button actions…',
    'Acciones del botón guardadas.': 'Button actions saved.',
    'No se guardaron las acciones del botón.': 'Button actions were not saved.',
    'No cargaron los controles USB.': 'USB controls did not load.',
    '¿Seguro que quieres apagar el equipo conectado?': 'Are you sure you want to power off the connected computer?',
    'No se pudo enviar el control USB.': 'Could not send the USB control.',
    'Control enviado': 'Control sent',
    'Los controles del host necesitan modo OTG (ARDUINO_USB_MODE=0).': 'Host controls need OTG mode (ARDUINO_USB_MODE=0).',
    'Hay una carga en curso.': 'A payload is already running.',
    'Esta acción solo se puede usar al configurar el botón.': 'This action is only available as a badge-button setting.',
    'El host USB no aceptó ese control.': 'The USB host did not accept that control.',
    'Acción de botón no válida.': 'Invalid button action.',
    'Acción USB no válida.': 'Invalid USB action.',
    'Confirma el apagado del equipo antes de enviarlo.': 'Confirm powering off the computer before sending it.',
    'Apagar el equipo no se puede asignar al botón.': 'Powering off the computer cannot be assigned to the button.',
    'No se pudieron guardar las acciones del botón.': 'Could not save the button actions.',
    'Abrir constructor de scripts': 'Open script builder',
    'WiFi Tethering': 'WiFi Tethering',
    'Modos USB': 'USB Modes', 'Usa un modo USB a la vez': 'Use one USB mode at a time',
    'Modo WiFi Tethering': 'WiFi Tethering mode', 'Unidad Field Notes': 'Field Notes Drive',
    'Comparte el Wi-Fi guardado con el equipo conectado mediante NCM. USB Serial sigue disponible.': 'Shares saved Wi-Fi with the connected computer through NCM. USB Serial remains available.',
    'Enlace': 'Link', 'Tráfico': 'Traffic', 'Disponible': 'Available', 'No disponible': 'Unavailable',
    'Puente activo': 'Bridge active', 'Listo para iniciar': 'Ready to start',
    'Wi-Fi conectado, sin puente': 'Wi-Fi connected, not bridging',
    'El puente sigue al Wi-Fi guardado: empieza solo al conectarse y no hay nada que iniciar. Sin Wi-Fi guardado el badge sale de este modo y vuelve a la Unidad Field Notes.': 'The bridge follows saved Wi-Fi: it starts on its own once connected, and there is nothing to start by hand. With no saved Wi-Fi the badge leaves this mode and returns to the Field Notes Drive.',
    'Wi-Fi guardado desconectado': 'Saved Wi-Fi disconnected',
    'Iniciar WiFi Tethering': 'Start WiFi Tethering', 'Detener WiFi Tethering': 'Stop WiFi Tethering',
    'Al iniciar el puente, el tráfico del Wi-Fi guardado pasa al equipo. Deténlo para usar ese enlace normalmente desde el badge.': 'When the bridge starts, saved Wi-Fi traffic goes to the computer. Stop it before using that link normally from the badge.',
    'WiFi Tethering iniciado.': 'WiFi Tethering started.',
    'WiFi Tethering detenido.': 'WiFi Tethering stopped.',
    'No se pudo cambiar WiFi Tethering.': 'Could not change WiFi Tethering.',
    'Modo USB: revisando…': 'USB mode: checking…',
    'Modo USB: WiFi Tethering': 'USB mode: WiFi Tethering',
    'Modo USB: Unidad Field Notes': 'USB mode: Field Notes Drive',
    'Solo lectura:': 'Read-only:', 'notas': 'notes', 'scripts': 'scripts',
    'Elige Unidad Field Notes para montar la unidad de solo lectura.': 'Choose Field Notes Drive to mount the read-only drive.',
    'Armando la instantánea; la unidad aparece en unos segundos.': 'Building the snapshot; the drive turns up in a few seconds.',
    'Usar modo Red Wi-Fi': 'Use Wi-Fi Network mode',
    'Usar modo Unidad Field Notes': 'Use Field Notes Drive mode',
    'Cambiar el modo USB reiniciará el badge. ¿Continuar?': 'Changing USB mode will reboot the badge. Continue?',
    'Guardando modo USB y reiniciando…': 'Saving USB mode and rebooting…',
    'No se pudo cambiar el modo USB.': 'Could not change USB mode.',
    'Abrir DuckyScript': 'Open DuckyScript', 'Abrir BadUSB': 'Open BadUSB',
    'Identidad USB': 'USB Identity', 'Cómo se presenta el badge': 'How the badge presents itself',
    'Por defecto el equipo conectado ve "': 'By default the connected computer sees "',
    '". Una identidad personalizada reemplaza el fabricante, producto, número de serie y VID/PID que el host ve -- útil en pruebas de acceso físico donde el badge no debe llamar la atención. También puedes reducir el paquete HID a solo teclado. Guardar esto reinicia el badge.':
      '". A custom identity replaces the manufacturer, product, serial number, and VID/PID the host sees -- useful in physical-access engagements where the badge should not draw attention. You can also reduce the HID bundle to keyboard-only. Saving this reboots the badge.',
    'Usar una identidad USB personalizada': 'Use a custom USB identity',
    'Fabricante': 'Manufacturer', 'ej. Generic': 'e.g. Generic',
    'Producto': 'Product', 'ej. USB Keyboard': 'e.g. USB Keyboard',
    'VID (hex)': 'VID (hex)', 'ej. 1209': 'e.g. 1209',
    'PID (hex)': 'PID (hex)', 'ej. dc32': 'e.g. dc32',
    'Número de serie': 'Serial number', 'opcional': 'optional',
    'Paquete HID': 'HID bundle',
    'Completo (teclado + ratón + medios)': 'Full (keyboard + mouse + media)',
    'Solo teclado': 'Keyboard only',
    'Guardar identidad USB': 'Save USB identity',
    'No se pudo leer la identidad USB.': 'Could not read the USB identity.',
    'Guardar la identidad USB reiniciará el badge. ¿Continuar?': 'Saving the USB identity will reboot the badge. Continue?',
    'Guardando identidad USB y reiniciando…': 'Saving USB identity and rebooting…',
    'No se pudo guardar la identidad USB.': 'Could not save the USB identity.',
    'Mantener tecla': 'Hold key', 'ALT, CTRL, SHIFT, GUI o una tecla': 'ALT, CTRL, SHIFT, GUI, or a key',
    'Mantiene presionada una tecla o modificador durante los bloques siguientes, hasta un Soltar. Solo en BadUSB.':
      'Holds a key or modifier down through the following blocks, until a Release. BadUSB only.',
    'Soltar tecla': 'Release key', 'la misma tecla que en Mantener': 'the same key as in Hold',
    'Suelta una tecla o modificador que un Mantener anterior dejó presionado.':
      'Releases a key or modifier an earlier Hold left pressed.',
    'Código Alt': 'Alt code', 'código, ej. 3': 'code, e.g. 3',
    'Teclea un carácter por su código numérico de Windows (Alt+número). Alt+3 escribe ♥. Solo en BadUSB.':
      'Types a character by its Windows numeric code (Alt+number). Alt+3 types ♥. BadUSB only.',
    'Varios códigos Alt': 'Multiple Alt codes',
    'códigos separados por espacio, ej. 3 9829': 'space-separated codes, e.g. 3 9829',
    'Teclea una secuencia de caracteres por sus códigos Alt, uno tras otro.':
      'Types a sequence of characters by their Alt codes, one after another.',
    'Pausa entre teclas (una vez)': 'Pause between keys (once)',
    'Fija la pausa entre cada tecla solo para el siguiente bloque de texto (útil con hosts lentos: KVMs, BIOS). Solo en BadUSB.':
      'Sets the pause between each key for just the next text block (useful with slow hosts: KVMs, BIOS). BadUSB only.',
    'Subidas al badge, como el tablero de ofrendas. Cargar las trae al editor; borrar las quita.': 'Uploaded to the badge, like the offerings board. Load brings one back to the editor; delete removes it.',
    'Cargar': 'Load', 'Candados': 'Locks', 'sin host': 'no host', 'ninguno': 'none',
    'nombre para subir': 'name to upload',
    'sin conexión': 'no connection',
    'Referencia': 'Reference', 'Comandos': 'Commands', 'Cargando…': 'Loading…',
    'Nada subido todavía.': 'Nothing uploaded yet.',
    'Cargado en el editor.': 'Loaded into the editor.',
    'Reproducir script guardado': 'Play saved script', 'Borrar script guardado': 'Erase saved script',
    'Escribe un script primero.': 'Write a script first.',
    'No se pudo enviar.': 'Could not send.', 'Enviado al equipo': 'Sent to device',
    'Ponle un nombre para subir.': 'Name it before uploading.',
    'No se pudo subir.': 'Could not upload.', 'Subida': 'Uploaded',
    'No se pudo borrar': 'Could not delete', 'Borrada': 'Deleted',
    '¿Seguro que quieres borrar este script?': 'Are you sure you want to erase this script?',
    'una nota; no se teclea': 'a note; nothing is typed',
    'escribe texto (LN agrega Enter)': 'type text (LN adds Enter)',
    'pausa: una vez / entre comandos': 'pause: once / between commands',
    'teclas con nombre': 'named keys', 'más teclas con nombre': 'more named keys',
    'combo (GUI/CTRL/ALT/SHIFT + tecla)': 'chord (GUI/CTRL/ALT/SHIFT + key)',
    'repite el comando anterior': 'repeat the previous command',
    'control del ratón': 'mouse control', 'teclas multimedia': 'media keys',
    'espera un cambio de candado del host': 'wait for a host lock-LED toggle',
    'Constructor': 'Builder', 'Arma tu DuckyScript': 'Build your DuckyScript',
    'DuckyScript generado': 'Generated DuckyScript', 'Ver / importar como texto': 'View / import as text',
    'Convertir a bloques': 'Convert to blocks', 'Vaciar': 'Clear',
    'Agregar paso': 'Add step', 'Elige o arrastra un bloque': 'Choose or drag a block',
    'Combinaciones comunes': 'Common shortcuts',
    'Seleccionar todo': 'Select all', 'Copiar': 'Copy', 'Pegar': 'Paste', 'Deshacer': 'Undo',
    'Guardar': 'Save', 'Abrir Ejecutar (Windows)': 'Open Run (Windows)',
    'Arrastra o toca un bloque de la izquierda para empezar.': 'Drag or tap a block on the left to start.',
    'Subidas al badge, como el tablero de ofrendas. Cargar las trae al constructor; borrar las quita.': 'Uploaded to the badge, like the offerings board. Load brings one into the builder; delete removes it.',
    'Cargado en el constructor.': 'Loaded into the builder.', 'Agrega al menos un bloque.': 'Add at least one block.',
    'Bloques reconstruidos desde el texto.': 'Blocks rebuilt from the text.',
    'Mover arriba': 'Move up', 'Mover abajo': 'Move down', 'Quitar': 'Remove',
    'Texto': 'Text', 'Teclas': 'Keys', 'Tiempo': 'Timing', 'Ratón': 'Mouse', 'Multimedia': 'Media', 'Nota': 'Note',
    'Teclear texto': 'Type text', 'Tecla': 'Key', 'Combinación': 'Key combo', 'Pausa': 'Pause',
    'Pausa por defecto': 'Default pause', 'Mover ratón': 'Move mouse', 'Clic ratón': 'Mouse click',
    'Rueda ratón': 'Mouse wheel', 'Tecla multimedia': 'Media key', 'Repetir anterior': 'Repeat previous',
    'Esperar candado': 'Wait for lock', 'Comentario': 'Comment', 'Línea literal': 'Literal line',
    'pulsar Enter al final': 'press Enter at the end',
    'texto a teclear': 'text to type', 'tecla (ej. r)': 'key (e.g. r)', 'nota': 'note', 'veces': 'times',
    'Teclea el texto tal cual en el equipo.': 'Types the text as-is on the computer.',
    'Pulsa y suelta una sola tecla.': 'Presses and releases a single key.',
    'Mantiene los modificadores marcados y pulsa la tecla. Ej: GUI+r abre Ejecutar en Windows.': 'Holds the checked modifiers and presses the key. e.g. GUI+r opens Run on Windows.',
    'Pausa los milisegundos indicados antes de seguir.': 'Pauses the given milliseconds before continuing.',
    'Fija la pausa automática entre cada comando siguiente.': 'Sets the automatic pause between each following command.',
    'Mueve el puntero (píxeles relativos, -128 a 127).': 'Moves the pointer (relative pixels, -128 to 127).',
    'Hace un clic con el botón elegido.': 'Clicks with the chosen button.',
    'Gira la rueda (positivo arriba, negativo abajo).': 'Scrolls the wheel (positive up, negative down).',
    'Envía una tecla de medios (volumen, reproducción…).': 'Sends a media key (volume, playback…).',
    'Repite el bloque inmediatamente anterior n veces más.': 'Repeats the immediately previous block n more times.',
    'Pausa hasta que el host encienda o apague ese candado del teclado.': 'Pauses until the host toggles that keyboard lock.',
    'Un comentario para ti; no se teclea nada.': 'A note for you; nothing is typed.',
    'Una línea DuckyScript escrita a mano (para comandos no listados).': 'A hand-written DuckyScript line (for commands not listed).',
    'Santa Muerte // Luces': 'Santa Muerte // Lights',
    'Santa Muerte // NFC': 'Santa Muerte // NFC',
    'Control local // ofrendas anónimas // sin internet': 'Local control // anonymous offerings // no internet',
    'ofrendas anónimas // solo local': 'anonymous offerings // local only',
    'Inicio': 'Home', 'Luces': 'Lights', 'Ofrendas': 'Field Notes',
    'Herramientas LED': 'LED Tools', 'Herramientas NFC': 'NFC Tools',
    'Leer': 'Read', 'Escribir': 'Write', 'Emular': 'Emulate',
    'Ofrendas // Tablero': 'Field Notes',
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
    'Dejar ofrenda': 'Leave offering', 'Las Ofrendas': 'Field Notes',
    'Las palabras duran más que los dibujos.': 'Words last longer than drawings.',
    'VER TODAS': 'SEE ALL', 'Ver todas': 'See all', 'vacía': 'empty',
    'Viendo el pasillo…': 'Checking the hallway…', 'El badge no contesta.': 'The badge is not answering.',
    'El muro está vacío...': 'The wall is empty...',
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
    'Rituales lentos': 'Slow rituals', 'Vela': 'Candle', 'Respira': 'Breathe',
    'Brasas': 'Embers', 'Marea': 'Tide', 'Vigilia': 'Vigil', 'Cometa': 'Comet',
    'Rosario': 'Rosary', 'Velo': 'Veil', 'Prisma': 'Prism', 'Ocaso': 'Sunset',
    'Océano': 'Ocean', 'Nebulosa': 'Nebula', 'Órbita': 'Orbit',
    'Florecer': 'Bloom', 'Espejismo': 'Mirage', 'Cosmos': 'Cosmos',
    'Apagado': 'Off', 'Animación': 'Animation', 'Brillo': 'Brightness', 'Velocidad': 'Speed',
    'Rueda de color': 'Color wheel',
    'Color hexadecimal': 'Hex color', 'Ajustes guardados': 'Settings saved',
    'El ESP32 no respondió.': 'The ESP32 did not respond.', 'Se perdió la conexión.': 'Connection lost.',
    'Los controles van directo al ESP32 por su Wi-Fi local.': 'Controls go straight to the ESP32 over its local Wi-Fi.',
    'Red del badge': 'Badge network',
    'Redes recordadas:': 'Remembered networks:',
    'Acceso al badge // Wi-Fi': 'Badge access // Wi-Fi', 'Cargando red…': 'Loading network…',
    'Nombre del punto de acceso (SSID)': 'Access-point name (SSID)',
    'De 1 a 32 bytes. Guardarlo reinicia el punto de acceso con este nombre.': '1 to 32 bytes. Saving restarts the access point with this name.',
    'Contraseña Wi-Fi': 'Wi-Fi password', 'Ocultar': 'Hide', 'Ver': 'Show',
    'Déjala vacía para una red abierta sin contraseña. Si escribes una, debe tener 8–63 caracteres ASCII.': 'Leave it empty for an open network without a password. If you enter one, it must contain 8–63 ASCII characters.',
    'Se muestra completa para leerla y pasarla. El badge crea tres palabras con tema; acepta 8–63 caracteres ASCII.': 'Shown in full so you can read and share it. The badge makes three themed words; it accepts 8–63 ASCII characters.',
    'Ocultar SSID': 'Hide SSID', 'Las redes ocultas se escriben a mano.': 'Hidden networks must be entered by hand.',
    'Guardar Wi-Fi': 'Save Wi-Fi', 'Contraseña válida.': 'Valid password.',
    'Usa ASCII visible. Letras con acento y emoji no caben en la clave WPA2.': 'Use visible ASCII. Accents and emoji do not fit in a WPA2 password.',
    'La contraseña debe tener 8 a 63 caracteres.': 'The password must be 8 to 63 characters.',
    'La configuración de red solo funciona al abrir esto desde el badge.': 'Network configuration only works when this page is opened from the badge.',
    'Abre esta página desde el badge en 10.69.4.20 para ver o cambiar la red.': 'Open this page from the badge at 10.69.4.20 to view or change the network.',
    'No cargó la configuración de red.': 'Network configuration did not load.',
    'No cargó la configuración Wi-Fi.': 'Wi-Fi configuration did not load.',
    'Guardando…': 'Saving…', 'No se guardó la configuración de red.': 'Network configuration was not saved.',
    'Reconectar al badge': 'Reconnect to badge',
    'Conectar al Wi-Fi guardado': 'Connect to saved Wi-Fi',
    'Wi-Fi guardado sin configurar.': 'Saved Wi-Fi not configured.',
    'Nombre de red (SSID)': 'Network name (SSID)',
    'Contraseña de red (opcional)': 'Network password (optional)',
    'Déjala vacía para una red abierta. Si usas una contraseña, se guarda en el badge y debe tener 8 a 63 caracteres ASCII visibles.': 'Leave it blank for an open network. If you use a password, it is saved on the badge and must be 8 to 63 visible ASCII characters.',
    'Guardar y conectar': 'Save and connect',
    'Abre el badge en': 'Open the badge at', 'o': 'or',
    'Nombre enviado al router:': 'Name sent to the router:',
    'Conectado al Wi-Fi guardado.': 'Connected to saved Wi-Fi.',
    'Conectando al Wi-Fi guardado…': 'Connecting to saved Wi-Fi…',
    'El Wi-Fi guardado no está conectado.': 'Saved Wi-Fi is not connected.',
    'Guardado. El badge está conectando a tu Wi-Fi guardado.': 'Saved. The badge is connecting to your saved Wi-Fi.',
    'Guardando y conectando…': 'Saving and connecting…',
    'Probando la red…': 'Testing the network…',
    'Probando la red… se guarda solo si conecta.': 'Testing the network… it is only saved if it connects.',
    'No se pudo conectar. Esa red no se guardó.': 'Could not connect. That network was not saved.',
    'Probando:': 'Testing:', 'No se guardó:': 'Not saved:',
    'El nombre de red debe tener 1 a 32 bytes.': 'The network name must be 1 to 32 bytes.',
    'El SSID guardado debe tener 1 a 32 bytes sin controles.': 'The saved Wi-Fi SSID must be 1 to 32 bytes with no control characters.',
    'Falta el nombre del Wi-Fi guardado.': 'The saved Wi-Fi name is required.',
    'No cargó el Wi-Fi guardado.': 'Saved Wi-Fi did not load.',
    'No se guardó el Wi-Fi guardado.': 'Saved Wi-Fi was not saved.',
    'El Wi-Fi guardado no se pudo guardar.': 'Saved Wi-Fi could not be saved.',
    'No se pudo abrir NVS para guardar el Wi-Fi guardado.': 'Could not open NVS to save saved Wi-Fi.',
    'Abre esta página desde el badge para conectar un Wi-Fi guardado.': 'Open this page from the badge to connect saved Wi-Fi.',
    'Punto de acceso del badge': 'Badge access point',
    'Deja encendida la red Santa Muerte para visitas y recuperación.': 'Keep the Santa Muerte network on for visitors and recovery.',
    'Modo Santa Muerte. Apágalo para cambiar al Wi-Fi guardado.': 'Santa Muerte mode. Turn it off to switch to saved Wi-Fi.',
    'Ojo: apagarlo desconecta esta página.': 'Heads up: turning it off disconnects this page.',
    'Vuelve por tu Wi-Fi guardado en SantaMuerte.local, o conecta USB y presiona': 'Return through saved Wi-Fi at SantaMuerte.local, or connect USB and press',
    'para encenderlo otra vez.': 'to turn it back on.',
    'El badge cambiará al Wi-Fi guardado. Si no llega, conecta USB y presiona': 'The badge will switch to saved Wi-Fi. If it cannot connect, use USB and press',
    'para volver al modo Santa Muerte.': 'to return to Santa Muerte mode.',
    'Encendiendo el punto de acceso…': 'Turning on the access point…',
    'Apagando el punto de acceso…': 'Turning off the access point…',
    'Falta el ajuste del punto de acceso.': 'The access-point setting is missing.',
    'No cambió el punto de acceso.': 'The access point did not change.',
    'Guardado. Encendiendo el punto de acceso del badge.': 'Saved. Turning on the badge access point.',
    'Guardado. El punto de acceso se apagará; usa el Wi-Fi guardado o USB para volver a encenderlo.': 'Saved. The access point will turn off; use saved Wi-Fi or USB to turn it back on.',
    'Guardado. Cambiando a tu Wi-Fi guardado.': 'Saved. Switching to your saved Wi-Fi.',
    'Guardando y cambiando de red…': 'Saving and switching networks…',
    'Guardado. Cambiando de red…': 'Saved. Switching networks…',
    'Guardado. El punto de acceso sigue apagado hasta que lo enciendas.': 'Saved. The access point stays off until you turn it on.',
    'No se pudo abrir NVS para guardar el ajuste del punto de acceso.': 'Could not open NVS to save the access-point setting.',
    'No se pudo guardar el ajuste del punto de acceso.': 'Could not save the access-point setting.',
    'Cambiar al Wi-Fi guardado': 'Switch to saved Wi-Fi',
    'Guardar y cambiar de red': 'Save and switch networks',
    'Comunión del badge // tags y emulación': 'Badge communion // tags and emulation',
    'Lector': 'Reader', 'Lee un tag': 'Read a tag', 'Lector sin datos': 'Reader idle',
    'PN532 fuera': 'PN532 offline', 'PN532 listo': 'PN532 ready', 'PN532 ocupado': 'PN532 Busy', 'Listo para leer': 'Ready to read',
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
    'Usa un modo de radio a la vez': 'Use one radio mode at a time',
    'Modos NFC': 'NFC Modes',
    'Emular tag guardado': 'Emulate saved tag',
    'Compartir Wi-Fi por NFC': 'Share Wi-Fi over NFC',
    'Parar modo activo': 'Stop active mode',
    'Wi-Fi por NFC': 'Wi-Fi over NFC', 'Emular tag': 'Emulate tag', 'Parar': 'Stop',
    'Modo NFC parado': 'NFC mode stopped', 'Wi-Fi por NFC activo': 'Wi-Fi over NFC active',
    'Emulación de tag activa': 'Tag emulation active', 'PN532 fuera': 'PN532 offline',
    'Tag recordado': 'Saved tag',
    'Edita el registro que emula el badge': 'Edit the record the badge emulates',
    'Guarda este registro al tocar Emular tag guardado arriba. Con emulación activa, el PN532 no lee ni escribe tags externos.': 'Save this record by selecting Emulate saved tag above. While emulation is active, the PN532 cannot read or write external tags.',
    'Guarda este registro al tocar Emular tag arriba. Con emulación activa, el PN532 no lee ni escribe tags externos.': 'Save this record by selecting Emulate tag above. While emulation is active, the PN532 cannot read or write external tags.',
    // The capture mode was called NFC Notes back when a read became an offering
    // on the Field Notes wall. Reads land in the NFC Log now and nothing about
    // it writes a note, so it is auto-scan. The old strings stay: they are what
    // an older cached page, or a badge on older firmware, still says.
    'Notas NFC': 'Auto-scan',
    'Notas NFC apagadas': 'Auto-scan off',
    'Notas NFC encendidas': 'Auto-scan on',
    'Notas NFC apagadas.': 'Auto-scan is off.',
    'Notas NFC encendidas. Cada tag que se lea va a Field Notes.': 'Auto-scan on. Every tag read goes to Field Notes.',
    'Notas NFC encendidas. Cada tag que se lea va al registro NFC.': 'Auto-scan on. Every tag read goes to the NFC log.',
    'Tags en notas': 'Tags seen',
    'Las notas NFC se apagaron para emular.': 'Auto-scan switched off for emulation.',
    'Tag guardado en Field Notes.': 'Tag saved in Field Notes.',
    'Tag sin datos. Su UID se guardó en Field Notes.': 'Tag had no data. Its UID was saved in Field Notes.',
    'Falta el ajuste de Notas NFC.': 'The auto-scan setting is missing.',
    'Escaneo automático': 'Auto-scan',
    'Escaneo automático apagado': 'Auto-scan off',
    'Escaneo automático encendido': 'Auto-scan on',
    'Escaneo automático apagado.': 'Auto-scan is off.',
    'Escaneo automático encendido. Cada tag que se lea va al registro NFC.': 'Auto-scan on. Every tag read goes to the NFC log.',
    'El escaneo automático se apagó para emular.': 'Auto-scan switched off for emulation.',
    'Falta el ajuste del escaneo automático.': 'The auto-scan setting is missing.',
    'PN532 emulando': 'PN532 emulating',
    'Esperando lector NFC': 'Waiting for NFC reader',
    'Controlador fuera.': 'Controller offline.',
    'La página no llega al NFC del ESP32.': 'This page cannot reach the ESP32 NFC controller.',
    'Se leyó un tag sin texto ni URL. No se subió nada.': 'Read a tag with no text or URL. Nothing was uploaded.',
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
    // Terminal page. The hint is deliberately split into its own fragments:
    // translation walks one text node at a time, and the <b> around Enter and
    // Color breaks the sentence into separate nodes, so a single key spanning
    // the bold words could never match one. Each fragment is its own entry.
    // Wi-Fi scan-and-pick.
    'Buscar redes': 'Scan networks',
    // NFC log page. The board was renamed to a log; the old "tablero" strings
    // stay so a browser holding an older cached document still shows the
    // current name, and so the reverse table -- which keeps the last key for a
    // repeated English value -- answers "NFC Log" with "Registro NFC".
    'Tablero NFC': 'NFC Log',
    'Santa Muerte // Tablero NFC': 'Santa Muerte // NFC Log',
    'Borrar tablero': 'Clear log',
    '¿Borrar todos los tags del tablero NFC?': 'Clear every tag from the NFC log?',
    'Registro NFC': 'NFC Log',
    'Santa Muerte // Registro NFC': 'Santa Muerte // NFC Log',
    'Encuentros NFC': 'NFC Encounters',
    'Cada tag que el lector conoció': 'Every tag the reader met',
    'Actualizar': 'Refresh',
    'Borrar registro': 'Clear log',
    'Solo UID — sin contenido legible': 'UID only — no readable content',
    'Los tags leídos ya no van a Field Notes; llegan aquí, con su UID, su tipo y lo que se pudo leer. Volver a ver el mismo tag suma a su cuenta en vez de repetirlo.': 'Tags the reader meets no longer go to Field Notes; they land here, with their UID, their type and whatever could be read. Meeting the same tag again adds to its count instead of repeating it.',
    '¿Borrar todos los tags del registro NFC?': 'Clear every tag from the NFC log?',
    // The empty state wraps "Escaneo automático" in <b>, which splits the
    // sentence into separate text nodes; translation walks one at a time, so
    // each half around the bold words has to be its own entry.
    'Todavía no hay tags. Enciende': 'No tags yet. Turn on',
    'en Herramientas NFC y acerca una tarjeta: su identidad y su contenido aparecerán aquí.': 'in NFC Tools and present a card: its identity and its content will show up here.',
    // Reader status lines the firmware sends, which name where a tag landed.
    'Tag guardado en el registro NFC.': 'Tag saved to the NFC log.',
    'Tag sin datos; su UID quedó en el registro NFC.': 'Tag had no data; its UID stayed in the NFC log.',
    'Registro NFC borrado.': 'NFC log cleared.',
    'No se pudo limpiar el registro NFC.': 'Could not clear the NFC log.',
    'Consola USB': 'USB Console',
    'La misma sesión que ve el cable': 'The same session the cable sees',
    'Conectando': 'Connecting', 'En vivo': 'Live', 'Sin conexión': 'Offline',
    'Escribe y pulsa Enter': 'Type, then press Enter',
    'Alterna el color ANSI (tecla p)': 'Toggles ANSI colour (key p)',
    'Haz clic en la pantalla y escribe igual que en el cable, o usa la caja de arriba desde el teléfono.': 'Click the screen and type just as you would over the cable, or use the box above from a phone.',
    'abre una sesión cerrada, y': 'reopens a closed session, and',
    'enciende el color ANSI en las dos vistas a la vez.': 'turns on ANSI colour in both views at once.',
    'Una sola sesión: el cable y esta página comparten pantalla y lo que está escrito a medias. Las claves guardadas, los payloads de teclado y borrar las notas siguen siendo solo por cable, porque el portal no pide contraseña.': 'One session: the cable and this page share a screen and whatever is half-typed. Saved passwords, keyboard payloads and clearing the notes stay cable-only, because the portal asks for no password.',
    'Cian': 'Cyan', 'Azul': 'Blue', 'Morado': 'Purple', 'Rosa': 'Pink', 'Blanco': 'White'
  };
  // The reverse table cannot be a blind inversion. "Red" is Spanish for
  // Network and also the English colour, so inverting 'Rojo': 'Red' taught the
  // en->es direction to rewrite the Network page's own title as "Rojo" -- on
  // the Spanish page, which never asked to be translated at all. An English
  // value that is itself a Spanish key meaning something else is ambiguous in
  // reverse, so it gets no entry; a word spelled the same in both languages,
  // like "Cosmos", is not ambiguous and keeps its own.
  const owns = (table, key) => Object.prototype.hasOwnProperty.call(table, key);
  const enEs = Object.fromEntries(
    Object.entries(esEn)
      .filter(([, en]) => !owns(esEn, en) || esEn[en] === en)
      .map(([es, en]) => [en, es]));
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
      if (/^Conectando al Wi-Fi guardado… /.test(key)) {
        converted = target === 'en-US'
          ? key.replace('Conectando al Wi-Fi guardado… ', 'Connecting to saved Wi-Fi… ')
          : key;
      }
      if (/^Connecting to saved Wi-Fi… /.test(key)) {
        converted = target === 'en-US'
          ? key
          : key.replace('Connecting to saved Wi-Fi… ', 'Conectando al Wi-Fi guardado… ');
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
    document.querySelectorAll('.language-toggle').forEach(control => {
      const spanish = locale === 'es-MX';
      const switchTo = spanish ? 'en-US' : 'es-MX';
      const label = spanish ? 'Switch to English' : 'Cambiar a español';
      // Keep the country order fixed; only the other language is clickable.
      control.innerHTML = spanish
        ? '<button class="flag" type="button" aria-label="Switch to English" title="Switch to English">🇺🇸</button><span class="divider" aria-hidden="true">//</span><span class="flag active" aria-current="true">🇲🇽</span>'
        : '<span class="flag active" aria-current="true">🇺🇸</span><span class="divider" aria-hidden="true">//</span><button class="flag" type="button" aria-label="Cambiar a español" title="Cambiar a español">🇲🇽</button>';
      const button = control.querySelector('button');
      button.setAttribute('aria-label', label);
      button.title = label;
      button.addEventListener('click', () => setLocale(switchTo));
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
    host.appendChild(control);
    updateControls();
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
  function loadFooterAddress() {
    const label = document.querySelector('.footer-identity');
    if (!label) return;
    fetch('/api/wifi/settings', {cache: 'no-store'})
      .then(response => response.ok ? response.json() : null)
      .then(state => {
        if (state && state.ip) {
          label.textContent = `SANTAMUERTE.LOCAL // ESP32 // ${state.ip}`;
        }
      })
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
    mountControl(); setLocale(locale, false); loadBadgeLocale(); loadFooterAddress();
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
