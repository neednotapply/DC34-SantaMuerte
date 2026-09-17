# Santa Muerte Badge

Firmware for the Sneakreaper Industries "Santa Muerte" badge (DEF CON 34).

The badge runs as a self-contained access point. Join it and a captive portal
opens an anonymous message board; the same server also hosts the LED studio and
the NFC workbench.

| | |
|---|---|
| `src/main.cpp` | Eleven-pixel LED engine, eight patterns, settings restored at boot. |
| `src/wifi.cpp` | Access point, captive portal, HTTP server and every API. |
| `src/board.cpp` | The message board's ring storage on LittleFS. |
| `src/badge_settings.cpp` | NVS: Wi-Fi credential, LED state, NFC mode. |
| `src/nfc.cpp` | PN532 reader, writer, and NFC Forum Type 4 tag emulation. |
| `src/usb_hid.cpp` | Composite USB HID: keyboard/mouse/media/system controls, the Ducky-Script runner, and payload storage. `src/ducky_map.h` holds the pure key map. |
| `data/` | The five pages served to a joined client, plus the shared theme and locale switch. |
| `tests/` | Host-side checks for the board's ring and the Ducky-Script key map. |
| `docs/led-map.md` | Where each of the eleven pixels sits on the PCB. |

## Joining the badge

The SSID is `Santa Muerte` followed by the last four hex digits of the module
MAC, so badges stay distinguishable in a crowded room. The password is generated on
first boot: three Spanish words in CamelCase, held in NVS and never regenerated.

## USB Altar Console

USB is the badge's local, physical-admin console. It is a plain, line-oriented
serial menu: it prints a numbered list, then waits for a selection followed by
Enter. It stays available whether the badge is running its own AP or has joined
saved Wi-Fi.

1. Plug the badge into a computer with a USB-C cable.
2. Open any serial terminal at **115200 baud** — PlatformIO Monitor, Arduino
   IDE, PuTTY, `screen`, or a phone serial application all work.
3. Press Enter to print the main menu if the terminal attached after boot.
   Choose a number and press Enter; `0` goes back (to the main menu from a
   top-level section) and `?` shows help. No cursor keys, full-screen terminal support, or live redraws are
   required.

PlatformIO's `pio device monitor -b 115200` remains convenient, but it is not
required. Any serial terminal at **115200 baud, 8 data bits, no parity, 1 stop
bit, and no flow control** works. For example: `screen /dev/ttyACM0 115200` is
common on Linux, `screen /dev/cu.usbmodem… 115200` is included with macOS, and
PuTTY's **Serial** connection (select `COMx`, set 115200) is a simple Windows
option. Turn local echo off if the terminal offers that setting; the badge
echoes input itself.

The main menu uses `1`–`6` for its sections. Inside a section, the printed
number means that section's action — for example `3` changes the AP password on
Network, while LED Tools lists its animations and controls numerically. `?`
opens help; `0` goes back; and Network option `6` briefly reveals
masked credentials.

Network mode is deliberately exclusive: choose **Santa Muerte AP** or saved
**saved Wi-Fi**, never both. Choosing an action carries it out: the menu
selection is the instruction, so nothing asks a second time. The exceptions are
the three that cost work nobody can get back — erasing the field note ring,
rebooting the badge, and powering off the attached computer — which still want
a `y` or `n` answer followed by Enter.

On every boot, the badge tries its remembered Wi-Fi networks (most recently
used first) for up to 30 seconds, even if it was last running its own AP. It
then restores its last configured AP automatically, so recovery never requires
waiting minutes or connecting a cable.

**A network is remembered only once it has actually been joined.** Submitting
one starts a trial: the credentials stay in RAM, the badge tries to associate
for 20 seconds, and only an association writes them to NVS. A typo, a wrong
passphrase, or a network that is not on air is discarded, so the list the badge
retries on every boot holds nothing but networks that have worked. Both
interfaces say which network is being tried and whether it was kept — the
portal in the status block, the serial console on the **Network** screen.

A remembered network may be **open**. Leave the password empty — blank in the
portal's field, a bare Enter at the serial prompt — and the badge joins
without one and remembers it on the same terms as any other network. A password
that is not empty still has to satisfy WPA2's 8 to 63 printable-ASCII rule.

Neither field is pre-filled with the network you are on. The connected and
remembered networks are named just above the form, and a box that came back
carrying an SSID but a blank password was one stray click away from rewriting a
WPA network as an open one.

The USB menu includes live LED controls and identify frames; NFC reading,
writing, auto-scan, and emulation; and recent text notes, creating
text-only notes, and clearing the ring. Drawings, photo uploads, and
firmware flashing remain web/host workflows rather than USB TUI features.

### Field Note image previews

The serial menu lists recent Field Notes with four-digit IDs. Enter one such
ID (for example `0001`) to read the full text in the console. If that note has
an image, the badge decodes its JPEG itself and prints a compact color Unicode
preview beneath the note. This uses ordinary ANSI colors and block characters;
there is no host application, image transfer, terminal-graphics extension, or
installation step.

### Menu map

- **Dashboard** — active network/IP/hostname, LED and NFC state, field-note-ring
  use, heap, uptime, and the last TUI notices.
- **Network** — choose the exclusive AP/home mode, change the AP password,
  save a home SSID/password (empty for an open network), toggle the AP hidden
  setting, and inspect the connection state.
- **LED Tools** — choose any of the sixteen patterns, set `#RRGGBB`,
  brightness, speed, and step through physical LED identify frames.
- **NFC Tools** — queue a read or text/URL write, turn auto-scan on/off,
  emulate text, URL, or the badge's Wi-Fi record, and stop emulation.
- **Field Notes** — browse recent IDs, enter a four-digit ID to inspect one
  note (including multiline text), add a text note, or erase the board, which
  asks first. Image notes render as a compact color Unicode preview directly
  in the serial terminal.
- **System** — view the bounded TUI event log, enter raw log streaming,
  temporarily reveal credentials, and reboot.
- **USB Tools** — send direct media, presentation, and supported system HID
  controls; choose separate short/long BOOT actions; start or stop the USB
  Wi-Fi bridge; or open the stored USB scripts. Running a script still requires
  `y/n`, and option `17` stops it. The screen shows the host's keyboard lock
  LEDs live.

**Tap it.** A freshly flashed badge emulates an NFC tag carrying its own
credentials. Android reads the Wi-Fi Simple Configuration record natively and
offers to join; iOS does not join Wi-Fi from NFC at all, so the same tag also
carries a plain-text record an NFC reader app can show.

After that first boot the NFC radio is remembered like everything else: leave
it emulating a Text or URL record, or stopped, and that is what it comes back
as. Set it back to the Wi-Fi record from `/nfc` whenever you want it. Sharing
credentials is no longer NFC's job alone, which is why it is free to be
something else — the USB Altar remains available either way.

**Or read it on screen.** `/` shows the password in the clear once you are
joined.

Once joined, a phone's own connectivity check is redirected to the board, which
is what raises the "sign in to network" notification.

## USB HID payloads (rubber ducky)

The badge is an ESP32-S3, whose USB-OTG peripheral can present a **composite**
device: the USB Altar console (CDC serial) and a **HID keyboard, mouse, media,
and system-control interface** share the one USB-C cable. So the badge can act as a
scriptable keystroke injector — a "rubber ducky" — while the console stays live.

This needs the TinyUSB stack instead of the fixed USB-Serial-JTAG bridge, so the
firmware builds with `ARDUINO_USB_MODE=0`. One consequence: there is no longer an
always-on JTAG serial for flashing. If an upload cannot auto-reset the board into
download mode, hold **BOOT** and tap **RESET/EN**, then flash.

**Nothing types on its own,** but once a payload runs it types for real, so only
plug the badge into a computer you own. A payload can be fired two ways: from the
physical **Payloads** screen on the USB Altar, where choosing it runs it
straight away; or from the Wi-Fi portal at **`/scripting`**, a
visual **DuckyScript builder**: drag or tap command blocks (each explained, with
editable fields) into a stack that compiles to DuckyScript live, then **Send to
device**. The portal also uploads, loads and deletes stored payloads — shown in a
feed like the field notes board, and **Load** reopens one back in the builder. Note that portal firing means anyone joined to the badge's AP
can inject keystrokes into the attached machine — treat the AP as trusted.

Payloads are small text files on LittleFS (`/payloads`, up to sixteen), written
in a Ducky-Script subset:

| Command | Meaning |
|---|---|
| `REM text` | A comment; does nothing. |
| `STRING text` / `STRINGLN text` | Type literal text (`LN` presses Enter after). |
| `DELAY ms` / `DEFAULTDELAY ms` | Pause once / between every command. |
| `ENTER TAB ESC SPACE BACKSPACE DELETE` … | Named keys, plus `HOME END PAGEUP PAGEDOWN`, the arrows, `F1`–`F12`, `CAPSLOCK`. |
| `GUI r`, `CTRL ALT DELETE` | A modifier chord (`GUI`/`CTRL`/`ALT`/`SHIFT` + a key). |
| `REPEAT n` | Repeat the previous command n more times. |
| `MOUSEMOVE dx dy`, `MOUSECLICK left\|right\|middle`, `MOUSESCROLL n` | Mouse control. |
| `MUTE VOLUP VOLDOWN PLAY NEXT PREV STOP` | Consumer/media keys. |
| `LEDWAIT CAPS\|NUM\|SCROLL` | Park until the host toggles that keyboard lock LED. |

`LEDWAIT` turns the keyboard's lock LEDs into a **return channel**: the OS reports
Caps/Num/Scroll-Lock state back to the badge, so a payload can wait for the
operator to flip Caps Lock before firing, and the Payloads screen shows the
host's locks live.

The runner never blocks the main loop — keystrokes are paced across `loop()`
iterations, so Wi-Fi, NFC and the LEDs keep running while a payload types.
Payloads assume a **US-QWERTY** host layout.

### Host controls and the BOOT button

`/usb` is the host-controls workspace: media, slides, supported system actions,
and independently saved short/long BOOT-button assignments. The same actions
are also available from the USB Altar under **USB Tools**. The computer sees
them as normal standard HID reports; **USB Serial remains available at the same
time**. Power-off is deliberately available only as a confirmed action, never
as a button binding.

`/scripting` is the dedicated DuckyScript workspace. It owns the visual
builder, saved scripts, and script execution so host controls can stay compact.

### WiFi Tethering

The USB device can use either a real **NCM network interface** or the
**Field Notes Drive**. The WiFi Tethering profile presents serial + NCM; the Drive
profile presents serial + HID + storage. The Drive exposes **two** volumes: a
read-only **Field Notes** disk that exports a `NOTES` folder with one `.TXT` and
(where present) matching `.JPG` artifact per note, plus `SCRIPTS`/`BADUSB`/`NFC
Log` folders; and a writable **DROP BOX** disk (see below). The ESP32-S3
cannot expose both descriptor sets together. Select **WiFi Tethering** in
USB Tools, then start the bridge after the badge has joined its saved Wi-Fi.
The computer then receives its IP configuration from that upstream Wi-Fi
network through the badge.

#### DROP BOX (writable drive)

Alongside the read-only Field Notes disk, the Drive profile presents a small
read/write **DROP BOX** volume with `DUCKY/` and `BADUSB/` folders. Drag a
DuckyScript into `DUCKY/` or a BadUSB script into `BADUSB/` and the badge saves
it into the same `/payloads` store the web portal writes — no copy-pasting into
`/scripting`. The file's name (minus a `.txt`/`.dd`/`.ducky`/`.badusb`
extension) becomes the script's name; the portal's limits apply (max 2048 bytes,
16 scripts per format). **Eject the drive — or just wait a couple of seconds
after dropping files — and the badge imports them;** an imported script then
appears on the read-only Field Notes disk and in the USB Tools menu. The host
owns the FAT and the badge only reads what you drop, so there is no risk of the
two corrupting each other.

The volume is backed by an `ffat` flash partition (`partitions.csv`) that
reclaims the firmware's unused second OTA app slot. Because `app0` and the
LittleFS partition keep their original offsets, adopting the new partition table
leaves the badge's stored notes, scripts, and NFC log in place — but back them
up over HTTP before the first flash anyway (see below), as a net.

**There is nothing to start.** Selecting the profile is the instruction to
tether: the bridge attaches as soon as the badge is on saved Wi-Fi and detaches
when that link drops. It used to need a second, separate switch, which left the
two states impossible to tell apart from outside — either way there was no badge
access point to join and a host that saw an adapter carrying nothing.

For the same reason the profile does not sit there without an upstream. If the
badge has no saved Wi-Fi, or gives up trying to reach it, it returns to the
Field Notes Drive and restarts, which is worth having plugged in with no network
at all. While the bridge is attached it owns traffic for the saved Wi-Fi
station, so the badge itself is not using that link; the badge AP remains the
safe path to the local portal.

To leave WiFi Tethering by hand, hold **BOOT** until the amber acknowledgement
plays, then release it. In this profile the hold is the way out and takes
precedence over whatever else the button is bound to — that binding still works
in the Field Notes Drive profile, where the badge spends its time.

### USB product id

The badge enumerates under Espressif's vendor id with a product id of its own,
`0x534D`, rather than the `0x1001` an ESP32-S3 sketch inherits by default. That
default is the product id of the chip's own ROM USB-Serial/JTAG unit, and
sharing it lets any host driver rule written for that unit match the badge
instead. Zadig, for example, pins WinUSB to `VID_303A&PID_1001&MI_02` the first
time anyone sets up JTAG debugging — and both profiles put a badge interface on
`MI_02`: the network adapter under WiFi Tethering, and the **serial console**
under Field Notes Drive, since descriptors are emitted in a fixed order and
mass storage takes `MI_00` ahead of it. Whichever one lands there is claimed by
the JTAG driver and vanishes from the host. A product id of the badge's own
keeps those rules where they belong.

## Pages

- `/notes` — Field Notes. Leave a drawing, text, both, or try a photo
  upload from a desktop browser.
- `/led` — the badge drawn with its eleven pixels live on it, a colour wheel,
  and a list of sixteen animations. Several of them are written for this
  layout specifically: the halo is a ring of eight and the hands are a triangle
  of three pointing up into it, so **Ofrenda** raises light from the hands and
  floods the halo, **Aureola** opens from the hands out to the crown, and
  **Encuentro** sends two heads up opposite sides of the ring to meet there.
  Everything is saved automatically.
- `/nfc` — read and write tags, emulate one, or turn on **auto-scan**:
  while it is on the reader polls by itself and every tag it reads lands in
  the NFC log.
- `/network` — the badge's Wi-Fi credentials and hidden-SSID switch. The
  former `/settings` address remains as a compatibility alias.

`/` is the board: joining the badge and following the sign-in notification
lands on the field notes, not on a setup form.

## No installable app

The portal is served over plain HTTP at `10.69.4.20`, which browsers do not
treat as a secure context. Service workers and the install prompt are only
available to secure contexts, so a manifest and a service worker could never
have worked here — they were dead weight, and removing them gave the board
back roughly 360 KB of its picture ring.

## The board

Posts live in a ring of 512 fixed-size slots in one preallocated file, and
pictures in a second ring of 64. The oldest entry is simply the slot the next
write lands on, so the board prunes itself, never fragments, and cannot run the
filesystem out of space mid-post.

A drawing costs roughly two hundred times what a line of text costs, which is
why the two rings are different lengths: the board keeps a deep history of
words and a shallow one of drawings. An old post keeps its text long after its
drawing has been overwritten, and says so rather than serving somebody else's.

Anything joined to the badge is on an access point with no uplink. The badge
has no image codec and no memory to run one: the posting browser turns a
drawing or selected photo into a 384-pixel JPEG and walks quality down until
it fits under 12 KB, and the badge stores those bytes verbatim after checking
they begin like a JPEG. Drawing remains available in the captive-portal
window, while photo-picking depends on the browser providing a file picker.

The badge has no clock. A post's time is whatever the posting browser claimed.

Run the host-side tests on any machine with a C++ compiler:

```
g++ -std=gnu++17 -I tests/shim -I src tests/board_test.cpp src/board.cpp -o /tmp/board_test && /tmp/board_test
```

```
g++ -std=gnu++17 -DARDUINO_USB_MODE=0 -DSM_USB_DRIVE=1 -I tests/shim -I src tests/usb_drive_test.cpp src/usb_drive.cpp -o /tmp/usb_drive_test && /tmp/usb_drive_test
```

The drive test also writes the rendered FAT12 image to `/tmp/smdrive.img`, so a
real checker can be pointed at the same bytes: `fsck.fat -n /tmp/smdrive.img`.

# Flashing Instructions:
1) Extract repo zip file or pull down repo to local folder
2) Download VS-Code (or open it)
3) Install "PlatformIO IDE" VS-Code extension
4) Navigate to "PlatformIO Home" via the extension
5) Select "Open Project" and select the project folder
6) Wait for all the project dependencies and assorted files to finish downloading
7) Go to `View` > `Command Pallette` or use the shortcut `Ctrl + shft + p ` and type ` PlatformIO: Open Core CLI`, hit enter.
8) Ensure the badge is plugged in: hold the "Boot" button on the back of the badge as you plug in the  USB-C cable, the button can be released once the two green LEDs on the back of the board light up. Then run these commands IN ORDER
```
pio run --target erase
pio run --target clean
pio run --target upload
pio run --target uploadfs
```
 You should not receive any errors, after running all commands the badge is flashed.

NOTE: you may need to hit the "Reset" button on the back of the badge after flashing, If the LEDs don't immediately turn on after being flashed, hitting reset will fix it.

If you have any issues please open a GH issue or DM @Solaris on the [discord](https://discord.gg/thesafehouse)
