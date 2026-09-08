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

## Unknown: chain order

Which physical pixel is index 0 cannot be read off the scan — the data chain
runs under the soldermask. Determine it on hardware before the simulator is
worth trusting.
