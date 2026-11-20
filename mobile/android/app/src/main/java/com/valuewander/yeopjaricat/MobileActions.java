package com.valuewander.yeopjaricat;

import java.util.LinkedHashMap;

final class MobileActions {
    static final LinkedHashMap<String,String> labels=new LinkedHashMap<>();
    static {
        add("walk","걷기"); add("stalk","살금살금 다가가기"); add("run","달리기"); add("turn","달리다가 턴");
        add("brake","브레이크 밟기"); add("slide","미끄러져 멈추기"); add("stumble","데굴 넘어지기");
        add("jump","점프하고 착지"); add("cursor-pounce","뛰어 덮치기"); add("climb","오른쪽 가장자리 오르기"); add("perch","선반 위로 뛰어올랐다 내려오기");
        add("ball","공 앞발로 치기"); add("ball-pounce","공 쫓아 덮치기"); add("toy","낚싯대 장난감");
        add("catnip","캣닢에 뒹굴기"); add("cushion","방석 위에서 꾹꾹이");
        add("fly","파리 사냥"); add("tower","버섯 캣타워 오르내리기"); add("scratch","선인장 스크래처 긁기");
        add("box","고양이 귀 박스에 쏙"); add("fish","생선 먹기"); add("eat","밥 먹기"); add("treat","간식 먹기");
        add("groom","그루밍"); add("scratch-fur","털 긁기"); add("knead","꾹꾹이"); add("tail-chase","꼬리 쫓기");
        add("roll","배 보이고 뒹굴기"); add("reach","앞발 뻗기"); add("punch","냥냥펀치"); add("settle","기지개 켜고 자리 잡기");
        add("watch","가만히 살펴보기"); add("ear-flick","귀 쫑긋"); add("tail-raise","꼬리 세우기"); add("loaf","식빵 굽기");
        add("peek","빼꼼 내다보기"); add("sneeze","재채기"); add("yawn","하품"); add("sleep","잠들었다 깨기");
        add("sleep-talk","잠꼬대"); add("hang","가장자리에 매달리기"); add("pride","의젓한 자세");
        add("sniff","킁킁 냄새 맡기"); add("look-down","아래 내려다보기"); add("balance","균형 잡기");
        add("startled","깜짝 놀라기"); add("idle-breathe","앉아서 숨 고르기");
    }
    private static void add(String action,String label){labels.put(action,label);}
    static final String[] all=labels.keySet().toArray(new String[0]);
    static final String[] social={"paw","sniff","follow","race","nap","snack","perch","surprise"};
    static final String[] socialLabels={"서로 앞발로 장난치기","코 맞대기","졸졸 따라가기","나란히 달리기","함께 낮잠","간식 먼저 먹기","가장자리 따라가기","친구 재채기에 깜짝"};
    private MobileActions(){}
}
