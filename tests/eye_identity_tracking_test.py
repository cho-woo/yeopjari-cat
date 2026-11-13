"""Eye color follows the eye through a supported roll, not screen x order."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
try:
    import numpy as np
    from pack_video_actions import EyeIdentityTracker
except ImportError:
    np=None


@unittest.skipIf(np is None,'authoring image dependencies are unavailable')
class EyeIdentityTest(unittest.TestCase):
    def test_anatomical_colors_survive_crossing_the_vertical_axis(self):
        tracker=EyeIdentityTracker();frame={'rect':[0,0,96,96],'anchor':[48,80]}
        for index in range(37):
            angle=math.radians(index*5)
            gold=(round(48-16*math.cos(angle)),round(42-16*math.sin(angle)))
            blue=(round(48+16*math.cos(angle)),round(42+16*math.sin(angle)))
            ordered=sorted([gold,blue]);mask=np.zeros((96,96),np.uint8);eyes=[]
            for ordinal,(x,y) in enumerate(ordered):
                mask[y,x]=ordinal+1
                eyes.append({'rect':[x-2,y-3,4,6],'material':'gold' if ordinal==0 else 'blue',
                             'anatomical_side':'right' if ordinal==0 else 'left','pixels':1})
            _,result=tracker.apply(eyes,mask,frame,index)
            self.assertEqual(result[gold[1],gold[0]],1)
            self.assertEqual(result[blue[1],blue[0]],2)


if __name__=='__main__':unittest.main()
