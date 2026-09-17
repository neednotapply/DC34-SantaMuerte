/* Small offline locale switch for the badge portal. Spanish is the default. */
(function () {
  // English is the source language: these are the strings the pages and the
  // firmware actually contain, paired with the Spanish they become. A missing
  // pair therefore leaves English showing on the Spanish page, which is the
  // harmless direction -- it can never leave Spanish on the English page.
  const enEs = {
    'Santa Muerte // Home': 'Santa Muerte // Inicio',
    'Santa Muerte // Field Notes': 'Santa Muerte // Notas de Campo',
    'Santa Muerte // LED Tools': 'Santa Muerte // Herramientas LED',
    'Santa Muerte // NFC Tools': 'Santa Muerte // Herramientas NFC',
    'Santa Muerte // Network': 'Santa Muerte // Red',
    'Network': 'Red',
    'Santa Muerte // USB Tools': 'Santa Muerte // Herramientas USB',
    'USB Tools': 'Herramientas USB',
    'Santa Muerte // Scripting': 'Santa Muerte // Scripts',
    'Scripting': 'Scripts',
    'SANTA MUERTE // ESP32 // LOCAL ONLY': 'SANTA MUERTE // ESP32 // SOLO LOCAL',
    'Controls': 'Controles',
    'Payload console': 'Consola de cargas',
    'Types into the computer attached over USB. Only use it on your own.': 'Teclea en la computadora conectada por USB. Úsalo solo en la tuya.',
    'Ducky-style script': 'Script estilo Ducky',
    'Send to the computer': 'Enviar a la computadora',
    'Upload': 'Subir',
    'Saved': 'Guardadas',
    'Uploaded scripts': 'Scripts subidos',
    'Host controls': 'Control del host',
    'USB remote': 'Control remoto USB',
    'Sends the usual controls to the connected computer. The USB console stays available while you use them.': 'Manda los controles de siempre a la computadora conectada. La consola USB sigue disponible mientras los usas.',
    'Previous': 'Anterior',
    'Play / Pause': 'Reproducir / Pausar',
    'Next': 'Siguiente',
    'Mute': 'Silenciar',
    'Vol −': 'Vol −',
    'Vol +': 'Vol +',
    'Previous slide': 'Presentación anterior',
    'Next slide': 'Presentación siguiente',
    'System controls': 'Controles del sistema',
    'Sleep': 'Suspender',
    'Wake': 'Despertar',
    'Power off the computer': 'Apagar la computadora',
    'Badge button': 'Botón del badge',
    'Physical actions': 'Acciones físicas',
    'The over-the-cable menu stays available. This only changes what the BOOT button does while the badge is running.': 'El menú por cable sigue disponible. Esto solo cambia lo que hace el botón BOOT mientras el badge está encendido.',
    'Press': 'Pulsación',
    'Hold': 'Mantener',
    'Save button actions': 'Guardar acciones del botón',
    'Current LED controls': 'Controles LED actuales',
    'No action': 'Sin acción',
    'Volume up': 'Subir volumen',
    'Volume down': 'Bajar volumen',
    'Next track': 'Pista siguiente',
    'Previous track': 'Pista anterior',
    'Sleep computer': 'Suspender equipo',
    'Wake computer': 'Despertar equipo',
    'Saving button actions…': 'Guardando acciones del botón…',
    'Button actions saved.': 'Acciones del botón guardadas.',
    'Button actions were not saved.': 'No se guardaron las acciones del botón.',
    'USB controls did not load.': 'No cargaron los controles USB.',
    'Are you sure you want to power off the connected computer?': '¿Seguro que quieres apagar la computadora conectada?',
    'Could not send the USB control.': 'No se pudo enviar el control USB.',
    'Control sent': 'Control enviado',
    'Host controls need OTG mode (ARDUINO_USB_MODE=0).': 'Los controles del host necesitan modo OTG (ARDUINO_USB_MODE=0).',
    'A payload is already running.': 'Hay una carga en curso.',
    'This action is only available as a badge-button setting.': 'Esta acción solo se puede usar al configurar el botón.',
    'The USB host did not accept that control.': 'El host USB no aceptó ese control.',
    'Invalid button action.': 'Acción de botón no válida.',
    'Invalid USB action.': 'Acción USB no válida.',
    'Confirm powering off the computer before sending it.': 'Confirma el apagado del equipo antes de enviarlo.',
    'Powering off the computer cannot be assigned to the button.': 'Apagar el equipo no se puede asignar al botón.',
    'Could not save the button actions.': 'No se pudieron guardar las acciones del botón.',
    'Open script builder': 'Abrir constructor de scripts',
    'WiFi Tethering': 'WiFi Tethering',
    'USB Modes': 'Modos USB',
    'Use one USB mode at a time': 'Usa un modo USB a la vez',
    'WiFi Tethering mode': 'Modo WiFi Tethering',
    'Field Notes Drive': 'Unidad Notas de Campo',
    'Shares the Wi-Fi the badge is on with the computer at the other end of the cable. The USB console stays available.': 'Comparte el Wi-Fi del badge con la computadora conectada por el cable. La consola USB sigue disponible.',
    'Status': 'Estado',
    'Data': 'Datos',
    'Available': 'Disponible',
    'Unavailable': 'No disponible',
    'Bridge active': 'Puente activo',
    'Ready to start': 'Listo para iniciar',
    'Wi-Fi connected, not shared yet': 'Wi-Fi conectado, todavía sin compartir',
    'It starts on its own once the badge joins your Wi-Fi; there is nothing to start. With no saved network the badge drops back to Field Notes Drive mode.': 'Empieza solo cuando el badge se conecta a tu Wi-Fi; no hay nada que iniciar. Si no hay ninguna red guardada, el badge vuelve al modo Unidad Notas de Campo.',
    'Saved Wi-Fi disconnected': 'Wi-Fi guardado desconectado',
    'Start WiFi Tethering': 'Iniciar WiFi Tethering',
    'Stop WiFi Tethering': 'Detener WiFi Tethering',
    'When the bridge starts, saved Wi-Fi traffic goes to the computer. Stop it before using that link normally from the badge.': 'Al iniciar el puente, el tráfico del Wi-Fi guardado pasa al equipo. Deténlo para usar ese enlace normalmente desde el badge.',
    'WiFi Tethering started.': 'WiFi Tethering iniciado.',
    'WiFi Tethering stopped.': 'WiFi Tethering detenido.',
    'Could not change WiFi Tethering.': 'No se pudo cambiar WiFi Tethering.',
    'USB mode: loading…': 'Modo USB: cargando…',
    'USB mode: WiFi Tethering': 'Modo USB: WiFi Tethering',
    'USB mode: Field Notes Drive': 'Modo USB: Unidad Notas de Campo',
    'Read-only:': 'Solo lectura:',
    'notes': 'notas',
    'scripts': 'scripts',
    'Pick Field Notes Drive to have the badge show up as a read-only USB stick.': 'Elige Unidad Notas de Campo para que el badge aparezca como una memoria USB de solo lectura.',
    'Getting the drive ready; it shows up in a few seconds.': 'Preparando la unidad; aparece en unos segundos.',
    'Use Wi-Fi Network mode': 'Usar modo Red Wi-Fi',
    'Use Field Notes Drive mode': 'Usar modo Unidad Notas de Campo',
    'Changing the USB mode restarts the badge. Continue?': 'Cambiar el modo USB reinicia el badge. ¿Continuar?',
    'Saving USB mode and rebooting…': 'Guardando modo USB y reiniciando…',
    'Could not change USB mode.': 'No se pudo cambiar el modo USB.',
    'Open DuckyScript': 'Abrir DuckyScript',
    'Open BadUSB': 'Abrir BadUSB',
    'USB Identity': 'Identidad USB',
    'How the badge presents itself': 'Cómo se presenta el badge',
    'By default the connected computer sees "': 'Por defecto el equipo conectado ve "',
    '". Here you can change that name and the numbers that identify it, so it passes for some other ordinary device. You can also have it introduce itself as a keyboard only. Saving restarts the badge.': '". Aquí puedes cambiar ese nombre y los números que lo identifican, para que pase por otro aparato cualquiera. También puedes hacer que se presente solo como teclado. Al guardar, el badge se reinicia.',
    'Change how the badge introduces itself': 'Cambiar cómo se presenta el badge',
    'Manufacturer': 'Fabricante',
    'e.g. Generic': 'ej. Generic',
    'Product': 'Producto',
    'e.g. USB Keyboard': 'ej. USB Keyboard',
    'Maker ID': 'ID del fabricante',
    'e.g. 1209': 'ej. 1209',
    'Product ID': 'ID del producto',
    'e.g. dc32': 'ej. dc32',
    'Serial number': 'Número de serie',
    'optional': 'opcional',
    'What it pretends to be': 'De qué se hace pasar',
    'Keyboard, mouse and volume controls': 'Teclado, ratón y controles de volumen',
    'Keyboard only': 'Solo teclado',
    'Save USB identity': 'Guardar identidad USB',
    'Could not read the USB identity.': 'No se pudo leer la identidad USB.',
    'Saving this restarts the badge. Continue?': 'Guardar esto reinicia el badge. ¿Continuar?',
    'Saving USB identity and rebooting…': 'Guardando identidad USB y reiniciando…',
    'Could not save the USB identity.': 'No se pudo guardar la identidad USB.',
    'Hold key': 'Mantener tecla',
    'ALT, CTRL, SHIFT, GUI, or a key': 'ALT, CTRL, SHIFT, GUI o una tecla',
    'Holds a key or modifier down through the following blocks, until a Release. BadUSB only.': 'Mantiene presionada una tecla o modificador durante los bloques siguientes, hasta un Soltar. Solo en BadUSB.',
    'Shortcuts': 'Atajos',
    'Keyboard Shortcuts': 'Atajos de teclado',
    'Sends a key combination to the connected computer with one tap. Pick the category that matches the system or the app you are using.': 'Manda una combinación de teclas a la computadora conectada con un solo toque. Elige la categoría según el sistema o la app que estés usando.',
    'Shortcut category': 'Categoría de atajos',
    'Available shortcuts': 'Atajos disponibles',
    'Could not send the shortcut.': 'No se pudo enviar el atajo.',
    'Shortcut sent': 'Atajo enviado',
    'Basic Keys': 'Teclas básicas',
    'Navigation': 'Navegación',
    'Editing': 'Edición',
    'Browser': 'Navegador',
    'Media / YouTube': 'Medios / YouTube',
    'Presentation': 'Presentación',
    'Function Keys': 'Teclas de función',
    'Backspace': 'Retroceso',
    'Delete': 'Suprimir',
    'Space': 'Espacio',
    'Menu key': 'Tecla de menú',
    'Up': 'Arriba',
    'Down': 'Abajo',
    'Left': 'Izquierda',
    'Right': 'Derecha',
    'End': 'Fin',
    'Page Up': 'Re Pág',
    'Page Down': 'Av Pág',
    'Copy (Ctrl+C)': 'Copiar (Ctrl+C)',
    'Paste (Ctrl+V)': 'Pegar (Ctrl+V)',
    'Cut (Ctrl+X)': 'Cortar (Ctrl+X)',
    'Undo (Ctrl+Z)': 'Deshacer (Ctrl+Z)',
    'Redo (Ctrl+Y)': 'Rehacer (Ctrl+Y)',
    'Select All (Ctrl+A)': 'Seleccionar todo (Ctrl+A)',
    'Find (Ctrl+F)': 'Buscar (Ctrl+F)',
    'New (Ctrl+N)': 'Nuevo (Ctrl+N)',
    'Open (Ctrl+O)': 'Abrir (Ctrl+O)',
    'Save (Ctrl+S)': 'Guardar (Ctrl+S)',
    'Print (Ctrl+P)': 'Imprimir (Ctrl+P)',
    'Paste Plain (Ctrl+Shift+V)': 'Pegar sin formato (Ctrl+Shift+V)',
    'Run (Win+R)': 'Ejecutar (Win+R)',
    'Explorer (Win+E)': 'Explorador (Win+E)',
    'Show Desktop (Win+D)': 'Mostrar escritorio (Win+D)',
    'Lock (Win+L)': 'Bloquear (Win+L)',
    'Search (Win+S)': 'Buscar (Win+S)',
    'Settings (Win+I)': 'Configuración (Win+I)',
    'Power User Menu (Win+X)': 'Menú avanzado (Win+X)',
    'Clipboard (Win+V)': 'Portapapeles (Win+V)',
    'Emoji Panel (Win+.)': 'Panel de emojis (Win+.)',
    'Project Display (Win+P)': 'Proyectar pantalla (Win+P)',
    'Task Switch (Alt+Tab)': 'Cambiar tarea (Alt+Tab)',
    'Task View (Win+Tab)': 'Vista de tareas (Win+Tab)',
    'Task Manager (Ctrl+Shift+Esc)': 'Administrador de tareas (Ctrl+Shift+Esc)',
    'Close App (Alt+F4)': 'Cerrar app (Alt+F4)',
    'Ctrl+Alt+Del': 'Ctrl+Alt+Supr',
    'Snipping (Win+Shift+S)': 'Recorte (Win+Shift+S)',
    'Print Screen': 'Imprimir pantalla',
    'Active Screenshot (Alt+PrtSc)': 'Captura de ventana (Alt+ImpPant)',
    'Reset Graphics (Win+Ctrl+Shift+B)': 'Reiniciar gráficos (Win+Ctrl+Shift+B)',
    'Terminal (Ctrl+Alt+T)': 'Terminal (Ctrl+Alt+T)',
    'Lock (Ctrl+Alt+L)': 'Bloquear (Ctrl+Alt+L)',
    'Spotlight (Cmd+Space)': 'Spotlight (Cmd+Espacio)',
    'App Switch (Cmd+Tab)': 'Cambiar app (Cmd+Tab)',
    'Force Quit (Cmd+Opt+Esc)': 'Forzar cierre (Cmd+Opt+Esc)',
    'Hide App (Cmd+H)': 'Ocultar app (Cmd+H)',
    'Quit App (Cmd+Q)': 'Cerrar app (Cmd+Q)',
    'Address Bar (Ctrl+L)': 'Barra de direcciones (Ctrl+L)',
    'New Tab (Ctrl+T)': 'Nueva pestaña (Ctrl+T)',
    'Close Tab (Ctrl+W)': 'Cerrar pestaña (Ctrl+W)',
    'Reopen Tab (Ctrl+Shift+T)': 'Reabrir pestaña (Ctrl+Shift+T)',
    'Private Window (Ctrl+Shift+N)': 'Ventana privada (Ctrl+Shift+N)',
    'Refresh (Ctrl+R)': 'Recargar (Ctrl+R)',
    'Full Screen (F11)': 'Pantalla completa (F11)',
    'Back (Alt+Left)': 'Atrás (Alt+Izquierda)',
    'Forward (Alt+Right)': 'Adelante (Alt+Derecha)',
    'Next Tab (Ctrl+Tab)': 'Pestaña siguiente (Ctrl+Tab)',
    'Prev Tab (Ctrl+Shift+Tab)': 'Pestaña anterior (Ctrl+Shift+Tab)',
    'Zoom In (Ctrl+=)': 'Acercar (Ctrl+=)',
    'Zoom Out (Ctrl+-)': 'Alejar (Ctrl+-)',
    'Zoom Reset (Ctrl+0)': 'Restablecer zoom (Ctrl+0)',
    'Dev Tools (Ctrl+Shift+I)': 'Herramientas de desarrollo (Ctrl+Shift+I)',
    'Play/Pause (Space)': 'Reproducir/Pausar (Espacio)',
    'YouTube Play/Pause (K)': 'YouTube Reproducir/Pausar (K)',
    'YouTube Mute (M)': 'YouTube Silenciar (M)',
    'YouTube Fullscreen (F)': 'YouTube Pantalla completa (F)',
    'YouTube Captions (C)': 'YouTube Subtítulos (C)',
    'YouTube Vol Up (Up)': 'YouTube Subir volumen (Arriba)',
    'YouTube Vol Down (Down)': 'YouTube Bajar volumen (Abajo)',
    'YouTube Back 10s (J)': 'YouTube Retroceder 10s (J)',
    'YouTube Forward 10s (L)': 'YouTube Avanzar 10s (L)',
    'YouTube Previous (Shift+P)': 'YouTube Anterior (Shift+P)',
    'YouTube Next (Shift+N)': 'YouTube Siguiente (Shift+N)',
    'Start (F5)': 'Iniciar (F5)',
    'Current Slide (Shift+F5)': 'Diapositiva actual (Shift+F5)',
    'Black Screen (B)': 'Pantalla negra (B)',
    'F1 Help': 'F1 Ayuda',
    'F2 Rename': 'F2 Renombrar',
    'F5 Refresh': 'F5 Recargar',
    'F12 Dev Tools': 'F12 Herramientas de desarrollo',
    'Release key': 'Soltar tecla',
    'the same key as in Hold': 'la misma tecla que en Mantener',
    'Releases a key or modifier an earlier Hold left pressed.': 'Suelta una tecla o modificador que un Mantener anterior dejó presionado.',
    'Alt code': 'Código Alt',
    'code, e.g. 3': 'código, ej. 3',
    'Types a character by its Windows numeric code (Alt+number). Alt+3 types ♥. BadUSB only.': 'Teclea un carácter por su código numérico de Windows (Alt+número). Alt+3 escribe ♥. Solo en BadUSB.',
    'Multiple Alt codes': 'Varios códigos Alt',
    'space-separated codes, e.g. 3 9829': 'códigos separados por espacio, ej. 3 9829',
    'Types a sequence of characters by their Alt codes, one after another.': 'Teclea una secuencia de caracteres por sus códigos Alt, uno tras otro.',
    'Pause between keys (once)': 'Pausa entre teclas (una vez)',
    'Sets the pause between each key for just the next text block (useful with slow hosts: KVMs, BIOS). BadUSB only.': 'Fija la pausa entre cada tecla solo para el siguiente bloque de texto (útil con hosts lentos: KVMs, BIOS). Solo en BadUSB.',
    'Uploaded to the badge, like the field notes board. Load brings one back to the editor; delete removes it.': 'Subidas al badge, como el tablero de notas. Cargar las trae al editor; borrar las quita.',
    'Load': 'Cargar',
    'Keyboard lights': 'Luces del teclado',
    'no computer': 'sin computadora',
    'none': 'ninguno',
    'name to upload': 'nombre para subir',
    'no connection': 'sin conexión',
    'Reference': 'Referencia',
    'Commands': 'Comandos',
    'Loading…': 'Cargando…',
    'Nothing uploaded yet.': 'Nada subido todavía.',
    'Loaded into the editor.': 'Cargado en el editor.',
    'Play saved script': 'Reproducir script guardado',
    'Erase saved script': 'Borrar script guardado',
    'Write a script first.': 'Escribe un script primero.',
    'Could not send.': 'No se pudo enviar.',
    'Sent to device': 'Enviado al equipo',
    'Give it a name before uploading.': 'Ponle un nombre antes de subirlo.',
    'Could not upload.': 'No se pudo subir.',
    'Uploaded': 'Subida',
    'Could not delete': 'No se pudo borrar',
    'Deleted': 'Borrada',
    'Are you sure you want to erase this script?': '¿Seguro que quieres borrar este script?',
    'a note; nothing is typed': 'una nota; no se teclea',
    'type text (LN adds Enter)': 'escribe texto (LN agrega Enter)',
    'pause: once / between commands': 'pausa: una vez / entre comandos',
    'named keys': 'teclas con nombre',
    'more named keys': 'más teclas con nombre',
    'chord (GUI/CTRL/ALT/SHIFT + key)': 'combo (GUI/CTRL/ALT/SHIFT + tecla)',
    'repeat the previous command': 'repite el comando anterior',
    'mouse control': 'control del ratón',
    'media keys': 'teclas multimedia',
    'wait for a host lock-LED toggle': 'espera un cambio de candado del host',
    'Builder': 'Constructor',
    'Build your DuckyScript': 'Arma tu DuckyScript',
    'Generated DuckyScript': 'DuckyScript generado',
    'View / import as text': 'Ver / importar como texto',
    'Convert to blocks': 'Convertir a bloques',
    'Clear': 'Limpiar',
    'Add step': 'Agregar paso',
    'Choose or drag a block': 'Elige o arrastra un bloque',
    'Common shortcuts': 'Combinaciones comunes',
    'Select all': 'Seleccionar todo',
    'Copy': 'Copiar',
    'Paste': 'Pegar',
    'Undo': 'Deshacer',
    'Save': 'Guardar',
    'Open Run (Windows)': 'Abrir Ejecutar (Windows)',
    'Drag or tap a block on the left to start.': 'Arrastra o toca un bloque de la izquierda para empezar.',
    'Uploaded to the badge, like the field notes board. Load brings one into the builder; delete removes it.': 'Subidas al badge, como el tablero de notas. Cargar las trae al constructor; borrar las quita.',
    'Loaded into the builder.': 'Cargado en el constructor.',
    'Add at least one block.': 'Agrega al menos un bloque.',
    'Blocks rebuilt from the text.': 'Bloques reconstruidos desde el texto.',
    'Move up': 'Mover arriba',
    'Move down': 'Mover abajo',
    'Remove': 'Quitar',
    'Text': 'Texto',
    'Keys': 'Teclas',
    'Timing': 'Tiempo',
    'Mouse': 'Ratón',
    'Media': 'Multimedia',
    'Note': 'Nota',
    'Type text': 'Teclear texto',
    'Key': 'Tecla',
    'Key combo': 'Combinación',
    'Pause': 'Pausa',
    'Default pause': 'Pausa por defecto',
    'Move mouse': 'Mover ratón',
    'Mouse click': 'Clic ratón',
    'Mouse wheel': 'Rueda ratón',
    'Media key': 'Tecla multimedia',
    'Repeat previous': 'Repetir anterior',
    'Wait for lock': 'Esperar candado',
    'Comment': 'Comentario',
    'Literal line': 'Línea literal',
    'press Enter at the end': 'pulsar Enter al final',
    'text to type': 'texto a teclear',
    'key (e.g. r)': 'tecla (ej. r)',
    'note': 'nota',
    'times': 'veces',
    'Types the text exactly as written on the computer.': 'Teclea el texto tal cual en la computadora.',
    'Presses and releases a single key.': 'Pulsa y suelta una sola tecla.',
    'Holds the ticked keys down and presses the other one. GUI+r opens Run on Windows, for example.': 'Mantiene presionadas las teclas marcadas y pulsa la otra. Por ejemplo, GUI+r abre Ejecutar en Windows.',
    'Pauses the given milliseconds before continuing.': 'Pausa los milisegundos indicados antes de seguir.',
    'Sets the automatic pause between each following command.': 'Fija la pausa automática entre cada comando siguiente.',
    'Moves the pointer (relative pixels, -128 to 127).': 'Mueve el puntero (píxeles relativos, -128 a 127).',
    'Clicks with the chosen button.': 'Hace un clic con el botón elegido.',
    'Scrolls the wheel (positive up, negative down).': 'Gira la rueda (positivo arriba, negativo abajo).',
    'Sends a media key (volume, playback…).': 'Envía una tecla de medios (volumen, reproducción…).',
    'Repeats the immediately previous block n more times.': 'Repite el bloque inmediatamente anterior n veces más.',
    'Waits until the computer turns that keyboard light on or off (Caps Lock, Num Lock or Scroll Lock).': 'Espera hasta que la computadora encienda o apague esa luz del teclado (Bloq Mayús, Bloq Num o Bloq Despl).',
    'A note for you; nothing is typed on the computer.': 'Una nota para ti; no se teclea nada en la computadora.',
    'A hand-written DuckyScript line (for commands not listed).': 'Una línea DuckyScript escrita a mano (para comandos no listados).',
    'Santa Muerte // Lights': 'Santa Muerte // Luces',
    'Santa Muerte // NFC': 'Santa Muerte // NFC',
    'Local control // anonymous notes // no internet': 'Control local // notas anónimas // sin internet',
    'anonymous notes // local only': 'notas anónimas // solo local',
    'Home': 'Inicio',
    'Lights': 'Luces',
    'Field Notes': 'Notas de Campo',
    'LED Tools': 'Herramientas LED',
    'NFC Tools': 'Herramientas NFC',
    'Read': 'Leer',
    'Write': 'Escribir',
    'As a tag': 'Como tag',
    'Menu': 'Menú',
    'Open menu': 'Abrir menú',
    'Badge navigation': 'Navegación del badge',
    'Leave a note': 'Deja una nota',
    'Draw, write, or attach an image. Keep it real.': 'Dibuja, escribe o pega una foto. Como salga.',
    'Note type': 'Tipo de nota',
    'Draw': 'Dibuja',
    'File': 'Archivo',
    'Select file': 'Seleccionar archivo',
    'Attached file': 'Archivo adjunto',
    'download': 'descargar',
    'Image': 'Imagen',
    'Choose an image': 'Elegir imagen',
    'Image to post': 'Foto para publicar',
    'Strip // message': 'Banda // mensaje',
    'STRIP // MESSAGE': 'BANDA // MENSAJE',
    'Drawing area': 'Área para dibujar',
    'Your message': 'Tu mensaje',
    'Write here…': 'Escribe aquí…',
    'Write and draw here.': 'Escribe y dibuja aquí.',
    'No names. No signature.': 'Sin nombres. Sin firma.',
    'Remove it': 'Quitarla',
    'Remove image': 'Quitar imagen',
    'The image stays separate, like a taped note.': 'La foto va aparte, como nota pegada.',
    'Post': 'Publicar',
    'Words last longer than drawings.': 'Las palabras duran más que los dibujos.',
    'SEE ALL': 'VER TODAS',
    'See all': 'Ver todas',
    'empty': 'vacía',
    'The badge is not answering.': 'El badge no contesta.',
    'No notes yet': 'Todavía no hay notas',
    'Draw, write, or do both.': 'Dibuja, escribe o haz las dos.',
    'Sending…': 'Enviando…',
    'Could not post the note.': 'No se pudo publicar la nota.',
    'Note posted.': 'Nota publicada.',
    'Could not load the notes.': 'No se pudieron cargar las notas.',
    'Could not reach the badge.': 'No se pudo llegar al badge.',
    'unknown time': 'hora desconocida',
    'now': 'ahora',
    'Shrinking the image…': 'Reduciendo la imagen…',
    'The image is still too big.': 'La imagen sigue siendo demasiado grande.',
    'Could not read the image.': 'No se pudo leer la imagen.',
    'The file exceeds the limit of': 'El archivo supera el límite de',
    'Not available here': 'No disponible aquí',
    'The browser could not read the image.': 'El navegador no pudo leer la imagen.',
    'Drawing posted': 'Dibujo publicado',
    'too much detail — simplify the drawing': 'demasiado detalle — simplifica el dibujo',
    'the drawing is too big': 'el dibujo es demasiado grande',
    'The drawing is too big. Try something simpler.': 'El dibujo es demasiado grande. Intenta con algo más simple.',
    'Badge light // local control': 'Luz del badge // control local',
    'Connected': 'Conectado',
    'Disconnected': 'Desconectado',
    '← Home': '← Inicio',
    'Purple wave': 'Ola morada',
    'Patterns': 'Patrones',
    'Solid': 'Fijo',
    'Rainbow': 'Arcoíris',
    'Chase': 'Carrera',
    'Pulse': 'Pulso',
    'Twinkle': 'Destello',
    'Theater': 'Teatro',
    'Slow rituals': 'Rituales lentos',
    'Candle': 'Vela',
    'Breathe': 'Respira',
    'Embers': 'Brasas',
    'Tide': 'Marea',
    'Vigil': 'Vigilia',
    'Comet': 'Cometa',
    'Rosary': 'Rosario',
    'Veil': 'Velo',
    'Prism': 'Prisma',
    'Sunset': 'Ocaso',
    'Ocean': 'Océano',
    'Nebula': 'Nebulosa',
    'Orbit': 'Órbita',
    'Bloom': 'Florecer',
    'Mirage': 'Espejismo',
    'Cosmos': 'Cosmos',
    'Off': 'Apagado',
    'Animation': 'Animación',
    'Brightness': 'Brillo',
    'Speed': 'Velocidad',
    'Color wheel': 'Rueda de color',
    'Hex color': 'Color hexadecimal',
    'Settings saved': 'Ajustes guardados',
    'The badge did not respond.': 'El badge no respondió.',
    'Connection lost.': 'Se perdió la conexión.',
    'Controls go straight to the ESP32 over its local Wi-Fi.': 'Los controles van directo al ESP32 por su Wi-Fi local.',
    'Badge network': 'Red del badge',
    'Remembered networks:': 'Redes recordadas:',
    'Badge hotspot': 'Hotspot del badge',
    'Loading network…': 'Cargando red…',
    'Hotspot name': 'Nombre del hotspot',
    '1 to 32 characters. Saving restarts the hotspot under the new name.': 'De 1 a 32 caracteres. Al guardar, el hotspot se reinicia con el nombre nuevo.',
    'Hotspot password': 'Contraseña del hotspot',
    'Hide': 'Ocultar',
    'Show': 'Ver',
    'Leave it empty and anyone can join without a password. If you set one, it needs 8 to 63 letters or numbers.': 'Déjala vacía para que cualquiera pueda entrar sin contraseña. Si pones una, debe tener de 8 a 63 letras o números.',
    'Shown in full so you can read and share it. The badge makes three themed words; it accepts 8–63 ASCII characters.': 'Se muestra completa para leerla y pasarla. El badge crea tres palabras con tema; acepta 8–63 caracteres ASCII.',
    'Hide the hotspot': 'Ocultar el hotspot',
    'It will not show up in the list of networks on your phone. You will have to type the name in by hand.': 'No aparecerá en la lista de redes del teléfono. Habrá que escribir el nombre a mano.',
    'Save hotspot': 'Guardar hotspot',
    'Valid password.': 'Contraseña válida.',
    'Use ordinary letters and numbers only. Accents and emoji do not work in a Wi-Fi password.': 'Usa solo letras y números normales. Los acentos y los emoji no funcionan en una contraseña Wi-Fi.',
    'The password must be 8 to 63 characters.': 'La contraseña debe tener 8 a 63 caracteres.',
    'Network configuration only works when this page is opened from the badge.': 'La configuración de red solo funciona al abrir esto desde el badge.',
    'Open this page from the badge at 10.69.4.20 to view or change the network.': 'Abre esta página desde el badge en 10.69.4.20 para ver o cambiar la red.',
    'Could not read the network settings.': 'No se pudo leer la configuración de red.',
    'Could not read the Wi-Fi settings.': 'No se pudo leer la configuración de Wi-Fi.',
    'Saving…': 'Guardando…',
    'Could not save the network settings.': 'No se pudo guardar la configuración de red.',
    'Reconnect to badge': 'Reconectar al badge',
    'Connect to saved Wi-Fi': 'Conectar al Wi-Fi guardado',
    'You have not saved a network yet.': 'Todavía no has guardado ninguna red.',
    'Network name': 'Nombre de la red',
    'Network password (optional)': 'Contraseña de la red (opcional)',
    'Leave it empty if the network is open. The password is saved on the badge and needs 8 to 63 letters or numbers.': 'Déjala vacía si la red es abierta. La contraseña se guarda en el badge y debe tener de 8 a 63 letras o números.',
    'Save and connect': 'Guardar y conectar',
    'Open the badge at': 'Abre el badge en',
    'or': 'o',
    'Name sent to the router:': 'Nombre enviado al router:',
    'Connected to saved Wi-Fi.': 'Conectado al Wi-Fi guardado.',
    'Connecting to saved Wi-Fi…': 'Conectando al Wi-Fi guardado…',
    'Saved Wi-Fi is not connected.': 'El Wi-Fi guardado no está conectado.',
    'Saved. The badge is connecting to your saved Wi-Fi.': 'Guardado. El badge está conectando a tu Wi-Fi guardado.',
    'Saving and connecting…': 'Guardando y conectando…',
    'Testing the network…': 'Probando la red…',
    'Testing the network… it is only saved if it connects.': 'Probando la red… solo se guarda si conecta.',
    'Could not connect. That network was not saved.': 'No se pudo conectar. Esa red no se guardó.',
    'Testing:': 'Probando:',
    'Not saved:': 'No se guardó:',
    'The saved Wi-Fi SSID must be 1 to 32 bytes with no control characters.': 'El SSID guardado debe tener 1 a 32 bytes sin controles.',
    'The saved Wi-Fi name is required.': 'Falta el nombre del Wi-Fi guardado.',
    'Could not read the saved network.': 'No se pudo leer la red guardada.',
    'Could not save the network.': 'No se pudo guardar la red.',
    'Saved Wi-Fi could not be saved.': 'El Wi-Fi guardado no se pudo guardar.',
    'Could not open NVS to save saved Wi-Fi.': 'No se pudo abrir NVS para guardar el Wi-Fi guardado.',
    'Open this page from the badge to connect it to your Wi-Fi.': 'Abre esta página desde el badge para conectarlo a tu Wi-Fi.',
    'Turn the hotspot on': 'Encender el hotspot',
    'Keep the Santa Muerte network on for visitors and recovery.': 'Deja encendida la red Santa Muerte para visitas y recuperación.',
    'The badge makes its own network. Turn this off to have it join your Wi-Fi instead.': 'El badge crea su propia red. Apágalo para que se conecte a tu Wi-Fi.',
    'Heads up: turning this off disconnects this page.': 'Ojo: al apagarlo, esta página se desconecta.',
    'Return through saved Wi-Fi at SantaMuerte.local, or connect USB and press': 'Vuelve por tu Wi-Fi guardado en SantaMuerte.local, o conecta USB y presiona',
    'to turn it back on.': 'para encenderlo otra vez.',
    'The badge will try to join your Wi-Fi. If it cannot, plug it in over USB and press': 'El badge intentará conectarse a tu Wi-Fi. Si no lo logra, conéctalo por cable USB y presiona',
    'to have it make its own network again.': 'para que vuelva a crear su propia red.',
    'Turning the hotspot on…': 'Encendiendo el hotspot…',
    'Turning the hotspot off…': 'Apagando el hotspot…',
    'The access-point setting is missing.': 'Falta el ajuste del punto de acceso.',
    'Could not change the hotspot.': 'No se pudo cambiar el hotspot.',
    'Saved. Turning on the badge access point.': 'Guardado. Encendiendo el punto de acceso del badge.',
    'Saved. The access point will turn off; use saved Wi-Fi or USB to turn it back on.': 'Guardado. El punto de acceso se apagará; usa el Wi-Fi guardado o USB para volver a encenderlo.',
    'Saved. Switching to your saved Wi-Fi.': 'Guardado. Cambiando a tu Wi-Fi guardado.',
    'Saving and switching networks…': 'Guardando y cambiando de red…',
    'Saved. Switching networks…': 'Guardado. Cambiando de red…',
    'Saved. The access point stays off until you turn it on.': 'Guardado. El punto de acceso sigue apagado hasta que lo enciendas.',
    'Could not open NVS to save the access-point setting.': 'No se pudo abrir NVS para guardar el ajuste del punto de acceso.',
    'Could not save the access-point setting.': 'No se pudo guardar el ajuste del punto de acceso.',
    'Join your Wi-Fi': 'Conectarse a tu Wi-Fi',
    'Badge communion // tags and emulation': 'Comunión del badge // tags y emulación',
    'Reader': 'Lector',
    'Read a tag': 'Lee un tag',
    'Reader idle': 'Lector sin datos',
    'Reader unavailable': 'Lector no disponible',
    'Reader ready': 'Lector listo',
    'Reader busy': 'Lector ocupado',
    'Ready to read': 'Listo para leer',
    'Ready': 'Listo',
    'Reader offline': 'Lector fuera',
    'NFC reader': 'Lector NFC',
    'NFC reader connected': 'Lector NFC conectado',
    'Pick an action, then hold a card or a keyfob to the badge.': 'Elige una acción y acerca una tarjeta o un llavero al badge.',
    'Read tag': 'Leer tag',
    'Tag type': 'Tipo de tag',
    'Capacity': 'Capacidad',
    'Result': 'Resultado',
    'Nothing read': 'Nada leído',
    'Hold a tag to the badge to see its text or its link.': 'Acerca un tag para ver su texto o su enlace.',
    'Raw data (NDEF)': 'Datos en crudo (NDEF)',
    'Writing': 'Escritura',
    'Save text or a link to a tag': 'Guarda texto o un enlace en un tag',
    'Web page or app link': 'Página o enlace de app',
    'Stored in the most compact form that fits.': 'Se guarda en el formato más compacto que quepa.',
    'Write the tag text': 'Escribe el texto del tag',
    'Stored as plain text.': 'Se guarda como texto simple.',
    'Write URL': 'Escribir URL',
    'Heads up:': 'Ojo:',
    'Current action': 'Acción actual',
    'What is going on shows up here.': 'Aquí aparece qué está pasando.',
    'Tag emulation': 'Emulación de tag',
    'Present an NFC record from the badge. Wi-Fi is shared at startup.': 'Presenta un registro NFC desde el badge. El Wi-Fi se comparte al encender.',
    'Link they will see': 'Enlace que verán',
    'Text they will see': 'Texto que verán',
    'Presented as read-only text.': 'Se presenta como texto de solo lectura.',
    'Start emulation': 'Iniciar emulación',
    'Share Wi-Fi over NFC': 'Compartir Wi-Fi por NFC',
    'Stop emulation': 'Parar emulación',
    'One NFC mode at a time:': 'Un modo NFC a la vez:',
    'Stopped': 'Detenido',
    'Reads': 'Lecturas',
    'Other readers will see the badge as a read-only tag until you stop it.': 'Otros lectores verán el badge como un tag de solo lectura hasta que lo detengas.',
    'At startup the badge offers its Wi-Fi. You can change that to a text or a link.': 'Al encender, el badge ofrece su Wi-Fi. Puedes cambiarlo por un texto o un enlace.',
    'Connect to the ESP32 Wi-Fi and open': 'Conéctate al Wi-Fi del ESP32 y abre',
    'Everything stays local, on the PCB.': 'Todo pasa local, en el PCB.',
    'Pick an action and bring a compatible tag to the PCB reader.': 'Elige una acción y acerca un tag compatible al lector del PCB.',
    'Erase': 'Borrar',
    'Brush size': 'Tamaño de pincel',
    'Draw in': 'Dibujar en',
    'Pen': 'Pluma',
    'Brush': 'Pincel',
    'Highlighter': 'Resaltador',
    'Red': 'Rojo',
    'Orange': 'Naranja',
    'Yellow': 'Amarillo',
    'Green': 'Verde',
    '⌁ Read tag': '⌁ Leer tag',
    'ESP32 NFC setup tag': 'Tag de configuración NFC del ESP32',
    'works with the most common tags (NTAG2xx, MIFARE Ultralight). Do not move the tag until it finishes.': 'funciona con los tags más comunes (NTAG2xx, MIFARE Ultralight). No muevas el tag hasta que termine.',
    'Sneakreaper badge NFC record': 'Registro NFC del badge Sneakreaper',
    'with emulation running, the PN532 will not read or write external tags. Unlock the phone, turn NFC on, and line up the antennas.': 'con emulación activa, el PN532 no lee ni escribe tags externos. Desbloquea el teléfono, prende NFC y acerca sus antenas.',
    '. Everything stays local, on the PCB.': '. Todo pasa local, en el PCB.',
    'Text for nearby phones': 'Escribe texto para teléfonos cerca',
    'Presented as a read-only link.': 'Se presenta como un enlace de solo lectura.',
    'Tag read.': 'Tag leído.',
    'Present a tag': 'Acerca un tag',
    'Reading tag…': 'Leyendo tag…',
    'Acting as a tag': 'Actuando como tag',
    'Failed': 'No pasó',
    'Waiting for a tag': 'Esperando tag',
    'Pick an NFC action.': 'Elige una acción NFC.',
    'No action running.': 'No hay acción activa.',
    'An NFC reader is reading the badge': 'El lector NFC está leyendo el badge',
    'The NFC reader is not answering.': 'El lector NFC no contesta.',
    'This page cannot reach the NFC reader.': 'Esta página no puede hablar con el lector NFC.',
    'Preview: nothing was done.': 'Vista previa: no se hizo nada.',
    'That did not work.': 'No se pudo hacer.',
    'Lost the connection to the badge.': 'Se perdió la conexión con el badge.',
    'Write text': 'Escribir texto',
    'Emulate URL tag': 'Emular tag URL',
    'Emulate text tag': 'Emular tag de texto',
    'Write something before saving.': 'Escribe algo antes de guardar.',
    'Type something first.': 'Escribe algo primero.',
    'The NFC reader has not started.': 'El lector NFC no ha iniciado.',
    'The badge is not acting as a tag.': 'El badge no está actuando como tag.',
    'PN532 SAM configuration failed.': 'Falló la configuración SAM del PN532.',
    'PN532 retry configuration failed.': 'Falló la configuración de reintentos del PN532.',
    'The NFC reader is not available.': 'El lector NFC no está disponible.',
    'Stop acting as a tag before reading or writing another one.': 'Deja de actuar como tag antes de leer o escribir otro.',
    'Another NFC action is already waiting for a tag.': 'Ya hay otra acción NFC esperando un tag.',
    'That Type 2 page is outside the NTAG2xx range.': 'La página Type 2 queda fuera del rango NTAG2xx.',
    'Could not read Type 2 page': 'No se pudo leer la página Type 2',
    'Refusing to write outside the NTAG2xx user range.': 'No se escribe fuera del rango de usuario NTAG2xx.',
    'Could not write and verify Type 2 page': 'No se pudo escribir y verificar la página Type 2',
    'The Type 2 TLV length exceeds the tag capacity.': 'El largo TLV Type 2 pasa la capacidad del tag.',
    'The NDEF message is too short to read.': 'El mensaje NDEF es muy corto para leerlo.',
    'UTF-16 text detected; only UTF-8 is shown here.': 'Se detectó texto UTF-16; aquí solo se muestra UTF-8.',
    'Unsupported NDEF record type. The raw bytes are shown below.': 'Tipo de registro NDEF no compatible. Abajo salen los bytes crudos.',
    'The tag answers as Type 2 memory but is not NDEF formatted.': 'El tag responde como memoria Type 2, pero no está en formato NDEF.',
    'Type 2 memory seen, but no valid container.': 'Se vio memoria Type 2, pero sin contenedor válido.',
    'The tag has no user memory.': 'El tag no tiene memoria de usuario.',
    'The tag reports a TLV longer than its data area.': 'El tag trae un largo TLV mayor que su área de datos.',
    'This tag carries an empty NDEF message.': 'Este tag trae un mensaje NDEF vacío.',
    'There is NDEF data, but it could not be read.': 'Hay datos NDEF, pero no se pudieron leer.',
    'No NDEF message was found on the tag.': 'No se encontró mensaje NDEF en el tag.',
    'Enter text or a URL before saving.': 'Escribe texto o una URL antes de guardar.',
    'The content exceeds the 700 byte limit.': 'El contenido pasa el límite de 700 bytes.',
    'The NDEF record is too big for this tag.': 'El registro NDEF pesa mucho para este tag.',
    'Could not read the Type 2 capability page.': 'No se pudo leer la página de capacidad Type 2.',
    'This tag reports its NDEF as read-only.': 'Este tag dice que su NDEF es solo lectura.',
    'This tag has no writable memory.': 'Este tag no tiene memoria para escribir.',
    'The SSID is empty or longer than 32 bytes.': 'El SSID está vacío o pasa de 32 bytes.',
    'The Wi-Fi details do not fit.': 'Los datos de Wi-Fi no caben.',
    'Could not build the Wi-Fi WSC record.': 'No se pudo armar el registro WSC de Wi-Fi.',
    'The Wi-Fi plus text NDEF message is too big.': 'El mensaje NDEF de Wi-Fi y texto pesa mucho.',
    'The Wi-Fi NDEF message length does not add up.': 'El largo del mensaje NDEF de Wi-Fi no cuadra.',
    'You can only present text or a link.': 'Solo puedes presentar texto o un enlace.',
    'Type something before you start.': 'Escribe algo antes de empezar.',
    'Only 220 characters fit.': 'Solo caben 220 caracteres.',
    'The content is too big to present.': 'El contenido es demasiado grande para presentarlo.',
    'A reader just read the badge.': 'Un lector acaba de leer el badge.',
    'NFC reader detected. Sending the content…': 'Lector NFC detectado. Enviando el contenido…',
    'Ready. Hold a phone or an NFC reader to the badge.': 'Listo. Acerca un teléfono o un lector NFC al badge.',
    'The NFC reader left before it finished.': 'El lector NFC se fue antes de terminar.',
    'The tag was detected, but its Type 2 memory could not be read.': 'Se detectó el tag, pero no se pudo leer su memoria Type 2.',
    'Read the tag ID.': 'Se leyó el ID del tag.',
    'The write failed before the whole NDEF record was saved.': 'Falló la escritura antes de guardar todo el registro NDEF.',
    'The tag was written, but the verify read failed.': 'El tag se escribió, pero falló la lectura de prueba.',
    'Present a writable Type 2 tag to save the URL.': 'Acerca un tag Type 2 que se pueda escribir para guardar la URL.',
    'Present a writable Type 2 tag to save the text.': 'Acerca un tag Type 2 que se pueda escribir para guardar el texto.',
    'Wait for the tag action to finish.': 'Espera a que termine la acción del tag.',
    'The PN532 is unavailable for Wi-Fi sharing.': 'El PN532 no está disponible para pasar Wi-Fi.',
    'Wait for the NFC action to finish.': 'Espera a que termine la acción NFC.',
    'Wi-Fi ready. Scan the badge to join its network.': 'Wi-Fi listo. Lee el badge para conectarte a su red.',
    'Reader ready. Pick an action and present a tag.': 'Lector listo. Elige una acción y acerca un tag.',
    'The PN532 did not return to reader mode.': 'El PN532 no volvió al modo lector.',
    'No tag was detected within 15 seconds.': 'No se detectó ningún tag en 15 segundos.',
    'Tag detected. Processing…': 'Tag detectado. Procesando…',
    'NFC synchronisation did not start.': 'No arrancó la sincronización NFC.',
    'The PN532 failed to start.': 'Falló el inicio del PN532.',
    'PN532 not found. Check power, SPI and wiring.': 'No se encontró el PN532. Revisa corriente, SPI y cables.',
    'The NFC task did not start.': 'No arrancó la tarea NFC.',
    'Hold an NFC tag over the PCB reader to scan it.': 'Acerca un tag NFC al lector del PCB para leerlo.',
    'The NFC action queue is full.': 'La cola de acciones NFC está llena.',
    'The type must be text or url.': 'El tipo debe ser texto o URL.',
    'Could not queue the NFC action.': 'No se pudo poner la acción NFC en cola.',
    'Present a Type 2 tag to save the URL.': 'Acerca un tag Type 2 para guardar la URL.',
    'Present a Type 2 tag to save the text.': 'Acerca un tag Type 2 para guardar el texto.',
    'Could not start acting as a tag.': 'No se pudo empezar a actuar como tag.',
    'Preparing the NDEF record…': 'Preparando el registro NDEF…',
    'The Wi-Fi data is not valid.': 'Los datos de Wi-Fi no son válidos.',
    'Could not queue Wi-Fi sharing.': 'No se pudo poner Wi-Fi en cola.',
    'Preparing the Wi-Fi records…': 'Preparando los registros de Wi-Fi…',
    'Another NFC action is still being queued.': 'Todavía se está poniendo otra acción NFC en cola.',
    'Stopping…': 'Dejando de actuar como tag…',
    'The board is unavailable.': 'El tablero no está disponible.',
    'Use one mode at a time': 'Usa un modo a la vez',
    'NFC Modes': 'Modos NFC',
    'Stop active mode': 'Parar modo activo',
    'Wi-Fi over NFC': 'Wi-Fi por NFC',
    'Act as a tag': 'Actuar como tag',
    'Stop': 'Parar',
    'No mode running': 'Sin modo activo',
    'Wi-Fi over NFC active': 'Wi-Fi por NFC activo',
    'The badge is acting as a tag': 'El badge está actuando como tag',
    'PN532 offline': 'PN532 fuera',
    'What other readers will see': 'Lo que otros lectores verán',
    'Save this record by selecting Emulate saved tag above. While emulation is active, the PN532 cannot read or write external tags.': 'Guarda este registro al tocar Emular tag guardado arriba. Con emulación activa, el PN532 no lee ni escribe tags externos.',
    'Save this record by selecting Emulate tag above. While emulation is active, the PN532 cannot read or write external tags.': 'Guarda este registro al tocar Emular tag arriba. Con emulación activa, el PN532 no lee ni escribe tags externos.',
    'Auto-scan': 'Escaneo automático',
    'Auto-scan off': 'Escaneo automático apagado',
    'Auto-scan on': 'Escaneo automático encendido',
    'Auto-scan is off.': 'Escaneo automático apagado.',
    'Auto-scan on. Every tag read goes to Field Notes.': 'Notas NFC encendidas. Cada tag que se lea va a Field Notes.',
    'Auto-scan on. Every tag read goes to the NFC log.': 'Escaneo automático encendido. Cada tag que se lea va al registro NFC.',
    'Tags seen': 'Tags en notas',
    'Auto-scan switched off.': 'El escaneo automático se apagó.',
    'Tag saved in Field Notes.': 'Tag guardado en Field Notes.',
    'Tag had no data. Its UID was saved in Field Notes.': 'Tag sin datos. Su UID se guardó en Field Notes.',
    'The auto-scan setting is missing.': 'Falta el ajuste del escaneo automático.',
    'PN532 emulating': 'PN532 emulando',
    'Waiting for NFC reader': 'Esperando lector NFC',
    'Controller offline.': 'Controlador fuera.',
    'This page cannot reach the ESP32 NFC controller.': 'La página no llega al NFC del ESP32.',
    'Read a tag with no text or URL. Nothing was uploaded.': 'Se leyó un tag sin texto ni URL. No se subió nada.',
    'The badge\'s eleven lights on the figure': 'Las once luces del badge sobre la figura',
    'halo // left shoulder': 'halo // hombro izquierdo',
    'halo // lower left': 'halo // izquierda baja',
    'halo // upper left': 'halo // izquierda alta',
    'halo // crown left': 'halo // corona izquierda',
    'halo // crown right': 'halo // corona derecha',
    'halo // upper right': 'halo // derecha alta',
    'halo // lower right': 'halo // derecha baja',
    'halo // right shoulder': 'halo // hombro derecho',
    'hands // top': 'manos // arriba',
    'hands // lower left': 'manos // abajo izquierda',
    'hands // lower right': 'manos // abajo derecha',
    'Animations': 'Animaciones',
    'Custom colour': 'Color propio',
    'Multicolour': 'Multicolor',
    'Silence': 'Silencio',
    'Offering': 'Ofrenda',
    'Aperture': 'Aureola',
    'Crown': 'Corona',
    'Collide': 'Encuentro',
    'Scanner': 'Escáner',
    'Hands': 'Manos',
    'Drift': 'Deriva',
    'ring': 'aro',
    'hands': 'manos',
    'all': 'todo',
    'The badge did not accept the settings.': 'El badge no aceptó los ajustes.',
    'No changes. The badge is unchanged.': 'Sin cambios. El badge sigue igual.',
    'Tap "Show" to read it and pass it on. The badge makes three themed words; it accepts 8–63 ASCII characters.': 'Toca «Ver» para leerla y pasarla. El badge crea tres palabras con tema; acepta 8–63 caracteres ASCII.',
    'Heads up: write the name down exactly as it appears above.': 'Ojo: apunta el nombre tal cual aparece arriba.',
    'If your phone forgets this network and you no longer remember the name, you will not get back in over Wi-Fi: you would have to plug the badge in over USB.': 'Si tu teléfono olvida esta red y ya no recuerdas el nombre, no podrás volver a entrar por Wi-Fi: tendrás que conectar el badge por cable USB.',
    'The badge sent a reply that could not be understood.': 'El badge mandó una respuesta que no se entiende.',
    'Open this page from the badge at 10.69.4.20 to see or change the Wi-Fi.': 'Abre esta página desde el badge en 10.69.4.20 para ver o cambiar el Wi-Fi.',
    'The Wi-Fi settings were not saved.': 'No se guardaron los ajustes de Wi-Fi.',
    'The badge answered oddly': 'El badge respondió raro',
    'Constant scanning': 'Escaneo constante',
    'The reader keeps looking. Every tag that turns up posts itself: its text or URL, or its UID when it carries nothing. Turns emulation off.': 'El lector se queda buscando. Cada tag que aparece se publica solo: su texto o URL, o su UID si no trae nada. Apaga la emulación.',
    'Tag had no data. Its UID was offered.': 'Tag sin datos. Se ofrendó su UID.',
    'Scan networks': 'Buscar redes',
    'NFC Log': 'Registro NFC',
    'Santa Muerte // NFC Log': 'Santa Muerte // Registro NFC',
    'Clear log': 'Borrar registro',
    'Clear every tag from the NFC log?': '¿Borrar todos los tags del registro NFC?',
    'Tags read': 'Tags leídos',
    'Every tag the reader met': 'Cada tag que el lector conoció',
    'Refresh': 'Actualizar',
    'ID only — nothing else could be read': 'Solo el ID — no se pudo leer nada más',
    'Tags the reader meets no longer go to Field Notes; they land here, with their UID, their type and whatever could be read. Meeting the same tag again adds to its count instead of repeating it.': 'Los tags leídos ya no van a Notas de Campo; llegan aquí, con su UID, su tipo y lo que se pudo leer. Volver a ver el mismo tag suma a su cuenta en vez de repetirlo.',
    'Nothing here yet. Turn on': 'Todavía no hay nada aquí. Enciende',
    'in NFC Tools and hold a card or a keyfob to the badge: whatever can be read shows up here.': 'en NFC Tools y acerca una tarjeta o un llavero: lo que se pueda leer aparecerá aquí.',
    'Tag saved to the NFC log.': 'Tag guardado en el registro NFC.',
    'Tag had no data; its UID stayed in the NFC log.': 'Tag sin datos; su UID quedó en el registro NFC.',
    'NFC log cleared.': 'Registro NFC borrado.',
    'Could not clear the NFC log.': 'No se pudo limpiar el registro NFC.',
    'USB Console': 'Consola USB',
    'The same thing you see over the USB cable': 'Lo mismo que ves por el cable USB',
    'Connecting': 'Conectando',
    'Live': 'En vivo',
    'Offline': 'Sin conexión',
    'Type, then press Enter': 'Escribe y pulsa Enter',
    'Toggles colours (key p)': 'Alterna los colores (tecla p)',
    'Click the screen and type just as you would over the cable, or use the box above from a phone.': 'Haz clic en la pantalla y escribe igual que en el cable, o usa la caja de arriba desde el teléfono.',
    'reopens a closed session, and': 'abre una sesión cerrada, y',
    'turns colours on in both views at once.': 'enciende los colores en las dos vistas a la vez.',
    'One session: the cable and this page share the same screen, down to whatever is half-typed. Seeing saved passwords, sending keyboard payloads and clearing the notes stay cable-only, because this page asks for no password.': 'Una sola sesión: el cable y esta página comparten la misma pantalla, incluso lo que está escrito a medias. Ver las contraseñas guardadas, enviar payloads de teclado y borrar las notas siguen siendo solo por cable, porque esta página no pide contraseña.',
    'Cyan': 'Cian',
    'Blue': 'Azul',
    'Purple': 'Morado',
    'Pink': 'Rosa',
    'White': 'Blanco',
    'Media and presentation controls': 'Controles multimedia y presentación',
    'A hand-written BadUSB line, for commands not listed here.': 'Una línea BadUSB escrita a mano (para comandos no listados).',
    'The hotspot name must be 1 to 32 characters.': 'El nombre del hotspot debe tener de 1 a 32 caracteres.',
    'The hotspot name has characters that cannot be used.': 'El nombre del hotspot tiene caracteres que no se pueden usar.',
    'Wi-Fi settings only appear if you open this page from the badge.': 'Los ajustes de Wi-Fi solo aparecen si abres esta página desde el badge.',
    'A password, if you set one, must be 8 to 63 characters.': 'Si usas contraseña, debe tener 8 a 63 caracteres.',
    'or whatever address shows up here.': 'o la dirección que aparezca aquí.',
    'No networks found.': 'No se encontró ninguna red.',
    'Storage': 'Almacenamiento',
    'no lights': 'sin luces',
    'Tag ID': 'ID del tag',
    'It is saved when you tap': 'Se guarda al tocar',
    'above. While the badge is acting as a tag it cannot read or write other tags.': 'arriba. Mientras el badge actúa como tag, no puede leer ni escribir otros tags.',
    'Supported': 'Compatible',
    'Four hex digits. Also called the VID.': 'Cuatro dígitos hex. También se le dice VID.',
    'Four hex digits. Also called the PID.': 'Cuatro dígitos hex. También se le dice PID.',
    'USB console + keyboard': 'Consola USB + teclado',
    'Type': 'Tipo',
    'Colour': 'Color',
    'Once it connects, open': 'Cuando se conecte, abre',
    'Keyboard lights: —': 'Luces del teclado: —',
    'USB controls': 'Controles USB',
    'Build your BadUSB': 'Arma tu BadUSB',
    'Generated BadUSB': 'BadUSB generado',
    'Anonymous': 'Anónimo',
    'Could not scan.': 'No se pudo buscar.',
    'the address shown here': 'la dirección que aparezca aquí',
    'Stopping.': 'Dejando de actuar como tag.',
    'after': 'después de',
    'sector(s) read with known keys. No NDEF. Preview:': 'sector(es) leídos con llaves conocidas. Sin NDEF. Vista:',
    'Could not authenticate sector 1 for writing.': 'No se pudo autenticar el sector 1 para escribir.',
    'is missing. Upload the LittleFS image and try again.': 'no existe. Carga la imagen LittleFS y prueba otra vez.',
    'Could not open': 'No se pudo abrir',
    'The language is missing.': 'Falta el idioma.',
    'The network name, the password or the hidden-network setting is missing.': 'Faltan el SSID, la contraseña o el ajuste de red oculta.',
    'Saved. Join again by typing the hidden network name and the new password by hand.': 'Guardado. Vuelve a entrar escribiendo a mano el SSID oculto y la contraseña nueva.',
    'Saved. Join the badge again with the new network name and password.': 'Guardado. Vuelve a entrar al badge con el SSID y la contraseña nuevos.',
    'That note has no drawing.': 'No hay dibujo para esa nota.',
    'The drawing is bigger than the badge can hold.': 'El dibujo pesa más de lo que aguanta el badge.',
    'Could not clear the board.': 'No se pudo limpiar el tablero.',
    'Every note was deleted.': 'Se borraron todas las notas.',
    'Page not found. Open http://10.69.4.20/notes for the notes,': 'No se encontró la página. Abre http://10.69.4.20/notes para las notas,',
    'http://10.69.4.20/led for the lights,': 'http://10.69.4.20/led para luces,',
    'http://10.69.4.20/nfc for NFC, or': 'http://10.69.4.20/nfc para NFC o',
    'http://10.69.4.20/network for the network settings.': 'http://10.69.4.20/network para la red.',
    'The badge network name must be 1 to 32 bytes with no control characters.': 'El SSID del badge debe tener 1 a 32 bytes sin controles.',
    'Could not open storage to save the badge network name.': 'No se pudo abrir NVS para guardar el SSID del badge.',
    'Could not save the badge network name.': 'No se pudo guardar el SSID del badge.',
    'The drawing is too big for the badge.': 'El dibujo pesa mucho para el badge.',
    'The image must be a JPEG.': 'La foto debe ser JPEG.',
    'Could not save the drawing.': 'No se pudo guardar el dibujo.',
    'Are you sure?': '¿Estás seguro?',
    'Yes': 'Sí',
    'No': 'No',
    'Take this note down': 'Quitar esta nota',
    'Take this tag down': 'Quitar este tag',
    'Take this script down': 'Quitar este script',
    'Delete this note': 'Borrar esta nota',
    'Could not delete that note.': 'No se pudo borrar la nota.',
    'That note is not on the board.': 'Esa nota ya no está en el tablero.',
    'Note deleted.': 'Nota borrada.',
    'That tag is not in the log.': 'Ese tag ya no está en el registro.',
    'Tag deleted.': 'Tag borrado.',
  };
  // The reverse table cannot be a blind inversion. "Red" is Spanish for
  // Network and also the English colour, so inverting 'Red': 'Rojo' would teach
  // the es->en direction to rewrite the Spanish Network page's own title "Red"
  // into the colour. A Spanish value that is itself an English key meaning
  // something else is ambiguous in reverse, so it gets no entry; a word spelled
  // the same in both languages, like "Cosmos", is not ambiguous and keeps its own.
  const owns = (table, key) => Object.prototype.hasOwnProperty.call(table, key);
  const esEn = Object.fromEntries(
    Object.entries(enEs)
      .filter(([, es]) => !owns(enEs, es) || enEs[es] === es)
      .map(([en, es]) => [es, en]));
  let locale = 'en-US';
  try { locale = localStorage.getItem('santa-muerte-locale') === 'es-MX' ? 'es-MX' : 'en-US'; } catch (_) {}

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
      // "hace N min" rendered as "Nh ago" -- a note from 10 minutes ago
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
  // English is the source, so going back to English is a restore, not another
  // lookup. That matters because the es->en table cannot be a complete inverse:
  // "Red" is Spanish for Network and English for the colour, so the ambiguity
  // guard above refuses to map it and a translated page would have been stuck
  // with "Red" in the nav forever. Remembering what the node actually said
  // sidesteps the whole class of collision.
  const sourceText = new WeakMap();
  const sourceAttr = new WeakMap();
  // Writing a node fires the observer below, so without this the translator's
  // own output looked like the page saying something new and threw away the
  // very source text it had just replaced.
  const lastWritten = new WeakMap();

  function rememberText(textNode, value) {
    if (!sourceText.has(textNode) && locale === 'en-US') sourceText.set(textNode, value);
  }

  function renderText(textNode) {
    const raw = undecorate(textNode.nodeValue);
    rememberText(textNode, raw);
    const restored = locale === 'en-US' ? sourceText.get(textNode) : undefined;
    let converted = restored !== undefined ? restored : convert(raw, locale);
    if (textNode.parentElement.closest('h1, h2, .saint-title, .settings-title')) converted = decorate(converted);
    if (converted !== textNode.nodeValue) textNode.nodeValue = converted;
    lastWritten.set(textNode, textNode.nodeValue);
  }

  function translateText(root) {
    const walker = document.createTreeWalker(root || document.body, NodeFilter.SHOW_TEXT);
    const nodes = []; let node;
    while ((node = walker.nextNode())) nodes.push(node);
    nodes.forEach(textNode => {
      if (skipped(textNode.parentElement)) return;
      renderText(textNode);
    });
  }
  function translateAttrs(root) {
    (root || document).querySelectorAll('*').forEach(element => {
      if (element.matches('[data-locale-control]')) return;
      let remembered = sourceAttr.get(element);
      ['aria-label', 'title', 'placeholder', 'alt'].forEach(attribute => {
        if (!element.hasAttribute(attribute)) return;
        const current = element.getAttribute(attribute);
        if (locale === 'en-US' && (!remembered || !(attribute in remembered))) {
          if (!remembered) sourceAttr.set(element, remembered = {});
          remembered[attribute] = current;
        }
        const restored = locale === 'en-US' && remembered ? remembered[attribute] : undefined;
        const converted = restored !== undefined ? restored : convert(current, locale);
        if (converted !== current) element.setAttribute(attribute, converted);
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
  // The page name sits in a fixed slot between the two candle menus, and how
  // wide it is depends on the name and the language: "Notas de Campo" runs half
  // again as long as "Red". A font-size in vw cannot know that, so a long name
  // used to slide out from under its slot and across the right-hand candle.
  // Measure what the name actually renders as and step it down until it fits.
  // It lives here because the text it measures is the text this file rewrites.
  function fitHeaderTitles() {
    document.querySelectorAll('.saint-header .header-page-title').forEach(slot => {
      const heading = slot.querySelector('h1');
      if (!heading) return;
      // Back to the stylesheet's size first, so a window that grew lets a name
      // that was shrunk earlier grow with it again. The measurement below is
      // taken while the name is still one line, which is what makes it mean
      // "how wide this name wants to be".
      heading.style.fontSize = '';
      heading.classList.remove('is-wrapped');
      const available = slot.clientWidth;
      const natural = heading.scrollWidth;
      if (!available || !natural || natural <= available) return;

      // The display face carries letter-spacing, which leaves a sliver of air
      // after the last glyph that still counts as width. Aim a few pixels short
      // so a name that "just fits" is not touching the candle beside it.
      const room = Math.max(0, available - 6);
      const ceiling = parseFloat(getComputedStyle(heading).fontSize);
      const fitted = ceiling * room / natural;
      // Past roughly a third off, shrinking stops reading as a masthead and
      // starts reading as a caption. A long name takes a second line instead.
      const floor = ceiling * 0.68;
      if (fitted >= floor) {
        heading.style.fontSize = Math.floor(fitted) + 'px';
        return;
      }

      heading.classList.add('is-wrapped');
      heading.style.fontSize = Math.floor(floor) + 'px';
      // Wrapping is not always enough: "Herramientas" is one word and stays
      // one word. Measure the longest line it actually produced and take the
      // size down again if that line is still wider than the slot.
      const lines = document.createRange();
      lines.selectNodeContents(heading);
      const widest = lines.getBoundingClientRect().width;
      if (widest > room) {
        heading.style.fontSize =
          Math.max(14, Math.floor(floor * room / widest)) + 'px';
      }
    });
  }

  let titleFitQueued = false;
  function scheduleTitleFit() {
    if (titleFitQueued) return;
    titleFitQueued = true;
    // A frame late on purpose: the observer below decorates the heading in a
    // microtask, and measuring before that would size the undecorated text.
    requestAnimationFrame(() => { titleFitQueued = false; fitHeaderTitles(); });
  }

  window.PortalLocale = { get locale() { return locale; }, setLocale, convert };
  document.addEventListener('portal-locale-change', scheduleTitleFit);
  window.addEventListener('resize', scheduleTitleFit);
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(scheduleTitleFit);
  document.addEventListener('DOMContentLoaded', () => {
    mountControl(); setLocale(locale, false); loadBadgeLocale(); loadFooterAddress();
    scheduleTitleFit();
    new MutationObserver(records => records.forEach(record => {
      if (record.type === 'characterData' && !skipped(record.target.parentElement)) {
        // Ignore the echo of our own write; anything else means the page is
        // saying something new, so what was remembered for it is stale.
        if (record.target.nodeValue === lastWritten.get(record.target)) return;
        sourceText.delete(record.target);
        renderText(record.target);
      }
      if (record.type === 'childList') { translateText(record.target); translateAttrs(record.target); }
    })).observe(document.body, {subtree:true, childList:true, characterData:true});
  });
})();
