# Enclosure design inputs

Orientation throughout: view from above, ribbon connector at the top, USB-C at the bottom, microSD at the left. All dimensions are millimetres. Machine-readable inputs are in `measurements.json`.

## FreeCAD model now available

Open [`cad/hp21xx_enclosure.FCStd`](cad/hp21xx_enclosure.FCStd). The native model has an editable dimensions spreadsheet, both enclosure halves, paired PCB clamping posts, countersunk rear screw holes and all external access openings. See [`cad/README.md`](cad/README.md) for editing instructions, previews, assumptions and validation. Draft overall size is 71.2 × 82.1 × 18.1 mm. The remaining unmeasured dimensions are exposed as adjustable parameters.

## Confirmed inputs

The KiCad board outline is 66.5 × 77.4, thickness 1.6. Four corner mounting holes are diameter 2.5, with centres 3 from the adjacent board edges. The display mounting holes in the main PCB are spaced 30.4 horizontally × 28.5 vertically.

The manufactured PCB was subsequently measured as **66.6 × 77.5**. The CAD reference now uses that measured envelope, centred on the original KiCad outline; hole and component coordinates are preserved. Clearance is reduced to **0.3 per side**, for a **67.2 × 78.1** internal cavity. The inside corner radius is also 0.3 to avoid pinching the square PCB corners. The 0.05-per-edge extension is a centring assumption; physical hole offsets have not been remeasured.

User measurements from the top surface of the main PCB:

| Feature | Height |
| --- | ---: |
| Display front | 9.1 |
| Released button tips | 13 |
| LED tips | 16 |
| Ribbon connector | 9 |
| USB-C connector | 3 |
| SD card | 2 |

Display module overall dimensions supplied: 35 × 32. Illuminated area: 28.5 wide × 15.3 high. Confirmed: the top edge of the illuminated area is 4 below the centre of the upper mounting hole, with the ribbon connector at the top.

USB-C, microSD and ribbon cable must be accessible from outside the assembled enclosure. Programming headers may remain inside for the draft.

Photos show a right-angle ribbon header with moving ejector latches, display mounting screw heads beneath the main PCB, and LEDs on extended leads. Do not replace measured LED height with the standard KiCad LED model height.

## Construction from sketch IMG_7707.JPG

Interpretation: main shell with the front panel and side walls in one piece, closed by a separate bottom cover. Four countersunk self-tapping screws pass through the bottom cover, its short posts and the PCB mounting holes, then thread into longer posts in the upper shell. This supersedes the earlier removable-top-lid suggestion.

The bottom cover has perimeter walls rising to PCB top level (Z = 0). The ribbon connector, USB-C and microSD have cutouts opening at this joint in the upper shell: nominal width × height 53 × 9, 9 × 3 and 13.5 × 2 respectively. USB-C and ribbon retain these nominal cutouts; the SD opening is enlarged by a shared rounded 22 × 8 finger recess (radius 3), spanning Z = -3 to +5 through both halves. The 2-thick bottom floor is preserved. Seat the PCB in the lower tray before fitting the upper shell; check actual mating plug and latch access during a physical fit check.

Confirmed by the user: each existing corner mounting hole sits between a coaxial pair of posts. The upper-shell post is longer and contacts the PCB top; the bottom-cover post is very short and contacts the PCB underside. Tightening the cover screws clamps the PCB between these posts.

Screw path from outside: countersunk head → bottom cover → clearance bore through short bottom post → existing 2.5 PCB mounting hole → self-tapping pilot hole in long upper post. Only the upper post provides thread engagement. Select screw diameter to clear the PCB holes, and set pilot bore and engagement depth for the chosen screw and enclosure material. Set countersink depth to retain sufficient material beneath the head.

The post pairs support the board locally around the four mounting holes. The remainder of the bottom cover must clear underside solder joints and the display mounting screw heads; the short-post height must provide that clearance. The upper posts must clear components near the mounting holes.

## Preliminary height concept — not a fabrication drawing

A front panel outside surface 12 above the PCB top would leave the released buttons projecting 1 and LED tips projecting 4. A 2-thick front panel would have its inner surface 10 above the PCB, leaving a nominal 0.9 gap above the display front. The screen would sit 2.9 below the front panel outside surface; the opening will need allowance for viewing angle. Display screw heads and other raised parts still need clearance checks.

With the 2-thick floor and user-specified 2.5 clearance beneath the PCB underside, overall enclosure height is 18.1. The lower posts are 2.5 long; the tray walls extend 4.1 above its inner floor to reach PCB top level.

## Access provisions to include

- Bottom wall: USB-C opening allowing the cable plug housing to approach the socket.
- Left wall: microSD opening with a shared 22 × 8 rounded finger recess through both halves; centred 1 above PCB top to allow a fingernail to press the card edge. The card protrudes only 1 beyond the PCB.
- Top wall: ribbon connector access allowing the mating plug and ejector latches to operate. Photos establish arrangement, not exact clearances.
- Front panel: display opening, three 3.4 mm diameter button openings and two 5.3 mm diameter LED openings.

## Remaining inputs

- Confirm whether the illuminated area is horizontally centred between mounting holes.
- Manufacturing method and screw size. PCB clamping and the shared screw path are now confirmed above.
- Check that underside protrusions, including display screws, fit the specified 2.5 clearance.
- Physical mating plug and latch access with the supplied nominal connector cutouts; adjust the spreadsheet if needed after a fit check.

## Existing 3D reference

`../build/enclosure/pcb-reference.step` contains the PCB and available KiCad component models. It omits the OLED assembly, USB-C and ribbon connector models, attached cables, card and fasteners. See `../build/enclosure/README.md` for export details.
