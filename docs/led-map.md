# Santa Muerte LED map

Eleven WS2812-family (5050) pixels on GPIO17, traced from `docs/images/badge-scan.jpg`
(1594 x 3210, front side, board upright).

Coordinates are the centre of each package, in scan pixels.

## Halo — 8 pixels

Arranged as solo / pair / pair / pair / solo, reading clockwise from the
figure's left shoulder.

| Position | x | y |
|---|---|---|
| left shoulder (solo) | 509 | 632 |
| upper left (pair, lower) | 469 | 432 |
| upper left (pair, upper) | 509 | 308 |
| crown (pair, left) | 682 | 197 |
| crown (pair, right) | 816 | 192 |
| upper right (pair, upper) | 993 | 303 |
| upper right (pair, lower) | 1060 | 423 |
| right shoulder (solo) | 1016 | 619 |

## Hands — 3 pixels

A triangle over the praying hands. The small part directly above the top
pixel is a decoupling capacitor, not an LED.

| Position | x | y |
|---|---|---|
| top | 780 | 958 |
| lower left | 712 | 1078 |
| lower right | 857 | 1080 |

## Chain order — verified 2026-09-08

The data chain runs under the soldermask and could not be read off the scan, so
it was measured on hardware with the identify frames (below). Strand index to
physical position:

| Strand | Position | x | y |
|---|---|---|---|
| 0 | halo, left shoulder | 509 | 632 |
| 1 | halo, left lower pair | 469 | 432 |
| 2 | halo, left upper pair | 509 | 308 |
| 3 | halo, crown left | 682 | 197 |
| 4 | halo, crown right | 816 | 192 |
| 5 | halo, right upper pair | 993 | 303 |
| 6 | halo, right lower pair | 1060 | 423 |
| 7 | halo, right shoulder | 1016 | 619 |
| 8 | hands, top | 780 | 958 |
| 9 | hands, lower **right** | 857 | 1080 |
| 10 | hands, lower **left** | 712 | 1078 |

The halo is sequential, clockwise from the left shoulder — the reading order
this document already used. The exception is the pair below the hands: strand 9
is the lower **right** pixel and strand 10 the lower **left**, the reverse of
the order the Hands table lists them in. `CHAIN` in `data/led.html` carries
exactly this, which is why it is `[0,1,2,3,4,5,6,7,8,10,9]` and not identity.

Left and right are the viewer's, facing the badge front-on.

### Re-checking it

Plug the badge into USB, open a serial monitor at 115200, and press `1`, `2`,
`3` — each holds four pixels (three on the last frame) in red / green / blue /
white at low brightness, with everything else dark. `0` releases the strip back
to its own pattern. The same frames are at
`http://10.10.10.100/api/led/identify?frame=1` for driving from a phone.

Low brightness is deliberate: a lit WS2812 blooms in a camera and neighbouring
hues stop being tellable apart. Red, green, blue and white survive any
exposure, which is what makes three photographs enough for eleven pixels.

## Where these coordinates are used

`data/led.html` draws the live preview over `data/assets/badge-figure.png`,
which is the badge's own artwork rather than anything derived from the scan.

`LED_SITES` still holds the coordinates in this document's frame — the scan's
`1594 x 3210` — so the table above stays the single source of truth. The `#leds`
group carries one affine that maps that frame onto the artwork's `208 x 287`:

    transform="matrix(0.241181 0.007313 -0.009401 0.250063 -75.2337 -29.5942)"

It was fitted by maximising ink overlap between the scan and the artwork
(Dice 0.90), not by eye. The fit has to be affine rather than a plain scale
because the scan is a photograph of a board lying on a table: its halo is a
slight ellipse, so a similarity transform leaves the halo pixels sitting at
visibly uneven radii. If the artwork is ever replaced, re-fit this matrix and
change nothing else.

Radii in the drawing code (`glow.r`, `chip.r`) are in scan units too; the
group's transform scales them along with the positions.

`CHAIN` next to `LED_SITES` maps strand index to entry in that list, and now
holds the verified order above. If the board is ever revised, that array is the
only thing to change — the firmware and the coordinates both stay as they are.

Per-pixel colour comes from `GET /api/led/pixels`, which reads the strip's own
buffer with `getPixelColor()` and returns eleven `RRGGBB` values. The page
polls it eight times a second while it is on screen, so the preview shows the
firmware's real output rather than a JavaScript re-implementation of the eight
patterns.
