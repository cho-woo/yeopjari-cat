package com.valuewander.yeopjaricat;

import android.content.Context;
import android.content.SharedPreferences;

final class PetSettings {
    static final String FILE = "pet_settings";
    final SharedPreferences prefs;
    PetSettings(Context context) { prefs = context.getSharedPreferences(FILE, Context.MODE_PRIVATE); }
    int coat() { return validCoat(prefs.getInt("coat", 0)); }
    int companionCoat() { return validCoat(prefs.getInt("companion_coat", 8)); }
    static int validCoat(int c) { return c == 8 || c == 10 ? c : 0; }
    boolean companion() { return prefs.getBoolean("companion", false); }
    boolean overlay() { return prefs.getBoolean("overlay", false); }
    boolean enabled() { return prefs.getBoolean("pets_enabled", true); }
    boolean homeTouch() { return prefs.getBoolean("touch_home", false); }
    static boolean changesScene(String key){return key!=null&&(key.equals("coat")||key.equals("companion")||key.equals("companion_coat")||key.equals("size")||key.equals("home_floor_percent"));}
    int size() { return Math.max(52, Math.min(100, prefs.getInt("size", 74))); }
    int background() { return prefs.getInt("background", 0xffeae3f6); }
    String photo() { return prefs.getString("photo", ""); }
    void put(String key, boolean value) { prefs.edit().putBoolean(key, value).apply(); }
    void put(String key, int value) { prefs.edit().putInt(key, value).apply(); }
    void put(String key, String value) { prefs.edit().putString(key, value).apply(); }
}
