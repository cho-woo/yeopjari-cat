"""Blender authoring source for an editable layered 2D kitten mesh.

blender -b --python tools/cream_mesh_rig/author.py -- --output build/cream-mesh-rig --mode rest
Modes rest, short, bake. Every bone, UV, mesh, foot control and keyframe is saved.
The old runtime cutout rigs are neither imported nor executed.
"""
from __future__ import annotations
import sys,math,json,argparse,hashlib
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
import motion as M
import bpy
from mathutils import Vector,Matrix

ROOT=Path(__file__).resolve().parents[2]
DEFAULT_ORIGINAL=ROOT/'assets/cream-mesh-source/original-walk-run.png'
DEFAULT_HIDDEN=ROOT/'assets/cream-mesh-source/hidden-parts-generated.png'

# Explicit source-art UV registration, not per-frame bounding-box fitting.
GEN_PATHS={
 'near_front':[(213.5,130),(232,280),(310.5,395),(318,445)],
 'far_front':[(219,120),(205,280),(243.5,395),(245.5,443)],
 'near_rear':[(269,120),(213.5,290),(181.5,395),(184,444)],
 'far_rear':[(250,120),(262,275),(237,395),(235,444)],
}
GEN_RECTS={'near_front':(512,0),'far_front':(1024,0),'near_rear':(0,512),'far_rear':(512,512)}
PAWS={
 'near_front':[(202,246),(227,246),(242,260),(242,278),(208,278),(201,269)],
 'far_front':[(178,251),(207,251),(213,260),(213,276),(178,276)],
 'near_rear':[(56,248),(95,248),(99,260),(99,277),(56,277)],
 'far_rear':[(118,252),(148,252),(151,264),(151,277),(118,277)],
}
DEPTH={'far_rear':.40,'far_front':.30,'tail':.20,'torso_fill':-.28,'torso':-.30,'near_rear':-.33,'near_front':-.35,'head':-.40}


def V(p,y=0):return Vector((float(p[0]),y,-float(p[1])))
def src(v):return (float(v.x),float(-v.z))
def rotate(v,a):return (v[0]*math.cos(a)-v[1]*math.sin(a),v[0]*math.sin(a)+v[1]*math.cos(a))
def catmull(path,i,t):
 a=path[max(0,i-1)];b=path[i];c=path[i+1];d=path[min(len(path)-1,i+2)]
 val=[];der=[]
 for k in (0,1):
  val.append(.5*((2*b[k])+(-a[k]+c[k])*t+(2*a[k]-5*b[k]+4*c[k]-d[k])*t*t+(-a[k]+3*b[k]-3*c[k]+d[k])*t*t*t))
  der.append(.5*((-a[k]+c[k])+2*(2*a[k]-5*b[k]+4*c[k]-d[k])*t+3*(-a[k]+3*b[k]-3*c[k]+d[k])*t*t))
 return tuple(val),tuple(der)
def smooth(t):t=max(0.,min(1.,t));return t*t*(3-2*t)
def bone_matrix(head,tail):
 h,t=V(head),V(tail);y=(t-h).normalized();z=Vector((0.,-1.,0.));x=y.cross(z).normalized()
 return Matrix(((x.x,y.x,z.x,h.x),(x.y,y.y,z.y,h.y),(x.z,y.z,z.z,h.z),(0,0,0,1)))
def inside(p,poly):
 x,y=p;c=False
 for i,a in enumerate(poly):
  b=poly[(i+1)%len(poly)]
  if ((a[1]>y)!=(b[1]>y)) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:c=not c
 return c


def image_material(name,path):
 image=bpy.data.images.load(str(path),check_existing=True);image.pack()
 mat=bpy.data.materials.new(name);mat.use_nodes=True
 if hasattr(mat,'surface_render_method'):mat.surface_render_method='BLENDED'
 # Different rig plates share this material. Suppressing overlap discards the
 # far paw behind a transparent near-leg envelope at some contact poses.
 if hasattr(mat,'use_transparency_overlap'):mat.use_transparency_overlap=True
 nodes=mat.node_tree.nodes;nodes.clear();links=mat.node_tree.links
 output=nodes.new('ShaderNodeOutputMaterial');shader=nodes.new('ShaderNodeEmission')
 tex=nodes.new('ShaderNodeTexImage');tex.image=image;coord=nodes.new('ShaderNodeTexCoord');links.new(coord.outputs['UV'],tex.inputs['Vector']);tex.interpolation='Linear';tex.extension='CLIP'
 weight=nodes.new('ShaderNodeVertexColor');weight.layer_name='Coverage'
 clip=nodes.new('ShaderNodeMath');clip.operation='GREATER_THAN';clip.inputs[1].default_value=16/255
 mult=nodes.new('ShaderNodeMath');mult.operation='MULTIPLY'
 mult2=nodes.new('ShaderNodeMath');mult2.operation='MULTIPLY'
 trans=nodes.new('ShaderNodeBsdfTransparent');mix=nodes.new('ShaderNodeMixShader')
 links.new(tex.outputs['Color'],shader.inputs['Color']);links.new(tex.outputs['Alpha'],clip.inputs[0]);links.new(tex.outputs['Alpha'],mult.inputs[0]);links.new(clip.outputs[0],mult.inputs[1]);links.new(mult.outputs[0],mult2.inputs[0]);links.new(weight.outputs['Alpha'],mult2.inputs[1]);links.new(mult2.outputs[0],mix.inputs[0]);links.new(trans.outputs[0],mix.inputs[1]);links.new(shader.outputs[0],mix.inputs[2]);links.new(mix.outputs[0],output.inputs['Surface'])
 return mat,image.size[:]


def mesh_object(name,points,faces,uvpoints,material,imagesize,armature,weights,coverage=None,depth=0):
 mesh=bpy.data.meshes.new(name+'Mesh');mesh.from_pydata([V(p,depth) for p in points],[],faces);mesh.update()
 obj=bpy.data.objects.new(name,mesh);bpy.context.collection.objects.link(obj);obj.parent=armature
 uv=mesh.uv_layers.new(name='PaintedArtworkUV')
 colors=mesh.color_attributes.new(name='Coverage',type='FLOAT_COLOR',domain='CORNER')
 # Adding a color attribute reallocates CustomData; reacquire RNA layer handles.
 uv=mesh.uv_layers['PaintedArtworkUV'];colors=mesh.color_attributes['Coverage']
 for poly in mesh.polygons:
  for li in poly.loop_indices:
   vi=mesh.loops[li].vertex_index;p=uvpoints[vi];uv.data[li].uv=(p[0]/imagesize[0],1-p[1]/imagesize[1]);colors.data[li].color=(1,1,1,(coverage or [1]*len(points))[vi])
 obj.data.materials.append(material)
 for group in sorted({k for d in weights for k in d}):
  vg=obj.vertex_groups.new(name=group)
  for i,d in enumerate(weights):
   if d.get(group,0)>0:vg.add([i],d[group],'REPLACE')
 mod=obj.modifiers.new('Smooth multi-joint skin','ARMATURE');mod.object=armature;mod.use_deform_preserve_volume=True
 return obj


def grid_patch(name,poly,material,imagesize,armature,group,uvmap=lambda p:p,coverage=lambda p:1,depth=0,step=4):
 # Editable regular triangle topology; padded artwork alpha preserves fur edge.
 x0=int(min(p[0] for p in poly));x1=int(max(p[0] for p in poly));y0=int(min(p[1] for p in poly));y1=int(max(p[1] for p in poly))
 xs=list(range(x0,x1,step))+[x1];ys=list(range(y0,y1,step))+[y1];points=[(x,y) for y in ys for x in xs];faces=[]
 for j in range(len(ys)-1):
  for i in range(len(xs)-1):
   mid=((xs[i]+xs[i+1])/2,(ys[j]+ys[j+1])/2)
   # Retain boundary cells and feather their vertices against the polygon.
   # A midpoint-only test exposed 4px staircase cuts through chest/underbelly.
   corners=[(xs[i],ys[j]),(xs[i+1],ys[j]),(xs[i],ys[j+1]),(xs[i+1],ys[j+1])]
   if inside(mid,poly) or any(inside(p,poly) for p in corners):
    a=j*len(xs)+i;b=a+1;c=a+len(xs);d=c+1;faces.extend([(a,c,b),(b,c,d)])
 weights=[group(p) if callable(group) else {group:1.} for p in points]
 def boundary_alpha(p):
  if not inside(p,poly):return 0.
  distances=[]
  for i,a in enumerate(poly):
   b=poly[(i+1)%len(poly)];v=(b[0]-a[0],b[1]-a[1]);t=max(0.,min(1.,((p[0]-a[0])*v[0]+(p[1]-a[1])*v[1])/(v[0]*v[0]+v[1]*v[1])))
   distances.append(math.dist(p,(a[0]+t*v[0],a[1]+t*v[1])))
  return smooth(min(distances)/3.)
 return mesh_object(name,points,faces,[uvmap(p) for p in points],material,imagesize,armature,weights,[coverage(p)*boundary_alpha(p) for p in points],depth)


def leg_mesh(name,armature,hiddenmat,hiddensize,origmat,origsize):
 rest=M.REST[name];path=GEN_PATHS[name];offset=GEN_RECTS[name]
 radii=([24,18,13,19] if 'front' in name else [30,19,13,18]);uradii={'near_front':[60,76.5,68,60],'far_front':[55.5,61.5,62,58],'near_rear':[80.5,110,68,58],'far_rear':[64.5,75.5,59.5,58]}[name]
 if name.startswith('far'):radii=[r*.88 for r in radii]
 # Continuous UV tube, with 7 samples across and fine rings along joints.
 points=[];uv=[];weights=[];faces=[];rows=[]
 for segment in range(3):
  for ring in range(9):
   if segment and ring==0:continue
   t=ring/8;center,vec=catmull(rest,segment,t);uc,uvec=catmull(path,segment,t)
   l=math.hypot(*vec);normal=(-vec[1]/l,vec[0]/l)
   ul=math.hypot(*uvec);un=(-uvec[1]/ul,uvec[0]/ul)
   radius=radii[segment]*(1-t)+radii[segment+1]*t;ur=uradii[segment]*(1-t)+uradii[segment+1]*t
   arc=segment+t
   if arc<.35:w={name+'.upper':1.}
   elif arc<1.65:
    f=smooth((arc-.35)/1.30);w={name+'.upper':1-f,name+'.lower':f}
   elif arc<1.70:w={name+'.lower':1.}
   elif arc<2.35:
    f=smooth((arc-1.70)/.65);w={name+'.lower':1-f,name+'.paw':f}
   else:w={name+'.paw':1.}
   start=len(points);rows.append(start)
   for j in range(7):
    a=-1+j/3;points.append((center[0]+normal[0]*radius*a,center[1]+normal[1]*radius*a));uv.append((offset[0]+uc[0]+un[0]*ur*a,offset[1]+uc[1]+un[1]*ur*a));weights.append(w)
 for i in range(len(rows)-1):
  for j in range(6):
   a=rows[i]+j;b=a+1;c=rows[i+1]+j;d=c+1;faces.extend([(a,c,b),(b,c,d)])
 # Near-side fur overlaps the torso, so feather the deliberately hidden root
 # rather than exposing a hard polygon boundary at the hip/shoulder.
 fur_coverage=[smooth((p[1]-rest[0][1])/(8 if 'rear' in name else 16))*(1-smooth((p[1]-(252 if name=='near_front' else 254))/12)) for p in points]
 obj=mesh_object(name+'.continuous_fur',points,faces,uv,hiddenmat,hiddensize,armature,weights,coverage=fur_coverage,depth=DEPTH[name])
 # Original painted toes are a separate rigid attachment. Only the ankle blend
 # changes; toe length/width/sole do not shear with the lower-leg skin.
 if name in ('far_front','far_rear'):
  # The source sheet's far forepaw is occluded by its near foreleg: there is
  # no opaque sole at the old proposed marker. Use the independently painted
  # far-side toe with its measured alpha200 sole at(245.5,443), rigidly mapped
  # to the control rather than assigning a marker to transparent source art.
  x,y=rest[-1];scale=.30 if name=='far_front' else .28;ux,uy=(245.5,443) if name=='far_front' else (235,444);ox,oy=GEN_RECTS[name]
  toe=[(x-22,244),(x+24,244),(x+24,280),(x-22,280)]
  grid_patch(name+'.painted_paw',toe,hiddenmat,hiddensize,armature,name+'.paw',uvmap=lambda p:(ox+ux+(p[0]-x)/scale,oy+uy+(p[1]-y)/scale),coverage=lambda p:smooth((p[1]-246)/12),depth=DEPTH[name]-.01,step=1)
 else:
  grid_patch(name+'.original_paw',PAWS[name],origmat,origsize,armature,name+'.paw',coverage=lambda p:smooth((p[1]-(247 if name=='near_front' else 250))/7),depth=DEPTH[name]-.01,step=1)
 return obj


def reach_derivation():
 def ratio(stride):
  maximum=0.;worst=None
  for i in range(1024):
   p=i/1024;b=M.body_shift(p)
   for n,r in M.REST.items():
    f=M.foot_pose(n,p,stride);v=rotate((r[2][0]-r[3][0],r[2][1]-r[3][1]),f['angle']);ank=(f['position'][0]+v[0],f['position'][1]+v[1]);distance=math.dist((r[0][0]+b[0],r[0][1]+b[1]),ank);total=math.dist(r[0],r[1])+math.dist(r[1],r[2]);q=distance/total
    if q>maximum:maximum=q;worst={'foot':n,'phase':p,'distance_source_px':distance,'fixed_link_sum_source_px':total}
  return maximum,worst
 lo,hi=0.,80.
 for _ in range(28):
  mid=(lo+hi)/2
  if ratio(mid)[0]<1:lo=mid
  else:hi=mid
 chosen=round((lo*.75)*2)/2
 chosenratio,worst=ratio(chosen)
 assert chosenratio<.98
 return {'fixed_links':True,'reach_scan_samples':1024,'physical_stride_bound_logical_px':lo,'safety_fraction_of_bound':.75,'chosen_stride_logical_px':chosen,'selected_max_reach_ratio':chosenratio,'worst':worst,'old64_reach_ratio':ratio(64)[0]}


def build_scene(args):
 bpy.ops.wm.read_factory_settings(use_empty=True);scene=bpy.context.scene
 scene.render.engine='BLENDER_EEVEE_NEXT';scene.render.film_transparent=True;scene.render.resolution_x=M.CANVAS;scene.render.resolution_y=M.CANVAS;scene.render.resolution_percentage=100
 scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA';scene.render.image_settings.color_depth='8'
 scene.render.fps=60;scene.view_settings.view_transform='Standard';scene.view_settings.look='None';scene.view_settings.exposure=0;scene.view_settings.gamma=1
 if hasattr(scene,'eevee'):scene.eevee.taa_render_samples=16
 origmat,origsize=image_material('Original unmodified face / upperbody / painted paws',args.original)
 hiddenmat,hiddensize=image_material('Purpose-drawn independent hidden fur parts',args.hidden)
 data=bpy.data.armatures.new('Cream editable mesh skeleton');rig=bpy.data.objects.new('Cream_Mesh_Rig',data);bpy.context.collection.objects.link(rig);bpy.context.view_layer.objects.active=rig;rig.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
 def bone(n,a,b,parent=None):
  v=data.edit_bones.new(n);v.head=V(a);v.tail=V(b);v.align_roll(Vector((0,-1,0)))
  if parent:v.parent=data.edit_bones[parent]
  return v
 bone('body',(140,197),(140,177));bone('head',M.HEAD_POINT,(M.HEAD_POINT[0],M.HEAD_POINT[1]-20),'body')
 tail=[(91,174),(70,169),(46,143),(44,106)]
 for i in range(3):bone('tail'+str(i),tail[i],tail[i+1],'body' if i==0 else 'tail'+str(i-1))
 for n,path in M.REST.items():
  bone(n+'.upper',path[0],path[1],'body');bone(n+'.lower',path[1],path[2],n+'.upper');bone(n+'.paw',path[2],path[3],n+'.lower')
 bpy.ops.object.mode_set(mode='OBJECT');rig.show_in_front=True;rig.data.display_type='STICK'
 controls={}
 for n,path in M.REST.items():
  ctrl=bpy.data.objects.new('FOOT_'+n,None);bpy.context.collection.objects.link(ctrl);ctrl.empty_display_type='CIRCLE';ctrl.empty_display_size=9;ctrl.location=V(path[3]);ctrl['foot_identity']=n;ctrl['sole_y_source']=275;ctrl['description']='Move this SOLE control; fixed-length two-link IK follows.'
  ankle=bpy.data.objects.new('ANKLE_'+n,None);bpy.context.collection.objects.link(ankle);ankle.parent=ctrl;ankle.location=V((path[2][0]-path[3][0],path[2][1]-path[3][1]));ankle.empty_display_size=3
  lower=rig.pose.bones[n+'.lower'];ik=lower.constraints.new('IK');ik.name='Fixed links / sole-driven ankle';ik.target=ankle;ik.chain_count=2;ik.use_stretch=False
  for suffix in ('.upper','.lower'):
   pb=rig.pose.bones[n+suffix];pb.ik_stretch=0;pb.lock_ik_x=True;pb.lock_ik_y=True;pb.lock_ik_z=False
  paw=rig.pose.bones[n+'.paw'];copy=paw.constraints.new('COPY_ROTATION');copy.name='Rigid painted toe orientation';copy.target=ctrl;copy.target_space='WORLD';copy.owner_space='WORLD'
  # Controller rotation equals the original paw-bone basis; inverse ankle
  # offset is evaluated separately in set_pose, so no toe length changes.
  ctrl.rotation_mode='QUATERNION';ctrl.rotation_quaternion=rig.data.bones[n+'.paw'].matrix_local.to_quaternion()
  controls[n]=(ctrl,ankle)
 torso=[(50,130),(143,130),(222,155),(250,184),(250,215),(244,237),(237,252),(162,252),(112,252),(65,242),(52,217)]
 grid_patch('Torso_new_hidden_belly',torso,hiddenmat,hiddensize,rig,'body',uvmap=lambda p:((p[0]-46)/.39,(p[1]-89)/.39),coverage=lambda p:1-smooth((p[1]-218)/15),depth=DEPTH['torso_fill'],step=2)
 grid_patch('Chest_new_hidden_join',[(178,165),(235,165),(249,187),(244,210),(235,231),(230,245),(187,244),(175,217)],hiddenmat,hiddensize,rig,'body',uvmap=lambda p:(512+250+(p[0]-211)*.6,155+(p[1]-166)*.7),depth=-.29)
 grid_patch('Torso_original_outer_fur',torso,origmat,origsize,rig,'body',coverage=lambda p:1-smooth((p[1]-241)/11),depth=DEPTH['torso'],step=2)
 grid_patch('Head_original_pixels',[(148,44),(304,44),(304,224),(148,224)],origmat,origsize,rig,'head',coverage=lambda p:1-smooth((p[1]-190)/32),depth=DEPTH['head'])
 def tailweights(p):
  distances=[]
  for i in range(3):
   a,b=tail[i:i+2];t=max(0,min(1,((p[0]-a[0])*(b[0]-a[0])+(p[1]-a[1])*(b[1]-a[1]))/(math.dist(a,b)**2)));q=(a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1]));distances.append((math.dist(p,q),i,t))
  _,i,t=min(distances)
  if t<.22 and i>0:
   f=.5+.5*smooth(t/.22);return {'tail'+str(i-1):1-f,'tail'+str(i):f}
  if t>.78 and i<2:
   f=.5*smooth((t-.78)/.22);return {'tail'+str(i):1-f,'tail'+str(i+1):f}
  return {'tail'+str(i):1.}
 # The source tail is contiguous with the body. A whole rectangular UV plate
 # copied neighboring torso pixels and revealed an L-shaped white seam on bend.
 tail_mask=[(20,88),(63,88),(75,106),(73,132),(77,148),(95,161),(96,179),(79,189),(51,181),(30,162),(20,139)]
 grid_patch('Tail_original_fur',tail_mask,origmat,origsize,rig,tailweights,depth=DEPTH['tail'],step=2)
 for n in M.FOOT_ORDER:leg_mesh(n,rig,hiddenmat,hiddensize,origmat,origsize)
 camera_data=bpy.data.cameras.new('Fixed313Camera');camera=bpy.data.objects.new('Fixed313Camera',camera_data);bpy.context.collection.objects.link(camera);camera.location=(156.5,-100,-156.5);camera.rotation_euler=(math.pi/2,0,0);camera_data.type='ORTHO';camera_data.ortho_scale=313;camera_data.lens=50;scene.camera=camera
 rig['source_reference_height']=221;rig['root_anchor_source']=[161.,275.];rig['artwork']='Original head/outer torso/tail/painted paws; purpose-drawn independent hidden limb and belly fur.'
 return scene,rig,controls


def set_pose(scene,rig,controls,p,root_x=0.,neutral=False,keyframe=None):
 rig.location.x=root_x
 body=(0.,0.) if neutral else M.body_shift(p);head=(0.,0.) if neutral else M.head_shift(p)
 for name,shift in [('body',body),('head',head)]:
  pb=rig.pose.bones[name];pb.matrix=Matrix.Translation(Vector((shift[0],0,-shift[1])))@rig.data.bones[name].matrix_local
  if keyframe is not None:
   pb.keyframe_insert('location',frame=keyframe);pb.keyframe_insert('rotation_quaternion',frame=keyframe);pb.keyframe_insert('scale',frame=keyframe)
 for i in range(3):
  pb=rig.pose.bones['tail'+str(i)];pb.rotation_mode='XYZ';pb.rotation_euler=(0,0,0 if neutral else math.radians(1.7)*math.sin(2*math.pi*p+i*.2))
  if keyframe is not None:pb.keyframe_insert('rotation_euler',frame=keyframe)
 for n,(ctrl,ankle) in controls.items():
  r=M.REST[n];f={'position':r[-1],'angle':0.,'planted':True} if neutral else M.foot_pose(n,p)
  ctrl.location=V((f['position'][0]+root_x,f['position'][1]));ctrl['planted']=f['planted']
  # Footcontrol quaternion is the rest paw orientation rotated in worldplane.
  rot=Matrix.Rotation(f['angle'],4,'Y');ctrl.rotation_quaternion=(rot@rig.data.bones[n+'.paw'].matrix_local).to_quaternion()
  # Child empty retains a WORLD rest offset rather than inheriting paw basis.
  wanted=rotate((r[2][0]-r[3][0],r[2][1]-r[3][1]),f['angle'])
  ankle.matrix_parent_inverse=ctrl.matrix_world.inverted();ankle.location=V(wanted)
  # Replace parent-space ambiguity with direct local offset inverse rotation.
  ankle.matrix_parent_inverse=Matrix.Identity(4);ankle.location=ctrl.rotation_quaternion.inverted()@V(wanted)
  if keyframe is not None:
   ctrl.keyframe_insert('location',frame=keyframe);ctrl.keyframe_insert('rotation_quaternion',frame=keyframe);ctrl.keyframe_insert('["planted"]',frame=keyframe);ankle.keyframe_insert('location',frame=keyframe)
 bpy.context.view_layer.update()


def actual_annotations(rig,controls,p,index,neutral=False):
 evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get());a=M.annotation(p);checks=[]
 a['root_anchor']=list(src(evaluated.matrix_world@V(M.ANCHOR)))
 a['landmarks']={'head':list(src(evaluated.matrix_world@evaluated.pose.bones['head'].head)),'body':list(src(evaluated.matrix_world@evaluated.pose.bones['body'].head))}
 for n in M.FOOT_ORDER:
  pb=evaluated.pose.bones[n+'.paw'];sole=src(evaluated.matrix_world@pb.tail);target=src(controls[n][0].matrix_world.translation);a['feet'][n]['planted']=float(controls[n][0].get('planted',True))>=.5;a['feet'][n]['x']=sole[0];a['feet'][n]['y']=sole[1]
  upper=evaluated.pose.bones[n+'.upper'];lower=evaluated.pose.bones[n+'.lower'];r=M.REST[n]
  obj=bpy.data.objects[n+'.continuous_fur'];deformed=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=deformed.to_mesh()
  widths=[];areas=[];inverted=[]
  for i in range(0,len(mesh.vertices),7):
   original=(obj.data.vertices[i].co-obj.data.vertices[i+6].co).length
   widths.append((mesh.vertices[i].co-mesh.vertices[i+6].co).length/original)
  for poly in mesh.polygons:
   indices=list(poly.vertices);oa,ob,oc=[obj.data.vertices[k].co for k in indices];da,db,dc=[mesh.vertices[k].co for k in indices];before=(ob-oa).cross(oc-oa).length;after=(db-da).cross(dc-da).length
   if before>1e-8:
    areas.append((after/before,poly.index))
    if (ob-oa).cross(oc-oa).dot((db-da).cross(dc-da))<0:inverted.append(poly.index)
  deformed.to_mesh_clear()
  minimum=min(areas);maximum=max(areas)
  checks.append({'foot':n,'ik_sole_error_source_px':math.dist(sole,target),'joints_source_px':{'root':src(evaluated.matrix_world@upper.head),'knee':src(evaluated.matrix_world@upper.tail),'ankle':src(evaluated.matrix_world@lower.tail),'sole':sole},'upper_length':float(upper.length),'lower_length':float(lower.length),'bind_upper_length':float(rig.data.bones[n+'.upper'].length),'bind_lower_length':float(rig.data.bones[n+'.lower'].length),'clamped':math.dist(sole,target)>.1,'min_cross_section_width_ratio':min(widths),'min_triangle_area_ratio':minimum[0],'min_triangle_index':minimum[1],'max_triangle_area_ratio':maximum[0],'max_triangle_index':maximum[1],'inverted_triangle_count':len(inverted),'inverted_triangle_indices':inverted})
 a['frame']=index
 return a,checks


def editable_fingerprint():
 """Hash base editing data, excluding evaluated transforms and render caches."""
 data={'meshes':{},'images':{},'curves':{}}
 for obj in bpy.data.objects:
  if obj.type!='MESH':continue
  mesh=obj.data;data['meshes'][obj.name]={'vertices':[tuple(v.co) for v in mesh.vertices],'faces':[tuple(p.vertices) for p in mesh.polygons],'uv':{layer.name:[tuple(p.uv) for p in layer.data] for layer in mesh.uv_layers},'weights':[[(obj.vertex_groups[g.group].name,g.weight) for g in v.groups] for v in mesh.vertices],'colors':{layer.name:[tuple(p.color) for p in layer.data] for layer in mesh.color_attributes}}
 for image in bpy.data.images:
  if image.packed_file:data['images'][image.name]=hashlib.sha256(image.packed_file.data).hexdigest()
 for action in bpy.data.actions:data['curves'][action.name]=[(c.data_path,c.array_index,[tuple(k.co) for k in c.keyframe_points]) for c in action.fcurves]
 return hashlib.sha256(json.dumps(data,sort_keys=True).encode()).hexdigest()


def main():
 argv=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
 parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=ROOT/'build/cream-mesh-rig');parser.add_argument('--mode',choices=['rest','short','bake'],default='rest');parser.add_argument('--original',type=Path,default=DEFAULT_ORIGINAL);parser.add_argument('--hidden',type=Path,default=DEFAULT_HIDDEN);parser.add_argument('--existing-blend',type=Path,help='Bake manually edited source without rebuilding any meshes, UVs, weights, materials or controls.')
 args=parser.parse_args(argv);args.output.mkdir(parents=True,exist_ok=True)
 if args.existing_blend:
  bpy.ops.wm.open_mainfile(filepath=str(args.existing_blend.resolve()));scene=bpy.context.scene;rig=bpy.data.objects['Cream_Mesh_Rig'];controls={n:(bpy.data.objects['FOOT_'+n],bpy.data.objects['ANKLE_'+n]) for n in M.FOOT_ORDER};count=int(rig['loop_pose_count']);derivation=json.loads(rig['reach_derivation_json']);M.STRIDE_LOGICAL=derivation['chosen_stride_logical_px'];contract=json.loads(rig['motion_contract_json']);before_fingerprint=editable_fingerprint()
 else:
  derivation=reach_derivation();M.STRIDE_LOGICAL=derivation['chosen_stride_logical_px'];count=math.ceil(max(M.STRIDE_LOGICAL,(M.STRIDE_LOGICAL/M.SPEED_LOGICAL)*60));scene,rig,controls=build_scene(args);scene.frame_start=0;scene.frame_end=count+1;contract=M.motion_contract(count)
  for i in range(count+1):set_pose(scene,rig,controls,i/count,keyframe=i+1)
  set_pose(scene,rig,controls,0,neutral=True,keyframe=0)
  for obj in [rig]+[o for pair in controls.values() for o in pair]:
   if obj.animation_data and obj.animation_data.action:
    for curve in obj.animation_data.action.fcurves:
     for key in curve.keyframe_points:key.interpolation='CONSTANT' if curve.data_path=='["planted"]' else 'LINEAR'
  text=bpy.data.texts.new('REBUILD_AND_EDIT_README');text.write('Editable offline 2D skinned source. FOOT_* controls are SOLES, not ankles. Frame0 is neutral side stand; frames1..'+str(count)+' are the loop, frame'+str(count+1)+' duplicates the exact seam. Edit mesh vertices, PaintedArtworkUV, Coverage vertex alpha, weights, materials, controls or curves directly. To preserve manual edits: blender -b edited.blend --python tools/cream_mesh_rig/author.py -- --existing-blend edited.blend --mode bake --output build/edited-mesh. That path never rebuilds or resets poses. The saved motion_contract_json is the intended motion contract: altered intentional bob requires an explicit contract edit; accidental jitter must fail validation.\n')
  rig['loop_pose_count']=count;rig['motion_contract_json']=json.dumps(contract);rig['reach_derivation_json']=json.dumps(derivation)
 scene.frame_set(0)
 if not args.existing_blend:set_pose(scene,rig,controls,0,neutral=True)
 standing,standing_checks=actual_annotations(rig,controls,0,count,neutral=True);(args.output/'standing-annotation.json').write_text(json.dumps({'annotation':standing,'legs':standing_checks},indent=2)+'\n');scene.render.filepath=str(args.output/'standing.png');bpy.ops.render.render(write_still=True)
 if not args.existing_blend:bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'authoring.blend'))
 (args.output/'reach-derivation.json').write_text(json.dumps(derivation,indent=2)+'\n')
 project={'schema':1,'name':'Cream editable side-walk mesh review','visual_approval':'FAILED; not a completed natural gait','blend':'authoring.blend','existing_blend_bake_supported':True,'canvas':[313,313],'camera':'orthographic fixed side view','coordinate_axes':{'x':'right','z':'up','y':'layer depth; negative in front'},'anchor_source_px':list(M.ANCHOR),'reference_height':M.REFERENCE_HEIGHT,'source_pose_count':count,'neutral_keyframe':0,'loop_keyframes':[1,count],'duplicate_endpoint_keyframe':count+1,'stride_logical_px':M.STRIDE_LOGICAL,'speed_logical_px_per_second':M.SPEED_LOGICAL,'duty_factor':M.DUTY,'phase_offsets':M.OFFSETS,'fixed_link_rest_joints_source_px':M.REST,'source_uv_limb_paths':GEN_PATHS,'source_uv_limb_cell_offsets':GEN_RECTS,'sole_controls':['FOOT_'+n for n in M.FOOT_ORDER],'mesh_groups':{'head':'Head_original_pixels','torso':'Torso_original_outer_fur','tail':'Tail_original_fur','limbs':[n+'.continuous_fur' for n in M.FOOT_ORDER]},'packed_images':True,'inputs':[{'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest() if p.is_file() else 'packed in existing blend'} for p in (args.original,args.hidden)],'original_art_kept':'head, outer torso, tail, near-side painted toes','new_purpose_art_used':'independent hidden four-limb fur, hidden belly/chest, complete far-side painted toes','rendered_pose_count_is_not_independent_painted_drawing_count':True,'rebake_command':'blender -b --python tools/cream_mesh_rig/author.py -- --existing-blend EDITED.blend --mode bake --output build/cream-mesh-edited'}
 (args.output/'rig-project.json').write_text(json.dumps(project,indent=2)+'\n')
 if args.existing_blend:
  after_fingerprint=editable_fingerprint();(args.output/'rebake-source-integrity.json').write_text(json.dumps({'before':before_fingerprint,'after':after_fingerprint,'manual_base_mesh_uv_weight_paint_and_curve_data_preserved':before_fingerprint==after_fingerprint,'source_file_written':False},indent=2)+'\n');assert before_fingerprint==after_fingerprint
 else:
  rig['project_json']=json.dumps(project);bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'authoring.blend'))
 if args.mode=='rest':return
 annotations=[];checks=[];frames=[]
 sub=args.output/('frames' if args.mode=='bake' else 'short');sub.mkdir(exist_ok=True)
 samples=count if args.mode=='bake' else 45
 for i in range(samples):
  p=i/count if args.mode=='bake' else .25+i/(samples-1)
  framevalue=(p%1)*count+1
  if args.mode=='bake':scene.frame_set(i+1)
  else:scene.frame_set(int(framevalue),subframe=framevalue-int(framevalue))
  if not args.existing_blend:set_pose(scene,rig,controls,p,root_x=0)
  annotation,rows=actual_annotations(rig,controls,p,i);annotations.append(annotation);checks.append({'frame':i,'phase':p,'legs':rows})
  scene.render.filepath=str(sub/f'frame-{i:04d}.png');bpy.ops.render.render(write_still=True)
  frames.append({'rect':[0,0,313,313],'anchor':annotation['root_anchor']})
 entry={'clip':'walk','character':0,'source':'atlas.png','columns':math.ceil(math.sqrt(count)),'rows':math.ceil(count/math.ceil(math.sqrt(count))),'reference_height':221,'frame_count':count,'frames':frames,'standing_frame':0,'stride_pixels':M.STRIDE_LOGICAL,'sequence':{'intro':[],'loop':[{'frame':i,'ms':1} for i in range(count)],'outro':[],'finish':'complete','duration_ms':0,'events':[]}}
 if args.mode=='bake':
  doc={'schema':1,'entry':entry,'annotations':annotations,'motion_contract':contract,'auxiliary_annotations':[standing],'source_blend':str(args.existing_blend or args.output/'authoring.blend')}
  (args.output/'annotations.json').write_text(json.dumps(doc,indent=2)+'\n')
 (args.output/('rig-evaluation.json' if args.mode=='bake' else 'short-evaluation.json')).write_text(json.dumps({'frame_count':samples,'stride_logical_px':M.STRIDE_LOGICAL,'source_pose_count':count,'reach_derivation':derivation,'frames':checks,'annotations':annotations},indent=2)+'\n')
 if not args.existing_blend:
  set_pose(scene,rig,controls,0);scene.frame_set(1);bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'authoring.blend'))

if __name__=='__main__':main()
