# Editor

The approved mockups (Kris, 2026-11-24) are drawn by `docs/ui/tympanis_mockup.py` (reference only; its paths point at a Cymbalis checkout). The DRUM page is the script's last version, with the Velocity Sensitivity knob (approved as the to-do layout, built 2026-11-24).

Everything is in design units of a 1536 x 1024 artwork, scaled with one transform; the editor resizes at 3:2 from 960 x 640 to 1536 x 1024 (as Cymbalis). Look, fonts, palette, knobs (drawn by the plugin, as Cymbalis since 2026-11-24: a body lit from the top left, a rim, the pointer and its shadow, at a = -150 + 300 p degrees; no filmstrip) with position arcs, boxes, popups and the DHammers logo are Cymbalis's (its `docs/UI_SPEC.md`), the code mostly ported from its `PluginEditor.cpp`.

**Opening size:** A new editor with no saved size opens at 1104 x 736 (as Cymbalis; Kris, 2026-11-25), smaller where the primary display's user area leaves less than 40 px across or 200 px down for a host's window around it, never below 960 x 640 (`plugin/ui/EditorSize.h`); a session's saved size wins.

## Assets (`resources/ui/`)

| File | |
|---|---|
| `TYMPANIS_Background.png` | Cymbalis's background with its title painted out and TYMPANIS / MAPLE MODELING INSTRUMENT drawn in (`tools/ui/make_background.py`) |
| `Carlito-Regular.ttf`, `Carlito-OFL.txt` | UI font (SIL OFL) |
| `PinyonScript-Regular.ttf`, `PinyonScript-OFL.txt` | DHammers logo (SIL OFL) |

## Pages

Common: the title (in the background), the tabs DRUM / KIT / TUNING / SETTINGS (y 258, 30 units apart; the active one bright, a bronze line under it), MIDI CHANNEL (a 4 x 4 grid of channels) on the DRUM page (the drum shown's own channel, outlined in bronze when it differs from the channel set for all) and on SETTINGS (every drum's: it overrides their own); not on KIT or TUNING (Kris, 2026-11-26). The DHammers logo. On DRUM and TUNING: the DRUM box (the 15 drums, two columns; a name too wide for its cell, POPCORN SNARE, is drawn with tighter letter spacing) and the spec line under it (`13" × 10"  ·  6 LUGS`).

- **DRUM**: the selected drum's wireframe (area 400,40 to 1180,750); the right column's ARTICULATION and MIDI NOTE rows (HEAD, RIM SHOT, SIDE STICK as the drum has them; a row lights while its hit's flash lasts); STRIKE POSITION (1349, 642; hidden on the bass drums, which have none); MAIN VOLUME (1350, 881); the drum's VELOCITY SENSITIVITY (487, 881), LEVEL (787, 881) and PAN (1087, 881). A dragged knob shows its value in place of its label; double-click resets; no mouse wheel.
- **KIT**: the kit's wireframe in the approved layout (the octobans are not drawn yet but play), with a bass drum pedal on each kick, and the gong drum on the right of the floor toms, tilted toward the drummer (Kris's markup, mockup approved 2026-11-26; the view reaches x 1510 for it). A click on a drum selects it, and so does playing it (Kris, 2026-11-26: the last drum played in each screen refresh, not while a menu is open), so the DRUM page shows the drum just played; the selected drum is drawn 35 % brighter and named top right. On the other pages a hit does not change the drum.
- **TUNING**: the selected drum's SNARES (the Snares On/Off switch, on the Snare and the Popcorn Snare only, first: a short two-position slide, OFF / ON, the value box reading ON or OFF; Kris, 2026-11-26, mockup approved), WIRE TENSION (Snare Wire Tension, the same two drums, under SNARES: TIGHT .. LOOSE, 0.00 .. 1.00; Kris, 2026-11-26, mockup A approved), TUNE, LUG DETUNE (EVEN .. UNEVEN; greyed out on the bass drums, which don't get it), DECAY, STRIKE POSITION (not on the bass drums), SIZE (the shell's depth: Toms 1-4; greyed out on the others), LEVEL and PAN, and the whole kit's SNARE BUZZ and MAIN VOLUME, as sliders with the value in a box (double-click it to type one) and a reset arrow; a bronze mark at a row whose value is not the default, MODIFIED when any is; RESET DRUM, RESET ALL (every parameter). A small wireframe of the drum in the left column. The drum's rows are 56 apart from y 160 (62 before Wire Tension, so the snares' 9 rows fit without moving anything else; the same spacing on every drum). The WHOLE KIT section sits one row lower than the per-drum rows can reach (band at y 682, rows from 742; RESET DRUM and RESET ALL at y 892), on every drum, so it does not move as drums change; RESET DRUM resets the switch to ON and Wire Tension to TIGHT, and either changed counts as MODIFIED.
- **SETTINGS** (Kris, 2026-11-26, mockups approved): DRUM FINISH, how the shells are drawn (a look, not a sound): two boxes (460,150 and 730,150, 250 x 214), GREEN and NATURAL MAPLE, each with Tom 3 drawn in that finish; a click chooses one, outlined in bronze, and every page's drawings follow it. The finish is saved with the session (not a parameter); a session saved before it existed opens green. MIDI MAP (band at y 392): the drums on their own channel (or "every drum on the channel set at the top right"), PRESET (the map's name, " *" when changed; its list: GENERAL MIDI, the recent maps, LOAD...) and SAVE AS... (the Mac's save dialog); then the grid: a row per drum from y 492, 30 apart, the HEAD / RIM SHOT / SIDE STICK columns 240 apart from x 790, a note box (96 x 25, as the DRUM page's, the same menu; it opens upward near the bottom) in each, a dash where the drum has no such articulation (docs/PLUGIN.md, "MIDI maps"). No DRUM box on this page.

## Wireframes (`plugin/ui/`)

`DrumModel` is the mockup's geometry ported line for line (no JUCE): maple shells drawn as their grain (below), two-tier die-cast hoops with ears, Starclassic bullet lugs, the kicks' wood hoops and claws, the head mesh (25 rings x 96 points) that flexes in membrane modes J_m after a hit and flashes where the stick lands. Sizes as drawn: `drumLook()` (the Popcorn Snare a 10" snare, Floor Tom 1 a 16" floor tom); the depths agree with `KitMap.h`, and the editor checks the drum counts agree (`kDrumCount`). The hoop at a drum's far end (the floor toms' and rack toms' bottom hoops) is drawn where it faces the camera and, beyond the shell's outline on screen, until the shell covers it: it sticks out past the shell (Kris, 2026-11-27: the floor toms' bottom hoops stopped short on the left). On the DRUM and TUNING pages each bass drum is turned as it sits in the kit's V (Kris, 2026-11-27): Kick 1, on the left, shows its shell to the left; Kick 2, on the right, to the right.

Size is the shell's depth on Toms 1-4 (Kris, 2026-11-25; mockups approved): the drawings follow it. On the DRUM and TUNING pages the head stays where the camera puts it and the shell grows or shrinks below; on the KIT page a rack tom's bottom stays where it rests on the kick and its head moves along its axis (down and a little back when shallower). The spec line under the DRUM box shows the depth played. Every drum on the DRUM page is drawn at one scale, the one that fits the largest (the bass drum) with the toms at their deepest, so the drums are proportional to each other; the TUNING page's small drum keeps its own scale (at its deepest).

**The shells' grain** (Kris's Starclassic brochure photos, mockups approved 2026-11-26): the outer maple ply's growth rings, drawn as lines in place of the old shell mesh. The rings are levels of a field on the shell: level bands with gentle waves and seven lens-shaped "eyes" where they bulge (about 0.9" a ring). Each ring has 3 lines, bright in the early wood and darker into the late wood, its edge left black, at 0.75 of the old mesh's brightness; the burst toward the shell's ends and the light are the mesh's. The outer ply's seam runs diagonally from the top of a top lug down to the bottom of the next bottom lug (single-headed drums: down to the bottom edge), on the side facing the camera and leaning down to the left as on the brochure's shell: the grain does not line up across it, one cell (about 0.08") of black with the lines either side at 45 %. Two finishes (SETTINGS): green (body 64,158,80; highlight 128,232,150; edges 30,60,28) and natural lacquered maple (244,214,162; 255,246,226; 150,112,64); the kicks' wood hoops and the shells' edges follow the finish. Each drum's grain is the same every time (seeded from its size). It is worked out once per shell, depth and seam (on a grid of separable wave tables, then marching squares) and kept for the last 24 shells drawn; a drawing only projects and lights it. Snapshots: `settings.png`, `settings-natural.png`, `drum-tom3-natural.png`, `kit-natural.png`, `tuning-tom3-natural.png`, `drum-kick1-natural.png`.

**Snare throw-offs and air vents** (Kris's photo of his Starclassic snare, mockups approved 2026-11-26):

- **Throw-off** on the Snare and the Popcorn Snare, in the lug gap facing the camera (the grain's seam is kept out of that gap): the body on a back plate with a blank front plate (no logo), the knurled tension knob on top, the lever on its pivot with a ribbed rubber grip, the slide down to the clamp under the bottom hoop with its two screws, and the snare cord running in under the hoop. The lever follows the drum's SNARES switch: up and to the left with the snares on, down with them off (the cord slack); every page redraws when it changes. The butt end sits opposite.
- **Air vent**: one brass grommet per shell, in the lug gap facing the camera that the seam does not cross (the snares: the gap to the right of the throw-off), a little above the middle of the shell. The gong drum and the octobans have none (Kris).

The gong drum and the octobans (Kris's TAMA photos, mockups approved 2026-11-24):

- **Gong drum**: TAMA's 22" head mounted timpani style on a 20" x 14" shell with no bottom head. A support ring flares from the shell's top edge out to the 22" bearing edge; a thin flat counter hoop, its top below the playing surface, pulls the head's collar down over it. As in Kris's photo (2026-11-25): the lugs sit on the shell just under the hoop; each rod angles out from its lug through an ear on the hoop, its tip standing above the hoop, so all ten tension points show round the hoop (the lugs only on the near side). On the DRUM page it tilts to the left and toward the drummer, seen from low down (10 degrees; the photo's view mirrored, Kris's choice). Only the shell hides the hardware behind it. The spec line reads 22" HEAD ON A 20" x 14" SHELL.
- **Octobans**: 6" tubes 17.5", 18.5", 21" and 23.5" long (Kris, 2026-11-25). The tom lug as on the toms (a shorter one looked tilted); a slimmer hoop with small ears; the head mesh draws every other ring and point (the full mesh made a moire on a 6" head). All four DRUM pages use Octoban 4's scale, so the lengths read from page to page.

**Bass drum pedals** (KIT page; Kris, 2026-11-25, mockups approved): `addKickPedal` in `DrumModel`, the shape of his photos of Trick Big Foot / Black Widow pedals, in the kit's chrome. Clamped to each kick's batter hoop: a base plate wide at the hoop and tapering to the heel, a single machined post on the drummer's left with the axle's housing and knurled knob, the hub's dial ring, an arc cam with the chain down to a toe link, a coil spring low at the front right with its lever, a flat footboard 13" x 2.75" (full width, then a tongue on the right clear of the post; no logo) hinged at the heel with end caps and two screws, and the felt beater on its clamp with a memory lock. The fixed parts are in the kick's still drawing; the moving parts (footboard, toe link, cam, chain, spring, beater) are drawn over the frame in their own box. On a kick hit the beater is against the head at the hit, the toe down, the cam turned, the spring compressed, and back at rest by 0.2 s (`pedalSwing`). Snapshots: `kit-pedals.png`, `kit-kick-hits.png`.

`WireRaster` draws it in software: anti-aliased lines combined by their brightest value, the side hardware only outside the drum's body, the kit drums hiding what is behind them, the mockup's bloom, the flash. `WireView` keeps the still drawing per drum, selection and size, in physical pixels (sharp on Retina), and after a hit draws only the moving head, in its own box, at 30 Hz for 2 s; then its timer stops. Times on the CI's Apple M1 (virtual), from `tympanis_snapshot`:

| | 1x (1536 x 1024) | 2x (Retina) |
|---|---|---|
| DRUM page still (another drum, resizing) | 22 ms | 32 ms |
| DRUM page moving frame | 2 ms | 4 ms |
| KIT page still (opening the page, resizing) | 70 ms | 117 ms |
| KIT page, another drum selected | | 13 ms |
| KIT page moving frame | 1 ms | 3 ms |
| SETTINGS page still | 7 ms | |

With the grain (2026-11-26) the stills went from 15 to 22 ms (DRUM) and from 39 to 70 ms (KIT) at 1x; the moving frames did not change.

At 30 Hz, 8 ms a frame is about a quarter of the message thread while a head moves on a Retina display.

## Hits

The audio thread writes one event per strike (row, velocity) into a 64-event FIFO (`TympanisProcessor::popHit`); the editor reads it on its 30 Hz timer. Head hits land at the drum's Strike Position (the bass drums near the centre), rim shots at the edge, side sticks at the edge and softer; the angle is the side nearest the drummer, give or take 35 degrees.

## Tools

`tympanis_snapshot OUT_DIR` renders every page (and every drum on the DRUM page) to PNG files and prints the drawing times. CI runs it for the times only.
