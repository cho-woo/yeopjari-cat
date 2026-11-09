"""Export exact problem polygons/vertices/UVs for the next mesh artist.

blender -b --python tools/cream_mesh_rig/diagnose.py -- build/cream-mesh-rig
This diagnoses the failed source; it does not repair or approve it.
"""
import json,sys
from pathlib import Path
import bpy

folder=Path(sys.argv[sys.argv.index('--')+1]).resolve()
evaluation=json.loads((folder/'rig-evaluation.json').read_text())
bpy.ops.wm.open_mainfile(filepath=str(folder/'authoring.blend'))
def source(v):return [v.x,-v.z]
reports=[]
for foot in ('near_front','far_front','near_rear','far_rear'):
    rows=[(frame,next(row for row in frame['legs'] if row['foot']==foot)) for frame in evaluation['frames']]
    for kind,key,index_key in [('collapse','min_triangle_area_ratio','min_triangle_index'),('stretch','max_triangle_area_ratio','max_triangle_index')]:
        frame,row=(min if kind=='collapse' else max)(rows,key=lambda pair:pair[1][key])
        bpy.context.scene.frame_set(frame['frame']+1);bpy.context.view_layer.update()
        obj=bpy.data.objects[foot+'.continuous_fur'];mesh=obj.data;deformed=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());actual=deformed.to_mesh();polygon=mesh.polygons[row[index_key]]
        uv=mesh.uv_layers['PaintedArtworkUV'];material=mesh.materials[0];image=next(node.image for node in material.node_tree.nodes if node.type=='TEX_IMAGE')
        vertices=[]
        for loop_index in polygon.loop_indices:
            vertex_index=mesh.loops[loop_index].vertex_index;v=mesh.vertices[vertex_index];tex=uv.data[loop_index].uv
            vertices.append({'vertex':vertex_index,'bind_source_px':source(v.co),'evaluated_source_px':source(actual.vertices[vertex_index].co),'uv_normalized':list(tex),'paint_source_px':[tex.x*image.size[0],(1-tex.y)*image.size[1]],'weights':{obj.vertex_groups[g.group].name:g.weight for g in v.groups}})
        reports.append({'problem':kind,'foot':foot,'mesh':obj.name,'frame_index':frame['frame'],'blender_keyframe':frame['frame']+1,'phase':frame['phase'],'triangle_area_ratio':row[key],'polygon':polygon.index,'vertices':vertices,'material_image':image.name,'blender_selection_python':f"obj=bpy.data.objects[{obj.name!r}]; bpy.context.view_layer.objects.active=obj; obj.select_set(True); [setattr(v, 'select', v.index in {[v['vertex'] for v in vertices]!r}) for v in obj.data.vertices]"})
        deformed.to_mesh_clear()
all_rows=[row for f in evaluation['frames'] for row in f['legs']]
summary={'visual_status':'FAILED / unapproved diagnostic source','minimum_triangle_area_ratio':min(row['min_triangle_area_ratio'] for row in all_rows),'maximum_triangle_area_ratio':max(row['max_triangle_area_ratio'] for row in all_rows),'inverted_triangles_counted_across_all_frames':sum(row['inverted_triangle_count'] for row in all_rows),'minimum_ring_width_ratio':min(row['min_cross_section_width_ratio'] for row in all_rows),'max_sole_control_error_source_px':max(row['ik_sole_error_source_px'] for row in all_rows),'ring_width_does_not_certify_longitudinal_surface_quality':True,'problem_regions':reports}
(folder/'skin-distortion-audit.json').write_text(json.dumps(summary,indent=2)+'\n')
print(json.dumps({key:value for key,value in summary.items() if key!='problem_regions'}))
