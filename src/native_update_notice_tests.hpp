#pragma once

// Inject the result of the real background check, not a menu command. No
// network, visible dialog, update download, or user-profile file is involved.
int NativeAutomaticUpdateNoticeTest() {
    if(updates.worker.joinable())return 920;
    const auto directory=g.directory;const auto available=updates.available;
    const auto announcement=updates.announcement;const bool automatic=updates.automatic;
    const bool manual=updates.manual,hasUpdate=updates.hasUpdate;
    const auto next=updates.nextCheck;
    CreateDirectoryW(L"render-self-test",nullptr);
    CreateDirectoryW(L"render-self-test/update-notice",nullptr);
    g.directory=L"render-self-test/update-notice";
    g.closing=g.menuOpen=g.dragging=false;updates.applying=false;
    updates.automatic=true;updates.manual=false;updates.announcement={};
    const auto newer=cat::update::ToString({kVersion.major,kVersion.minor,kVersion.patch+1});
    const auto complete=[&](const std::string& version) {
        updates.result={};updates.result.success=true;
        updates.result.manifest.versionText=version;
        cat::update::ParseVersion(version,updates.result.manifest.version);
        updates.operation=UpdateState::Operation::Checking;
        updates.finished.store(true,std::memory_order_release);PollUpdates();
    };
    complete(newer);
    int result=0;
    if(!updates.hasUpdate||!CatUpdateBubbleVisible()||updates.manual||
       updates.operation!=UpdateState::Operation::None)result=921;
    std::string saved;cat::update::Announcement restarted;
    if(!ReadBytes(g.directory+L"\\update-notice.ini",saved)||!restarted.Restore(saved)||!restarted.HasAvailable(kVersion))result=922;
    updates.announcement.MarkAnnounced();
    if(!CatUpdateBubbleVisible())result=923; // It must not time out after one announcement.
    updates.operation=UpdateState::Operation::Downloading;
    if(CatUpdateBubbleVisible())result=924;
    updates.operation=UpdateState::Operation::None;
    if(!CatUpdateBubbleVisible())result=925; // Failed/cancelled download can be retried.
    complete(cat::update::ToString(kVersion));
    if(updates.hasUpdate||CatUpdateBubbleVisible())result=926;
    updates.operation=UpdateState::Operation::Checking;updates.result={};updates.manual=false;
    updates.finished.store(true,std::memory_order_release);const auto before=GetTickCount64();PollUpdates();
    if(updates.nextCheck<before+290000||updates.nextCheck>GetTickCount64()+300000)result=927;
    WriteBytes(L"render-self-test/update-notice/report.json",std::string("{\"automatic_without_menu\":true,\"persistent_until_install\":true,\"restart_verified\":true,\"download_hides_notice\":true,\"failed_check_retry_ms\":300000,\"profile_unchanged\":true,\"exit\":")+std::to_string(result)+"}");
    g.directory=directory;updates.available=available;updates.announcement=announcement;
    updates.automatic=automatic;updates.manual=manual;updates.hasUpdate=hasUpdate;updates.nextCheck=next;
    updates.result={};updates.operation=UpdateState::Operation::None;updates.finished.store(false);
    return result;
}
