"""Fallback mesh exporter and model preview. Run with Blender --background --python.

Uses the same solid recipe as the native Fusion generator. No AI image assets.
"""
from pathlib import Path
import json
import math
import sys
import bpy
import bmesh
from mathutils import Vector

HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from geometry import load_parameters, recipes, references, validate_layout, value

P=load_parameters()
OUT=HERE/'exports'
OUT.mkdir(exist_ok=True)


def primitive(op):
    v=lambda n:value(op[n],P)
    if op['kind']=='hexagon':
        radius=v('flats')/math.sqrt(3)
        vertices=[(x,v('u')+radius*math.cos(i*math.pi/3),v('v')+radius*math.sin(i*math.pi/3))
                  for x in (v('start'),v('start')+v('length')) for i in range(6)]
        faces=[tuple(reversed(range(6))),tuple(range(6,12))]
        faces += [(i,(i+1)%6,(i+1)%6+6,i+6) for i in range(6)]
        mesh=bpy.data.meshes.new(op['name']); mesh.from_pydata(vertices,[],faces); mesh.update()
        obj=bpy.data.objects.new(op['name'],mesh); bpy.context.collection.objects.link(obj)
        return obj
    if op['kind']=='box':
        bpy.ops.mesh.primitive_cube_add(size=1,location=(v('x')+v('w')/2,v('y')+v('h')/2,v('z')+v('d')/2))
        obj=bpy.context.object; obj.dimensions=(v('w'),v('h'),v('d'))
    else:
        axis=op['axis']; start=v('start'); length=v('length'); u=v('u'); w=v('v')
        center={'X':(start+length/2,u,w),'Y':(u,start+length/2,w),'Z':(u,w,start+length/2)}[axis]
        rotate={'X':(0,math.pi/2,0),'Y':(-math.pi/2,0,0),'Z':(0,0,0)}[axis]
        vertices=6 if op['kind']=='hexagon' else 96
        radius=v('flats')/math.sqrt(3) if op['kind']=='hexagon' else v('diameter')/2
        bpy.ops.mesh.primitive_cylinder_add(vertices=vertices,radius=radius,depth=length,end_fill_type='NGON',location=center,rotation=rotate)
        obj=bpy.context.object
        if op['kind']=='hexagon':
            # CadQuery YZ polygon starts on +Y; cylinder's rotated +X is -Z.
            obj.rotation_euler.rotate_axis('Z',math.pi/2)
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    obj.name=op['name']
    return obj


def material(name,color):
    mat=bpy.data.materials.new(name); mat.diffuse_color=(*color,1)
    return mat


def export_mesh(obj,path):
    bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
    bpy.ops.wm.stl_export(filepath=str(path),export_selected_objects=True,ascii_format=False)


def build():
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    report={'layout':validate_layout(P),'parts':{},'backend':'Blender exact Boolean; STEP geometry is generated separately by CadQuery when available'}
    objects={}
    colors=[(0.20,0.23,0.27),(0.10,0.53,0.67),(0.90,0.46,0.12),(0.45,0.48,0.52)]
    for i,(name,ops) in enumerate(recipes().items()):
        obj=None
        for op in ops:
            tool=primitive(op)
            if obj is None: obj=tool; continue
            bpy.context.view_layer.objects.active=obj
            modifier=obj.modifiers.new(name=op['name'],type='BOOLEAN')
            modifier.operation='DIFFERENCE' if op['cut'] else 'UNION'; modifier.solver='EXACT'; modifier.object=tool
            bpy.ops.object.modifier_apply(modifier=modifier.name)
            bpy.data.objects.remove(tool,do_unlink=True)
        obj.name=name
        mesh=bmesh.new(); mesh.from_mesh(obj.data)
        bmesh.ops.remove_doubles(mesh,verts=list(mesh.verts),dist=1e-6)
        bmesh.ops.recalc_face_normals(mesh,faces=list(mesh.faces))
        bad=sum(not edge.is_manifold for edge in mesh.edges)
        volume=mesh.calc_volume(signed=False)
        assert not bad, f'{name}: {bad} nonmanifold edges'
        assert volume>0
        remaining=set(mesh.verts); connected=0
        while remaining:
            connected+=1; queue=[remaining.pop()]
            while queue:
                vertex=queue.pop()
                for edge in vertex.link_edges:
                    other=edge.other_vert(vertex)
                    if other in remaining: remaining.remove(other); queue.append(other)
        assert connected==1, f'{name}: {connected} disconnected components'
        mesh.to_mesh(obj.data); mesh.free()
        obj.data.materials.clear()
        obj.data.materials.append(material(name,colors[min(i,3)]))
        for polygon in obj.data.polygons: polygon.material_index=0
        export_mesh(obj,OUT/f'{name}.stl')
        report['parts'][name]={'watertight_stl':True,'connected_components':connected,'volume_cm3':round(volume/1000,3),'size_mm':[round(x,3) for x in obj.dimensions]}
        objects[name]=obj
        print(name,report['parts'][name],flush=True)
    # Neutral and tilted interface collision checks against the printed housing.
    report['hinge_housing_collision_mm3']={}
    original=objects['04_Hinge_tongue_fit_gauge']
    from mathutils import Matrix
    for angle in (-90,-60,-45,0,45,60,90):
        probe=original.copy(); probe.data=original.data.copy(); bpy.context.collection.objects.link(probe)
        position=Vector(((P['case_w']-P['tongue_t'])/2,P['pivot_y']-22,P['pivot_z']-P['pivot_d']/2))
        pivot=Vector((P['case_w']/2,P['pivot_y'],P['pivot_z']))
        probe.matrix_world=Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(angle),4,'X') @ Matrix.Translation(-pivot) @ Matrix.Translation(position) @ original.matrix_world
        bpy.context.view_layer.objects.active=probe
        modifier=probe.modifiers.new(name='Fit check',type='BOOLEAN'); modifier.operation='INTERSECT'; modifier.solver='EXACT'; modifier.object=objects['01_Housing']
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        mesh=bmesh.new(); mesh.from_mesh(probe.data); overlap=mesh.calc_volume(signed=False); mesh.free()
        assert overlap<1e-4, f'Hinge collision: {angle} deg, {overlap} mm3'
        report['hinge_housing_collision_mm3'][str(angle)]=round(overlap,6)
        bpy.data.objects.remove(probe,do_unlink=True)
    (OUT/'mesh_validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    return objects


def preview(objects):
    scene=bpy.context.scene
    scene.render.engine='BLENDER_WORKBENCH'
    scene.render.resolution_x=1400; scene.render.resolution_y=1100; scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'
    shading=scene.display.shading
    shading.light='STUDIO'; shading.color_type='MATERIAL'; shading.show_shadows=True
    shading.show_cavity=True; shading.cavity_type='BOTH'; shading.show_specular_highlight=True
    shading.background_type='WORLD'; scene.world.color=(0.85,0.87,0.9)
    shading.show_object_outline=True
    bpy.ops.object.camera_add()
    camera=bpy.context.object; scene.camera=camera; camera.data.type='ORTHO'; camera.data.lens=50
    def render(name,location,target,scale):
        camera.location=location; camera.rotation_euler=(Vector(target)-camera.location).to_track_quat('-Z','Y').to_euler()
        camera.data.ortho_scale=scale; camera.data.clip_end=3000
        scene.render.filepath=str(OUT/name); bpy.ops.render.render(write_still=True)
    for name,obj in objects.items():
        obj.hide_render=name.startswith(('04_','05_'))
    render('preview_assembled.png',(245,-230,310),(60,42,25),210)
    objects['02_Front_bezel'].hide_render=True
    render('preview_inside.png',(210,-155,280),(60,48,18),205)
    objects['02_Front_bezel'].hide_render=False
    for name,obj in objects.items(): obj.hide_render=name!='02_Front_bezel'
    render('preview_bezel_back.png',(220,200,-180),(60,54,48),180)
    for name,obj in objects.items(): obj.hide_render=name.startswith(('04_','05_'))
    objects['03_Electronics_carrier'].location.z+=70
    objects['02_Front_bezel'].location.z+=105
    for ref in references(P):
        primitive_op=dict(kind='box',name=ref['name'],x=ref['x'],y=ref['y'],z=ref['z'],w=ref['w'],h=ref['h'],d=ref['d'])
        obj=primitive(primitive_op)
        obj.location.z+=105 if ref['name'].startswith(('Display','Antenna')) else 70
        obj.data.materials.append(material(ref['name'],ref['color']))
        if ref['name'].startswith('Display'):
            # Only display's bounding envelope is known. Mark it as a reference.
            obj.hide_render=True
            screen=primitive(dict(kind='box',name='Display reference face',x=(P['case_w']-P['screen_w'])/2,y=P['screen_y'],z=P['case_d']-2,w=P['screen_w'],h=P['screen_h'],d=2))
            screen.location.z+=105; screen.data.materials.append(material('Display reference',(0.12,0.35,0.4)))
    render('preview_exploded.png',(315,-320,360),(60,42,78),300)
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'LapTimer_preview.blend'))


if __name__=='__main__':
    if '--preview-only' in sys.argv:
        bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
        objects={}
        colors=[(0.20,0.23,0.27),(0.10,0.53,0.67),(0.90,0.46,0.12),(0.45,0.48,0.52)]
        for index,name in enumerate(recipes()):
            bpy.ops.wm.stl_import(filepath=str(OUT/f'{name}.stl'))
            obj=bpy.context.object; obj.name=name
            obj.data.materials.clear(); obj.data.materials.append(material(name,colors[min(index,3)]))
            for polygon in obj.data.polygons: polygon.material_index=0
            objects[name]=obj
        preview(objects)
    else:
        preview(build())
