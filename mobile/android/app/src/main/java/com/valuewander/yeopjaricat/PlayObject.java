package com.valuewander.yeopjaricat;

import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;

/** World-space ball and floor props. The actor and its paw use these same dp coordinates. */
final class PlayObject {
    String kind="";
    float x,y,vx,vy,lift,spin;
    float lastPawX,lastPawY,lastBallX,lastBallY;
    boolean observed;
    int hitMask,hits;
    final Paint ink=new Paint(Paint.ANTI_ALIAS_FLAG);
    void place(String type,float px,float floor){
        kind=type;x=px;y=floor;vx=vy=lift=spin=0;hits=0;beginStroke();
    }
    void beginStroke(){hitMask=0;observed=false;}
    void move(double ms,float width,float u){
        if(!kind.equals("ball"))return;
        float dt=(float)(ms*.001),r=12*u;
        x+=vx*dt;spin+=vx*dt/r*57.296f;
        if(x<r || x>width-r){x=Math.max(r,Math.min(width-r,x));vx*=-.57f;}
        if(lift>0||vy>0){vy-=620*u*dt;lift+=vy*dt;if(lift<=0){lift=0;vy=vy< -55*u?-vy*.32f:0;vx*=.83f;}}
        vx*=Math.exp(-dt*(lift>0?.4:2.9));if(Math.abs(vx)<u*.8f)vx=0;
    }
    void contact(PetScene.Actor actor,double dt,float u){
        MotionLibrary.Clip clip=actor.clip;
        int f=actor.clock.frame();
        if(f>=clip.paws.length)return;
        boolean pounce=actor.action.equals("ball-pounce"),tux=actor.coat==8;
        double ms=actor.age;
        int stroke=0;
        double start=tux?2292:2083,end=tux?2750:2458;
        if(pounce){
            stroke=ms>=(tux?2600:2900)?1:0;
            start=stroke==0?(tux?1875:2000):(tux?2750:3083);
            end=stroke==0?(tux?2250:2375):(tux?3375:3542);
        }
        float px=actor.x+actor.facing*clip.paws[f][0]*u;
        float py=actor.footY()-clip.paws[f][1]*u,by=y-lift-12*u;
        if(observed && ms>=start && ms<end && (hitMask&(1<<stroke))==0 && dt>0){
            float rx=lastBallX-lastPawX,ry=lastBallY-lastPawY;
            float dx=x-px-rx,dy=by-py-ry, length=dx*dx+dy*dy;
            float t=length>0?Math.max(0,Math.min(1,-(rx*dx+ry*dy)/length)):0;
            float nx=rx+t*dx,ny=ry+t*dy,dist=(float)Math.hypot(nx,ny);
            if(dist<=16*u && dist>.01f && nx*actor.facing>0){
                float closing=-(dx*nx+dy*ny)/dist*(float)(1000/dt);
                if(closing>.75*u){
                    float impulse=Math.min(240*u,Math.max(46*u,closing*1.3f+40*u));
                    if(pounce&&stroke==0)impulse*=.18f;
                    vx+=impulse*nx/dist;vy=Math.max(0,-ny/dist)*impulse*.52f;
                    hitMask|=1<<stroke;hits++;
                }
            }
        }
        observed=true;lastPawX=px;lastPawY=py;lastBallX=x;lastBallY=by;
    }
    RectF bounds(float u){
        if(kind.equals("ball"))return new RectF(x-13*u,y-lift-25*u,x+13*u,y-lift+u);
        return new RectF(x-55*u,y-22*u,x+55*u,y+4*u);
    }
    void draw(Canvas canvas,float u){
        if(kind.isEmpty())return;
        int sc=canvas.save();canvas.translate(x,y-lift);canvas.scale(u,u);
        if(kind.equals("ball")){
            canvas.translate(0,-12);canvas.rotate(spin);ink.setColor(0xffec6f4c);canvas.drawCircle(0,0,12,ink);
            ink.setColor(0xffa14936);ink.setStyle(Paint.Style.STROKE);ink.setStrokeWidth(1.3f);
            canvas.drawArc(-9,-9,9,9,210,145,false,ink);ink.setStyle(Paint.Style.FILL);
            ink.setColor(0xcfffffff);canvas.drawCircle(-4,-6,2,ink);
        } else if(kind.equals("ledge")){
            ink.setColor(0xffbc9377);canvas.drawRoundRect(-49,0,49,4,2,2,ink);
            ink.setColor(0xffe6c49e);canvas.drawRoundRect(-51,-2,51,1,2,2,ink);
        } else if(kind.equals("cushion")){
            ink.setColor(0xff9c80be);canvas.drawRoundRect(-48,-13,48,2,10,9,ink);
            ink.setColor(0xffd6bbed);canvas.drawOval(-47,-18,47,-3,ink);
        } else {
            ink.setColor(0xff63885a);canvas.drawOval(-12,-6,9,0,ink);
            canvas.rotate(-30);ink.setColor(0xff93b57e);canvas.drawOval(-9,-11,3,-3,ink);
        }
        canvas.restoreToCount(sc);
    }
}
