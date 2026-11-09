"""Authoring motion curves: source-pixel rig coordinates, no image warping."""
from __future__ import annotations
import math

CANVAS = 313
ANCHOR = (161.0,275.0)
REFERENCE_HEIGHT = 221.0
SCALE = 110.0/REFERENCE_HEIGHT
FOOT_ORDER = ('near_front','far_front','near_rear','far_rear')
OFFSETS = {'near_rear':0.0,'near_front':0.25,'far_rear':0.5,'far_front':0.75}
# Deliberately bent anatomical rest paths give two fixed links reach reserve.
REST = {
 'near_front':[(198.,185.),(222.,222.),(207.,259.),(223.,275.)],
 'near_rear':[(92.,182.),(116.,220.),(77.,259.),(77.5,275.)],
 'far_front':[(178.,185.),(196.,224.),(180.,259.),(196.,275.)],
 'far_rear':[(115.,182.),(147.,225.),(134.,259.),(134.5,275.)],
}
STRIDE_LOGICAL=30.0
DUTY=.64
LIFT_LOGICAL=4.4
SPEED_LOGICAL=22.0
BODY_POINT=(140.,197.)
HEAD_POINT=(231.,127.)

def smooth5(t):return t*t*t*(10+t*(-15+6*t))
def smooth5d(t):return 30*t*t*(t-1)*(t-1)
def body_shift(p):return (.6*math.sin(2*math.pi*p),1.4*math.sin(4*math.pi*p))
def head_shift(p):return (.15*math.sin(2*math.pi*p),.3*math.sin(4*math.pi*p))
def body_velocity(p):return (.6*2*math.pi*math.cos(2*math.pi*p),1.4*4*math.pi*math.cos(4*math.pi*p))
def head_velocity(p):return (.15*2*math.pi*math.cos(2*math.pi*p),.3*4*math.pi*math.cos(4*math.pi*p))

def foot_pose(name,p,stride=None,duty=DUTY,lift=LIFT_LOGICAL):
 if stride is None:stride=STRIDE_LOGICAL
 q=(p-OFFSETS[name])%1.0
 s=stride/SCALE
 x=REST[name][-1][0]+s*(duty*.5-q)
 y=ANCHOR[1];vx=-s;vy=0.;angle=0.
 planted=q<duty
 if not planted:
  u=(q-duty)/(1-duty)
  x+=s*smooth5(u)
  vx+=s*smooth5d(u)/(1-duty)
  y-=lift/SCALE*math.sin(math.pi*u)**2
  vy=-lift/SCALE*math.pi*math.sin(2*math.pi*u)/(1-duty)
  angle=math.radians(10)*math.sin(math.pi*u)**2
 return {'position':(x,y),'velocity':(vx,vy),'planted':planted,'angle':angle,'phase':q}

def annotation(p):
 b=body_shift(p);h=head_shift(p)
 return {'feet':{n:{'x':foot_pose(n,p)['position'][0],'y':foot_pose(n,p)['position'][1],'planted':foot_pose(n,p)['planted']} for n in FOOT_ORDER},
         'landmarks':{'head':[HEAD_POINT[0]+h[0],HEAD_POINT[1]+h[1]],'body':[BODY_POINT[0]+b[0],BODY_POINT[1]+b[1]]}}

def boundary(p):
 def landmark(point,shift,velocity):
  xy=shift(p);v=velocity(p)
  return {'position':[(point[i]+xy[i]-ANCHOR[i])*SCALE for i in (0,1)],'velocity':[a*SCALE for a in v]}
 return {'landmarks':{'head':landmark(HEAD_POINT,head_shift,head_velocity),'body':landmark(BODY_POINT,body_shift,body_velocity)},
  'feet':{n:{'position':[(foot_pose(n,p)['position'][i]-ANCHOR[i])*SCALE for i in (0,1)],'velocity':[a*SCALE for a in foot_pose(n,p)['velocity']],'planted':foot_pose(n,p)['planted']} for n in FOOT_ORDER}}

def motion_contract(count):
 def curve(point,ax,ay):
  return {'base':[(point[i]-ANCHOR[i])*SCALE for i in (0,1)],'harmonics':[{'order':1,'cos':[0.,0.],'sin':[ax*SCALE,0.]},{'order':2,'cos':[0.,0.],'sin':[0.,ay*SCALE]}]}
 return {'schema':1,'coordinate_space':'root_relative_logical_px','foot_source':'rig_controls','sample_phases':[i/count for i in range(count)],
         'stride_logical_px':STRIDE_LOGICAL,'speed_logical_px_per_second':SPEED_LOGICAL,'source_pose_fps':60,
         'landmarks':{'head':curve(HEAD_POINT,.15,.3),'body':curve(BODY_POINT,.6,1.4)},
         'loop_boundary':{'start':boundary(0.),'end':boundary(1.)}}
