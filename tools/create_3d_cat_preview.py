"""Refined experimental kitten, articulated clips and native runtime export.

Blender 4.3.2: blender -b -t 4 --python-exit-code 1 --python
  tools/create_3d_cat_preview.py -- --output build/3d-preview-model --render
This generates a review asset, not an automatic replacement for the 2D pet.
"""
import argparse
import json
import math
import random
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import create_3d_cat_prototype as base

FPS = 30
CLIPS = {"idle": (90, True), "walk": (36, True), "run": (24, True),
         "stalk": (54, True), "belly": (129, False), "airborne": (30, False),
         "landing": (9, False), "ear": (60, False), "paw": (72, False)}
STRIDES = {"walk": .48, "run": .72, "stalk": .36}


def mix(a, b, t):
    return tuple(x * (1-t) + y*t for x, y in zip(a, b))


def smooth(a, b, x):
    return base.smoothstep(a, b, x)


def solve_leg(hip, ankle, upper, lower, bend):
    # Analytic two-bone IK in the sagittal plane, with a fixed paw target.
    d = ankle-hip
    distance = min(upper+lower-1e-5, max(abs(upper-lower)+1e-5, d.length))
    direction = d.normalized()
    a = (upper*upper-lower*lower+distance*distance)/(2*distance)
    h = math.sqrt(max(0, upper*upper-a*a))
    perpendicular = Vector((0, -direction.z, direction.y)).normalized()
    return hip+direction*a+perpendicular*(h*bend)


def materials():
    coat = base.material("SoftIvoryCoat", (.86, .74, .54), .9)
    attr = coat.node_tree.nodes.new("ShaderNodeVertexColor")
    attr.layer_name = "CoatColor"
    coat.node_tree.links.new(attr.outputs["Color"], coat.node_tree.nodes.get("Principled BSDF").inputs["Base Color"])
    return {"coat": coat, "ivory": base.material("MuzzleIvory", (.92, .84, .69), .88),
            "pink": base.material("WarmPink", (.68, .26, .25), .65),
            "iris": base.material("AmberIris", (.105, .036, .009), .24),
            "pupil": base.material("DeepBrownPupil", (.006, .003, .0015), .15),
            "glint": base.material("SoftEyeGlint", (1, .95, .82), .15),
            "mouth": base.material("Mouth", (.12, .047, .024), .8)}


def coat_color(p):
    x, y, z = p
    ivory, ginger = (.88, .79, .63), (.72, .405, .155)
    top = smooth(.99, 1.16, z)*smooth(.065, .14, abs(x))
    back = smooth(.59, .71, z)*smooth(-.15, .09, y)
    tail = smooth(.48, .59, y)
    amount = max(top, back, tail)
    stripe = .5+.5*math.cos((y*21 if y>.38 else x*38)+z*3)
    ginger = mix(ginger, (.48, .23, .065), .28*smooth(.72, .94, stripe))
    color = mix(ivory, ginger, amount)
    if y>.60:
        color = mix(color, ivory, smooth(1.04, 1.13, z))
    return (*color, 1)


def paint(obj):
    color = obj.data.color_attributes.new(name="CoatColor", type="FLOAT_COLOR", domain="POINT")
    for vertex, datum in zip(obj.data.vertices, color.data):
        datum.color = coat_color(obj.matrix_world @ vertex.co)


def ear(side, inset=False):
    obj = base.ear_mesh(side, inset)
    for v in obj.data.vertices:
        v.co.x *= .86
        v.co.y += .20
        v.co.z = 1.13+(v.co.z-1.34)*.77
    return obj


def capsule(name, a, b, width):
    a, b = Vector(a), Vector(b)
    obj = base.ellipsoid(name, (a+b)/2, (width, width, (b-a).length/2+width*.58))
    obj.rotation_euler = (b-a).to_track_quat("Z", "Y").to_euler()
    return obj


def rig_create():
    data = bpy.data.armatures.new("KittenSkeleton")
    rig = bpy.data.objects.new("KittenRig", data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    specs, legs = {}, {}

    def add(name, a, b, parent=None):
        bone = data.edit_bones.new(name)
        bone.head, bone.tail = a, b
        if parent:
            bone.parent = data.edit_bones[parent]
        specs[name] = (Vector(a), Vector(b))

    add("root", (0, 0, 0), (0, 0, .1))
    data.edit_bones["root"].use_deform = False
    add("pelvis", (0, .29, .46), (0, .10, .48), "root")
    add("spine", (0, .10, .48), (0, -.12, .51), "pelvis")
    add("chest", (0, -.12, .51), (0, -.27, .62), "spine")
    add("neck", (0, -.27, .62), (0, -.40, .86), "chest")
    add("head", (0, -.40, .86), (0, -.40, 1.10), "neck")
    for side, label in ((1, "L"), (-1, "R")):
        add(f"ear.{label}", (side*.2365, -.39, 1.145), (side*.314, -.368, 1.438), "head")
        add(f"eye.{label}", (side*.165, -.708, .98), (side*.165, -.708, 1.045), "head")
        for kind, y, ankle_y, lengths, bend in (
                ("front", -.22, -.245, (.205, .188), 1),
                ("back", .28, .29, (.225, .18), -1)):
            hip = Vector((side*.205, y, .455))
            ankle = Vector((side*.205, ankle_y, .105))
            knee = solve_leg(hip, ankle, *lengths, bend)
            upper, lower, paw = [f"{kind}_{part}.{label}" for part in ("upper", "lower", "paw")]
            add(upper, hip, knee, "chest" if kind=="front" else "pelvis")
            add(lower, knee, ankle, upper)
            add(paw, ankle, ankle+Vector((0, -.10, -.035)), lower)
            legs[(kind, label)] = (hip, ankle, lengths, bend)
    tail = [(0, .40, .51), (.01, .59, .64), (.10, .73, .82),
            (.22, .76, 1.00), (.26, .69, 1.115)]
    for i, (a, b) in enumerate(zip(tail, tail[1:])):
        add(f"tail.{i}", a, b, "pelvis" if i==0 else f"tail.{i-1}")
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    for p in rig.pose.bones:
        p.rotation_mode = "QUATERNION"
    return rig, specs, legs, tail


def weights_for(p, specs, skull=False):
    x, y, z = p
    # High tail vertices share the ears' height, but must never follow the
    # head. Anatomical regions need depth as well as height/lateral position.
    if skull:
        if z>1.14 and abs(x)>.16:
            amount = smooth(1.14, 1.26, z)
            return {"head":1-amount, "ear.L" if x>0 else "ear.R":amount}
        return {"head":1}
    if y>.48 and z>.48:
        names = [f"tail.{i}" for i in range(4)]
    elif z>.54 and y<-.10:
        amount=smooth(.54,.72,z)
        return {"chest":1-amount,"neck":amount}
    elif z<.42 and abs(x)>.09:
        kind, label = ("front" if y<0 else "back"), ("L" if x>0 else "R")
        if z<.12:
            return {f"{kind}_paw.{label}":1}
        names = [f"{kind}_{part}.{label}" for part in ("upper", "lower", "paw")]
        if z>.26:
            names.append("chest" if y<0 else "pelvis")
    else:
        names = ["pelvis", "spine", "chest", "neck"]
    return {name:1/(.024+base.distance_to_segment(p, *specs[name]))**5 for name in names}


def make_model(rig, specs, legs, tail, mats):
    parts = [base.ellipsoid("Body", (0, .025, .47), (.305, .465, .265)),
             base.ellipsoid("ShortNeck", (0, -.28, .65), (.235, .235, .235))]
    skull_parts=[base.ellipsoid("RoundedHead", (0, -.40, .945), (.425, .337, .36))]
    for side in (-1, 1):
        skull_parts.append(base.ellipsoid("Cheek", (side*.245, -.50, .83), (.18, .185, .17)))
        skull_parts.append(ear(side))
    for (kind, label), (hip, ankle, lengths, bend) in legs.items():
        knee = solve_leg(hip, ankle, *lengths, bend)
        parts.extend([capsule("UpperLeg", hip, knee, .077 if kind=="front" else .106),
                      capsule("LowerLeg", knee, ankle, .065),
                      base.ellipsoid("RoundedPaw", ankle+Vector((0, -.045, -.035)), (.105, .135, .070))])
    parts.append(base.tube("FluffyTail", tail, .092, mats["coat"]))
    parts.append(base.ellipsoid("TailEnd", tail[-1], (.092,)*3))
    # Preserve anatomical ownership through remeshing. The skull is a rigid
    # volume overlapping the neck; its jaw/occiput cannot inherit chest weights.
    joined=[]
    for group,is_skull in ((parts,False),(skull_parts,True)):
        bpy.ops.object.select_all(action="DESELECT")
        for obj in group:
            obj.select_set(True)
        bpy.context.view_layer.objects.active=group[0]
        bpy.ops.object.join()
        surface=bpy.context.object
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        mod=surface.modifiers.new("ConnectedSurface","REMESH")
        mod.mode,mod.voxel_size,mod.use_smooth_shade="VOXEL",.016,True
        bpy.ops.object.modifier_apply(modifier=mod.name)
        mod=surface.modifiers.new("SoftSurface","SMOOTH")
        mod.factor,mod.iterations=.6,4
        bpy.ops.object.modifier_apply(modifier=mod.name)
        surface.data.materials.clear()
        surface.data.materials.append(mats["coat"])
        paint(surface)
        base.bind(surface,rig,lambda p,region=is_skull:weights_for(p,specs,region))
        joined.append(surface)
    bpy.ops.object.select_all(action="DESELECT")
    for obj in joined:
        obj.select_set(True)
    bpy.context.view_layer.objects.active=joined[0]
    bpy.ops.object.join()
    body=bpy.context.object
    body.name="CreamKitten"
    objects = [body]
    for side, label in ((1, "L"), (-1, "R")):
        inner = ear(side, True)
        inner.name = f"PinkEar.{label}"
        inner.data.materials.append(mats["pink"])
        base.bind(inner, rig, lambda p, bone=f"ear.{label}": {"head":1-smooth(1.14,1.26,p.z), bone:smooth(1.14,1.26,p.z)})
        objects.append(inner)

        def add(obj, bone="head"):
            base.bind(obj, rig, lambda _, b=bone: {b:1})
            objects.append(obj)

        add(base.ellipsoid(f"Iris.{label}", (side*.165, -.696, .985), (.119, .038, .133), mats["iris"]), f"eye.{label}")
        add(base.ellipsoid(f"Pupil.{label}", (side*.162, -.726, .991), (.079, .012, .108), mats["pupil"]), f"eye.{label}")
        add(base.ellipsoid(f"Glint.{label}", (side*.162-.025, -.739, 1.035), (.023, .006, .025), mats["glint"]), f"eye.{label}")
        add(base.ellipsoid(f"GlintSmall.{label}", (side*.162+.025, -.739, .952), (.008, .004, .009), mats["glint"]), f"eye.{label}")
        add(base.ellipsoid(f"Muzzle.{label}", (side*.063, -.715, .854), (.083, .066, .054), mats["ivory"]))
        add(base.tube(f"Smile.{label}", [(0,-.786,.870),(side*.021,-.780,.842),(side*.049,-.769,.849)], .0032, mats["mouth"]))
        for i in range(3):
            add(base.tube(f"Whisker.{label}.{i}", [(side*.13,-.701,.855-i*.018),
                     (side*.26,-.729,.863-i*.027), (side*.37,-.71,.88-i*.04)], .0015, mats["ivory"]))
    nose = base.ellipsoid("Nose", (0,-.784,.886), (.034,.014,.023), mats["pink"])
    for v in nose.data.vertices:
        v.co.x *= .55+.45*smooth(-.023,.023,v.co.z)
    base.bind(nose, rig, lambda _: {"head":1})
    objects.append(nose)
    # Short tapered tufts follow the surface and carry the same skin weights.
    rng = random.Random(203)
    verts, faces, fur_weights = [], [], []
    candidates = list(body.data.vertices)
    rng.shuffle(candidates)
    for v in candidates[:3800]:
        p, n = v.co.copy(), v.normal.normalized()
        if (p.y<-.64 and .77<p.z<1.14) or p.z<.12:
            continue
        tangent = n.cross(Vector((0,0,1)))
        if tangent.length<.01:
            tangent = n.cross(Vector((1,0,0)))
        tangent.normalize()
        length = rng.uniform(.006,.015)
        width = rng.uniform(.0015,.0030)
        tip = p+n*length+Vector((0,.002,-.002))
        for side in (tangent, n.cross(tangent).normalized()):
            idx = len(verts)
            verts.extend([p-side*width, p+side*width, tip])
            influences={body.vertex_groups[g.group].name:g.weight for g in v.groups if g.weight>0}
            fur_weights.extend([influences]*3)
            faces.append((idx, idx+1, idx+2))
    mesh = bpy.data.meshes.new("ShortFurGeometry")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    fur = bpy.data.objects.new("ShortFur", mesh)
    bpy.context.collection.objects.link(fur)
    mesh.materials.append(mats["coat"])
    paint(fur)
    weight_iterator=iter(fur_weights)
    base.bind(fur, rig, lambda _:next(weight_iterator))
    objects.append(fur)
    return body, objects


def aim(rig, name, head, tail):
    bone = rig.data.bones[name]
    rotation = (bone.tail_local-bone.head_local).rotation_difference(tail-head)
    matrix = rotation.to_matrix().to_4x4() @ bone.matrix_local
    matrix.translation = head
    rig.pose.bones[name].matrix = matrix
    bpy.context.view_layer.update()


def set_world_offset(rig, name, offset):
    mat = rig.pose.bones[name].matrix.copy()
    mat.translation += Vector(offset)
    rig.pose.bones[name].matrix = mat
    bpy.context.view_layer.update()


def pose_frame(rig, legs, name, t):
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    turn = 0
    if name=="belly":
        if t<.22:
            roll = 0
        elif t<.52:
            roll = math.pi*smooth(.22,.52,t)
        elif t<.73:
            roll = math.pi+math.radians(4)*math.sin((t-.52)/.21*math.tau)
        else:
            roll = math.pi*(1-smooth(.73,1,t))
        turn = min(1, roll/math.pi)
        # Lower first, then roll around the long axis; never somersault.
        fold = smooth(0,.20,t)*(1-smooth(.80,1,t))
        # Bring the arched back into the support plane. Compensate at the
        # chest so the neck attachment keeps the same overall placement.
        for axis, shift in (("spine",.14), ("chest",.07)):
            set_world_offset(rig,axis,(0,0,shift*turn))
        neck_start = rig.pose.bones["neck"].head.copy()
        head_pivot = Vector((0, -.40-.025*turn, .86-.31*turn-.035*fold*(1-turn)))
        aim(rig,"neck",neck_start,head_pivot)
        head = Matrix.Rotation(math.radians(86)*turn,4,"X") @ rig.data.bones["head"].matrix_local
        head.translation = head_pivot
        rig.pose.bones["head"].matrix = head
        bpy.context.view_layer.update()
        for (kind,label),(hip,ankle,lengths,bend) in legs.items():
            target = ankle+Vector((0,(-.08 if kind=="front" else .03)*fold,.23*fold))
            if turn>.7:
                target.z += .018*math.sin(t*26+(0 if label=="L" else math.pi))
            joint_leg(rig,kind,label,target,lengths,bend)
        # Curl to the upper side before rolling, preserving segment lengths.
        # Leaving an upright tail unchanged would turn it into a floor prop.
        directions=[Vector((0,.18,-.04)),Vector((-.17,.14,-.03)),
                    Vector((-.19,-.10,0)),Vector((-.05,-.12,.02))]
        tail_head=rig.pose.bones["tail.0"].head.copy()
        for i in range(4):
            bone=rig.data.bones[f"tail.{i}"]
            rest=bone.tail_local-bone.head_local
            direction=rest.normalized().lerp(directions[i].normalized(),fold).normalized()
            tail_end=tail_head+direction*rest.length
            aim(rig,f"tail.{i}",tail_head,tail_end)
            tail_head=tail_end
        root = Matrix.Translation((0,0,.40)) @ Matrix.Rotation(roll,4,"Y") @ Matrix.Translation((0,0,-.40))
        rig.pose.bones["root"].matrix = root @ rig.data.bones["root"].matrix_local
    else:
        down = .065*math.sin(math.pi*t)**2 if name=="landing" else 0
        if name in STRIDES:
            down += (.012 if name=="stalk" else .004)*(1-math.cos(t*math.tau*2))
            if name=="run":
                down += .035
        root = rig.data.bones["root"].matrix_local.copy()
        root.translation.z -= down
        rig.pose.bones["root"].matrix = root
        bpy.context.view_layer.update()
        for (kind,label),(hip,ankle,lengths,bend) in legs.items():
            target = ankle.copy()
            if name in STRIDES:
                duty = {"walk":.68,"run":.55,"stalk":.78}[name]
                offsets = {("back","L"):0,("front","L"):.25,("back","R"):.5,("front","R"):.75}
                if name=="run":
                    offsets = {("back","L"):0,("front","R"):0,("back","R"):.5,("front","L"):.5}
                phase = (t+offsets[(kind,label)])%1
                stride = STRIDES[name]
                if phase<duty:
                    # stride is ROOT travel per whole cycle. During stance,
                    # relative paw travel cancels exactly that root travel.
                    target.y += stride*(phase-.5*duty)
                else:
                    swing=(phase-duty)/(1-duty)
                    # Hermite endpoints preserve ground-relative velocity at
                    # lift-off/touchdown instead of snapping to zero velocity.
                    t2,t3=swing*swing,swing*swing*swing
                    target.y += stride*(.5*duty*(2*t3-3*t2+1)
                                  -.5*duty*(-2*t3+3*t2)
                                  +(1-duty)*(t3-2*t2+swing+t3-t2))
                    target.z += {"walk":.048,"run":.085,"stalk":.029}[name]*math.sin(math.pi*swing)**2
            elif name=="airborne":
                tuck = math.sin(math.pi*t)**2
                # Resting endpoints meet the physical contact plane. A fixed
                # ankle lift here causes a visible drop when Box2D lands.
                target.z += .16*tuck
                target.y += (-.05 if kind=="front" else .035)*math.sin(math.pi*t)
            elif name=="paw" and kind=="front" and label=="L":
                raised=smooth(.05,.25,t)*(1-smooth(.72,.98,t))
                tap=math.sin(math.tau*max(0,min(1,(t-.3)/.35)))**2
                target += Vector((0,-.14*raised,raised*(.13-.025*tap)))
            joint_leg(rig,kind,label,target,lengths,bend)
        if name=="ear":
            pulse = math.sin(math.pi*t)**2*math.sin(t*math.tau*3)
            for label,factor in (("L",.3),("R",-.22)):
                rig.pose.bones[f"ear.{label}"].rotation_quaternion = Matrix.Rotation(factor*pulse,4,"X").to_quaternion()
    # Local eyelid action: the eyeball assembly closes inside the unchanged head.
    blink = max(0,1-abs(t-.67)/.055) if name in ("idle","ear") else (.92*turn if name=="belly" else 0)
    for label in ("L","R"):
        rig.pose.bones[f"eye.{label}"].scale.y = 1-.97*blink
    bpy.context.view_layer.update()


def joint_leg(rig,kind,label,ankle,lengths,bend):
    upper,lower,paw = [f"{kind}_{part}.{label}" for part in ("upper","lower","paw")]
    hip=rig.pose.bones[upper].head.copy()
    knee=solve_leg(hip,ankle,*lengths,bend)
    aim(rig,upper,hip,knee)
    aim(rig,lower,knee,ankle)
    mat=rig.data.bones[paw].matrix_local.copy()
    mat.translation=ankle
    rig.pose.bones[paw].matrix=mat
    bpy.context.view_layer.update()


def grounded_min(objects):
    deps=bpy.context.evaluated_depsgraph_get()
    result=1e9
    for obj in objects:
        # Fur and whiskers are visual detail, not structural contact points.
        if obj.name in ("ShortFur",) or obj.name.startswith("Whisker"):
            continue
        evaluated=obj.evaluated_get(deps)
        mesh=evaluated.to_mesh()
        result=min(result,min((evaluated.matrix_world@v.co).z for v in mesh.vertices))
        evaluated.to_mesh_clear()
    return result


def settle_reclining_head(rig,body,head_vertices,support_vertices,t):
    """Small head-contact IK; keep skull and nape on the same support plane."""
    influence=smooth(.32,.52,t)*(1-smooth(.73,.92,t))
    if influence<=0:
        return 0.0
    correction=0.0
    for _ in range(6):
        evaluated=body.evaluated_get(bpy.context.evaluated_depsgraph_get())
        mesh=evaluated.to_mesh()
        head_low=min(mesh.vertices[i].co.z for i in head_vertices)
        support_low=min(mesh.vertices[i].co.z for i in support_vertices)
        evaluated.to_mesh_clear()
        difference=head_low-support_low
        if abs(difference)<.0001:
            break
        shift=max(-.08-correction,min(.08-correction,-difference*influence))
        if abs(shift)<1e-6:
            break
        head=rig.pose.bones["head"].matrix.copy()
        head.translation.z+=shift
        neck_start=rig.pose.bones["neck"].head.copy()
        aim(rig,"neck",neck_start,head.translation)
        rig.pose.bones["head"].matrix=head
        bpy.context.view_layer.update()
        correction+=shift
    return correction


def animate(rig,body,objects,legs):
    rig.animation_data_create()
    actions,report={},{}
    head_vertices,support_vertices=[],[]
    for vertex in body.data.vertices:
        weights={body.vertex_groups[g.group].name:g.weight for g in vertex.groups}
        if weights.get("head",0)>.99:
            head_vertices.append(vertex.index)
        elif not any(w>.05 for name,w in weights.items() if name=="head" or name.startswith("tail") or name.startswith("ear")):
            support_vertices.append(vertex.index)
    assert head_vertices and support_vertices
    for name,(steps,loop) in CLIPS.items():
        action=bpy.data.actions.new(name)
        action.use_fake_user=True
        rig.animation_data.action=action
        contacts=[]
        head_corrections=[]
        for frame in range(steps+1):
            # Force the dependency graph out of the preceding keyed pose first.
            bpy.context.scene.frame_set(frame+1)
            pose_frame(rig,legs,name,frame/steps)
            if name=="belly":
                head_corrections.append(settle_reclining_head(rig,body,head_vertices,support_vertices,frame/steps))
            if name!="airborne":
                floor=grounded_min(objects)
                root=rig.pose.bones["root"].matrix.copy()
                root.translation.z-=floor
                rig.pose.bones["root"].matrix=root
                bpy.context.view_layer.update()
                contacts.append(grounded_min(objects))
            for bone in rig.pose.bones:
                bone.keyframe_insert("location",frame=frame+1,group=bone.name)
                bone.keyframe_insert("rotation_quaternion",frame=frame+1,group=bone.name)
                bone.keyframe_insert("scale",frame=frame+1,group=bone.name)
        for curve in action.fcurves:
            for key in curve.keyframe_points:
                key.interpolation="LINEAR"
        actions[name]=action
        report[name]={"frames":steps+1,"fps":FPS,"loop":loop,
                      "ground_min_error":max((abs(v) for v in contacts),default=None),
                      "stride":STRIDES.get(name),
                      "max_head_contact_adjustment":max((abs(v) for v in head_corrections),default=0)}
        print("BAKED",name,report[name],flush=True)
    rig.animation_data.action=actions["idle"]
    bpy.context.scene.frame_set(1)
    return actions,report


def flatten(matrix):
    return [v for row in matrix for v in row]


def trs(matrix):
    loc,rot,scale=matrix.decompose()
    return (*loc,rot.x,rot.y,rot.z,rot.w,*scale)


def export_native(path,rig,objects,actions):
    bones=list(rig.data.bones)
    indices={bone.name:i for i,bone in enumerate(bones)}
    vertices,triangles=[],[]
    object_ranges=[]
    for obj in objects:
        mesh=obj.data
        mesh.calc_loop_triangles()
        attr=mesh.color_attributes.get("CoatColor")
        # Use shared mesh vertices. All materials in each object are uniform.
        offset=len(vertices)
        mat=mesh.materials[0]
        principled=mat.node_tree.nodes.get("Principled BSDF")
        color=tuple(principled.inputs["Base Color"].default_value)
        roughness=principled.inputs["Roughness"].default_value
        for v in mesh.vertices:
            position=obj.matrix_world@v.co
            normal=(obj.matrix_world.to_3x3().inverted().transposed()@v.normal).normalized()
            color_value=tuple(attr.data[v.index].color) if attr else color
            weighted=[(indices[obj.vertex_groups[g.group].name],g.weight) for g in v.groups if g.weight>0]
            weighted=sorted(weighted,key=lambda a:a[1],reverse=True)[:4]
            total=sum(w for _,w in weighted)
            if obj.name in ("CreamKitten","ShortFur") and position.y>.60:
                assert all(bones[i].name.startswith("tail.") for i,w in weighted if w>.001), \
                    ("Tail vertex attached outside tail",tuple(position),weighted)
            if obj.name in ("CreamKitten","ShortFur") and .90<position.z<1.13 and position.y<0:
                assert all(bones[i].name=="head" for i,w in weighted if w>.001), \
                    ("Cranium vertex detached from head",tuple(position),weighted)
            weighted=[(i,w/total) for i,w in weighted]+[(0,0)]*(4-len(weighted))
            uv=(math.atan2(position.x,position.y)/(2*math.pi)+.5,position.z)
            vertices.append(struct.pack("<13f4H4f",*position,*normal,*uv,*color_value,roughness,
                                        *(i for i,_ in weighted),*(w for _,w in weighted)))
        for tri in mesh.loop_triangles:
            triangles.extend(offset+i for i in tri.vertices)
        object_ranges.append({"name":obj.name,"first_vertex":offset,"vertex_count":len(mesh.vertices),
                              "structural_contact":obj.name!="ShortFur" and not obj.name.startswith("Whisker")})
    with path.open("wb") as out:
        out.write(b"YPC3DM01")
        out.write(struct.pack("<5I",1,len(vertices),len(triangles),len(bones),len(actions)))
        for data in vertices:
            out.write(data)
        out.write(struct.pack(f"<{len(triangles)}I",*triangles))
        for bone in bones:
            parent=indices[bone.parent.name] if bone.parent else -1
            local=bone.parent.matrix_local.inverted()@bone.matrix_local if bone.parent else bone.matrix_local
            out.write(struct.pack("<i26f",parent,*flatten(bone.matrix_local.inverted()),*trs(local)))
        for name,action in actions.items():
            rig.animation_data.action=action
            steps,loop=CLIPS[name]
            encoded=name.encode("utf-8")
            out.write(struct.pack("<I",len(encoded))+encoded+struct.pack("<IfI",steps+1,FPS,int(loop)))
            for frame in range(steps+1):
                bpy.context.scene.frame_set(frame+1)
                for bone in bones:
                    pb=rig.pose.bones[bone.name]
                    matrix=pb.parent.matrix.inverted()@pb.matrix if pb.parent else pb.matrix
                    out.write(struct.pack("<10f",*trs(matrix)))
    rig.animation_data.action=actions["idle"]
    bpy.context.scene.frame_set(1)
    return {"vertices":len(vertices),"triangles":len(triangles)//3,"bones":len(bones),"bytes":path.stat().st_size,
            "bone_names":[b.name for b in bones],"objects":object_ranges}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=Path("build/3d-preview-model"))
    parser.add_argument("--render",action="store_true")
    parser.add_argument("--animate",action="store_true")
    args=parser.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])
    output=args.output.resolve()
    output.mkdir(parents=True,exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    mats=materials()
    rig,specs,legs,tail=rig_create()
    body,objects=make_model(rig,specs,legs,tail,mats)
    scene=bpy.context.scene
    scene.render.fps=FPS
    actions,report=animate(rig,body,objects,legs)
    manifest=export_native(output/"cat3d.bin",rig,objects,actions)
    scene=base.setup_studio()
    scene.render.fps=FPS
    scene.cycles.samples=64
    scene.camera.data.ortho_scale=2.15
    scene.camera.location=(2.7,-4.1,1.85)
    scene.camera.rotation_euler=(Vector((0,-.02,.72))-scene.camera.location).to_track_quat("-Z","Y").to_euler()
    scene.frame_start,scene.frame_end=1,CLIPS["idle"][0]+1
    bpy.ops.object.select_all(action="DESELECT")
    for obj in [rig,*objects]:
        obj.select_set(True)
    bpy.context.view_layer.objects.active=rig
    bpy.ops.wm.save_as_mainfile(filepath=str(output/"cream-kitten-preview.blend"))
    bpy.ops.export_scene.gltf(filepath=str(output/"cream-kitten-preview.glb"),export_format="GLB",
                             use_selection=True,export_animations=True,export_animation_mode="ACTIONS",
                             export_frame_range=False,export_cameras=False,export_lights=False)
    manifest.update({"clips":report,"status":"experimental; requires motion and Windows renderer visual review"})
    (output/"model-report.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print("EXPORTED",json.dumps(manifest),flush=True)
    if args.render:
        for name,fraction in (("idle",0),("walk",.15),("walk",.4),("belly",.4),("belly",.63),("landing",.5)):
            rig.animation_data.action=actions[name]
            scene.frame_set(1+round(CLIPS[name][0]*fraction))
            scene.render.filepath=str(output/f"{name}-{fraction:.2f}.png")
            bpy.ops.render.render(write_still=True)
    if args.animate:
        scene.render.resolution_x=scene.render.resolution_y=360
        scene.cycles.samples=16
        scene.frame_step=3
        for name in ("walk","belly","landing"):
            rig.animation_data.action=actions[name]
            scene.frame_end=CLIPS[name][0]+1
            scene.render.filepath=str(output/"frames"/name/"frame-")
            bpy.ops.render.render(animation=True)


if __name__=="__main__":
    main()
