# Editor

The approved mockups (Kris, 2026-11-24) are drawn by `docs/ui/tympanis_mockup.py` (reference only; its paths point at a Cymbalis checkout). The DRUM page is the script's last version, with the Velocity Sensitivity knob (approved as the to-do layout, built 2026-11-24).

Everything is in design units of a 1536 x 1024 artwork, scaled with one transform; the editor resizes at 3:2 from 960 x 640 to 1536 x 1024 (as Cymbalis). Look, fonts, palette, knob filmstrip with position arcs, boxes, popups and the DHammers logo are Cymbalis's (its `docs/UI_SPEC.md`), the code mostly ported from its `PluginEditor.cpp`.

**Opening size:** A new editor with no saved size opens at 1104 x 736 (as Cymbalis; Kris, 2026-11-25), smaller where the primary display's user area leaves less than 40 px across or 200 px down for a host's window around it, never below 960 x 640 (`plugin/ui/EditorSize.h`); a session's saved size wins.

## Assets (`resources/ui/`)

| File | |
|---|---|
| `TYMPANIS_Background.png` | Cymbalis's background with its title painted out and TYMPANIS / MAPLE MODELING INSTRUMENT drawn in (`tools/ui/make_background.py`) |
| `knob.png` | Cymbalis's knob filmstrip, 91 frames of 128 x 128 |
| `Carlito-Regular.ttf`, `Carlito-OFL.txt` | UI font (SIL OFL) |
| `PinyonScript-Regular.ttf`, `PinyonScript-OFL.txt` | DHammers logo (SIL OFL) |

## Pages

Common: the title (in the background), the tabs DRUM / KIT / TUNING (y 258; the active one bright, a bronze line under it), MIDI CHANNEL (a 4 x 4 grid of channels), the DHammers logo. On DRUM and TUNING: the DRUM box (the 17 drums, two columns) and the spec line under it (`13" × 10"  ·  6 LUGS`).

- **DRUM**: the selected drum's wireframe (area 400,40 to 1180,750); the right column's ARTICULATION and MIDI NOTE rows (HEAD, RIM SHOT, SIDE STICK as the drum has them; a row lights while its hit's flash lasts); STRIKE POSITION (1349, 642; hidden on the bass drums, which have none); MAIN VOLUME (1350, 881); the drum's VELOCITY SENSITIVITY (487, 881), LEVEL (787, 881) and PAN (1087, 881). A dragged knob shows its value in place of its label; double-click resets; no mouse wheel.
- **KIT**: the kit's wireframe in the approved layout (Snare 1 is drawn for Snare 1 and 2; Snare 3, the gong drum and the octobans are not drawn yet but play). A click on a drum selects it (a click on the selected Snare 1 selects Snare 2); the selected drum is drawn 35 % brighter and named top right.
- **TUNING**: the selected drum's TUNE, LUG DETUNE, DECAY, STRIKE POSITION (not on the bass drums), SIZE (the shell's depth: Toms 1-4; greyed out on the others), LEVEL and PAN, and the whole kit's SNARE BUZZ and MAIN VOLUME, as sliders with the value in a box (double-click it to type one) and a reset arrow; a bronze mark at a row whose value is not the default, MODIFIED when any is; RESET DRUM, RESET ALL (every parameter). A small wireframe of the drum in the left column.

## Wireframes (`plugin/ui/`)

`DrumModel` is the mockup's geometry ported line for line (no JUCE): shells in British racing green, two-tier die-cast hoops with ears, Starclassic bullet lugs, the kicks' wood hoops and claws, the head mesh (25 rings x 96 points) that flexes in membrane modes J_m after a hit and flashes where the stick lands. Sizes as drawn: `drumLook()` (Snare 4 a 10" snare, Tom 5 a 16" floor tom; Snare 3's depth is a guess, to be confirmed); the depths agree with `KitMap.h`.

Size is the shell's depth on Toms 1-4 (Kris, 2026-11-25; mockups approved): the drawings follow it. On the DRUM and TUNING pages the head stays where the camera puts it and the shell grows or shrinks below; on the KIT page a rack tom's bottom stays where it rests on the kick and its head moves along its axis (down and a little back when shallower). The spec line under the DRUM box shows the depth played. Every drum on the DRUM page is drawn at one scale, the one that fits the largest (the bass drum) with the toms at their deepest, so the drums are proportional to each other; the TUNING page's small drum keeps its own scale (at its deepest).

The gong drum and the octobans (Kris's TAMA photos, mockups approved 2026-11-24):

- **Gong drum**: TAMA's 22" head mounted timpani style on a 20" x 14" shell with no bottom head. A support ring flares from the shell's top edge out to the 22" bearing edge; a thin flat counter hoop, its top below the playing surface, pulls the head's collar down over it; the lugs sit on the shell, their rods angled out to claws hooked over the hoop. On the DRUM page it tilts toward the drummer, as on its stand. Only the shell hides the hardware behind it (with the wider head, the lugs at the sides went missing). The spec line reads 22" HEAD ON A 20" x 14" SHELL.
- **Octobans**: 6" tubes 11", 14.7", 18.3" and 22" long (Octobans 2 and 3 evenly between 1 and 4; equal ratios, 13.9" and 17.5", is the alternative, not decided). The tom lug as on the toms (a shorter one looked tilted); a slimmer hoop with small ears; the head mesh draws every other ring and point (the full mesh made a moire on a 6" head). All four DRUM pages use Octoban 4's scale, so the lengths read from page to page.

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
