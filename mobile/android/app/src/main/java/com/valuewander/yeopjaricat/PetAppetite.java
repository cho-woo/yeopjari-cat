package com.valuewander.yeopjaricat;

import android.content.SharedPreferences;

/** A pet slot keeps its appetite across outfits, Home, preview and app updates. */
final class PetAppetite {
    private final SharedPreferences prefs;
    private final String amountKey, timeKey;
    private static final double MILLIS_PER_POINT=3600000.0/3.0;
    PetAppetite(SharedPreferences prefs,int slot,long now){
        this.prefs=prefs;
        amountKey="appetite."+slot+".amount";
        timeKey="appetite."+slot+".time";
        synchronized(PetAppetite.class){
            if(!prefs.contains(timeKey))prefs.edit().putFloat(amountKey,55).putLong(timeKey,now).apply();
        }
    }
    static boolean food(String action){return "fish".equals(action)||"eat".equals(action)||"treat".equals(action);}
    private double amount(long now){
        double elapsed=Math.max(0.0,(double)now-prefs.getLong(timeKey,now));
        return Math.max(20,prefs.getFloat(amountKey,55)-elapsed/MILLIS_PER_POINT);
    }
    int displayed(long now){synchronized(PetAppetite.class){return (int)Math.round(amount(now));}}
    boolean accepts(String food,long now){return displayed(now)<("treat".equals(food)?92:85);}
    boolean serve(String food,long now){
        if(!food(food))return false;
        synchronized(PetAppetite.class){
            double current=amount(now);
            boolean treat="treat".equals(food);
            if(Math.round(current)>=(treat?92:85))return false;
            float updated=(float)Math.min(100,current+(treat?12:35));
            prefs.edit().putFloat(amountKey,updated).putLong(timeKey,Math.max(now,prefs.getLong(timeKey,now))).apply();
            return true;
        }
    }
}
