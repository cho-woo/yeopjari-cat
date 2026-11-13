#include "../src/cat_update_notice.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) throw std::runtime_error("check failed at line " + std::to_string(__LINE__)); } while (false)

cat::update::NoticeContext Quiet() {
    cat::update::NoticeContext context;
    context.now = 60000; context.notBefore = 60000;
    context.enabled = context.visible = context.calm = true;
    context.busy = context.bubbleVisible = false;
    return context;
}
void NewerVersionOnly() {
    cat::update::Announcement notice;
    auto quiet = Quiet();
    CHECK(!notice.Ready(quiet));
    CHECK(!notice.SetAvailable({1, 7, 0}, "1.7.0"));
    CHECK(!notice.SetAvailable({1, 7, 0}, "1.6.9"));
    CHECK(!notice.SetAvailable({1, 7, 0}, "1.8.0\nextra"));
    CHECK(!notice.Ready(quiet));
    CHECK(notice.SetAvailable({1, 7, 0}, "1.10.0"));
    CHECK(notice.Ready(quiet));
    CHECK(!notice.SetAvailable({1, 10, 0}, "1.10.0"));
    CHECK(!notice.Ready(quiet));
}
void WaitWithoutDroppingTheNotice() {
    cat::update::Announcement notice;
    CHECK(notice.SetAvailable({1, 7, 0}, "1.8.0"));
    auto context = Quiet();
    context.now = 59999; CHECK(!notice.Ready(context));
    context = Quiet(); context.enabled = false; CHECK(!notice.Ready(context));
    context = Quiet(); context.visible = false; CHECK(!notice.Ready(context));
    context = Quiet(); context.calm = false; CHECK(!notice.Ready(context));
    context = Quiet(); context.busy = true; CHECK(!notice.Ready(context));
    context = Quiet(); context.bubbleVisible = true; CHECK(!notice.Ready(context));
    CHECK(notice.Ready(Quiet()));
    CHECK(notice.LastAnnounced().empty());
    notice.MarkAnnounced();
    CHECK(!notice.Ready(Quiet()));
    CHECK(notice.LastAnnounced() == "1.8.0");
}
void SurvivesRestartAndLaterReleases() {
    cat::update::Announcement first;
    first.SetAvailable({1, 7, 0}, "1.8.0"); first.MarkAnnounced();
    cat::update::Announcement restarted;
    CHECK(restarted.Restore(first.Serialize()));
    CHECK(restarted.SetAvailable({1, 7, 0}, "1.8.0"));
    CHECK(!restarted.Ready(Quiet()));
    CHECK(restarted.SetAvailable({1, 7, 0}, "1.8.1"));
    CHECK(restarted.Ready(Quiet()));
    restarted.MarkAnnounced();
    CHECK(restarted.LastAnnounced() == "1.8.1");
    CHECK(!restarted.Ready(Quiet()));
    CHECK(restarted.SetAvailable({1, 7, 0}, "2.0.0"));
    CHECK(restarted.Ready(Quiet()));
}
void CorruptionDoesNotChangeAcceptedHistory() {
    cat::update::Announcement notice;
    CHECK(notice.Restore("schema=1\nversion=1.8.0\n"));
    for (const std::string& invalid : {std::string{}, std::string(200, 'x'),
        std::string("schema=2\nversion=1.8.0\n"), std::string("schema=1\nversion=1.8.0\ntrailing"),
        std::string("schema=1\nversion=1.08.0\n"), std::string("schema=1\nversion=1.8.0"),
        std::string("schema=1\nversion=1.8.0\nversion=9.9.9\n")}) {
        CHECK(!notice.Restore(invalid));
        CHECK(notice.LastAnnounced() == "1.8.0");
    }
    CHECK(notice.SetAvailable({1, 7, 0}, "1.9.0"));
    CHECK(notice.Ready(Quiet()));
    CHECK(std::wstring(cat::update::kCatUpdateMessage) == L"업데이트 해줘");
}
void PersistentUntilInstalled() {
    cat::update::Announcement notice;
    CHECK(notice.SetAvailable({2,0,28},"2.0.29"));
    CHECK(notice.HasAvailable({2,0,28}));
    notice.MarkAnnounced();
    CHECK(!notice.Ready(Quiet()));
    CHECK(notice.HasAvailable({2,0,28}));
    cat::update::Announcement restarted;
    CHECK(restarted.Restore(notice.Serialize()));
    CHECK(restarted.HasAvailable({2,0,28}));
    CHECK(!restarted.HasAvailable({2,0,29}));
    CHECK(!restarted.SetAvailable({2,0,28},"2.0.28"));
    CHECK(!restarted.HasAvailable({2,0,28}));
    cat::update::Announcement cleared;
    CHECK(cleared.Restore(restarted.Serialize()));
    CHECK(!cleared.HasAvailable({2,0,28}));
    CHECK(cleared.Restore("schema=1\nversion=2.0.29\n"));
    CHECK(cleared.HasAvailable({2,0,28}));
}
}
int main() {
    try {
        NewerVersionOnly(); WaitWithoutDroppingTheNotice();
        SurvivesRestartAndLaterReleases(); CorruptionDoesNotChangeAcceptedHistory();
        PersistentUntilInstalled();
        std::cout << "Update notice: 4 groups; " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
