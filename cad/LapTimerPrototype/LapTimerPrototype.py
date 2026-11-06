"""Run in Fusion: Utilities > Scripts and Add-Ins > Scripts > add this folder.

Creates a NEW parametric document. Does not change the currently open design.
All dimensions are mm at the source boundary; Fusion internal geometry uses cm.
"""
import math
from pathlib import Path
import sys
import traceback

import adsk.core
import adsk.fusion

HERE=Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0,str(HERE))
from geometry import load_parameters, recipes, validate_layout, value


class NativeBuilder:
    def __init__(self,design,params):
        self.design=design; self.params=params; self.messages=[]

    def number(self,expression):
        if isinstance(expression,(int,float)):
            return float(expression)
        # Fusion evaluates expressions containing explicit mm units and parameter
        # names, and returns its internal length unit (cm).
        return self.design.unitsManager.evaluateExpression(self.expression(expression),'mm')*10

    def expression(self,expression):
        return f'{expression} mm' if isinstance(expression,(float,int)) or str(expression).replace('.','',1).lstrip('-').isdigit() else str(expression)

    def input(self,expression):
        return adsk.core.ValueInput.createByString(self.expression(expression))

    def plane(self,component,axis,start):
        base={'X':component.yZConstructionPlane,'Y':component.xZConstructionPlane,'Z':component.xYConstructionPlane}[axis]
        normal=base.geometry.normal
        sign={'X':normal.x,'Y':normal.y,'Z':normal.z}[axis]
        signed=start if sign>0 else f'-({self.expression(start)})'
        inp=component.constructionPlanes.createInput()
        inp.setByOffset(base,self.input(signed))
        plane=component.constructionPlanes.add(inp)
        plane.isLightBulbOn=False
        return plane,sign

    def projected(self,sketch,axis,start,u,v):
        # Determine the sketch's signed/permuted axes instead of assuming that
        # XZ/YZ sketch coordinates have a particular orientation in Fusion.
        start_mm=self.number(start)
        def project(a,b):
            xyz={'X':(start_mm,a,b),'Y':(a,start_mm,b),'Z':(a,b,start_mm)}[axis]
            return sketch.modelToSketchSpace(adsk.core.Point3D.create(*(q/10 for q in xyz)))
        origin=project(0,0); pu=project(10,0); pv=project(0,10)
        out=[]
        for key in ('x','y'):
            du=getattr(pu,key)-getattr(origin,key); dv=getattr(pv,key)-getattr(origin,key)
            expr=u if abs(du)>0.5 else v
            sign=du if abs(du)>0.5 else dv
            expr=self.expression(expr)
            out.append(expr if sign>0 else f'-({expr})')
        # Only the parameter expressions are used to constrain sketch geometry;
        # numerical positions provide a correctly oriented initial sketch.
        return project(self.number(u),self.number(v)),out

    def anchor(self,sketch,point,xexpr,yexpr):
        p=point.geometry; origin=sketch.originPoint; constraints=sketch.geometricConstraints
        if point.isFullyConstrained:
            return
        text=adsk.core.Point3D.create(p.x+0.6,p.y+0.6,0)
        orientation=adsk.fusion.DimensionOrientations
        if abs(p.x)<1e-8 and abs(p.y)<1e-8:
            constraints.addCoincident(point,origin); return
        if abs(p.x)<1e-8:
            constraints.addVerticalPoints(origin,point)
        else:
            dim=sketch.sketchDimensions.addDistanceDimension(origin,point,orientation.HorizontalDimensionOrientation,text)
            dim.parameter.expression=xexpr if p.x>0 else f'-({xexpr})'
        if abs(p.y)<1e-8:
            constraints.addHorizontalPoints(origin,point)
        else:
            dim=sketch.sketchDimensions.addDistanceDimension(origin,point,orientation.VerticalDimensionOrientation,text)
            dim.parameter.expression=yexpr if p.y>0 else f'-({yexpr})'

    def rectangle(self,sketch,op):
        p0,e0=self.projected(sketch,'Z',op['z'],op['x'],op['y'])
        p1,e1=self.projected(sketch,'Z',op['z'],f"({self.expression(op['x'])})+({self.expression(op['w'])})",f"({self.expression(op['y'])})+({self.expression(op['h'])})")
        lines=sketch.sketchCurves.sketchLines.addTwoPointRectangle(p0,p1)
        pts=[lines.item(i).startSketchPoint for i in range(lines.count)]
        target=lambda x,y:min(pts,key=lambda p:(p.geometry.x-x)**2+(p.geometry.y-y)**2)
        left,bottom=min(p0.x,p1.x),min(p0.y,p1.y)
        right,top=max(p0.x,p1.x),max(p0.y,p1.y)
        bl=target(left,bottom); br=target(right,bottom); tl=target(left,top)
        exlo,exhi=(e0[0],e1[0]) if p0.x<p1.x else (e1[0],e0[0])
        eylo,eyhi=(e0[1],e1[1]) if p0.y<p1.y else (e1[1],e0[1])
        self.anchor(sketch,bl,exlo,eylo)
        orient=adsk.fusion.DimensionOrientations
        width=sketch.sketchDimensions.addDistanceDimension(bl,br,orient.HorizontalDimensionOrientation,adsk.core.Point3D.create(left,bottom-0.5,0))
        width.parameter.expression=f'({exhi})-({exlo})'
        height=sketch.sketchDimensions.addDistanceDimension(bl,tl,orient.VerticalDimensionOrientation,adsk.core.Point3D.create(left-0.5,bottom,0))
        height.parameter.expression=f'({eyhi})-({eylo})'

    def circle(self,sketch,op):
        center,expr=self.projected(sketch,op['axis'],op['start'],op['u'],op['v'])
        circle=sketch.sketchCurves.sketchCircles.addByCenterRadius(center,self.number(op['diameter'])/20)
        self.anchor(sketch,circle.centerSketchPoint,*expr)
        dim=sketch.sketchDimensions.addDiameterDimension(circle,adsk.core.Point3D.create(center.x+0.7,center.y+0.7,0))
        dim.parameter.expression=self.expression(op['diameter'])

    def hexagon(self,sketch,op):
        points=[]
        for i in range(6):
            angle=i*math.pi/3
            # Same circumcircle and orientation as CadQuery's YZ polygon.
            u=f"({self.expression(op['u'])})+({math.cos(angle):.14g})*({self.expression(op['flats'])})/sqrt(3)"
            v=f"({self.expression(op['v'])})+({math.sin(angle):.14g})*({self.expression(op['flats'])})/sqrt(3)"
            pos,expr=self.projected(sketch,op['axis'],op['start'],u,v)
            point=sketch.sketchPoints.add(pos); self.anchor(sketch,point,*expr); points.append(point)
        for i in range(6):
            sketch.sketchCurves.sketchLines.addByTwoPoints(points[i],points[(i+1)%6])

    def part(self,name,ops):
        occ=self.design.rootComponent.occurrences.addNewComponent(adsk.core.Matrix3D.create())
        component=occ.component; component.name=name
        body=None
        for index,op in enumerate(ops):
            axis='Z' if op['kind']=='box' else op['axis']
            start=op['z'] if op['kind']=='box' else op['start']
            plane,sign=self.plane(component,axis,start)
            sketch=component.sketches.add(plane); sketch.name=f"{index+1:03d} {op['name']}"
            if op['kind']=='box': self.rectangle(sketch,op)
            elif op['kind']=='cylinder': self.circle(sketch,op)
            else: self.hexagon(sketch,op)
            if sketch.profiles.count!=1:
                raise RuntimeError(f"{name}: {op['name']} has {sketch.profiles.count} profiles")
            operations=adsk.fusion.FeatureOperations
            operation=operations.CutFeatureOperation if op['cut'] else operations.JoinFeatureOperation if body else operations.NewBodyFeatureOperation
            inp=component.features.extrudeFeatures.createInput(sketch.profiles.item(0),operation)
            length=op['d'] if op['kind']=='box' else op['length']
            extent=adsk.fusion.DistanceExtentDefinition.create(self.input(length))
            direction=adsk.fusion.ExtentDirections.PositiveExtentDirection if sign>0 else adsk.fusion.ExtentDirections.NegativeExtentDirection
            inp.setOneSideExtent(extent,direction)
            if op['cut']: inp.participantBodies=[body]
            feature=component.features.extrudeFeatures.add(inp); feature.name=op['name']
            if feature.healthState==adsk.fusion.FeatureHealthStates.ErrorFeatureHealthState:
                raise RuntimeError(feature.errorOrWarningMessage)
            if body is None: body=feature.bodies.item(0)
            sketch.isLightBulbOn=False
        if component.bRepBodies.count!=1:
            raise RuntimeError(f'{name}: expected a single solid')
        body.name=name
        return occ


def run(context):
    app=adsk.core.Application.get(); ui=app.userInterface
    try:
        params=load_parameters(); validate_layout(params)
        doc=app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        doc.name='LapTimer PETG prototype v1'
        design=adsk.fusion.Design.cast(app.activeProduct)
        design.designType=adsk.fusion.DesignTypes.ParametricDesignType
        design.unitsManager.distanceDisplayUnits=adsk.fusion.DistanceUnits.MillimeterDistanceUnits
        for name,number in params.items():
            design.userParameters.add(name,adsk.core.ValueInput.createByString(f'{number} mm'),'mm','Measured or prototype design dimension; see README')
        builder=NativeBuilder(design,params)
        output=HERE/'exports_fusion'; output.mkdir(exist_ok=True)
        for index,(name,ops) in enumerate(recipes().items()):
            occurrence=builder.part(name,ops)
            if name.startswith('04_') or name.startswith('05_'):
                # Fit gauges/spacers are separate parts, outside the main assembly.
                transform=adsk.core.Matrix3D.create(); transform.translation=adsk.core.Vector3D.create((params['case_w']+20+(index-3)*20)/10,0,0)
                occurrence.transform2=transform
            options=design.exportManager.createSTLExportOptions(occurrence.component.bRepBodies.item(0),str(output/f'{name}.stl'))
            options.meshRefinement=adsk.fusion.MeshRefinementSettings.MeshRefinementHigh
            if not design.exportManager.execute(options): raise RuntimeError(f'Export failed: {name}')
        design.computeAll()
        options=design.exportManager.createFusionArchiveExportOptions(str(output/'LapTimerPrototype.f3d'))
        if not design.exportManager.execute(options): raise RuntimeError('F3D export failed')
        app.activeViewport.fit()
        ui.messageBox('Model ustvarjen v NOVEM dokumentu.\nModify > Change Parameters za mere.\nF3D in STL: '+str(output)+f"\n\nOdprtina zaslona {params['window_w']} x {params['window_h']} mm; predpostavljena sredinska lega in prikljucki zahtevajo preizkus. Glej README.")
    except:
        ui.messageBox('Model ni bil v celoti ustvarjen. Obstojece datoteke niso bile spremenjene.\n'+traceback.format_exc())
