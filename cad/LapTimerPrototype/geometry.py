"""Shared solid recipe, in mm. No CAD dependencies; used by Fusion and CadQuery.

Axes in assembled view: X left/right, Y bottom/top, Z back/front.
Every primitive retains its parameter expressions for Fusion sketches/features.
"""
import json
import math
from pathlib import Path


def load_parameters(path=None):
    return json.loads(Path(path or Path(__file__).with_name('parameters.json')).read_text(encoding='utf-8'))


def value(expression, params):
    if isinstance(expression, (int, float)):
        return float(expression)
    return float(eval(expression, {'__builtins__': {}}, params))


def recipes():
    parts = {}
    def part(name):
        ops = []; parts[name] = ops
        def box(label, x, y, z, w, h, d, cut=False):
            ops.append(dict(kind='box', name=label, x=x, y=y, z=z, w=w, h=h, d=d, cut=cut))
        def cylinder(label, axis, u, v, start, length, diameter, cut=False):
            ops.append(dict(kind='cylinder', name=label, axis=axis, u=u, v=v, start=start, length=length, diameter=diameter, cut=cut))
        def hexagon(label, u, v, start, length, flats):
            ops.append(dict(kind='hexagon', name=label, axis='X', u=u, v=v, start=start, length=length, flats=flats, cut=True))
        return box, cylinder, hexagon

    box, cyl, hexa = part('01_Housing')
    box('Back floor', 0,0,0,'case_w','case_h','wall')
    box('Left wall',0,0,'wall','wall','case_h','case_d-wall')
    box('Right wall','case_w-wall',0,'wall','wall','case_h','case_d-wall')
    box('Bottom wall','wall',0,'wall','case_w-2*wall','wall','case_d-wall')
    box('Top wall','wall','case_h-wall','wall','case_w-2*wall','wall','case_d-wall')
    for x in ('6','case_w-6'):
        for y in ('6','case_h-6'):
            cyl('Lid screw tower','Z',x,y,'wall','case_d-wall',10)
    mounts = [(16,52),(104,72),(60,96),(104,16)]
    for x,y in mounts:
        cyl('Carrier pedestal','Z',x,y,'wall','carrier_z-wall',8)
    # Reinforced two-ear hinge, deliberately larger than a GoPro interface.
    for x in ('(case_w-2*fork_ear-fork_gap)/2','(case_w+fork_gap)/2'):
        box('Hinge root',x,'pivot_y','pivot_z-pivot_d/2','fork_ear','-pivot_y+wall','pivot_d')
        cyl('Rounded hinge ear','X','pivot_y','pivot_z',x,'fork_ear','pivot_d')
        box('Hinge root reinforcement',f'({x})-2',-3,'pivot_z-pivot_d/2-2','fork_ear+4',8,'pivot_d+4')
    # Service windows: prototype access, intentionally not weather sealed.
    box('Left USB service window',-1,11,10,'wall+2',34,18,True)
    box('Right connector and SD window','case_w-wall-1',10,10,'wall+2',54,34,True)
    for x,d in ((17,'switch_cable_hole'),('case_w-17','battery_cable_hole')):
        cyl('Cable exit','Y',x,12,-1,'wall+2',d,True)
    for x in ('6','case_w-6'):
        for y in ('6','case_h-6'):
            cyl('M3 lid pilot','Z',x,y,'case_d-14',15,'case_pilot',True)
    for x,y in mounts:
        cyl('M3 carrier pilot','Z',x,y,1,'carrier_z','case_pilot',True)
    cyl('M6 hinge through bore','X','pivot_y','pivot_z','(case_w-2*fork_ear-fork_gap)/2-1','2*fork_ear+fork_gap+2','pivot_hole',True)
    hexa('M6 captured nut','pivot_y','pivot_z','(case_w+fork_gap)/2+fork_ear-nut_depth','nut_depth+1','nut_flats')

    box, cyl, _ = part('02_Front_bezel')
    box('Front plate',0,0,'case_d','case_w','case_h','lid_t')
    # Open-sided antenna shelf. Rails are joined to the lid before its floor.
    ax='(case_w-antenna_w)/2'
    for y in ('antenna_y-antenna_fit-2','antenna_y+antenna_h+antenna_fit'):
        box('Antenna shelf rail',f'{ax}-antenna_fit',y,'antenna_z-1.5','antenna_w+2*antenna_fit+2',2,'case_d-antenna_z+1.7')
    box('Antenna shelf end',f'{ax}+antenna_w+antenna_fit','antenna_y-antenna_fit','antenna_z-1.5',2,'antenna_h+2*antenna_fit','case_d-antenna_z+1.7')
    box('Antenna rear support',f'{ax}-antenna_fit','antenna_y-antenna_fit','antenna_z-1.5','antenna_w+2*antenna_fit+2','antenna_h+2*antenna_fit',1.5)
    box('Antenna cable relief',f'{ax}-antenna_fit-1','antenna_y+antenna_h/2-3','antenna_z-2','antenna_w/2+3',6,4,True)
    box('Screen aperture with assembly clearance','(case_w-window_w)/2','screen_y+(screen_h-window_h)/2','case_d-1','window_w','window_h','lid_t+2',True)
    for x in ('(case_w-screen_pitch_x)/2','(case_w+screen_pitch_x)/2'):
        for y in ('screen_y+(screen_h-screen_pitch_y)/2','screen_y+(screen_h+screen_pitch_y)/2'):
            cyl('Display M3 mounting hole','Z',x,y,'case_d-1','lid_t+2','screen_bolt',True)
    for x in ('6','case_w-6'):
        for y in ('6','case_h-6'):
            cyl('Lid M3 clearance','Z',x,y,'case_d-1','lid_t+2',3.4,True)

    box, cyl, _ = part('03_Electronics_carrier')
    box('Removable tray',7,7,'carrier_z','case_w-14','case_h-14','carrier_t')
    for x in (7,'case_w-12'):
        for y in (7,'case_h-12'):
            box('Corner tower clearance',x,y,'carrier_z-1',5,5,'carrier_t+2',True)
    # Edge supports leave room below the ESP for solder joints. Zip ties retain it.
    for x in ('esp_x+4','esp_x+esp_w-12'):
        for y in ('esp_y+1','esp_y+esp_h-3'):
            box('ESP support pad',x,y,'carrier_z+carrier_t',8,2,'board_standoff')
    # Both short ends stay open for the existing USB/SD connectors.
    for x in ('esp_x+15','esp_x+esp_w-15'):
        for y in ('esp_y-3','esp_y+esp_h+0.5'):
            box('ESP zip tie slot',x,y,'carrier_z-1',3,2.5,'carrier_t+2',True)
    # GPS 36 x 26, confirmed 31 x 21 pitch, assumed symmetric.
    for x in ('gps_x+(gps_w-gps_pitch_x)/2','gps_x+(gps_w+gps_pitch_x)/2'):
        for y in ('gps_y+(gps_h-gps_pitch_y)/2','gps_y+(gps_h+gps_pitch_y)/2'):
            cyl('GPS standoff','Z',x,y,'carrier_z+carrier_t','board_standoff',7.5)
            cyl('GPS M3 clearance','Z',x,y,'carrier_z-1','carrier_t+board_standoff+2',3.4,True)
    # IMU hole row is provisional. Slots allow +/- 1.8 mm adjustment in Y.
    for x in ('imu_x+(imu_w-imu_pitch)/2','imu_x+(imu_w+imu_pitch)/2'):
        y='imu_y+imu_h-imu_hole_edge'
        box('IMU slotted support',f'({x})-4',f'({y})-5.2','carrier_z+carrier_t',8,10.4,'board_standoff')
        box('IMU adjustable mounting slot',f'({x})-1.7',f'({y})-3.5','carrier_z-1',3.4,7,'carrier_t+board_standoff+2',True)
    for x,y in mounts:
        cyl('Carrier M3 clearance','Z',x,y,'carrier_z-1','carrier_t+2',3.4,True)
    # Harness tie-down slots near the two external cable exits.
    for x in (14,'case_w-20'):
        for y in (8,15):
            box('Harness zip tie slot',x,y,'carrier_z-1',5,2.5,'carrier_t+2',True)

    box, cyl, _ = part('04_Hinge_tongue_fit_gauge')
    # Reference interface for the user's separate motorcycle base, NOT that base.
    box('Gauge stem',0,0,0,'tongue_t',22,'pivot_d')
    cyl('Gauge round end','X',22,'pivot_d/2',0,'tongue_t','pivot_d')
    cyl('Gauge M6 bore','X',22,'pivot_d/2',-1,'tongue_t+2','pivot_hole',True)
    for height in (2,4,6):
        box, cyl, _ = part(f'05_Display_spacer_{height}mm')
        cyl('Loose display spacer','Z',0,0,0,height,7)
        cyl('Spacer M3 clearance','Z',0,0,-1,height+2,3.4,True)
    return parts


def references(params):
    p=params
    board_z=p['carrier_z']+p['carrier_t']+p['board_standoff']
    return [
        dict(name='Display envelope - connector depth included',x=(p['case_w']-p['screen_w'])/2,y=p['screen_y'],z=p['case_d']-p['screen_d'],w=p['screen_w'],h=p['screen_h'],d=p['screen_d'],color=(0.1,0.5,0.7)),
        dict(name='ESP32 envelope',x=p['esp_x'],y=p['esp_y'],z=board_z,w=p['esp_w'],h=p['esp_h'],d=p['esp_d'],color=(0.1,0.45,0.25)),
        dict(name='SD protrusion allowance',x=p['esp_x']+p['esp_w'],y=p['esp_y']+4,z=board_z,w=p['esp_sd_extra'],h=p['esp_h']-8,d=p['esp_d'],color=(0.65,0.4,0.1)),
        dict(name='IMU envelope - height provisional',x=p['imu_x'],y=p['imu_y'],z=board_z,w=p['imu_w'],h=p['imu_h'],d=4,color=(0.5,0.2,0.65)),
        dict(name='GPS module',x=p['gps_x'],y=p['gps_y'],z=board_z,w=p['gps_w'],h=p['gps_h'],d=p['gps_d'],color=(0.1,0.45,0.25)),
        dict(name='Antenna envelope',x=(p['case_w']-p['antenna_w'])/2,y=p['antenna_y'],z=p['antenna_z'],w=p['antenna_w'],h=p['antenna_h'],d=p['antenna_d'],color=(0.85,0.75,0.55))
    ]


def validate_layout(p):
    assert p['fork_gap'] > p['tongue_t']
    assert p['pivot_d'] > p['pivot_hole'] + 12
    assert p['screen_pitch_x'] < p['screen_w'] and p['screen_pitch_y'] < p['screen_h']
    assert 0<p['window_w']<p['screen_w'] and 0<p['window_h']<p['screen_h']
    assert p['gps_pitch_x'] < p['gps_w'] and p['gps_pitch_y'] < p['gps_h']
    assert p['carrier_z'] > p['wall'] + 2.4, 'Space for M3 nuts below carrier'
    board_top=p['carrier_z']+p['carrier_t']+p['board_standoff']+p['esp_d']
    assert board_top+3 <= p['case_d']-p['screen_d'], 'ESP/display envelope clearance'
    assert p['antenna_y']-p['antenna_fit']-2 >= p['screen_y']+p['screen_h']+0.5, 'Screen/antenna rail clearance'
    assert p['antenna_y']+p['antenna_h']+p['antenna_fit']+2 <= p['case_h']-p['wall']
    assert p['antenna_z']+p['antenna_d']+0.5 <= p['case_d']
    # Minimum direct antenna cable route; a real routing check remains necessary.
    gps=(p['gps_x']+p['gps_w'],p['gps_y']+p['gps_h']/2,17)
    ant=(p['case_w']/2,p['antenna_y']+p['antenna_h']/2,p['antenna_z'])
    route=math.dist(gps,ant)
    assert route+15 <= p['antenna_cable'], 'Reserve 15 mm for bends'
    return dict(esp_screen_clearance_mm=round(p['case_d']-p['screen_d']-board_top,2),
                antenna_direct_route_mm=round(route,2),fork_side_clearance_mm=round((p['fork_gap']-p['tongue_t'])/2,2))
