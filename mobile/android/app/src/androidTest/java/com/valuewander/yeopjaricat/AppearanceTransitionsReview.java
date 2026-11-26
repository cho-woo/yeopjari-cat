package com.valuewander.yeopjaricat;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.os.Bundle;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.TreeSet;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;
import org.json.JSONObject;

/** Check every ordered clip boundary using the production canvas and both actor slots.
 * Only the first and last registered bitmap are retained, keeping test memory bounded. */
final class AppearanceTransitionsReview {
    private final MotionSmoke test;
    AppearanceTransitionsReview(MotionSmoke test){this.test=test;}

    private static MotionLibrary.Clip endpoints(MotionLibrary.Clip source)throws Exception{
        JSONObject spec=new JSONObject(source.spec.toString());
        JSONArray frames=new JSONArray(), intro=new JSONArray();
        Bitmap[] pages=new Bitmap[2];
        for(int k=0;k<2;k++){
            int i=source.frames[k==0?0:source.frames.length-1];
            int[] r=source.rectangles[i];
            Bitmap cropped=Bitmap.createBitmap(source.pages[source.page[i]],r[0],r[1],r[2],r[3]);
            pages[k]=cropped;
            JSONObject f=new JSONObject().put("page",k)
                .put("rect",new JSONArray(Arrays.asList(0,0,r[2],r[3])))
                .put("anchor",new JSONArray(Arrays.asList(source.anchors[i][0],source.anchors[i][1])));
            frames.put(f);
            intro.put(new JSONObject().put("frame",k).put("ms",42));
        }
        spec.put("frames",frames).put("sequence",new JSONObject().put("intro",intro)
                .put("loop",new JSONArray()).put("outro",new JSONArray()));
        spec.remove("paws");
        return new MotionLibrary.Clip(spec,pages,false);
    }
    void run(int coat)throws Exception{
        ActionParityReview setup=new ActionParityReview(test);
        setup.configure(coat,coat);
        PetScene scene=setup.scene;
        test.runOnMainSync(()->{test.activity.preview.stop();scene.size=42;scene.floor=105;});
        TreeSet<String> names=new TreeSet<>();
        String prefix="c"+coat+"-";
        for(String file:test.activity.getAssets().list("motions"))
            if(file.startsWith(prefix)&&file.endsWith(".json"))names.add(file.substring(prefix.length(),file.length()-5));
        test.check(names.size()>=44,"Missing motion library");
        String focus=test.arguments.getString("clip");
        if(focus!=null){test.check(names.contains(focus),focus);names.retainAll(java.util.Collections.singleton(focus));}
        ArrayList<MotionLibrary.Clip> poses=new ArrayList<>();
        for(String name:names){
            CountDownLatch loaded=new CountDownLatch(1);
            MotionLibrary.Clip[] slot=new MotionLibrary.Clip[1];
            test.runOnMainSync(()->scene.library.request(coat,name,c->{slot[0]=c;loaded.countDown();}));
            test.check(loaded.await(20,TimeUnit.SECONDS)&&slot[0]!=null,name);
            poses.add(endpoints(slot[0]));
        }
        final int size=144;
        Bitmap before=Bitmap.createBitmap(size,size,Bitmap.Config.ARGB_8888);
        Bitmap after=Bitmap.createBitmap(size,size,Bitmap.Config.ARGB_8888);
        Canvas first=new Canvas(before),second=new Canvas(after);
        int[] a=new int[size*size],b=new int[a.length];
        int[][][][] endpoints=new int[poses.size()][2][2][];
        android.graphics.Paint brush=new android.graphics.Paint(android.graphics.Paint.FILTER_BITMAP_FLAG|android.graphics.Paint.ANTI_ALIAS_FLAG);
        for(int c=0;c<poses.size();c++)for(int frame=0;frame<2;frame++)for(int direction=0;direction<2;direction++){
            after.eraseColor(0xffe5dad0);
            poses.get(c).paint(second,frame,70,scene.floor,scene.size,direction==0?1:-1,brush);
            int[] pixels=new int[a.length];
            after.getPixels(pixels,0,size,0,0,size,size);endpoints[c][frame][direction]=pixels;
        }
        int surfaceDelta=0;
        if(focus!=null){
            Bitmap transparent=Bitmap.createBitmap(size,size,Bitmap.Config.ARGB_8888);
            MotionLibrary.Clip sample=poses.get(0);
            sample.paint(new Canvas(transparent),0,70,scene.floor,scene.size,1,brush);
            before.eraseColor(0xffe5dad0);
            first.drawBitmap(transparent,null,new android.graphics.Rect(0,0,size,size),null);
            before.getPixels(a,0,size,0,0,size,size);
            for(int i=0;i<a.length;i++)for(int sh=0;sh<=16;sh+=8)
                surfaceDelta=Math.max(surfaceDelta,Math.abs(((a[i]>>sh)&255)-((endpoints[0][0][0][i]>>sh)&255)));
        }
        int worst=0,checked=0,failures=0,maxMix=0;long offMix=0;String worstPair="",mixPair="";
        for(int src=0;src<poses.size();src++)for(int dst=0;dst<poses.size();dst++){
            for(int actorIndex=0;actorIndex<2;actorIndex++){
                PetScene.Actor actor=scene.actors[actorIndex];
                actor.x=70;actor.lift=0;actor.offsetY=0;actor.carryMs=0;
                actor.heldBlend=null;
                actor.previousPose=poses.get(src);actor.previousFrame=1;
                actor.previousFacing=(src+actorIndex)%2==0?1:-1;
                actor.clip=poses.get(dst);actor.clock=actor.clip.clock();
                actor.facing=(dst+actorIndex)%2==0?1:-1;
                actor.fade=actorIndex==0?.33f:.67f;
                int incoming=Math.round(PetScene.smooth(actor.fade)*255);
                int[] outgoingPixels=endpoints[src][1][actor.previousFacing==1?0:1];
                int[] incomingPixels=endpoints[dst][0][actor.facing==1?0:1];
                before.eraseColor(0xffe5dad0);after.eraseColor(0xffe5dad0);
                actor.draw(first);
                actor.remember();actor.clock=null;actor.clip=null;
                actor.draw(second);
                before.getPixels(a,0,size,0,0,size,size);after.getPixels(b,0,size,0,0,size,size);
                int sum=0,count=0;
                for(int i=0;i<a.length;i++){
                    if(a[i]==0xffe5dad0 && b[i]==a[i] && incomingPixels[i]==a[i] && outgoingPixels[i]==a[i])continue;
                    for(int shift=0;shift<=16;shift+=8){
                        int start=(outgoingPixels[i]>>shift)&255,end=(incomingPixels[i]>>shift)&255;
                        int expected=(start*(255-incoming)+end*incoming+127)/255;
                        int deviation=Math.abs(expected-((a[i]>>shift)&255));
                        if(deviation>3)offMix++;
                        if(deviation>maxMix){
                            maxMix=deviation;
                            mixPair=poses.get(src).spec.getString("name")+"/"+poses.get(dst).spec.getString("name")+
                                " xy="+(i%size)+","+(i/size)+" expected="+expected+" got="+((a[i]>>shift)&255)+
                                " ends="+start+","+end+" channel="+shift+" weight="+incoming;
                        }
                    }
                    int red=Math.abs(Color.red(a[i])-Color.red(b[i]));
                    int green=Math.abs(Color.green(a[i])-Color.green(b[i]));
                    int blue=Math.abs(Color.blue(a[i])-Color.blue(b[i]));
                    sum+=Math.max(red,Math.max(green,blue));
                    count++;
                }
                test.check(count>30,"Empty join");
                int meanTimes100=Math.round(sum*100f/count);
                if(meanTimes100>worst){worst=meanTimes100;worstPair=poses.get(src).spec.getString("name")+" -> "+poses.get(dst).spec.getString("name");}
                if(meanTimes100>400)failures++;
                checked++;
                actor.heldBlend=null;
            }
        }
        Bundle report=new Bundle();
        report.putString("stream","coat="+coat+" clips="+names.size()+" actor_transitions="+checked+
                " failed="+failures+" worst_mean="+(worst/100f)+" max_mix_error="+maxMix+
                " off_mix_channels="+offMix+" surface_delta="+surfaceDelta+" mix="+mixPair+" pair="+worstPair);
        test.finish(failures==0 && maxMix<=3?MotionSmoke.RESULT_OK:MotionSmoke.RESULT_CANCELED,report);
    }
}

