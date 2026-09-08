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
| `src/badge_settings.cpp` | NVS: Wi-Fi credential, LED state, NFC boot mode. |
| `src/nfc.cpp` | PN532 reader, writer, and NFC Forum Type 4 tag emulation. |
| `data/` | The four pages served to a joined client. |
| `tests/` | Host-side checks for the board's ring, pruning and sanitizing. |
| `docs/led-map.md` | Where each of the eleven pixels sits on the PCB. |

## Joining the badge

The SSID is `Santa Muerte` followed by the last four hex digits of the module
MAC, so badges stay distinguishable in a crowded room. The password is generated on
first boot: three Spanish words in CamelCase, held in NVS and never regenerated.

**Tap it.** The badge emulates an NFC tag carrying its own credentials, and
it goes back to presenting that on every boot, whatever it was emulating when
it was last powered down, so a badge can never end up unreachable. Android reads the Wi-Fi Simple Configuration
record natively and offers to join. iOS does not join Wi-Fi from NFC at all, so
the same tag also carries a plain-text record an NFC reader app can show.

**Or read it.** The dashboard shows the password in the clear, and the serial
console prints it at every boot.

Once joined, a phone's own connectivity check is redirected to the board, which
is what raises the "sign in to network" notification.

## Pages

- `/board` — the anonymous board. Text, a picture, a link, and up to eight tags.
- `/led` — patterns, colour wheel, brightness and speed. Saved automatically.
- `/nfc` — read and write tags, or emulate one.
- `/` — dashboard and Wi-Fi settings.

## The board

Posts live in a ring of 512 fixed-size slots in one preallocated file, and
pictures in a second ring of 64. The oldest entry is simply the slot the next
write lands on, so the board prunes itself, never fragments, and cannot run the
filesystem out of space mid-post.

A picture costs roughly two hundred times what a line of text costs, which is
why the two rings are different lengths: the board keeps a deep history of
words and a shallow one of images. An old post keeps its text long after its
picture has been overwritten, and says so rather than serving somebody else's.

Anything joined to the badge is on an access point with no uplink, so pictures
are uploaded rather than linked. The badge has no image codec and no memory to
run one: the posting browser scales the picture to 384 pixels on its longest
edge and walks JPEG quality down until it fits under 12 KB, and the badge
stores those bytes verbatim after checking they begin like a JPEG. A link field
is still there, but it is kept as text — following it means leaving the badge
network.

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
