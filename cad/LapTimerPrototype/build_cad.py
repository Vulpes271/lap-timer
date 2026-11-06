"""Generate validated STEP/STL artifacts from the same recipe used by Fusion."""
from pathlib import Path
import json
import math
import os
import struct
import sys
from collections import Counter

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / '.pio' / 'cad-deps'))
import cadquery as cq
from geometry import load_parameters, recipes, references, validate_layout, value


def primitive(op, params):
    v=lambda name: value(op[name],params)
    if op['kind']=='box':
        return cq.Workplane('XY').box(v('w'),v('h'),v('d'),centered=False).translate((v('x'),v('y'),v('z'))).val()
    axis=op['axis']; u=v('u'); w=v('v'); start=v('start'); length=v('length')
    if op['kind']=='hexagon':
        return cq.Workplane('YZ',origin=(start,u,w)).polygon(6,2*v('flats')/math.sqrt(3)).extrude(length).val()
    point={'X':(start,u,w),'Y':(u,start,w),'Z':(u,w,start)}[axis]
    direction={'X':(1,0,0),'Y':(0,1,0),'Z':(0,0,1)}[axis]
    return cq.Solid.makeCylinder(v('diameter')/2,length,cq.Vector(*point),cq.Vector(*direction))


def check_stl(path):
    raw=path.read_bytes()
    count=struct.unpack_from('<I',raw,80)[0]
    assert len(raw)==84+50*count, 'Expected binary STL'
    edges=Counter()
    for i in range(count):
        xyz=struct.unpack_from('<9f',raw,84+50*i+12)
        vertices=[tuple(round(x,4) for x in xyz[j:j+3]) for j in (0,3,6)]
        assert len(set(vertices))==3, 'Degenerate mesh triangle'
        for a,b in ((0,1),(1,2),(2,0)):
            edges[tuple(sorted((vertices[a],vertices[b])))]+=1
    bad=sum(n!=2 for n in edges.values())
    assert not bad, f'{bad} boundary/non-manifold edges in {path.name}'
    neighbors={}
    for a,b in edges:
        neighbors.setdefault(a,set()).add(b); neighbors.setdefault(b,set()).add(a)
    remaining=set(neighbors); connected=0
    while remaining:
        connected+=1; queue=[remaining.pop()]
        while queue:
            for neighbor in neighbors[queue.pop()]:
                if neighbor in remaining: remaining.remove(neighbor); queue.append(neighbor)
    assert connected==1, f'{connected} disconnected mesh components in {path.name}'
    return count


def build(output=None, standalone=False):
    params=load_parameters(); layout=validate_layout(params)
    dest=Path(output or HERE/'exports'); dest.mkdir(parents=True,exist_ok=True)
    shapes={}; report={'parameters_mm':params,'layout':layout,'parts':{},'limitations':[
        f"Screen face measured 83 x 55 mm; aperture {params['window_w']} x {params['window_h']} mm. Centered position assumed.",
        'Display mounting-plane offset, USB alignment, IMU height/row need physical fit check.',
        'No weather sealing or structural load validation.',
        'Hinge tongue is an interface gauge, not the motorcycle base.'
    ]}
    for name,ops in recipes().items():
        shape=None
        for op in ops:
            item=primitive(op,params)
            if shape is None:
                assert not op['cut']; shape=item
            else:
                shape=shape.cut(item) if op['cut'] else shape.fuse(item)
        shape=shape.clean()
        assert shape.isValid(), f'Invalid solid: {name}'
        assert len(shape.Solids())==1, f'Detached pieces: {name}'
        assert shape.Volume()>0
        shapes[name]=shape
        cq.exporters.export(shape,str(dest/f'{name}.step'))
        cq.exporters.export(shape,str(dest/f'{name}.stl'),tolerance=0.04,angularTolerance=0.1)
        triangles=check_stl(dest/f'{name}.stl')
        bb=shape.BoundingBox()
        report['parts'][name]=dict(valid_solid=True,watertight_stl=True,solids=1,
            triangles=triangles,volume_cm3=round(shape.Volume()/1000,3),
            size_mm=[round(bb.xlen,3),round(bb.ylen,3),round(bb.zlen,3)])
        print(name, report['parts'][name],flush=True)
    assembly=cq.Assembly(name='LapTimer_PETG_prototype')
    for name,color in [('01_Housing',(0.18,0.21,0.25)),('02_Front_bezel',(0.12,0.52,0.65)),('03_Electronics_carrier',(0.85,0.48,0.15))]:
        assembly.add(shapes[name],name=name,color=cq.Color(*color))
    assembly.export(str(dest/'LapTimer_assembled.step'))
    exploded=cq.Assembly(name='LapTimer_exploded')
    for name,z in [('01_Housing',0),('03_Electronics_carrier',60),('02_Front_bezel',90)]:
        exploded.add(shapes[name],name=name,loc=cq.Location(cq.Vector(0,0,z)))
    exploded.export(str(dest/'LapTimer_exploded.step'))
    # Check the custom pivot's neutral fit and usable angular sweep with exact solids.
    tongue=shapes['04_Hinge_tongue_fit_gauge'].translate(((params['case_w']-params['tongue_t'])/2,params['pivot_y']-22,params['pivot_z']-params['pivot_d']/2))
    pivot=(params['case_w']/2,params['pivot_y'],params['pivot_z'])
    sweep={}
    for angle in (-90,-60,-45,0,45,60,90):
        tilted=tongue.rotate(pivot,(pivot[0]+1,pivot[1],pivot[2]),angle)
        overlap=tilted.intersect(shapes['01_Housing']).Volume()
        sweep[str(angle)]=round(overlap,6)
        assert overlap<1e-5, f'Hinge collision at {angle} degrees'
    report['hinge_housing_collision_mm3']=sweep
    # Actual reference envelopes are exported separately, never in printing STLs.
    refs=cq.Assembly(name='Measured_component_envelopes_NOT_FOR_PRINT')
    report['component_printed_part_intersections_mm3']={}
    for ref in references(params):
        box=cq.Workplane('XY').box(ref['w'],ref['h'],ref['d'],centered=False).translate((ref['x'],ref['y'],ref['z'])).val()
        refs.add(box,name=ref['name'].replace(' ','_'),color=cq.Color(*ref['color']))
        for name in ('01_Housing','02_Front_bezel','03_Electronics_carrier'):
            overlap=box.intersect(shapes[name]).Volume()
            report['component_printed_part_intersections_mm3'][f"{ref['name']} / {name}"]=round(overlap,6)
            assert overlap<1e-5, f"Component collision: {ref['name']} / {name}: {overlap}"
    report['printed_assembly_intersections_mm3']={}
    for first,second in [('01_Housing','02_Front_bezel'),('01_Housing','03_Electronics_carrier'),('02_Front_bezel','03_Electronics_carrier')]:
        overlap=shapes[first].intersect(shapes[second]).Volume()
        report['printed_assembly_intersections_mm3'][f'{first} / {second}']=round(overlap,6)
        assert overlap<1e-5, f'Assembly collision: {first} / {second}: {overlap}'
    refs.export(str(dest/'Component_envelopes_NOT_FOR_PRINT.step'))
    (dest/'validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (dest/'mesh_validation.json').write_text(json.dumps(dict(backend='CadQuery/OCP; final binary STL edge incidence and connectivity',parts=report['parts']),indent=2),encoding='utf-8')
    if standalone:
        # In this Windows environment native CAD finalizers hang at interpreter
        # shutdown. All exports are closed and all checks passed at this point.
        print('CAD_EXPORT_COMPLETE: 7 solids, 7 closed meshes, 28 collision checks passed',flush=True)
        sys.stdout.flush(); sys.stderr.flush()
        os._exit(0)
    return shapes,report


if __name__=='__main__':
    build(standalone=True)
