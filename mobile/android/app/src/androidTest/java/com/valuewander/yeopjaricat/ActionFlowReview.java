package com.valuewander.yeopjaricat;

import android.graphics.Canvas;
import android.os.Bundle;
import android.os.SystemClock;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.HashSet;
import org.json.JSONArray;
import org.json.JSONObject;

/** Exercise two independent schedulers, registered poses and both actors on the real Android renderer. */
final class ActionFlowReview {
    private final MotionSmoke test;
    private final ActionParityReview fixture;
    ActionFlowReview(MotionSmoke test){this.test=test;fixture=new ActionParityReview(test);}

    private static final class Track {
        String story="";
        int started,shortest=Integer.MAX_VALUE,count,shortStories,emptySit;
        final HashSet<String> variety=new HashSet<>();
        final JSONArray history=new JSONArray();
        void observe(PetScene.Actor a,int time){
            String next=a.selfDirected?a.owner:"";
            if(!next.equals(story)){
                if(!story.isEmpty()){
                    int duration=time-started;
                    shortest=Math.min(shortest,duration);
                    count++;if(duration<=500)shortStories++;
                    history.put(story+" "+duration+"ms");
                    variety.add(story);
                }
                story=next;started=time;
            }
            if(a.clock!=null&&a.selfDirected&&a.step.bridge<0&&a.plan.isEmpty())emptySit++;
        }
        JSONObject result()throws Exception{
            return new JSONObject().put("completed",count).put("distinct",variety.size())
                .put("shortest_ms",shortest).put("under_or_equal_500ms",shortStories)
                .put("unrequested_seated_outro_samples",emptySit).put("history",history);
        }
    }
    void film()throws Exception{
        String label=test.arguments.getString("sample","after");
        int[] coats={0,8,10};
        for(int i=0;i<coats.length;i++){
            fixture.configure(coats[i],coats[(i+1)%3]);
            PetScene scene=fixture.scene;
            File dir=new File(test.directory,"flow-film-"+label+"-"+coats[i]);
            if(!dir.isDirectory()&&!dir.mkdirs())throw new java.io.IOException(dir.toString());
            Canvas canvas=new Canvas(fixture.view);
            test.runOnMainSync(()->{
                scene.random.setSeed(8731);
                scene.automatic=true;
                for(PetScene.Actor a:scene.actors)a.start("walk",true);
            });
            for(int frame=0;frame<400;){
                boolean[] ready={false};
                test.runOnMainSync(()->{
                    if(scene.actors[0].loading||scene.actors[1].loading)return;
                    scene.tick(50);
                    fixture.view.eraseColor(0xffeee5df);
                    scene.draw(canvas);ready[0]=true;
                });
                if(!ready[0]){SystemClock.sleep(5);continue;}
                try(java.io.FileOutputStream file=new java.io.FileOutputStream(new File(dir,frame+".png"))){
                    fixture.view.compress(android.graphics.Bitmap.CompressFormat.PNG,100,file);
                }
                frame++;
            }
        }
        Bundle done=new Bundle();done.putString("stream","ACTION_FLOW_FILM_OK");test.finish(MotionSmoke.RESULT_OK,done);
    }
    void run()throws Exception{
        int[] coats={0,8,10};JSONArray results=new JSONArray();
        String label=test.arguments.getString("sample","after");
        for(int round=0;round<2;round++)for(int i=0;i<3;i++){
            int coat=coats[i],companion=coats[(i+1)%3],seed=6877+round*79+i;
            fixture.configure(coat,companion);
            PetScene scene=fixture.scene;scene.random.setSeed(seed);scene.automatic=true;
            int[] now={0};Track[] tracks={new Track(),new Track()};
            Canvas canvas=new Canvas(fixture.view);
            long deadline=SystemClock.elapsedRealtime()+110000;
            while(now[0]<150000 && SystemClock.elapsedRealtime()<deadline){
                test.runOnMainSync(()->{
                    for(int n=0;n<100;n++){
                        if(scene.actors[0].loading||scene.actors[1].loading)break;
                        scene.tick(16);now[0]+=16;
                        if(n%8==0)scene.draw(canvas);
                        for(int slot=0;slot<2;slot++)tracks[slot].observe(scene.actors[slot],now[0]);
                    }
                });
                SystemClock.sleep(3);
            }
            test.check(now[0]>=150000,"Scene stalled "+scene.state());
            for(int slot=0;slot<2;slot++){
                Track t=tracks[slot];
                results.put(t.result().put("coat",slot==0?coat:companion).put("slot",slot).put("seed",seed));
                Files.write(new File(test.directory,"action-flow-"+label+".json").toPath(),results.toString(2).getBytes(StandardCharsets.UTF_8));
                if(!label.equals("before"))test.check(t.count>=5 && t.variety.size()>=4 && t.shortStories==0 && t.emptySit==0,t.result().toString());
            }
        }
        Bundle done=new Bundle();done.putString("stream","ACTION_FLOW_PASSED "+label+" "+results.length());test.finish(MotionSmoke.RESULT_OK,done);
    }
}

