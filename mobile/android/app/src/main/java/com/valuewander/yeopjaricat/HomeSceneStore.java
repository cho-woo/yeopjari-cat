package com.valuewander.yeopjaricat;

import android.content.Context;

/** Application-only state. Wallpaper and its optional touch windows share one clock. */
final class HomeSceneStore {
    static final java.util.ArrayList<Runnable> listeners=new java.util.ArrayList<>();
    static void listen(Runnable listener){if(!listeners.contains(listener))listeners.add(listener);}
    static void unlisten(Runnable listener){listeners.remove(listener);}
    private static PetScene home;
    private static String configuration="";
    private static int floorPercent=Integer.MIN_VALUE;
    static boolean visible;
    static float zoom;
    static PetScene get(Context context){
        PetSettings s=new PetSettings(context);
        String current=s.coat()+":"+s.companion()+":"+s.companionCoat()+":"+s.size()+":"+s.prefs.getInt("home_floor_percent",58);
        if(home==null||!configuration.equals(current)){
            if(home!=null){home.savePositions();home.close();}configuration=current;
            int requestedFloor=s.prefs.getInt("home_floor_percent",58);
            // An explicit height-slider command overrides previously dragged heights.
            if(floorPercent!=Integer.MIN_VALUE&&floorPercent!=requestedFloor)s.prefs.edit().remove("position.home.0.y").remove("position.home.1.y").apply();
            floorPercent=requestedFloor;
            home=new PetScene(context.getApplicationContext(),()->{},"home");
        }
        return home;
    }
    static void visibility(Context context,boolean value){visible=value;notifyLayer(context);}
    static void zoom(Context context,float value){zoom=value;notifyLayer(context);}
    static void notifyLayer(Context context){for(Runnable listener:new java.util.ArrayList<>(listeners))listener.run();}
    static boolean touchEligible(){return visible&&zoom<.1f;}
    static void save(){if(home!=null)home.savePositions();}
}
