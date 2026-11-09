"""Blender smoke test: make a manual mesh/UV/weight edit for --existing-blend.

blender -b --python tools/cream_mesh_rig/test_editable_rebake.py -- ORIGINAL.blend EDITED.blend
The real rebake branch records a before/after hash of all base mesh, UV, weight,
Coverage, packed paint and keyframe data. This file creates actual edits, not a
test that merely reruns the procedural source generator.
"""
import sys,json
from pathlib import Path
import bpy

args=sys.argv[sys.argv.index('--')+1:];source,target=map(Path,args)
bpy.ops.wm.open_mainfile(filepath=str(source.resolve()))
obj=bpy.data.objects['Head_original_pixels'];index=len(obj.data.vertices)//2
before=list(obj.data.vertices[index].co);obj.data.vertices[index].co.x+=.75
uv=obj.data.uv_layers['PaintedArtworkUV'];uv.data[0].uv.x+=.0001
group=obj.vertex_groups['head'];group.add([index],.9375,'REPLACE')
obj['manual_edit_smoke_test']='vertex+0.75srcpx, UV+0.0001, weight0.9375'
bpy.ops.wm.save_as_mainfile(filepath=str(target.resolve()))
(target.parent/'manual-edit-smoke-test.json').write_text(json.dumps({'source':str(source),'edited_blend':str(target),'object':obj.name,'vertex':index,'before':before,'after':list(obj.data.vertices[index].co),'manual_uv_delta':.0001,'manual_weight':.9375},indent=2)+'\n')
