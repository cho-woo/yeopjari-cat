package com.valuewander.yeopjaricat;

import android.graphics.RectF;

enum PlayBackdrop {
    SUNROOM("sunroom","햇살 마루",R.drawable.scene_sunroom,R.drawable.portrait_sunroom,R.drawable.thumb_sunroom,.64f,.62f,.51f,.52f,.45f),
    GARDEN("garden","꽃담 정원",R.drawable.scene_garden,R.drawable.portrait_garden,R.drawable.thumb_garden,.64f,.63f,.47f,.51f,.47f),
    ROOFTOP("rooftop","달빛 옥상",R.drawable.scene_rooftop,R.drawable.portrait_rooftop,R.drawable.thumb_rooftop,.67f,.65f,.62f,.55f,.55f),
    PHOTO("photo","내 사진",0,0,0,0,0,.5f,0,0);

    static final String PREF="play_backdrop";
    final String key,label;
    final int image,portraitImage,thumbnail;
    final float ground,portraitGround,focusX,backEdge,portraitBackEdge;

    PlayBackdrop(String key,String label,int image,int portraitImage,int thumbnail,float ground,float portraitGround,float focusX,float backEdge,float portraitBackEdge){
        this.key=key;this.label=label;this.image=image;this.thumbnail=thumbnail;
        this.portraitImage=portraitImage;this.ground=ground;this.portraitGround=portraitGround;this.focusX=focusX;this.backEdge=backEdge;this.portraitBackEdge=portraitBackEdge;
    }
    static PlayBackdrop selected(PetSettings settings){
        String stored=settings.prefs.getString(PREF,"");
        for(PlayBackdrop design:values()){
            if(design.key.equals(stored)&&(design!=PHOTO||!settings.photo().isEmpty()))return design;
        }
        return settings.photo().isEmpty()?SUNROOM:PHOTO;
    }

    float farthestWalkableY(float w,float h,float floor){
        boolean tall=w<h*.74f;
        RectF drawn=new RectF();
        frame(drawn,w,h,tall?941:1254,tall?1672:1254,floor);
        return drawn.top+(tall?portraitBackEdge:backEdge)*drawn.height()+Math.min(6,h*.02f);
    }

    // Keep the registered strip of open ground under the feet on narrow and
    // unfolded screens. Photo framing retains the original center crop.
    void frame(RectF dest,float w,float h,int imageW,int imageH,float floor){
        float scale=Math.max(w/imageW,h/imageH);
        boolean portraitArt=imageH>imageW;
        float footBand=portraitArt?portraitGround:ground;
        float y;
        if(this==PHOTO)y=(h-imageH*scale)/2;
        else {
            float feet=Math.max(0,Math.min(h,floor));
            scale=Math.max(scale,Math.max(feet/(footBand*imageH),(h-feet)/((1-footBand)*imageH)));
            y=feet-footBand*imageH*scale;
        }
        float width=imageW*scale,height=imageH*scale;
        float x=Math.max(w-width,Math.min(0,w/2-width*(portraitArt?.5f:focusX)));
        dest.set(x,y,x+width,y+height);
    }
}
