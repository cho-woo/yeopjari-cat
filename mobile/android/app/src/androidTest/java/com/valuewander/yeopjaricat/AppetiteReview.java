package com.valuewander.yeopjaricat;

import android.os.Bundle;
import android.content.SharedPreferences;

final class AppetiteReview {
    final MotionSmoke test;
    final ActionParityReview runner;
    AppetiteReview(MotionSmoke test){this.test=test;runner=new ActionParityReview(test);}
    void check(boolean value,String message){test.check(value,message);}
    void serve(PetScene.Actor actor,String food)throws Exception{
        test.runOnMainSync(()->actor.request(food));
        runner.sweep(food,()->!actor.busy());
        check(food.equals(actor.finishedOwner),"Food did not finish: "+food);
    }
    void run()throws Exception{
        int[] coats={0,8,10};
        for(int i=0;i<coats.length;i++){
            runner.configure(coats[i],coats[(i+1)%3]);
            PetScene s=runner.scene;PetScene.Actor first=s.actors[0],second=s.actors[1];
            SharedPreferences prefs=s.settings.prefs;
            long now=System.currentTimeMillis();
            prefs.edit().putFloat("appetite.0.amount",55).putFloat("appetite.1.amount",55)
                .putLong("appetite.0.time",now).putLong("appetite.1.time",now).commit();
            serve(first,"fish");check(first.appetite.displayed(System.currentTimeMillis())==90,"Fish portion");
            long completed=first.completed;
            test.runOnMainSync(()->first.request("eat"));
            check(!first.busy() && first.completed==completed,"Full meal restarted");
            serve(first,"treat");check(first.appetite.displayed(System.currentTimeMillis())==100,"Immediate treat");
            check(second.appetite.displayed(System.currentTimeMillis())==55,"Other cat gained fullness");
            test.runOnMainSync(()->{second.request("eat");second.request("treat");});
            runner.sweep("queued treat",()->!second.busy() && second.queued==null);
            check(second.appetite.displayed(System.currentTimeMillis())==100,"Queued portion");
            PetAppetite restored=new PetAppetite(new PetSettings(test.activity).prefs,0,now);
            check(restored.displayed(now)==100 && !restored.serve("treat",now),"Restore/overfeed");
            long updated=prefs.getLong("appetite.0.time",0);
            check(restored.displayed(updated+6*3600000L)==82 && restored.serve("eat",updated+6*3600000L),"Offline appetite");
            check(restored.displayed(updated)==100,"Clock rollback added appetite");
        }
        Bundle b=new Bundle();b.putString("stream","APPETITE_PASSED 3 coats x 2 roles");test.finish(MotionSmoke.RESULT_OK,b);
    }
}
