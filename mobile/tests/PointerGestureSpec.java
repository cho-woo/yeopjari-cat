package com.valuewander.yeopjaricat;

public final class PointerGestureSpec {
    static final class Events implements PointerGesture.Listener {
        int taps,menus,releases;float x,y;
        public void drag(float dx,float dy){x+=dx;y+=dy;}
        public void tap(){taps++;}public void menu(){menus++;}public void release(boolean dragged){releases++;}
    }
    public static void main(String[] args){
        int passed=0;
        for(int hold:new int[]{0,100,300,549,550,800,3000}){
            Events e=new Events();PointerGesture g=new PointerGesture(8,e);g.down(40,40,100);g.hold(100+hold);g.hold(100+hold);g.up(100+hold);g.hold(20000);
            if(e.taps!=(hold<550?1:0)||e.menus!=(hold>=550?1:0)||e.releases!=1)throw new AssertionError("Tap/long press mixed");passed++;
        }
        for(int dx=-100;dx<=100;dx+=10)for(int dy=-80;dy<=80;dy+=10){
            Events e=new Events();PointerGesture g=new PointerGesture(8,e);g.down(0,0,0);g.move(dx,dy);g.hold(2000);g.up(2100);
            boolean dragged=Math.hypot(dx,dy)>8;
            if(dragged&&(e.menus!=0||e.taps!=0||Math.abs(e.x-dx)>.01||Math.abs(e.y-dy)>.01))throw new AssertionError("Drag became menu or lost travel");passed++;
        }
        Events e=new Events();PointerGesture g=new PointerGesture(8,e);g.down(0,0,0);g.cancel();g.up(2000);g.hold(3000);if(e.taps!=0||e.menus!=0)throw new AssertionError("Cancel emitted action");passed++;
        for(int direction:new int[]{-1,1}){
            Events end=new Events();PointerGesture drag=new PointerGesture(8,end);drag.down(0,0,0);drag.move(30*direction,-40);drag.up(40*direction,-60,400);
            if(end.x!=40*direction||end.y!=-60||end.taps!=0||end.menus!=0)throw new AssertionError("Release endpoint was dropped");passed++;
        }
        System.out.println("Pointer gestures: "+passed+" tap, drag, long-press and cancellation cases passed");
    }
}
