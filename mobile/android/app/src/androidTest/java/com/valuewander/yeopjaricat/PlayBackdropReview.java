package com.valuewander.yeopjaricat;

import android.graphics.RectF;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;

final class PlayBackdropReview {
    private final MotionSmoke test;
    private final MainActivity activity;

    PlayBackdropReview(MotionSmoke test){this.test=test;activity=test.activity;}

    private View tile(View view,String label){
        if(label.contentEquals(view.getContentDescription()==null?"":view.getContentDescription()))return view;
        if(view instanceof ViewGroup){
            ViewGroup group=(ViewGroup)view;
            for(int i=0;i<group.getChildCount();i++){
                View found=tile(group.getChildAt(i),label);
                if(found!=null)return found;
            }
        }
        return null;
    }

    private void select(PlayBackdrop design){
        View item=tile(activity.getWindow().getDecorView(),design.label);
        test.check(item!=null&&item.performClick(),"Missing selector "+design.label);
        test.check(item.isSelected(),"Missing selected state "+design.label);
    }
    private void loaded(PlayBackdrop design)throws Exception{
        test.waitFor(()->activity.preview.backdrop.displayed==design&&activity.preview.backdrop.bitmap!=null,"Backdrop not drawn: "+design);
    }
    private void checkArt(PlayBackdrop design){
        for(float[] viewport:new float[][]{{360,800},{800,700},{340,225},{800,225}}){
            for(float lane:new float[]{.30f,.58f,.78f,.88f}){
                float w=viewport[0],h=viewport[1];RectF r=new RectF();
                boolean tall=w<h*.74f;
                design.frame(r,w,h,tall?941:1254,tall?1672:1254,h*lane);
                test.check(r.left<=.01f&&r.top<=.01f&&r.right>=w-.01f&&r.bottom>=h-.01f,"Exposed edge "+design);
                test.check(Math.abs(r.top+(tall?design.portraitGround:design.ground)*r.height()-h*lane)<.02f,"Ground drift "+design);
            }
        }
    }
    void run() throws Exception {
        String photo=activity.settings.photo();
        if(!photo.isEmpty()&&!activity.settings.prefs.contains(PlayBackdrop.PREF))loaded(PlayBackdrop.PHOTO);
        ActionParityReview rig=new ActionParityReview(test);
        rig.configure(10,8);
        PetScene scene=rig.scene;
        test.runOnMainSync(()->{scene.socialTimer=180000;activity.preview.start();});
        PlayBackdrop[] choices={PlayBackdrop.SUNROOM,PlayBackdrop.GARDEN,PlayBackdrop.ROOFTOP};
        String[][] actions={{"walk","groom"},{"run","ball"},{"tower","scratch"}};
        for(int i=0;i<choices.length;i++){
            PlayBackdrop design=choices[i];
            String[] pair=actions[i];
            checkArt(design);
            PetScene.Actor a=scene.actors[0],b=scene.actors[1];
            long ac=a.completed,bc=b.completed;
            test.runOnMainSync(()->{
                a.request(pair[0]);b.request(pair[1]);int revision=a.revision;
                select(design);activity.preview.wake();
                test.check(scene==activity.preview.scene&&a.revision==revision&&a.owner.equals(pair[0]),"Scene restarted");
            });
            loaded(design);
            SystemClock.sleep(1600);
            test.shot("play-"+design.key);
            long deadline=SystemClock.elapsedRealtime()+55000;
            boolean[] finished={false};
            while(!finished[0]&&SystemClock.elapsedRealtime()<deadline){
                test.runOnMainSync(()->finished[0]=a.completed>ac&&b.completed>bc);
                if(!finished[0])SystemClock.sleep(100);
            }
            test.check(finished[0]&&a.finishedOwner.equals(pair[0])&&b.finishedOwner.equals(pair[1]),"Playback failed on "+design);
            test.check(activity.settings.photo().equals(photo),"Lost existing photo");
        }
        // A high position saved on a photo can be above a preset's back wall.
        test.runOnMainSync(()->{
            PetScene.Actor a=scene.actors[0];a.cancelStory();
            scene.drag(0,0,-scene.height);scene.fitBackdropGround(false);activity.preview.wake();
        });
        float min=PlayBackdrop.ROOFTOP.farthestWalkableY(scene.width,scene.height,scene.floor);
        test.waitFor(()->scene.actors[0].ground()>=min-1,"cat did not walk down onto the stage");
        test.runOnMainSync(()->{select(PlayBackdrop.GARDEN);select(PlayBackdrop.ROOFTOP);select(PlayBackdrop.SUNROOM);});
        loaded(PlayBackdrop.SUNROOM);
        if(!photo.isEmpty()){
            test.click("내 사진 다시 사용");loaded(PlayBackdrop.PHOTO);
        }
        test.runOnMainSync(()->select(PlayBackdrop.ROOFTOP));loaded(PlayBackdrop.ROOFTOP);
        Bundle result=new Bundle();result.putString("stream","BACKDROPS_PASSED presets=3 ground_layouts=48 completed_actions=6 retained_photo="+!photo.isEmpty());
        test.finish(MotionSmoke.RESULT_OK,result);
    }
}
