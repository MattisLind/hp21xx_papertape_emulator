"""Build an editable native FreeCAD enclosure; run as a macro or with FreeCAD Python.

All geometry uses native Part primitives/booleans driven by a Spreadsheet.
No external Python packages or persistent Python feature proxies are needed.
"""
from pathlib import Path
import sys
import json
import math

FREECAD_LIB = Path('/Applications/FreeCAD.app/Contents/Resources/lib')
if FREECAD_LIB.exists():
    sys.path.insert(0, str(FREECAD_LIB))
import FreeCAD as App
import Part
import MeshPart
import FreeCADGui as Gui

OUT = Path(__file__).resolve().parent
Gui.showMainWindow()
doc = App.newDocument('HP21xx_Enclosure')
sheet = doc.addObject('Spreadsheet::Sheet', 'Dimensions')
sheet.Label = 'Dimensions — edit values in column B'
sheet.set('A1', 'HP21xx enclosure dimensions (mm)')
sheet.set('A2', 'Parameter'); sheet.set('B2', 'Value'); sheet.set('C2', 'Meaning / status')
sheet.setColumnWidth('A', 255); sheet.setColumnWidth('B', 110); sheet.setColumnWidth('C', 660)
sheet.setStyle('A1:C2', 'bold', 'add')
rows = [
    ('BoardWidth',66.6,'Measured produced PCB width; centred on the KiCad outline'),
    ('BoardLength',77.5,'Measured produced PCB length; centred on the KiCad outline'),
    ('LayoutWidth',66.5,'KiCad datum width; preserves mounting holes and component locations'),
    ('LayoutLength',77.4,'KiCad datum length; preserves mounting holes and component locations'),
    ('BoardThickness',1.6,'KiCad board thickness; PCB top is Z = 0'),
    ('BoardGap',0.3,'Reduced clearance per side from measured PCB edges to inner walls'),
    ('Wall',2.0,'Assumed side wall thickness'),
    ('CornerRadius',3.0,'Outside corner radius'),
    ('PanelTop',12.0,'Front outer face above PCB top; measured button tip is 13'),
    ('PanelThickness',2.0,'Front panel thickness'),
    ('UnderBoard',2.5,'User-specified PCB underside clearance and short-post length'),
    ('CoverThickness',2.0,'Bottom cover plate thickness'),
    ('HoleInset',3.0,'KiCad corner mounting-hole centre inset'),
    ('PostDiameter',4.8,'Paired posts at existing 2.5 mm PCB mounting holes'),
    ('PilotDiameter',1.7,'PROVISIONAL pilot for ~2.2 mm self-tapping screw; tune for material'),
    ('PilotDepth',8.0,'Blind pilot depth measured upwards from PCB top'),
    ('ScrewClearance',2.5,'Bottom-cover/post clearance bore; PCB hole is also 2.5'),
    ('HeadDiameter',4.4,'PROVISIONAL countersunk screw head diameter'),
    ('DisplayCenterX',32.472,'Centred between display mounting-hole columns; check actual screen centring'),
    ('DisplayUpperHoleY',36.8098,'KiCad upper display mounting-hole row'),
    ('DisplayOffset',4.0,'CONFIRMED: hole centre down to illuminated area top'),
    ('DisplayWidth',28.5,'Measured illuminated area width'),
    ('DisplayHeight',15.3,'Measured illuminated area height'),
    ('DisplayMargin',0.8,'Opening allowance on each side; adjustable for viewing angle'),
    ('DisplayFront',9.1,'Measured OLED front height above main PCB'),
    ('ModuleWidth',35.0,'Measured OLED module overall width'),
    ('ModuleLength',32.0,'Measured OLED module overall length'),
    ('ModuleThickness',1.6,'Approximate reference only; module PCB thickness not measured'),
    ('DisplayScrewReliefDiameter',5.6,'Blind recesses above the four display screw heads'),
    ('DisplayScrewReliefDepth',1.0,'PROVISIONAL relief into inside of front panel; check actual screw height'),
    ('ButtonX',56.694,'KiCad switch plunger centres, not first pad'),
    ('ButtonUpY',37.0,'KiCad SW2 plunger centre'),
    ('ButtonDownY',24.0,'KiCad SW3 plunger centre'),
    ('ButtonSelectY',11.0,'KiCad SW1 plunger centre'),
    ('ButtonHole',3.4,'User-specified button opening diameter'),
    ('ButtonTip',13.0,'Measured released button tip above main PCB'),
    ('LedGreenX',4.953,'KiCad LED body centre (1.27 from first lead)'),
    ('LedRedX',12.319,'KiCad LED body centre'),
    ('LedY',61.5748,'KiCad LED body centre Y'),
    ('LedHole',5.3,'User-specified LED opening diameter'),
    ('LedTip',16.0,'Measured LED tip above main PCB'),
    ('UsbX',33.399,'KiCad USB-C centre'),
    ('UsbWidth',9.0,'User-specified USB-C cutout width; nominal, no added tolerance'),
    ('UsbTop',3.0,'User-specified USB-C cutout height above PCB top'),
    ('SdY',25.866,'Current J3 centre: KiCad Y=126.534; enclosure Y=152.4-Y'),
    ('SdWidth',24.6,'SLA slot width; 24 mm SD card plus manufacturing clearance'),
    ('SdHeight',2.6,'SLA slot height; standard SD card is nominally 2.1 mm thick'),
    ('SdBottom',0.35,'PROVISIONAL slot bottom above PCB; card seat estimated at 0.6 mm'),
    ('SdJointGap',0.2,'SLA clearance at each side of the tray lip within the upper-shell notch'),
    ('SdCardWidth',24.0,'Standard full-size SD card reference width'),
    ('SdCardThickness',2.1,'Standard full-size SD card reference thickness'),
    ('SdCardBottom',0.6,'PROVISIONAL card underside above PCB from socket front drawing'),
    ('SdMouthX',0.5804,'J3 mouth relative to original left PCB edge; footprint front at local Y=8.6'),
    ('SdInsertedDepth',14.5,'PROVISIONAL seated insertion depth; confirm with actual socket/card'),
    ('SdCardLength',32.0,'Standard full-size SD card reference length'),
    ('SdSocketHeight',2.9,'Socket overall height from manufacturer drawing'),
    ('RibbonX',33.02,'Centre of KiCad ribbon-header courtyard'),
    ('RibbonWidth',53.0,'User-specified ribbon cutout width; nominal, no added tolerance'),
    ('RibbonTop',9.0,'Updated measured ribbon connector height above PCB top'),
]
for row, (alias,value,note) in enumerate(rows,3):
    sheet.set(f'A{row}',alias); sheet.set(f'B{row}',f'{value} mm')
    sheet.setAlias(f'B{row}',alias); sheet.set(f'C{row}',note)
    if 'PROVISIONAL' in note or 'Assumed' in note:
        sheet.setBackground(f'B{row}',(1.0,0.91,0.68))
    else:
        sheet.setBackground(f'B{row}',(0.85,0.94,1.0))

# A small expression vocabulary keeps the construction readable.
def D(name): return 'Dimensions.'+name
W=D('BoardWidth'); L=D('BoardLength'); G=D('BoardGap'); T=D('Wall')
PT=D('PanelTop'); PTH=D('PanelThickness'); BT=D('BoardThickness')
UB=D('UnderBoard'); CT=D('CoverThickness'); R=D('CornerRadius')
boardminx=f'({D("LayoutWidth")}-{W})/2'
boardminy=f'({D("LayoutLength")}-{L})/2'
left=f'({boardminx})-({G}+{T})'; front=f'({boardminy})-({G}+{T})'
ow=f'{W}+2*({G}+{T})'; ol=f'{L}+2*({G}+{T})'
seam='0 mm'  # The bottom-cover walls meet the upper shell at PCB top level.
floorinner=f'-({BT}+{UB})'; bottom=f'-({BT}+{UB}+{CT})'
panelinner=f'{PT}-{PTH}'
all_geometry=[]
def expression(obj,prop,value):
    if isinstance(value,str): obj.setExpression(prop,value)
    else: obj.setExpression(prop,f'{value} mm')
def make(kind,name,label):
    o=doc.addObject(kind,name); o.Label=label; all_geometry.append(o); return o
def box(name,x,y,z,dx,dy,dz):
    o=make('Part::Box',name,name)
    for prop,val in [('Length',dx),('Width',dy),('Height',dz),
                     ('Placement.Base.x',x),('Placement.Base.y',y),('Placement.Base.z',z)]:
        expression(o,prop,val)
    return o
def cylinder(name,x,y,z,r,h):
    o=make('Part::Cylinder',name,name)
    for prop,val in [('Radius',r),('Height',h),('Placement.Base.x',x),
                     ('Placement.Base.y',y),('Placement.Base.z',z)]: expression(o,prop,val)
    return o
def fuse(name,shapes,label=None):
    o=make('Part::MultiFuse',name,label or name); o.Shapes=shapes; o.Refine=True; return o
def cut(name,base,tool,label=None):
    o=make('Part::Cut',name,label or name); o.Base=base; o.Tool=tool; o.Refine=True; return o
def rounded_slab(name,x,y,z,w,l,h,r):
    pieces=[box(name+'WebX',f'({x})+({r})',y,z,f'({w})-2*({r})',l,h),
            box(name+'WebY',x,f'({y})+({r})',z,w,f'({l})-2*({r})',h)]
    for i,(xx,yy) in enumerate([(f'({x})+({r})',f'({y})+({r})'),
         (f'({x})+({w})-({r})',f'({y})+({r})'),
         (f'({x})+({r})',f'({y})+({l})-({r})'),
         (f'({x})+({w})-({r})',f'({y})+({l})-({r})')]):
        pieces.append(cylinder(name+'Corner'+str(i),xx,yy,z,r,h))
    return fuse(name,pieces)

# Top shell: rounded outside minus open cavity, then the long clamping posts.
outside=rounded_slab('ShellBlank',left,front,seam,ow,ol,f'{PT}-({seam})',R)
inside=rounded_slab('Cavity',f'({boardminx})-{G}',f'({boardminy})-{G}',f'({seam})-0.1 mm',
                    f'{W}+2*{G}',f'{L}+2*{G}',f'({panelinner})-({seam})+0.1 mm',G)
shell=cut('HollowShell',outside,inside,'Hollow shell — front panel and walls')
corners=[(D('HoleInset'),D('HoleInset')),
         (f'{D("LayoutWidth")}-{D("HoleInset")}',D('HoleInset')),
         (D('HoleInset'),f'{D("LayoutLength")}-{D("HoleInset")}'),
         (f'{D("LayoutWidth")}-{D("HoleInset")}',f'{D("LayoutLength")}-{D("HoleInset")}')]
upperposts=[cylinder('UpperPost'+str(i+1),x,y,0,f'{D("PostDiameter")}/2',panelinner) for i,(x,y) in enumerate(corners)]
shell=fuse('ShellWithPosts',[shell]+upperposts,'Upper shell with long PCB clamping posts')
pilots=[cylinder('Pilot'+str(i+1),x,y,-0.01,f'{D("PilotDiameter")}/2',f'{D("PilotDepth")}+0.01 mm') for i,(x,y) in enumerate(corners)]
shell=cut('ShellPilots',shell,fuse('PilotTools',pilots),'Blind self-tapping pilot holes')

# Front cutouts. The display aperture tracks the confirmed mounting-hole datum.
display_x=f'{D("DisplayCenterX")}-{D("DisplayWidth")}/2-{D("DisplayMargin")}'
display_y=f'{D("DisplayUpperHoleY")}-{D("DisplayOffset")}-{D("DisplayHeight")}-{D("DisplayMargin")}'
front_tools=[box('DisplayOpening',display_x,display_y,f'({panelinner})-0.1 mm',
                f'{D("DisplayWidth")}+2*{D("DisplayMargin")}',
                f'{D("DisplayHeight")}+2*{D("DisplayMargin")}',f'{PTH}+0.2 mm')]
for n,y in [('Up',D('ButtonUpY')),('Down',D('ButtonDownY')),('Select',D('ButtonSelectY'))]:
    front_tools.append(cylinder('ButtonOpening'+n,D('ButtonX'),y,f'({panelinner})-0.1 mm',f'{D("ButtonHole")}/2',f'{PTH}+0.2 mm'))
for n,x in [('Green',D('LedGreenX')),('Red',D('LedRedX'))]:
    front_tools.append(cylinder('LedOpening'+n,x,D('LedY'),f'({panelinner})-0.1 mm',f'{D("LedHole")}/2',f'{PTH}+0.2 mm'))
# H1 (upper-left OLED mounting hole) was deleted for the full-size SD socket.
for i,(dx,dy) in enumerate([(15.2,0),(-15.2,-28.5),(15.2,-28.5)]):
    front_tools.append(cylinder('DisplayScrewRelief'+str(i+1),
        f'{D("DisplayCenterX")}+({dx} mm)',f'{D("DisplayUpperHoleY")}+({dy} mm)',
        f'({panelinner})-0.1 mm',f'{D("DisplayScrewReliefDiameter")}/2',
        f'{D("DisplayScrewReliefDepth")}+0.1 mm'))
shell=cut('FrontOpenings',shell,fuse('FrontCuttingTools',front_tools),'Display, button and LED openings')

# Side slots stop at the cover joint and leave the long posts intact.
port_tools=[
    box('UsbAccess',f'{D("UsbX")}-{D("UsbWidth")}/2',f'({front})-0.1 mm',f'({seam})-0.1 mm',
        D('UsbWidth'),f'{T}+0.2 mm',f'{D("UsbTop")}-({seam})+0.1 mm'),
    box('SdAccess',f'({left})-0.1 mm',f'{D("SdY")}-{D("SdWidth")}/2',-0.1,
        f'{T}+0.2 mm',D('SdWidth'),f'{D("SdBottom")}+{D("SdHeight")}+0.1 mm'),
    box('RibbonAccess',f'{D("RibbonX")}-{D("RibbonWidth")}/2',f'({boardminy})+{L}+{G}-0.1 mm',f'({seam})-0.1 mm',
        D('RibbonWidth'),f'{T}+0.2 mm',f'{D("RibbonTop")}-({seam})+0.1 mm')]
upper=cut('UpperShell',shell,fuse('PortCuttingTools',port_tools),'UPPER SHELL — finished part')

# Rear tray: floor and perimeter walls up to PCB top, plus short posts to PCB underside.
coverblank=rounded_slab('CoverBlank',left,front,bottom,ow,ol,f'-({bottom})',R)
covercavity=rounded_slab('CoverCavity',f'({boardminx})-{G}',f'({boardminy})-{G}',floorinner,
    f'{W}+2*{G}',f'{L}+2*{G}',f'-({floorinner})+0.1 mm',G)
plate=cut('CoverTray',coverblank,covercavity,'Bottom tray — floor and walls to PCB top')
# A low continuation of the tray wall forms the slot sill. This avoids a
# fragile 0.35 mm strip spanning the opening on the separately printed lid.
sd_sill=box('SdTraySill',left,
    f'{D("SdY")}-{D("SdWidth")}/2+{D("SdJointGap")}',-0.1,
    T,f'{D("SdWidth")}-2*{D("SdJointGap")}',f'{D("SdBottom")}+0.1 mm')
plate=fuse('TrayWithSdSill',[plate,sd_sill],'Bottom tray with supported SD slot sill')
lowerposts=[cylinder('LowerPost'+str(i+1),x,y,floorinner,f'{D("PostDiameter")}/2',UB) for i,(x,y) in enumerate(corners)]
lower=fuse('CoverWithPosts',[plate]+lowerposts,'Bottom tray with short PCB clamping posts')
covertools=[]
for i,(x,y) in enumerate(corners):
    covertools.append(cylinder('Clearance'+str(i+1),x,y,f'({bottom})-0.01 mm',
                              f'{D("ScrewClearance")}/2',f'{CT}+{UB}+0.02 mm'))
    o=make('Part::Cone','Countersink'+str(i+1),'90° countersink '+str(i+1))
    for prop,val in [('Radius1',f'{D("HeadDiameter")}/2'),('Radius2',f'{D("ScrewClearance")}/2'),
                     ('Height',f'({D("HeadDiameter")}-{D("ScrewClearance")})/2'),
                     ('Placement.Base.x',x),('Placement.Base.y',y),('Placement.Base.z',bottom)]: expression(o,prop,val)
    covertools.append(o)
cover=cut('BottomCover',lower,fuse('CoverCuttingTools',covertools),'BOTTOM COVER — finished part')

# Lightweight measured references. These deliberately avoid misleading stock LED heights.
refs=doc.addObject('App::Part','ReferenceAssembly'); refs.Label='REFERENCE — PCB and simplified assembled parts'
pcb=box('PCB',boardminx,boardminy,f'-{BT}',W,L,BT)
pcbtools=[cylinder('PcbHole'+str(i),x,y,f'-{BT}-0.1 mm',1.25,f'{BT}+0.2 mm') for i,(x,y) in enumerate(corners)]
pcb=cut('BoardReference',pcb,fuse('PcbHoleTools',pcbtools),'PCB — measured envelope, KiCad mounting holes')
refs.addObject(pcb)
module=box('OledModule',f'{D("DisplayCenterX")}-{D("ModuleWidth")}/2',
           f'{D("DisplayUpperHoleY")}-30.25 mm',f'{D("DisplayFront")}-{D("ModuleThickness")}',
           D('ModuleWidth'),D('ModuleLength'),D('ModuleThickness'))
module.Label='OLED overall envelope — lower edge approximate';refs.addObject(module)
screen=box('IlluminatedArea',f'{D("DisplayCenterX")}-{D("DisplayWidth")}/2',
           f'{D("DisplayUpperHoleY")}-{D("DisplayOffset")}-{D("DisplayHeight")}',
           D('DisplayFront'),D('DisplayWidth'),D('DisplayHeight'),0.05)
screen.Label='OLED illuminated area — measured'; refs.addObject(screen)
for n,y in [('Up',D('ButtonUpY')),('Down',D('ButtonDownY')),('Select',D('ButtonSelectY'))]:
    o=cylinder('Actuator'+n,D('ButtonX'),y,3,2,f'{D("ButtonTip")}-3 mm'); refs.addObject(o)
    o.Label='Button actuator '+n+' — diameter approximate, not measured'
for n,x in [('Green',D('LedGreenX')),('Red',D('LedRedX'))]:
    o=cylinder('MeasuredLed'+n,x,D('LedY'),f'{D("LedTip")}-6 mm',2.5,6);refs.addObject(o)

socket=box('SdSocketReference',D('SdMouthX'),f'{D("SdY")}-13.9 mm',0,
    16.5,27.8,D('SdSocketHeight'))
socket.Label='SD socket — simplified envelope at current J3 position';refs.addObject(socket)
card=box('SdCardReference',f'{D("SdMouthX")}+{D("SdInsertedDepth")}-{D("SdCardLength")}',
    f'{D("SdY")}-{D("SdCardWidth")}/2',D('SdCardBottom'),
    D('SdCardLength'),D('SdCardWidth'),D('SdCardThickness'))
card.Label='Full-size SD card — seated depth/height provisional';refs.addObject(card)

doc.recompute()
for o in all_geometry:
    o.Visibility=False
for o in [upper,cover,pcb,module,screen,socket,card]+[doc.getObject('Actuator'+n) for n in ['Up','Down','Select']]+[doc.getObject('MeasuredLed'+n) for n in ['Green','Red']]:
    o.Visibility=True
upper.ViewObject.ShapeColor=(0.30,0.40,0.52)
cover.ViewObject.ShapeColor=(0.58,0.64,0.70)
pcb.ViewObject.ShapeColor=(0.10,0.48,0.23)
module.ViewObject.ShapeColor=(0.08,0.13,0.22)
screen.ViewObject.ShapeColor=(0.08,0.70,0.95)
socket.ViewObject.ShapeColor=(0.55,0.55,0.58)
socket.ViewObject.Transparency=75
card.ViewObject.ShapeColor=(0.12,0.20,0.36)
doc.MeasuredLedGreen.ViewObject.ShapeColor=(0.05,0.85,0.12)
doc.MeasuredLedRed.ViewObject.ShapeColor=(0.9,0.05,0.04)
for n in ['Up','Down','Select']:doc.getObject('Actuator'+n).ViewObject.ShapeColor=(0.10,0.10,0.10)
for o in all_geometry:
    o.ViewObject.LineColor=(0.15,0.18,0.22)
    o.ViewObject.Deviation=0.10
    o.ViewObject.AngularDeflection=15
    o.ViewObject.DisplayMode='Flat Lines'
upper.addProperty('App::PropertyString','FitStatus','Design notes')
upper.FitStatus='Full-size SD slot 24.6 x 2.6 for SLA; card seat height/depth provisional. Other openings unchanged. Physical fit and screw dimensions need verification.'
cover.addProperty('App::PropertyString','ScrewPath','Design notes')
cover.ScrewPath='Countersunk head -> cover -> short clearance post -> PCB 2.5 hole -> long upper post pilot.'
sheet.addProperty('App::PropertyString','Editing','Help')
sheet.Editing='Change column B; geometry uses native expressions. Recompute after editing. Yellow cells are provisional. Units are mm.'
doc.recompute()

def assert_valid(o):
    assert not o.Shape.isNull(),o.Name+' is null'
    assert o.Shape.isValid(),o.Name+' is invalid'
    assert len(o.Shape.Solids)==1,(o.Name,len(o.Shape.Solids))
    assert o.Shape.Volume>0,o.Name+' has no volume'
assert_valid(upper);assert_valid(cover)
assert upper.Shape.common(cover.Shape).Volume < 1e-6,'Shell and cover overlap'
assert upper.Shape.common(pcb.Shape).Volume < 1e-6,'Shell penetrates PCB'
assert cover.Shape.common(pcb.Shape).Volume < 1e-6,'Cover penetrates PCB'
assert abs(cover.Shape.BoundBox.ZMax-0.35)<1e-6,'SD sill must end at slot bottom'
assert abs(upper.Shape.BoundBox.ZMin)<1e-6,'Upper shell must start at PCB top'
assert abs(pcb.Shape.BoundBox.XLength-66.6)<1e-6,'Incorrect measured PCB width'
assert abs(pcb.Shape.BoundBox.YLength-77.5)<1e-6,'Incorrect measured PCB length'
assert abs(doc.Cavity.Shape.BoundBox.XMin-pcb.Shape.BoundBox.XMin+0.3)<1e-6,'Incorrect left PCB gap'
assert abs(doc.Cavity.Shape.BoundBox.XMax-pcb.Shape.BoundBox.XMax-0.3)<1e-6,'Incorrect right PCB gap'
assert abs(doc.Cavity.Shape.BoundBox.YMin-pcb.Shape.BoundBox.YMin+0.3)<1e-6,'Incorrect bottom PCB gap'
assert abs(doc.Cavity.Shape.BoundBox.YMax-pcb.Shape.BoundBox.YMax-0.3)<1e-6,'Incorrect top PCB gap'
assert abs(doc.CavityCorner0.Radius.Value-0.3)<1e-6,'Inner corner must preserve clearance around square PCB corners'
assert abs(doc.CoverCavityCorner0.Radius.Value-0.3)<1e-6,'Tray corner must preserve clearance around square PCB corners'
front_openings=[(doc.getObject('ButtonOpening'+n),3.4) for n in ['Up','Down','Select']]
front_openings += [(doc.getObject('LedOpening'+n),5.3) for n in ['Green','Red']]
for tool,diameter in front_openings:
    assert abs(2*tool.Radius.Value-diameter)<1e-6,(tool.Name,'incorrect opening diameter')
    centre=tool.Placement.Base
    for dx,dy in [(1,0),(-1,0),(0,1),(0,-1)]:
        inside=App.Vector(centre.x+dx*(diameter/2-0.05),centre.y+dy*(diameter/2-0.05),11)
        outside=App.Vector(centre.x+dx*(diameter/2+0.05),centre.y+dy*(diameter/2+0.05),11)
        assert not upper.Shape.isInside(inside,1e-6,False),(tool.Name,'opening obstructed')
        assert upper.Shape.isInside(outside,1e-6,False),(tool.Name,'opening oversized')
for tool,width,height in [(doc.UsbAccess,9.0,3.0),(doc.RibbonAccess,53.0,9.0)]:
    actual_width=tool.Width.Value if tool.Name=='SdAccess' else tool.Length.Value
    assert abs(actual_width-width)<1e-6,(tool.Name,'incorrect cutout width')
    assert abs(tool.Shape.BoundBox.ZMax-height)<1e-6,(tool.Name,'incorrect cutout top')
# Verify the small card-only aperture is aligned with current J3 and stays above the joint.
slot=doc.SdAccess.Shape.BoundBox
assert abs(slot.YLength-24.6)<1e-6
assert abs(slot.ZMax-doc.SdTraySill.Shape.BoundBox.ZMax-2.6)<1e-6
assert abs((slot.YMin+slot.YMax)/2-25.866)<1e-6
assert abs(doc.SdTraySill.Shape.BoundBox.ZMax-0.35)<1e-6
assert doc.SdAccess.Shape.common(upper.Shape).Volume<1e-6,'SD slot obstructed'
assert card.Shape.common(upper.Shape).Volume<1e-6,'Shell obstructs seated card'
assert card.Shape.common(cover.Shape).Volume<1e-6,'Tray obstructs seated card'
# Larger sliding envelope includes maximum card thickness and lateral allowance.
approach=Part.makeBox(24,24.2,2.25,App.Vector(-7,25.866-12.1,0.525))
assert approach.common(upper.Shape).Volume<1e-6,'Upper shell obstructs card insertion'
assert approach.common(cover.Shape).Volume<1e-6,'Tray obstructs card insertion'
wallx=slot.XMin+0.5
for yy,zz in [(25.866,0.175),(25.866,3.1),(13.4,1.5),(38.3,1.5)]:
    assert (upper.Shape.isInside(App.Vector(wallx,yy,zz),1e-6,False) or
            cover.Shape.isInside(App.Vector(wallx,yy,zz),1e-6,False)),'Missing material around SD slot'
# The old finger recess is gone: the tray wall remains intact below the slot.
assert cover.Shape.isInside(App.Vector(-1.3,25.866,-2),1e-6,False)
assert len([o for o in front_tools if o.Name.startswith('DisplayScrewRelief')])==3
assert upper.Shape.isInside(App.Vector(17.272,36.8098,10.2),1e-6,False),'Deleted H1 relief remains'
for x,y in [(3,3),(63.5,3),(3,74.4),(63.5,74.4)]:
    assert upper.Shape.isInside(App.Vector(x+1.7,y,0.1),1e-6,False),'Missing upper clamping contact'
    assert cover.Shape.isInside(App.Vector(x+1.7,y,-1.7),1e-6,False),'Missing lower clamping contact'
    assert not upper.Shape.isInside(App.Vector(x,y,4),1e-6,False),'Upper pilot blocked'
    assert not cover.Shape.isInside(App.Vector(x,y,-4),1e-6,False),'Lower clearance bore blocked'

# Verify the native expression graph actually rebuilds when a primary dimension changes.
original_height=upper.Shape.BoundBox.ZLength
panel_cell='B'+str(3+next(i for i,r in enumerate(rows) if r[0]=='PanelTop'))
sheet.set(panel_cell,'12.5 mm');doc.recompute()
assert_valid(upper)
assert abs(upper.Shape.BoundBox.ZLength-original_height-0.5)<1e-6,'Panel height did not propagate'
sheet.set(panel_cell,'12 mm');doc.recompute();assert_valid(upper)
sd_cell='B'+str(3+next(i for i,r in enumerate(rows) if r[0]=='SdWidth'))
before_sd_volume=upper.Shape.Volume
sheet.set(sd_cell,'25 mm');doc.recompute()
assert_valid(upper);assert_valid(cover)
assert upper.Shape.Volume<before_sd_volume,'SD width must update aperture'
sheet.set(sd_cell,'24.6 mm');doc.recompute();assert_valid(upper);assert_valid(cover)

view=Gui.activeDocument().activeView()
view.setAnimationEnabled(False) if hasattr(view,'setAnimationEnabled') else None
view.viewIsometric();view.fitAll()
Gui.updateGui()
doc.recompute()
doc.saveAs(str(OUT/'hp21xx_enclosure.FCStd'))
view.saveImage(str(OUT/'assembled.png'),1600,1200,'White')
# Side-on review image: SD side, with ribbon end on the right.
view.setCameraOrientation(App.Rotation(App.Vector(0,-1,0),App.Vector(0,0,1),App.Vector(-1,0,0),'ZXY').Q)
view.fitAll();Gui.updateGui()
view.saveImage(str(OUT/'sd_access.png'),1600,900,'White')

# Inspection image, with cover offset beside the upper shell to show both sets of posts.
refs.Visibility=False
cover.Visibility=False;upper.Visibility=False
preview_top=doc.addObject('Part::Feature','PreviewUpper')
preview_top.Shape=upper.Shape.copy()
preview_top.Placement=App.Placement(App.Vector(0,77.4,12),App.Rotation(App.Vector(1,0,0),180))
preview_top.ViewObject.ShapeColor=upper.ViewObject.ShapeColor
preview_cover=doc.addObject('Part::Feature','PreviewCover')
preview_cover.Shape=cover.Shape.copy()
preview_cover.Placement.Base=App.Vector(90,0,0)
preview_cover.ViewObject.ShapeColor=cover.ViewObject.ShapeColor
doc.recompute();view.viewIsometric();view.fitAll();Gui.updateGui()
view.saveImage(str(OUT/'inside.png'),1800,1000,'White')
preview_top.Visibility=False
preview_cover.Placement=App.Placement(App.Vector(0,77.4,cover.Shape.BoundBox.ZMin),App.Rotation(App.Vector(1,0,0),180))
doc.recompute();view.viewIsometric();view.fitAll();Gui.updateGui()
view.saveImage(str(OUT/'underside.png'),1400,1000,'White')
doc.removeObject(preview_top.Name);doc.removeObject(preview_cover.Name)
refs.Visibility=True;upper.Visibility=True;cover.Visibility=True
view.viewIsometric();view.fitAll();doc.recompute()

# Portable solids, plus the native document. STL files are for fit checks, not released fabrication.
Part.export([upper,cover],str(OUT/'hp21xx_enclosure.step'))
step_path=OUT/'hp21xx_enclosure.step'
step_path.write_text('\n'.join(line.rstrip() for line in step_path.read_text().splitlines())+'\n')
for obj,name in [(upper,'upper_shell'),(cover,'bottom_cover')]:
    mesh=MeshPart.meshFromShape(Shape=obj.Shape,LinearDeflection=0.08,AngularDeflection=0.15,Relative=False)
    mesh.write(str(OUT/(name+'.stl')))
report={
    'freecad_version':App.Version(),
    'outer_size_mm':[round(upper.Shape.BoundBox.XLength,3),round(upper.Shape.BoundBox.YLength,3),round(upper.Shape.BoundBox.ZMax-cover.Shape.BoundBox.ZMin,3)],
    'upper_shell':{'valid':upper.Shape.isValid(),'solids':len(upper.Shape.Solids),'volume_mm3':upper.Shape.Volume},
    'bottom_cover':{'valid':cover.Shape.isValid(),'solids':len(cover.Shape.Solids),'volume_mm3':cover.Shape.Volume},
    'joint_z_mm':0.0,
    'under_pcb_clearance_mm':2.5,
    'measured_pcb_size_mm':[66.6,77.5],
    'pcb_edge_clearance_per_side_mm':0.3,
    'inner_size_mm':[67.2,78.1],
    'pcb_datum_note':'Measured outline centred on original KiCad outline; holes and components retain their KiCad coordinates.',
    'nominal_port_cutouts_mm':{'USB-C':[9.0,3.0],'SD':[24.6,2.6],'ribbon':[53.0,9.0]},
    'front_opening_diameters_mm':{tool.Name:round(2*tool.Radius.Value,3) for tool,_ in front_openings},
    'sd_slot_mm':{'width':24.6,'height':2.6,'center_y':25.866,'z_min':0.35,'z_max':2.95,'card_bottom_estimate':0.6,'supported_sill':True,'sill_end_clearance':0.2},
    'mounting':{'corner_post_pairs':4,'display_screw_reliefs':3,'deleted_pcb_hole':'H1'},
    'print_process':'JLCPCB SLA resin; use material-specific process review before ordering',
    'checks':['single valid solid per half','no shell/cover volume overlap','no shell/PCB volume overlap','no cover/PCB volume overlap','measured PCB envelope and 0.3 mm clearance on each edge','joint at PCB top except supported SD sill at Z=0.35 with 0.2 mm end clearance','USB/ribbon nominal openings unchanged; SD slot uses documented SLA allowance','four paired clamping contacts and open coaxial bores; three OLED screw-head reliefs','spreadsheet panel-height change rebuilds shell correctly','full-size card insertion envelope clears both halves','slot centre follows current J3 position','old finger recess removed; material surrounds narrow slot','spreadsheet SD width change rebuilds aperture'],
    'provisional':['SD card seat height and insertion stop; verify actual hardware','physical connector fit and mating plug/latch access','LED flange clearance','screw pilot and head dimensions','display screw-head relief depth','OLED horizontal centring and approximate overall-envelope lower edge'],
}
# Compare with the preserved pre-revision model, if available locally.
baseline_path=OUT/'revisions'/'before_full_size_sd.FCStd'
if baseline_path.exists():
    baseline=App.openDocument(str(baseline_path))
    old_sd=Part.makeBox(2.3,22.2,8.2,App.Vector(-2.5,20.1678-11.1,-3.1))
    new_sd=Part.makeBox(2.3,24.8,3.2,App.Vector(-2.5,25.866-12.4,-0.1))
    h1_relief=Part.makeCylinder(2.9,1.3,App.Vector(17.272,36.8098,9.8))
    allowed=old_sd.fuse(new_sd).fuse(h1_relief)
    for current,previous in [(upper,baseline.UpperShell),(cover,baseline.BottomCover)]:
        difference=current.Shape.cut(previous.Shape).fuse(previous.Shape.cut(current.Shape))
        assert difference.cut(allowed).Volume<1e-5,'Geometry changed outside SD/H1 areas'
    App.closeDocument(baseline.Name)
    report['checks'].append('baseline comparison: geometry changes confined to SD wall and deleted H1 relief')
    App.setActiveDocument(doc.Name)
(OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
# Save the restored assembled state, without transient inspection objects.
doc.save()
del view
App.closeDocument(doc.Name)
Gui.updateGui()
reopened=App.openDocument(str(OUT/'hp21xx_enclosure.FCStd'))
reopened.recompute()
assert_valid(reopened.UpperShell);assert_valid(reopened.BottomCover)
for o in reopened.Objects:
    assert not o.State or 'Invalid' not in o.State,(o.Name,o.State)
report['checks'].append('saved FCStd reopens and recomputes with valid finished solids')
for name,diameter in report['front_opening_diameters_mm'].items():
    saved_tool=reopened.getObject(name)
    assert abs(2*saved_tool.Radius.Value-diameter)<1e-6,(saved_tool.Name,'saved opening diameter differs')
report['checks'].append('three 3.4 mm button holes and two 5.3 mm LED holes have clear interiors, surrounding panel material, and correct saved diameters')
(OUT/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('ENCLOSURE_READY '+json.dumps(report),flush=True)
# Destroy Qt views while Python's interpreter is still alive. Deferred view deletion
# during interpreter finalization can otherwise crash in PyGILState_Ensure on macOS.
from PySide import QtCore, QtWidgets
App.closeDocument(reopened.Name)
Gui.updateGui()
Gui.getMainWindow().close()
app=QtWidgets.QApplication.instance()
for _ in range(3):
    QtCore.QCoreApplication.sendPostedEvents(None,QtCore.QEvent.DeferredDelete)
    app.processEvents()
print('FREECAD_CLEAN_SHUTDOWN',flush=True)
