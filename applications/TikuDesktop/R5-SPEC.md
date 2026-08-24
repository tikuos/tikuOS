# The R5 look — measured spec

The interface layer draws every pixel, and this table is the default
theme rather than the only one — `../kits/interface/tiku_theme.c` holds
these values in code, and every semantic colour reads through it, so
another table recolours every widget without moving any layout at
all.  Values come from the documented BeOS API
(the `tint_color` constants and the standard control metrics), not from
Haiku source.  Where a number is inference rather than documentation it
is marked (~) and wants a screenshot calibration pass.

## Tints

BeOS derives every shade from one panel colour by tinting:

    tint < 1 (lighten):  out = 255 - (255 - c) * tint
    tint > 1 (darken):   out = c * (2 - tint)

| constant          | tint | on bg 216 |
|-------------------|------|-----------|
| B_LIGHTEN_MAX     | 0.00 | 255       |
| B_LIGHTEN_2       | 0.70 | 228       |
| B_LIGHTEN_1       | 0.90 | 220       |
| B_NO_TINT         | 1.00 | 216       |
| B_DARKEN_1        | 1.02 | 212       |
| B_DARKEN_2        | 1.06 | 203       |
| B_DARKEN_3        | 1.07 | 201       |
| B_DARKEN_4        | 1.08 | 199       |
| B_DARKEN_MAX      | 2.00 | 0         |

## Palette

| role                | value           | note                        |
|---------------------|-----------------|-----------------------------|
| panel background    | 216,216,216     | the BeOS grey               |
| panel text          | 0,0,0           |                             |
| document background | 255,255,255     | list and text-entry fields  |
| window tab (active) | 255,203,0 (~)   | the yellow                  |
| window tab (idle)   | 232,232,232     | tint of panel               |
| tab text            | 0,0,0           |                             |
| selection           | 51,102,152 (~)  | classic desktop blue        |
| selection text      | 255,255,255     |                             |
| keyboard focus ring | 0,0,229 (~)     | navigation blue             |
| menu background     | 216,216,216     |                             |
| menu selection      | 51,102,152 (~)  |                             |
| desktop backdrop    | 51,102,152 (~)  | the classic blue-grey       |

## Bevel language

The whole look is 1-px light/shadow pairs around a flat face.  Raised
means light on top+left, shadow on bottom+right; sunken is the reverse.
Nothing is antialiased except glyphs; nothing has a gradient.

    raised outer   light = LIGHTEN_MAX (255)   shadow = DARKEN_2 (203)
    raised inner   light = LIGHTEN_1  (220)    shadow = DARKEN_1 (212)
    sunken field   shadow = DARKEN_3 (201) top+left,
                   light  = LIGHTEN_MAX bottom+right
    frame line     DARKEN_4 (199) for control outlines

## Metrics

| element            | size                                          |
|--------------------|-----------------------------------------------|
| UI font            | 12 px sans (Swiss721 originally; DejaVu here) |
| button height      | 24 px, min width 75 px, 1-px rounded corners  |
| button label inset | 10 px horizontal                              |
| default button     | +3 px ring around the frame                   |
| checkbox / radio   | 13 x 13 px box, 4 px gap to label             |
| text control       | 20 px high, sunken, 2 px text inset           |
| scrollbar          | 15 px wide/high; arrow buttons square         |
| scrollbar thumb    | knurled: 3 grip lines at the centre           |
| menu bar height    | 18 px; item padding 8 px                      |
| window tab         | 21 px high, starts 4 px from the left edge,   |
|                    | width fits the title + 24 px                  |
| window border      | 5 px sides and bottom, 1-px dark outline      |
| list row height    | font height + 4 px                            |

## Calibration status (updated after S1)

Structure and tints: from the documented API — high confidence.
Marked (~) values and the exact tab geometry: inference, to be checked
against an R5 screenshot before S1 is called done.
