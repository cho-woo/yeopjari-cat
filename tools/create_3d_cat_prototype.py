"""Build an editable, skinned Blender prototype; not a production app asset.

Run with Blender 4.3+: blender -b -t 4 --python tools/create_3d_cat_prototype.py
Optional arguments after --: --output PATH --render --animate
No downloads, external assets, production resources, or save data are used.
"""

import argparse
import json
import math
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/3d-prototype"))
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--animate", action="store_true")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def material(name, color, roughness=0.6):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Roughness"].default_value = roughness
    mat.diffuse_color = (*color, 1)
    return mat


def ellipsoid(name, location, scale, mat=None):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=32, ring_count=20, location=location)
    obj = bpy.context.object
    obj.name = name
    obj.scale = scale
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if mat:
        obj.data.materials.append(mat)
    for poly in obj.data.polygons:
        poly.use_smooth = True
    return obj


def tube(name, points, radius, mat):
    curve = bpy.data.curves.new(name, "CURVE")
    curve.dimensions = "3D"
    curve.resolution_u = 12
    curve.bevel_depth = radius
    curve.bevel_resolution = 3
    spline = curve.splines.new("BEZIER")
    spline.bezier_points.add(len(points) - 1)
    for p, co in zip(spline.bezier_points, points):
        p.co = co
        p.handle_left_type = p.handle_right_type = "AUTO"
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    curve.materials.append(mat)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.convert(target="MESH")
    return bpy.context.object


def ear_mesh(side, inset=False):
    # Rounded, tapered volume, with a smaller pink volume on the front face.
    verts, faces = [], []
    rings, segments = 15, 20
    for i in range(rings):
        t = i / (rings - 1)
        width = (0.095 if inset else 0.145) * (1 - t) ** 0.75 + 0.003
        depth = (0.008 if inset else 0.064) * (1 - t) + 0.003
        z = (1.43 if inset else 1.34) + (0.24 if inset else 0.40) * t
        cx = side * (0.275 + 0.09 * t)
        cy = (-0.638 if inset else -0.59) + 0.022 * t
        for j in range(segments):
            angle = j * math.tau / segments
            verts.append((cx + width * math.cos(angle), cy + depth * math.sin(angle), z))
    for i in range(rings - 1):
        for j in range(segments):
            a, b = i * segments + j, i * segments + (j + 1) % segments
            faces.append((a, b, b + segments, a + segments))
    faces.extend([tuple(reversed(range(segments))),
                  tuple((rings - 1) * segments + j for j in range(segments))])
    mesh = bpy.data.meshes.new("EarGeometry")
    mesh.from_pydata(verts, [], faces)
    mesh.update()
    obj = bpy.data.objects.new("InnerEar" if inset else "Ear", mesh)
    bpy.context.collection.objects.link(obj)
    for poly in mesh.polygons:
        poly.use_smooth = True
    return obj


def make_rig():
    data = bpy.data.armatures.new("KittenSkeleton")
    rig = bpy.data.objects.new("KittenRig", data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    specs = {}

    def bone(name, head, tail, parent=None):
        b = data.edit_bones.new(name)
        b.head, b.tail = head, tail
        if parent:
            b.parent = data.edit_bones[parent]
        specs[name] = (Vector(head), Vector(tail))

    bone("root", (0, 0, 0), (0, 0, 0.15))
    data.edit_bones["root"].use_deform = False
    bone("pelvis", (0, 0.38, 0.62), (0, 0.12, 0.65), "root")
    bone("spine", (0, 0.12, 0.65), (0, -0.18, 0.70), "pelvis")
    bone("chest", (0, -0.18, 0.70), (0, -0.40, 0.79), "spine")
    bone("neck", (0, -0.40, 0.79), (0, -0.56, 0.98), "chest")
    bone("head", (0, -0.56, 0.98), (0, -0.60, 1.36), "neck")
    for side, label in ((1, "L"), (-1, "R")):
        x = side * 0.24
        bone(f"ear.{label}", (side * 0.275, -0.59, 1.36),
             (side * 0.365, -0.568, 1.74), "head")
        for kind, y, knee_y, ankle_y, toe_y, parent in (
                ("front", -0.37, -0.38, -0.39, -0.55, "chest"),
                ("back", 0.40, 0.46, 0.36, 0.19, "pelvis")):
            upper, lower, paw = (f"{kind}_{part}.{label}" for part in ("upper", "lower", "paw"))
            bone(upper, (x, y, 0.62), (x, knee_y, 0.33), parent)
            bone(lower, (x, knee_y, 0.33), (x, ankle_y, 0.14), upper)
            bone(paw, (x, ankle_y, 0.14), (x, toe_y, 0.095), lower)
    tail_points = [(0, 0.54, 0.67), (0.04, 0.79, 0.81), (0.16, 0.94, 1.04),
                   (0.31, 0.95, 1.26), (0.36, 0.85, 1.38)]
    for i, (a, b) in enumerate(zip(tail_points, tail_points[1:])):
        bone(f"tail.{i}", a, b, "pelvis" if i == 0 else f"tail.{i - 1}")
    bpy.ops.object.mode_set(mode="OBJECT")
    rig.show_in_front = True
    data.display_type = "OCTAHEDRAL"
    for pb in rig.pose.bones:
        pb.rotation_mode = "XYZ"
    return rig, specs, tail_points


def bind(obj, rig, weights):
    # Normalize and limit to four influences for glTF skinning.
    groups = {name: obj.vertex_groups.new(name=name) for name in rig.data.bones.keys()
              if name != "root"}
    for v in obj.data.vertices:
        values = sorted(weights(obj.matrix_world @ v.co).items(), key=lambda w: w[1], reverse=True)[:4]
        total = sum(value for _, value in values)
        assert total > 0
        for name, value in values:
            if value > 0:
                groups[name].add([v.index], value / total, "REPLACE")
    mod = obj.modifiers.new("SkeletalDeformation", "ARMATURE")
    mod.object = rig
    obj.parent = rig


def smoothstep(low, high, v):
    t = max(0, min(1, (v - low) / (high - low)))
    return t * t * (3 - 2 * t)


def distance_to_segment(p, a, b):
    d = b - a
    t = max(0, min(1, (p - a).dot(d) / d.length_squared))
    return (p - a - t * d).length


def make_body(rig, specs, tail_points, mats):
    pieces = [ellipsoid("Torso", (0, 0.02, 0.63), (0.335, 0.61, 0.35)),
              ellipsoid("Neck", (0, -0.40, 0.81), (0.28, 0.28, 0.29)),
              ellipsoid("Head", (0, -0.62, 1.11), (0.445, 0.36, 0.395))]
    for side in (-1, 1):
        pieces.append(ear_mesh(side))
        for y, lower_y in ((-0.37, -0.39), (0.40, 0.36)):
            pieces.extend([
                ellipsoid("UpperLeg", (side * 0.24, y, 0.43),
                          (0.12 if y < 0 else 0.16, 0.145, 0.245)),
                ellipsoid("LowerLeg", (side * 0.24, lower_y, 0.23), (0.103, 0.11, 0.18)),
                ellipsoid("Paw", (side * 0.24, lower_y - 0.06, 0.105), (0.14, 0.18, 0.105))])
    pieces.append(tube("Tail", tail_points, 0.095, mats[0]))
    # Close the tail tip before remeshing.
    pieces.append(ellipsoid("TailTip", tail_points[-1], (0.095,) * 3))
    bpy.ops.object.select_all(action="DESELECT")
    for obj in pieces:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = pieces[0]
    bpy.ops.object.join()
    body = bpy.context.object
    body.name = "CreamKittenBody"
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    remesh = body.modifiers.new("UnifiedSurface", "REMESH")
    remesh.mode = "VOXEL"
    remesh.voxel_size = 0.022
    remesh.use_smooth_shade = True
    bpy.ops.object.modifier_apply(modifier=remesh.name)
    smooth = body.modifiers.new("SurfaceSmoothing", "SMOOTH")
    smooth.factor = 0.65
    smooth.iterations = 4
    bpy.ops.object.modifier_apply(modifier=smooth.name)
    body.data.materials.clear()
    for mat in mats[:2]:
        body.data.materials.append(mat)
    for poly in body.data.polygons:
        x, y, z = poly.center
        orange = ((z > 1.34 and abs(x) > 0.075)
                  or (y > -0.1 and z > 0.88)
                  or (y > 0.61 and z < 1.29))
        poly.material_index = int(orange)
        poly.use_smooth = True

    def weights(p):
        x, y, z = p
        if z > 1.35 and abs(x) > 0.17:
            amount = smoothstep(1.35, 1.49, z)
            return {"head": 1 - amount, "ear.L" if x > 0 else "ear.R": amount}
        if z > 0.83 and y < -0.33:
            amount = smoothstep(0.83, 1.00, z)
            return {"head": amount, "neck": 1 - amount}
        if y > 0.60 and z > 0.58:
            names = [f"tail.{i}" for i in range(4)]
        elif z < 0.60 and abs(x) > 0.105:
            label = "L" if x > 0 else "R"
            kind = "front" if y < 0 else "back"
            names = [f"{kind}_{part}.{label}" for part in ("upper", "lower", "paw")]
            names.append("chest" if y < 0 else "pelvis")
        else:
            names = ["pelvis", "spine", "chest", "neck"]
        return {name: 1 / (0.035 + distance_to_segment(p, *specs[name])) ** 4 for name in names}

    bind(body, rig, weights)
    return body


def make_face(rig, mats):
    cream, orange, pink, brown, black, highlight = mats
    parts = []

    def add(obj, bone="head"):
        bind(obj, rig, lambda _: {bone: 1})
        parts.append(obj)

    for side, label in ((1, "L"), (-1, "R")):
        inner = ear_mesh(side, inset=True)
        inner.name = f"InnerEar.{label}"
        inner.data.materials.append(pink)
        # Same blend as the outer surface to avoid sliding ear inserts.
        bind(inner, rig, lambda p, b=f"ear.{label}": {
            "head": 1 - smoothstep(1.35, 1.49, p.z), b: smoothstep(1.35, 1.49, p.z)})
        parts.append(inner)
        add(ellipsoid(f"Eye.{label}", (side * 0.185, -0.939, 1.14), (0.115, 0.052, 0.136), brown))
        add(ellipsoid(f"Pupil.{label}", (side * 0.18, -0.983, 1.15), (0.072, 0.018, 0.103), black))
        add(ellipsoid(f"EyeLight.{label}", (side * 0.18 - 0.022, -1.002, 1.19), (0.024, 0.009, 0.025), highlight))
        add(ellipsoid(f"EyeLightSmall.{label}", (side * 0.18 + 0.025, -1.002, 1.10), (0.009, 0.005, 0.009), highlight))
        add(ellipsoid(f"Muzzle.{label}", (side * 0.075, -0.946, 1.002), (0.10, 0.079, 0.064), cream))
        add(tube(f"Smile.{label}", [(0, -1.029, 1.021),
                                    (side * 0.022, -1.026, 0.987),
                                    (side * 0.058, -1.017, 0.996)], 0.0045, brown))
    nose = ellipsoid("PinkNose", (0, -1.025, 1.043), (0.041, 0.020, 0.026), pink)
    for vertex in nose.data.vertices:
        vertex.co.x *= 0.55 + 0.45 * smoothstep(-0.026, 0.026, vertex.co.z)
    add(nose)
    return parts


def make_actions(rig):
    rig.animation_data_create()
    names = ["ear.L", "ear.R", "front_upper.L", "front_lower.L", "front_paw.L"]
    actions = {}
    for name, end in (("EarFlick", 33), ("PawTap", 49)):
        action = bpy.data.actions.new(name)
        rig.animation_data.action = action
        for f in range(1, end + 1):
            for bone in names:
                rig.pose.bones[bone].rotation_euler = (0, 0, 0)
            if name == "EarFlick":
                t = (f - 1) / (end - 1)
                pulse = math.sin(math.pi * t) ** 2 * math.sin(math.tau * t * 2)
                rig.pose.bones["ear.L"].rotation_euler.x = 0.34 * pulse
                rig.pose.bones["ear.R"].rotation_euler.x = -0.21 * pulse
            else:
                lift = smoothstep(1, 14, f) * (1 - smoothstep(37, 49, f))
                tap = math.sin(math.pi * max(0, min(1, (f - 17) / 18)) * 2) ** 2
                rig.pose.bones["front_upper.L"].rotation_euler.x = -lift * (0.73 + 0.09 * tap)
                rig.pose.bones["front_lower.L"].rotation_euler.x = lift * (0.44 - 0.20 * tap)
                rig.pose.bones["front_paw.L"].rotation_euler.x = lift * 0.20
            for bone in names:
                rig.pose.bones[bone].keyframe_insert(data_path="rotation_euler", frame=f, group=bone)
        action.use_fake_user = True
        actions[name] = action
    rig.animation_data.action = actions["EarFlick"]
    bpy.context.scene.frame_set(1)
    return actions


def check_rig(rig, body, actions):
    scene = bpy.context.scene
    result = {"bones": len(rig.data.bones), "body_vertices": len(body.data.vertices),
              "clips": {}, "production_ready": False,
              "limitations": ["procedural exterior prototype", "no production fur or facial rig",
                              "no walking, reclining, jump or IK validation", "not integrated into Windows app"]}
    for name, action in actions.items():
        rig.animation_data.action = action
        values = []
        baseline_head = baseline_ear = baseline_paw = None
        for f in range(1, int(action.frame_range.y) + 1):
            scene.frame_set(f)
            head = rig.pose.bones["head"].matrix.copy()
            ear = rig.pose.bones["ear.L"].tail.copy()
            paw = rig.pose.bones["front_paw.L"].tail.copy()
            if baseline_head is None:
                baseline_head, baseline_ear, baseline_paw = head, ear, paw
            assert all(abs(head[i][j] - baseline_head[i][j]) < 1e-6 for i in range(4) for j in range(4))
            assert all(abs(v - 1) < 1e-6 for pb in rig.pose.bones for v in pb.scale)
            values.append(((ear - baseline_ear).length, (paw - baseline_paw).length))
        result["clips"][name] = {"frames": len(values), "head_unchanged": True,
                                  "bone_scale_unchanged": True,
                                  "ear_tip_travel": max(v[0] for v in values),
                                  "paw_tip_travel": max(v[1] for v in values)}
        assert result["clips"][name]["ear_tip_travel" if name == "EarFlick" else "paw_tip_travel"] > 0.01
    for vertex in body.data.vertices:
        assert 1 <= len(vertex.groups) <= 4
        assert abs(sum(group.weight for group in vertex.groups) - 1) < 1e-5
    rig.animation_data.action = actions["EarFlick"]
    scene.frame_set(1)
    return result


def setup_studio():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 48
    # Debian's Blender can include Cycles without OpenImageDenoise.
    scene.cycles.use_denoising = False
    scene.render.resolution_x = scene.render.resolution_y = 640
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.film_transparent = False
    scene.render.fps = 24
    scene.world.color = (0.22, 0.22, 0.22)
    floor_mat = material("StudioFloor", (0.67, 0.71, 0.74), 0.85)
    bpy.ops.mesh.primitive_plane_add(size=200, location=(0, 0, -0.006))
    floor = bpy.context.object
    floor.name = "StudioFloor"
    floor.data.materials.append(floor_mat)
    for name, position, power, size in (
            ("Key", (2, -3, 4.5), 450, 4),
            ("Fill", (-3, -1, 2.5), 230, 3),
            ("Rim", (0, 3, 3.5), 400, 3)):
        data = bpy.data.lights.new(name, "AREA")
        data.energy, data.shape, data.size = power, "DISK", size
        obj = bpy.data.objects.new(name, data)
        bpy.context.collection.objects.link(obj)
        obj.location = position
        obj.rotation_euler = (Vector((0, 0, 0.8)) - obj.location).to_track_quat("-Z", "Y").to_euler()
    data = bpy.data.cameras.new("ReviewCamera")
    camera = bpy.data.objects.new("ReviewCamera", data)
    bpy.context.collection.objects.link(camera)
    camera.location = (2.7, -4.1, 2.1)
    camera.rotation_euler = (Vector((0, -0.05, 0.85)) - camera.location).to_track_quat("-Z", "Y").to_euler()
    data.type, data.ortho_scale = "ORTHO", 2.55
    scene.camera = camera
    return scene


def main():
    args = arguments()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    mats = [material("Ivory", (0.92, 0.82, 0.63)),
            material("Ginger", (0.66, 0.30, 0.075)),
            material("Rose", (0.72, 0.24, 0.25)),
            material("WarmEye", (0.085, 0.028, 0.009), 0.23),
            material("Pupil", (0.007, 0.004, 0.003), 0.17),
            material("EyeHighlight", (1, 0.97, 0.91), 0.15)]
    rig, specs, tail_points = make_rig()
    body = make_body(rig, specs, tail_points, mats)
    face = make_face(rig, mats)
    actions = make_actions(rig)
    report = check_rig(rig, body, actions)
    scene = setup_studio()
    scene.frame_start, scene.frame_end = 1, 33
    bpy.ops.object.select_all(action="DESELECT")
    for obj in [rig, body, *face]:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    rig["status"] = "Procedural rig prototype; not a production character"
    bpy.ops.wm.save_as_mainfile(filepath=str(output / "cream-kitten-prototype.blend"))
    bpy.ops.export_scene.gltf(filepath=str(output / "cream-kitten-prototype.glb"),
                              export_format="GLB", use_selection=True,
                              export_animations=True, export_animation_mode="ACTIONS",
                              export_frame_range=False, export_cameras=False, export_lights=False)
    (output / "rig-check.json").write_text(json.dumps(report, indent=2) + "\n")
    print("RIG_CHECK", json.dumps(report), flush=True)
    if args.render:
        for action_name, frame, suffix in (("EarFlick", 1, "rest"), ("EarFlick", 12, "ear"),
                                           ("PawTap", 25, "paw")):
            rig.animation_data.action = actions[action_name]
            scene.frame_set(frame)
            scene.render.filepath = str(output / f"preview-{suffix}.png")
            bpy.ops.render.render(write_still=True)
    if args.animate:
        scene.render.resolution_x = scene.render.resolution_y = 360
        scene.cycles.samples = 12
        for name, action in actions.items():
            rig.animation_data.action = action
            scene.frame_end = int(action.frame_range.y)
            scene.frame_step = 2
            scene.render.filepath = str(output / "frames" / name / "frame-")
            bpy.ops.render.render(animation=True)


if __name__ == "__main__":
    main()
