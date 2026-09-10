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
| `data/` | The four pages served to a joined client, plus the shared theme and locale switch. |
| `tests/` | Host-side checks for the board's ring, pruning and sanitizing. |
| `docs/led-map.md` | Where each of the eleven pixels sits on the PCB. |

## Joining the badge

The SSID is `Santa Muerte` followed by the last four hex digits of the module
MAC, so badges stay distinguishable in a crowded room. The password is generated on
first boot: three Spanish words in CamelCase, held in NVS and never regenerated.

## USB Altar TUI

USB is the badge's local, physical-admin console. It starts its **USB Altar**
dashboard automatically after boot and stays available whether the badge is
running its own AP or has joined home Wi-Fi.

1. Plug the badge into a computer with a USB-C cable.
2. Open any serial monitor at **115200 baud** — the PlatformIO monitor
   (`pio device monitor`), Arduino IDE, PuTTY, `screen`, or a phone serial
   application. A full ANSI terminal is recommended.
3. The dashboard opens on its own. Press `p` if the serial program is a basic
   monitor: that switches to the same plain-text menu with no ANSI escapes.

Move between sections from the dashboard with the arrow keys or `j`/`k` — the
`>` marks the selection — and press `Enter` to open one. Number keys are never
section jumps: on any screen a digit is one of that screen's own actions, so
`3` means "Change AP password" on Network and "Chase" in LED Tools. `?` opens
help; `Esc`/`q` returns to the dashboard; `r` temporarily streams raw
diagnostics until the next key; and `v` briefly reveals masked network
passwords on the Network page.

Network mode is deliberately exclusive: choose **Santa Muerte AP** or saved
**home Wi-Fi**, never both. Changes, credential saves, clearing offerings, NFC
writes/emulation, and reboot all require `y/n` confirmation. If a terminal
session gets lost in a menu, capital `W` starts the AP/home-mode recovery
switch from anywhere except a text entry prompt. It is capital so it cannot
collide with the lowercase per-screen menus, and it avoids `A`-`D` because
those are the bytes that terminate an arrow-key sequence.

The USB menu includes live LED controls and identify frames; NFC reading,
writing, offering capture, and emulation; and recent text offerings, creating
text-only offerings, and clearing the ring. Drawings, photo uploads, and
firmware flashing remain web/host workflows rather than USB TUI features.

### Menu map

- **Dashboard** — active network/IP/hostname, LED and NFC state, offering-ring
  use, heap, uptime, and the last TUI notices.
- **Network** — choose the exclusive AP/home mode, change the AP password,
  save a home SSID/password, toggle the AP hidden setting, and inspect the
  connection state.
- **LED Tools** — choose any of the sixteen patterns, set `#RRGGBB`,
  brightness, speed, and step through physical LED identify frames.
- **NFC Tools** — queue a read or text/URL write, turn NFC Offering on/off,
  emulate text, URL, or the badge's Wi-Fi record, and stop emulation.
- **Offerings** — browse the newest text posts, add one text-only offering,
  inspect ring capacity, or erase the board after confirmation.
- **System** — view the bounded TUI event log, enter raw log streaming,
  temporarily reveal credentials, and reboot.

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

## Pages

- `/board` — the anonymous board. Leave a drawing, text, both, or try a photo
  upload from a desktop browser.
- `/led` — the badge drawn with its eleven pixels live on it, a colour wheel,
  and a list of sixteen animations. Several of them are written for this
  layout specifically: the halo is a ring of eight and the hands are a triangle
  of three pointing up into it, so **Ofrenda** raises light from the hands and
  floods the halo, **Aureola** opens from the hands out to the crown, and
  **Encuentro** sends two heads up opposite sides of the ring to meet there.
  Everything is saved automatically.
- `/nfc` — read and write tags, emulate one, or turn on **NFC Offering**:
  while it is on the reader polls by itself and every tag it reads posts its
  text to the board, credited to the reader rather than to a phone.
- `/settings` — the badge's Wi-Fi credentials and hidden-SSID switch.

`/` is the board: joining the badge and following the sign-in notification
lands on the offerings, not on a setup form.

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

Run the board's tests on any machine with a C++ compiler:

```
g++ -std=gnu++17 -I tests/shim -I src tests/board_test.cpp src/board.cpp -o /tmp/board_test && /tmp/board_test
```

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
