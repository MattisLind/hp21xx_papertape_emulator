# Editable FreeCAD enclosure

Open **hp21xx_enclosure.FCStd** in FreeCAD. It contains the assembled upper shell and bottom cover, plus a simplified PCB/display/button/LED reference assembly. No extra workbench, macro or Python package is required to edit the saved model.

![Assembled enclosure](assembled.png)

![Inside of both halves](inside.png)

![SD-card access from the left side](sd_access.png)

## Editing

1. Double-click **Dimensions — edit values in column B** in the model tree.
2. Change the dimension in column B and recompute the document. Units are millimetres. Yellow cells identify provisional dimensions.
3. Return to the 3D view to inspect the result.
4. Select **UPPER SHELL — finished part**, **BOTTOM COVER — finished part**, or **REFERENCE — PCB and simplified assembled parts** and press Space to toggle its visibility. Hide the upper shell to inspect PCB alignment and the short posts. Hide the cover and reference assembly, then rotate the view, to inspect the long upper posts.

The model uses native Part primitives and boolean operations with expressions linked to the dimensions sheet. Expand the finished parts in the tree to inspect their construction. Spreadsheet-controlled properties should be edited through the sheet. The model contains no custom Python feature requiring the builder at load time.

Useful parameters:

| Parameter | Purpose |
| --- | --- |
| PanelTop | Front outer face height above the PCB top; currently 12 |
| BoardWidth / BoardLength | Measured PCB envelope, currently 66.6 × 77.5 |
| BoardGap | Clearance per edge, currently 0.3; cavity is 67.2 × 78.1 |
| LayoutWidth / LayoutLength | Original KiCad datum, 66.5 × 77.4; keep fixed to preserve hole coordinates |
| UnderBoard | Short-post length and clearance below PCB underside; currently 2.5, specified by user |
| Wall / PanelThickness / CoverThickness | Shell and cover thicknesses |
| PostDiameter | Diameter of both sets of corner posts |
| PilotDiameter / PilotDepth | Blind self-tapping screw pilot in the long upper posts |
| ScrewClearance / HeadDiameter | Rear clearance bores and 90-degree countersinks |
| DisplayMargin | Extra aperture space on every edge of the illuminated area |
| DisplayScrewReliefDiameter / DisplayScrewReliefDepth | Blind recesses inside the front panel above the display screws |
| UsbWidth / UsbTop | USB plug access slot size |
| SdWidth / SdHeight | Full-size SD aperture; 24.6 × 2.6 for SLA resin |
| SdY / SdBottom | Slot centre along left wall (25.866) and bottom above PCB (0.35) |
| SdJointGap | Clearance at each end of supported tray sill; 0.2 |
| SdCardBottom / SdInsertedDepth | Provisional card seat height (0.6) and insertion depth (14.5) |
| RibbonWidth / RibbonTop | Header, plug and latch access slot size |
| LedHole / ButtonHole | User-specified front opening diameters: 5.3 / 3.4 |

Keep `CornerRadius > Wall`, positive wall/post dimensions, and `PilotDepth < PanelTop - PanelThickness`. Keep `DisplayScrewReliefDepth < PanelThickness`. Keep `SdBottom` positive and `2 × SdJointGap < SdWidth`. The narrow SD slot is formed by an upper-shell notch and a supported sill on the lower tray; keep the sill ends clear of the notch sides. When changing the screw dimensions, the physical PCB mounting holes remain 2.5 diameter. Keep sufficient plastic beneath the countersunk heads and check that each post still clears nearby components. Large design changes may require editing the native construction as well as the dimensions sheet.

## Construction and dimensions

The draft envelope is **71.2 × 82.1 × 18.1 mm**, excluding protruding LEDs, buttons and connectors. The PCB top is Z = 0. X points toward the buttons, Y toward the ribbon connector and Z out through the front panel.

The four long upper posts contact the PCB top. Four short bottom-cover posts contact the PCB underside. Screws enter through the bottom cover countersinks, pass through the short posts and existing PCB mounting holes, then thread into the upper posts. The cover screws also clamp the PCB; there are no separate PCB fasteners.

Current heights: front panel outer face Z = 12, inner face Z = 10, upper post length 10, PCB underside Z = -1.6, short post length 2.5, cover inner face Z = -4.1, cover outside Z = -6.1. The bottom cover is a tray: its perimeter walls rise to PCB top level, Z = 0, and meet the upper shell there, except the local SD sill which rises to Z = 0.35. Seat the PCB on the lower posts in the tray, then fit the upper shell. Connector slots in the upper shell open at this joint.

The manufactured PCB measures 66.6 × 77.5. The cavity is 67.2 × 78.1, giving 0.3 clearance per side. The inside corner radius tracks BoardGap (currently 0.3) to preserve clearance around square PCB corners.

The measured outline is assumed centred on the original 66.5 × 77.4 KiCad outline, extending 0.05 beyond it on each edge. Mounting holes, button centres, LED centres and connector locations retain the original KiCad coordinates; they are not rescaled or moved with the measured board envelope. Physical mounting-hole offsets have not been remeasured. The display aperture follows the measured 28.5 × 15.3 illuminated area and confirmed 4 offset below the upper mounting-hole centres. It has 0.8 clearance per edge. The display is assumed horizontally centred between its mounting-hole columns.

The reference assembly is simplified: the full-size SD socket and seated card have envelope models; other connector bodies, mating cables, solder joints, screws and actual OLED internal layers are not modelled. SD seat height and insertion stop are provisional drawing-derived estimates, not measured hardware dimensions. LED cylinders show the measured tip height, rather than the inaccurate stock KiCad model placement. The button reference cylinders retain an unmeasured 4 mm diameter assumption; they do not define the user-specified 3.4 mm openings. The full KiCad STEP reference remains in `../../build/enclosure/pcb-reference.step`.

The front panel has three **3.4 mm diameter button openings** and two **5.3 mm diameter LED openings**, as specified by the user. The model before reducing these from 4.8 and 6.2 mm respectively is preserved in `revisions/before_front_hole_reduction.FCStd`.

## Full-size SD slot — JLCPCB SLA resin revision

The large microSD finger recess has been removed. The assembled opening is
**24.6 wide × 2.6 high**, with its centre at enclosure Y = **25.866** and
its lower edge Z = **0.35** above the PCB top. The slot now follows J3 at
KiCad (98.0804, 126.534), rotation -90°. Enclosure coordinates retain the
original PCB datum: X = KiCad X − 88.9; Y = 152.4 − KiCad Y. The new slot
centre is 5.6982 towards the ribbon end from the old microSD centre.

The requested 24.2 × 2.2 opening is tight for a standard **24 × 32 × 2.1**
SD card. JLCPCB quotes **±0.2 mm** resin dimensional accuracy below 100 mm.
The selected aperture provides 0.6 mm total nominal width clearance and
0.5 mm total nominal thickness clearance. These are allowances, not a
physical fit guarantee. Change `SdWidth` / `SdHeight` in the spreadsheet if
an actual fit check requires adjustment.

A small continuation of the lower tray wall forms the sill at Z = 0.35,
with 0.2 mm end clearance inside the upper notch. This avoids printing a
fragile 0.35 mm strip spanning the slot on the lid. The bottom tray wall
and floor are continuous; no large finger opening remains. The card is
expected to project well beyond the enclosure wall, so the original
microSD fingernail recess is unnecessary. Seated depth remains provisional.

The socket's 2.9 mm overall height and front drawing inform the reference
model. Card underside at Z = 0.6 and insertion depth 14.5 remain explicit
provisional parameters; check those against the assembled socket before a
production print. The nominal and 24.2 × 2.25 sliding-card envelopes clear
both halves in the geometry checks.

The PCB has lost **H1**, the upper-left OLED mounting hole. Its blind
screw-head relief in the lid has been removed. The other three display
reliefs and all **four corner post pairs and cover screw holes** remain.
No other opening or outer enclosure dimension was changed.

The previous native model is preserved locally as
`revisions/before_full_size_sd.FCStd`. Earlier finger-recess and front-hole
revisions remain in the same local revisions directory.

Sources: [SHOU HAN socket drawing](https://datasheet.lcsc.com/datasheet/pdf/205c75655f9098343d6dd157d86292c5.pdf?productCode=C7529393),
[SD Association physical specification](https://www.sdcard.org/cms/wp-content/themes/sdcard-org/dl.php?f=Part1_Physical_Layer_Simplified_Specification_Ver6.00.pdf),
[JLCPCB print design guidelines](https://jlc3dp.com/help/article/3d-printing-design-guideline).

The model before reducing the board clearance is preserved in `revisions/before_reduced_board_clearance.FCStd`.

## Fit checks before fabrication

The underside clearance is 2.5 as specified by the user. Nominal connector envelopes are **USB-C 9 × 3**, **full-size SD 24.6 × 2.6**, and **ribbon 53 × 9**, each measured from PCB top. USB-C and ribbon openings retain those nominal dimensions. The SD slot is positioned above the PCB with the clearance and supported sill described above. The dimensions sheet remains editable if a fit check shows a need for additional clearance. Physical fit, mating plug/latch access, LED flange clearance, display screw-head recesses and screw dimensions still need verification. The intended process is JLCPCB SLA resin; a specific resin grade has not been selected.

The pilot is initially 1.7 diameter, clearance bores 2.5 and countersink head diameter 4.4, as a starting point for a small self-tapping countersunk screw of roughly 2.2 nominal diameter. Select an actual screw and adjust these values for its geometry and the enclosure material. Check length against the complete stack and blind-pilot depth; no specific purchased screw has been validated.

The initial model with the flat bottom cover is preserved in `revisions/initial_flat_bottom_cover.FCStd`.

## Files and validation

- `hp21xx_enclosure.FCStd`: editable native model with saved view and colours.
- `hp21xx_enclosure.step`: portable export of the two enclosure solids.
- `upper_shell.stl`, `bottom_cover.stl`: draft mesh exports in assembly coordinates; orient them as appropriate in a slicer.
- `assembled.png`, `inside.png`, `underside.png`, `sd_access.png`: review images.
- `validation.json`: actual build version, dimensions and geometry checks.
- `build_enclosure.py`: reproducible builder using the installed FreeCAD Python environment. Running it regenerates the files and overwrites model edits; save a separate copy before rebuilding.

Validation checks both halves are single valid solids; shell/cover/PCB volume
intersections; unchanged 0.3 mm PCB cavity clearance; the four paired
clamping contacts and screw bores; three OLED screw reliefs and absence of
the deleted H1 relief; SD aperture size/position and unobstructed card
insertion; material surrounding the slot; and spreadsheet propagation for
panel height and SD width. It reopens/recomputes the saved FCStd. When the
local pre-revision model is available, a solid comparison confirms that
changes are confined to the SD wall and removed H1 relief.

To regenerate on this Mac:

```sh
/Applications/FreeCAD.app/Contents/Resources/bin/python box/cad/build_enclosure.py
```
