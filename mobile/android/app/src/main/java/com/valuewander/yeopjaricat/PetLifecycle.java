package com.valuewander.yeopjaricat;

import android.content.Context;
import android.content.Intent;

final class PetLifecycle {
    static void stop(Context context){
        HomeSceneStore.save();
        new PetSettings(context).prefs.edit().putBoolean("pets_enabled",false).putBoolean("overlay",false).putBoolean("touch_home",false).apply();
        context.stopService(new Intent(context,CatOverlayService.class));
        HomeSceneStore.notifyLayer(context);
    }
    static void resume(Context context){new PetSettings(context).prefs.edit().putBoolean("pets_enabled",true).apply();HomeSceneStore.notifyLayer(context);}
}
