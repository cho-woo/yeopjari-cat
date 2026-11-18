package com.valuewander.yeopjaricat;

import android.app.AppOpsManager;
import android.app.usage.UsageEvents;
import android.app.usage.UsageStatsManager;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ResolveInfo;
import android.os.Build;
import android.os.Process;
import android.os.SystemClock;

/** Compare only the latest resumed package with the selected Home app. No UI contents or history is stored. */
final class HomeVisibility {
    private static long checked,lastEventTime;
    private static boolean cached;
    private static String foreground,foregroundClass;
    static boolean hasAccess(Context context){
        AppOpsManager ops=(AppOpsManager)context.getSystemService(Context.APP_OPS_SERVICE);
        int mode=Build.VERSION.SDK_INT>=29?ops.unsafeCheckOpNoThrow(AppOpsManager.OPSTR_GET_USAGE_STATS,Process.myUid(),context.getPackageName()):ops.checkOpNoThrow(AppOpsManager.OPSTR_GET_USAGE_STATS,Process.myUid(),context.getPackageName());
        return mode==AppOpsManager.MODE_ALLOWED;
    }
    @SuppressWarnings("deprecation") static synchronized boolean isHome(Context context){
        if(!hasAccess(context)){foreground=null;foregroundClass=null;lastEventTime=0;cached=false;return false;}
        long now=SystemClock.uptimeMillis();if(now-checked<150)return cached;checked=now;
        long wall=System.currentTimeMillis();
        UsageEvents events;
        try{events=((UsageStatsManager)context.getSystemService(Context.USAGE_STATS_SERVICE)).queryEvents(lastEventTime==0?wall-120000:Math.max(wall-120000,lastEventTime-1),wall);}
        catch(SecurityException denied){foreground=null;cached=false;return false;}
        if(events==null){foreground=null;cached=false;return false;}
        UsageEvents.Event event=new UsageEvents.Event();
        while(events!=null&&events.hasNextEvent()){
            events.getNextEvent(event);
            if(event.getTimeStamp()<lastEventTime)continue;
            if(event.getEventType()==UsageEvents.Event.MOVE_TO_FOREGROUND){
                foreground=event.getPackageName();
                foregroundClass=event.getClassName();
                lastEventTime=event.getTimeStamp();
            }else if(event.getEventType()==UsageEvents.Event.MOVE_TO_BACKGROUND
                    && event.getPackageName().equals(foreground)
                    && java.util.Objects.equals(event.getClassName(),foregroundClass)){
                foreground=null;
                foregroundClass=null;
                lastEventTime=event.getTimeStamp();
            }
        }
        Intent home=new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME);
        ResolveInfo selected=context.getPackageManager().resolveActivity(home,android.content.pm.PackageManager.MATCH_DEFAULT_ONLY);
        cached=selected!=null && selected.activityInfo!=null
                && selected.activityInfo.packageName.equals(foreground)
                && foregroundClass!=null
                && (foregroundClass.equals(selected.activityInfo.name)
                    || foregroundClass.equals(selected.activityInfo.targetActivity));
        return cached;
    }
}
