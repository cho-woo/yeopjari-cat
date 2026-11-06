#include "../src/cat_gait.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
int checks=0;
#define CHECK(condition) do { ++checks; if(!(condition)) throw std::runtime_error( \
    std::string(__FILE__)+":"+std::to_string(__LINE__)+" failed: " #condition); } while(false)
using namespace cat::gait;
constexpr Parameters gallop{42,.32,6.5,Sequence::Gallop};
bool Near(double a,double b,double tolerance=1e-9) {return std::abs(a-b)<=tolerance;}
Feet StaticFeet(double phase,const Parameters& parameters=gallop) {
    Feet feet;
    for(int leg=0;leg<4;++leg)feet[leg]=FootAt(phase+LegPhase(leg,parameters.duty,parameters.sequence),
        parameters.stride,parameters.duty,parameters.lift);
    return feet;
}

void ForeThenHindContactsAndTwoSuspensions() {
    // The forefeet land at 0 and .10; hindfeet at .50 and .60. These
    // independently specified touchdown instants distinguish gallop from
    // the existing diagonal-pair trot without depending on enum offsets.
    const std::array<double,4> touchdown{{.50,.60,0,.10}};
    for(int leg=0;leg<4;++leg) {
        CHECK(!StaticFeet(touchdown[leg]-1e-5)[leg].stance);
        CHECK(StaticFeet(touchdown[leg]+1e-5)[leg].stance);
        CHECK(StaticFeet(touchdown[leg]+gallop.duty-1e-5)[leg].stance);
        CHECK(!StaticFeet(touchdown[leg]+gallop.duty+1e-5)[leg].stance);
    }
    std::array<int,5> support{};
    for(int sample=0;sample<10000;++sample) {
        const double phase=(sample+.5)/10000;
        const auto feet=StaticFeet(phase);int grounded=0;
        for(const auto& foot:feet)grounded+=foot.stance;
        ++support[grounded];
        if(phase>.42 && phase<.50)CHECK(grounded==0);
        if(phase>.92)CHECK(grounded==0);
        if(phase>.10 && phase<.32)CHECK(feet[2].stance && feet[3].stance);
        if(phase>.60 && phase<.82)CHECK(feet[0].stance && feet[1].stance);
        // A forepaw and hindpaw never form the synchronized diagonal
        // touchdown pair used by the previous high-speed trot.
        CHECK(!(feet[0].stance && feet[2].stance));
        CHECK(!(feet[1].stance && feet[3].stance));
    }
    CHECK(support[0]==1600); // Two 8%-of-cycle flight intervals.
    CHECK(support[1]==4000);
    CHECK(support[2]==4400);
    CHECK(support[3]==0 && support[4]==0);
}

void PersistentContactsKeepWorldPosition() {
    for(int direction:{-1,1})for(double fps:{30.,60.,120.,240.}) {
        Cycle cycle(gallop,false,0);
        auto previous=cycle.Targets();double root=0;
        std::array<double,4> previousWorld{};
        for(int leg=0;leg<4;++leg)previousWorld[leg]=direction*previous[leg].x;
        int planted=0,flight=0,contacts=0;
        for(int sample=0;sample<static_cast<int>(fps*3);++sample) {
            const double distance=165/fps;root+=direction*distance;
            cycle.Advance(distance,1/fps,true,false);
            const auto settings=cycle.Settings();const auto feet=cycle.Targets();
            CHECK(settings.sequence==Sequence::Gallop);
            CHECK(settings.stride==42 && settings.duty==.32 && settings.lift==6.5);
            int grounded=0;
            for(int leg=0;leg<4;++leg) {
                const auto& foot=feet[leg];const double world=root+direction*foot.x;
                CHECK(std::isfinite(foot.x) && std::isfinite(foot.y));
                CHECK(foot.y<=0 && foot.y>=-gallop.lift-1e-10);
                CHECK(std::abs(foot.x)<10); // No clipped long-stance reach.
                if(foot.stance && previous[leg].stance && foot.contact==previous[leg].contact) {
                    CHECK(Near(world,previousWorld[leg],2e-10));++planted;
                }
                contacts+=foot.contact>previous[leg].contact;
                grounded+=foot.stance;
                previousWorld[leg]=world;
            }
            flight+=grounded==0;
            previous=feet;
        }
        CHECK(planted>20 && flight>0 && contacts>12);
    }
    // At sufficiently fine resolution each touchdown is observable in
    // order, including repeated cycles, rather than just pairwise phases.
    Cycle cycle(gallop,false,0);auto previous=cycle.Targets();std::vector<int> order;
    for(int sample=0;sample<2000;++sample) {
        cycle.Advance(165./2000,1./2000,true,false);const auto feet=cycle.Targets();
        for(int leg=0;leg<4;++leg)if(feet[leg].contact>previous[leg].contact)order.push_back(leg);
        previous=feet;
    }
    CHECK(order.size()>12);
    const std::array<int,4> expected{{3,0,1,2}};
    for(std::size_t index=0;index<order.size();++index)CHECK(order[index]==expected[index%4]);
}

void FixedGaitIsIndependentOfTickRateAndPauses() {
    std::array<Feet,3> finalFeet{};int at=0;
    for(int fps:{30,60,120}) {
        Cycle cycle(gallop,false,0);
        for(int sample=0;sample<fps*2;++sample)cycle.Advance(165./fps,1./fps,true,false);
        finalFeet[at++]=cycle.Targets();
        const auto held=cycle.Targets();const double phase=cycle.Phase();
        for(int sample=0;sample<100;++sample)cycle.Advance(0,.01,true,false);
        CHECK(SameFeet(held,cycle.Targets()) && cycle.Phase()==phase);
        for(int sample=0;sample<100;++sample)cycle.Advance(0,.01,false,false);
        CHECK(SameFeet(held,cycle.Targets()) && cycle.Phase()==phase);
        cycle.Reset();
        CHECK(cycle.Settings().sequence==Sequence::Gallop);
        CHECK(cycle.Settings().stride==gallop.stride && cycle.Phase()==0);
        CHECK(SameFeet(cycle.Targets(),StaticFeet(0)));
    }
    for(int i=1;i<3;++i)for(int leg=0;leg<4;++leg) {
        CHECK(Near(finalFeet[i][leg].x,finalFeet[0][leg].x));
        CHECK(Near(finalFeet[i][leg].y,finalFeet[0][leg].y));
        CHECK(finalFeet[i][leg].stance==finalFeet[0][leg].stance);
        CHECK(finalFeet[i][leg].contact==finalFeet[0][leg].contact);
    }
}

void FootCurvesRemainContinuousAtLiftAndPlant() {
    constexpr double epsilon=1e-6;
    for(double duty:{.25,.32,.42,.54,.68,.72,.85}) {
        for(double boundary:{0.,duty,1.}) {
            const auto a=FootAt(boundary-epsilon,42,duty,6.5);
            const auto b=FootAt(boundary,42,duty,6.5);
            const auto c=FootAt(boundary+epsilon,42,duty,6.5);
            CHECK(std::abs(a.x-b.x)<.001 && std::abs(c.x-b.x)<.001);
            CHECK(std::abs(a.y-b.y)<.001 && std::abs(c.y-b.y)<.001);
            CHECK(std::abs((b.x-a.x)/epsilon-(c.x-b.x)/epsilon)<.02);
            CHECK(std::abs((b.y-a.y)/epsilon-(c.y-b.y)/epsilon)<.02);
            CHECK(Near((c.x-a.x)/(2*epsilon),-42,.02));
        }
    }
}

void ConfiguredTransitionsKeepContactsAndEaseSwingVelocity() {
    constexpr Parameters walk{18,.72,3.1};
    constexpr Parameters productionRun{42,.32,8,Sequence::Gallop};
    const std::array<double,4> runBias{{0,0,4,2.5}};
    Cycle cycle(walk,false,0);
    CHECK(cycle.Configure(walk));
    auto previous=cycle.Targets();std::array<double,4> previousWorld{};
    for(int leg=0;leg<4;++leg)previousWorld[leg]=previous[leg].x;
    double root=0;int anchored=0,transitions=0;
    for(int sample=0;sample<2400;++sample) {
        const bool running=sample>=600 && sample<1500;
        if(sample==600 || sample==1500) {
            const auto held=cycle.Targets();const double heldPhase=cycle.Phase();
            CHECK(cycle.Configure(running?productionRun:walk,running?runBias:std::array<double,4>{}));
            CHECK(SameFeet(held,cycle.Targets()));
            CHECK(heldPhase==cycle.Phase());++transitions;
        }
        const double distance=(running?165.:22.)/600;root+=distance;
        cycle.Advance(distance,1./600,true,false);const auto feet=cycle.Targets();
        for(int leg=0;leg<4;++leg) {
            const double world=root+feet[leg].x;
            if(feet[leg].stance && previous[leg].stance && feet[leg].contact==previous[leg].contact) {
                CHECK(Near(world,previousWorld[leg],2e-10));++anchored;
            }
            // No display-coordinate scaling or direct paw relocation is
            // permitted during the speed/sequence/neutral-position switch.
            CHECK(std::isfinite(feet[leg].x) && std::isfinite(feet[leg].y));
            CHECK(feet[leg].y<=0 && feet[leg].y>=-8-1e-9);
            // Short-legged production art cannot recover ten pixels behind
            // its hip while still almost touching the floor. Retiming must
            // shorten the backwards matching-tangent interval, not relocate
            // an already planted foot or stretch the bone in the renderer.
            if(leg<2)CHECK(feet[leg].x>=-7.5 && feet[leg].x<=7.5);
            else CHECK(feet[leg].x>=-7.5 && feet[leg].x<=11.5);
            CHECK(std::abs(feet[leg].x-previous[leg].x)<1.5);
            previousWorld[leg]=world;
        }
        previous=feet;
    }
    CHECK(transitions==2 && anchored>3000);
    // Sampling the infinitesimal interval following a switch demonstrates
    // a continuous swing velocity rather than an immediate 35% retime jump.
    Cycle moving(walk,false,0);CHECK(moving.Configure(walk));
    for(int sample=0;sample<31;++sample)moving.Advance(22./240,1./240,true,false);
    constexpr double epsilon=1e-7;
    auto unchanged=moving,switched=moving;
    CHECK(switched.Configure(productionRun,runBias));
    const auto start=moving.Targets();
    unchanged.Advance(22*epsilon,epsilon,true,false);
    switched.Advance(22*epsilon,epsilon,true,false);
    const auto a=unchanged.Targets(),b=switched.Targets();int recovered=0;
    for(int leg=0;leg<4;++leg)if(!start[leg].stance&&!a[leg].stance&&!b[leg].stance) {
        CHECK(std::abs((a[leg].x-start[leg].x)/epsilon-(b[leg].x-start[leg].x)/epsilon)<.001);
        CHECK(std::abs((a[leg].y-start[leg].y)/epsilon-(b[leg].y-start[leg].y)/epsilon)<.001);
        ++recovered;
    }
    CHECK(recovered>0);
    CHECK(switched.Configure(productionRun,runBias));switched.Reset();
    CHECK(switched.Settings().sequence==Sequence::Gallop);
    const auto reset=switched.Targets(),reference=StaticFeet(0,productionRun);
    for(int leg=0;leg<4;++leg)CHECK(Near(reset[leg].x,reference[leg].x+runBias[leg]));
    const auto before=switched.Targets();const auto settings=switched.Settings();
    CHECK(!switched.Configure({42,.1,6.5,Sequence::Gallop},runBias));
    CHECK(!switched.Configure(gallop,{{0,0,std::numeric_limits<double>::quiet_NaN(),0}}));
    CHECK(SameFeet(before,switched.Targets()) && switched.Settings().sequence==settings.sequence);

    // Configuring a primed controller must also preserve a recovery that
    // began before Configure. Its compact tangent is selected at liftoff,
    // never by changing a global flag halfway through an existing curve.
    Cycle primed(walk,false,0);primed.Prime();
    auto oldCurve=primed,configured=primed;
    CHECK(configured.Configure(walk));
    const auto initial=primed.Targets();
    oldCurve.Advance(22*epsilon,epsilon,true,false);
    configured.Advance(22*epsilon,epsilon,true,false);
    const auto oldFeet=oldCurve.Targets(),newFeet=configured.Targets();
    int initialRecoveries=0;
    for(int leg=0;leg<4;++leg)if(!initial[leg].stance){
        CHECK(Near(oldFeet[leg].x,newFeet[leg].x,1e-8));
        CHECK(Near(oldFeet[leg].y,newFeet[leg].y,1e-8));++initialRecoveries;
    }
    CHECK(initialRecoveries>0);
}

void ExistingWalkAndAdaptiveTrotKeepTheirSequence() {
    CHECK(Parameters{}.sequence==Sequence::WalkTrot);
    for(double speed:{0.,12.5,22.,45.,90.,165.,230.})for(bool stalk:{false,true})
        CHECK(ForSpeed(stalk,speed).sequence==Sequence::WalkTrot);
    for(double duty:{.54,.62,.68,.72,.78})for(int leg=0;leg<4;++leg)
        CHECK(LegPhase(leg,duty)==LegPhase(leg,duty,Sequence::WalkTrot));
    // Existing 72%-duty four-beat walk and 54%-duty diagonal trot retain
    // their recorded support distributions rather than inheriting flight.
    for(const auto p:{Parameters{18,.72,2.6},Parameters{52,.54,4.8}}) {
        std::array<int,5> counts{};
        for(int sample=0;sample<10000;++sample) {
            int planted=0;for(const auto foot:StaticFeet((sample+.5)/10000,p))planted+=foot.stance;
            ++counts[planted];
        }
        CHECK(counts[0]==0);
        if(p.duty==.72)CHECK(counts[2]==1200 && counts[3]==8800);
        else CHECK(counts[2]==9200 && counts[4]==800);
    }
}
}
int main() {
    try {
        ForeThenHindContactsAndTwoSuspensions();
        PersistentContactsKeepWorldPosition();
        FixedGaitIsIndependentOfTickRateAndPauses();
        FootCurvesRemainContinuousAtLiftAndPlant();
        ConfiguredTransitionsKeepContactsAndEaseSwingVelocity();
        ExistingWalkAndAdaptiveTrotKeepTheirSequence();
        std::cout<<"Gallop sequence: "<<checks<<" checks passed\n";
        return EXIT_SUCCESS;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return EXIT_FAILURE;}
}
