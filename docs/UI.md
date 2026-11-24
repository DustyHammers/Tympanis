# Editor

The approved mockups (Kris, 2026-11-24) are drawn by `docs/ui/tympanis_mockup.py` (reference only; its paths point at a Cymbalis checkout). The approved DRUM page is the one without the Velocity Sensitivity knob: LEVEL at x 637 and PAN at 937 (the script's last version draws the Velocity Sensitivity variant, which waits for that parameter).

Everything is in design units of a 1536 x 1024 artwork, scaled with one transform; the editor resizes at 3:2 from 960 x 640 to 1536 x 1024 (as Cymbalis). Look, fonts, palette, knob filmstrip with position arcs, boxes, popups and the DHammers logo are Cymbalis's (its `docs/UI_SPEC.md`), the code mostly ported from its `PluginEditor.cpp`.

## Assets (`resources/ui/`)

| File | |
|---|---|
| `TYMPANIS_Background.png` | Cymbalis's background with its title painted out and TYMPANIS / MAPLE MODELING INSTRUMENT drawn in (`tools/ui/make_background.py`) |
| `knob.png` | Cymbalis's knob filmstrip, 91 frames of 128 x 128 |
| `Carlito-Regular.ttf`, `Carlito-OFL.txt` | UI font (SIL OFL) |
| `PinyonScript-Regular.ttf`, `PinyonScript-OFL.txt` | DHammers logo (SIL OFL) |

## Pages

Common: the title (in the background), the tabs DRUM / KIT / TUNING (y 258; the active one bright, a bronze line under it), MIDI CHANNEL (a 4 x 4 grid of channels), the DHammers logo. On DRUM and TUNING: the DRUM box (the 17 drums, two columns) and the spec line under it (`13" × 10"  ·  6 LUGS`).

- **DRUM**: the selected drum's wireframe (area 400,40 to 1180,750); the right column's ARTICULATION and MIDI NOTE rows (HEAD, RIM SHOT, SIDE STICK as the drum has them; a row lights while its hit's flash lasts); STRIKE POSITION (1349, 642; hidden on the bass drums, which have none); MAIN VOLUME (1350, 881); the drum's LEVEL (637, 881) and PAN (937, 881). A dragged knob shows its value in place of its label; double-click resets; no mouse wheel.
- **KIT**: the kit's wireframe in the approved layout (Snare 1 is drawn for Snare 1 and 2; Snare 3, the gong drum and the octobans are not drawn yet but play). A click on a drum selects it (a click on the selected Snare 1 selects Snare 2); the selected drum is drawn 35 % brighter and named top right.
- **TUNING**: the selected drum's TUNE, LUG DETUNE, DECAY, STRIKE POSITION (not on the bass drums), SIZE, LEVEL and PAN, and the whole kit's SNARE BUZZ and MAIN VOLUME, as sliders with the value in a box (double-click it to type one) and a reset arrow; a bronze mark at a row whose value is not the default, MODIFIED when any is; RESET DRUM, RESET ALL (every parameter). A small wireframe of the drum in the left column.

## Wireframes (`plugin/ui/`)

`DrumModel` is the mockup's geometry ported line for line (no JUCE): shells in British racing green, two-tier die-cast hoops with ears, Starclassic bullet lugs, the kicks' wood hoops and claws, the head mesh (25 rings x 96 points) that flexes in membrane modes J_m after a hit and flashes where the stick lands. Sizes as drawn: `drumLook()` (Snare 4 a 10" snare, Tom 5 a 16" floor tom; the depths of Snare 3, the gong drum and the octobans are a guess, to be confirmed). The drawing does not follow the Size parameter.

`WireRaster` draws it in software: anti-aliased lines combined by their brightest value, the side hardware only outside the drum's body, the kit drums hiding what is behind them, the mockup's bloom, the flash. `WireView` keeps the still drawing per drum, selection and size, in physical pixels (sharp on Retina), and after a hit draws only the moving head, in its own box, at 30 Hz for 2 s; then its timer stops. Times on the CI's Apple M1 (virtual), from `tympanis_snapshot`:

| | 1x (1536 x 1024) | 2x (Retina) |
|---|---|---|
| DRUM page still (another drum, resizing) | 15 ms | 45 ms |
| DRUM page moving frame | 4 ms | 8 ms |
| KIT page still (opening the page, resizing) | 39 ms | 98 ms |
| KIT page, another drum selected | | 9 ms |
| KIT page moving frame | 1 ms | 3 ms |

At 30 Hz, 8 ms a frame is about a quarter of the message thread while a head moves on a Retina display.

## Hits

The audio thread writes one event per strike (row, velocity) into a 64-event FIFO (`TympanisProcessor::popHit`); the editor reads it on its 30 Hz timer. Head hits land at the drum's Strike Position (the bass drums near the centre), rim shots at the edge, side sticks at the edge and softer; the angle is the side nearest the drummer, give or take 35 degrees.

## Tools

`tympanis_snapshot OUT_DIR` renders every page (and every drum on the DRUM page) to PNG files and prints the drawing times. CI runs it for the times only.
