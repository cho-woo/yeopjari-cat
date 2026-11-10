#pragma once
// Win32 host glue included after the primary renderer and storage helpers.
// The model owns the second animal; this layer only connects windows and menus.
constexpr UINT kCompanionToggle = 2400, kCompanionFeed = 2401, kCompanionTreat = 2402;
constexpr UINT kCompanionStatus = 2403, kCompanionPause = 2404;
constexpr UINT kCompanionFish = 2405;
constexpr UINT kCompanionCharacterFirst = 2600;
// Retired wardrobe commands 2620..2632 stay unused; persisted preferences remain readable.

cat::companion::Platform CompanionPlatform(HWND hwnd, RECT* actual = nullptr) {
    RECT rect{};
    if (!QueryPerchRect(hwnd, rect)) return {};
    if (actual) *actual = rect;
    return {reinterpret_cast<std::uintptr_t>(hwnd),
        (std::max(rect.left, g.work.left) - g.work.left) / g.dpiScale,
        (std::min(rect.right, g.work.right) - g.work.left) / g.dpiScale,
        (g.work.bottom - rect.top) / g.dpiScale, true};
}
cat::companion::Context CompanionContext(ULONGLONG ticks, double elapsed) {
    cat::companion::Context c;
    c.nowUtc = UtcNow(); c.ticks = ticks; c.elapsedSeconds = elapsed;
    SYSTEMTIME local{}; GetLocalTime(&local); c.localHour = local.wHour;
    c.paused = g.hidden || g.menuOpen || g.dragging || g.companionWindow.Interacting() || g.closing || updates.applying;
    c.primaryVisible = !g.hidden;
    c.primary = g.physics.CurrentPose(); c.primaryMotion = g.motion;
    c.primaryBusy = g.dragging || g.perch.transitioning || g.waitingForStop ||
        g.toyEngaged || g.approachingToy || g.approachingUser || g.memorySequenceCount || g.lifeSequenceCount ||
        g.lifeCrossing.Active() || g.physics.Hanging() || EnvironmentBusy() || !c.primary.grounded ||
        // Joining an existing sleep/run story does not replace the primary's
        // action. Care and unrelated gestures still require a quiet boundary.
        (!g.companionReaction && g.motion!=Motion::Sleep && g.motion!=Motion::Zoomies &&
            !PrimarySocialBoundary(ticks));
    if (g.perch.active) c.primaryPlatform = CompanionPlatform(g.perch.hwnd);
    const auto support = reinterpret_cast<HWND>(g.companionModel.SupportId());
    if (support) c.supportPlatform = CompanionPlatform(support);
    return c;
}
int CompanionSpriteBottomInset(const cat::companion::Snapshot& s,double dpi) {
    const auto* sprite=CareSpriteFor(s.animation,s.preferences.character);
    if(!sprite || (sprite->layout->clip!=cat::sprite_sequence::Clip::Fish &&
            sprite->layout->clip!=cat::sprite_sequence::Clip::Jump &&
            sprite->layout->clip!=cat::sprite_sequence::Clip::Eat))return 0;
    return cat::render::SpriteBottomSafetyInset(*sprite->layout,110*dpi*s.growthScale,s.body.bodyHeight);
}
void PaintCompanion(HDC, unsigned char* pixels, int width, int height,
                    const cat::companion::Snapshot& s, double dpi) {
    Bitmap bitmap(width, height, width * 4, PixelFormat32bppPARGB, pixels);
    Graphics graphics(&bitmap);
    graphics.SetCompositingMode(CompositingModeSourceCopy); graphics.Clear(Color(0,0,0,0));
    graphics.SetCompositingMode(CompositingModeSourceOver);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    const int bottomInset=std::clamp(CompanionSpriteBottomInset(s,dpi),0,height-1);
    DrawPetStyle(graphics, s.preferences.character, s.preferences.outfit, s.animation, s.body,
        static_cast<REAL>(width / 2.0 + s.animation.x * dpi * s.growthScale),
        static_cast<REAL>(height - 1 - bottomInset - s.animation.lift * dpi * s.growthScale),
        static_cast<REAL>(110 * dpi * s.growthScale),
        s.presentationTicks?&CompanionCarePresentation():nullptr,
        s.presentationTicks,s.presentationGeneration,s.presentationPaused);
    graphics.Flush(FlushIntentionSync);
}
void ShowCompanionStatus() {
    if (!g.companionStore.available()) return;
    const auto& s = g.companionStore.state;
    const auto age = cat::Age(s.pet.bornUtc, UtcNow());
    const auto bond = cat::memory::Summarize(s.memory).bond;
    std::wostringstream text;
    text << cat::CharacterName(s.preferences.character) << L" · 두 번째 동물\n함께 " << age.days + 1
        << L"일\n성격: " << cat::life::TemperamentName(s.life.temperament)
        << L"\n몸 상태: " << cat::nutrition::BodyDescription(s.nutrition)
        << L"\n친밀도: " << (bond >= 75 ? L"오랜 단짝" : bond >= 50 ? L"마음을 연 사이" : bond >= 25 ? L"익숙한 친구" : L"천천히 알아가는 중")
        << L"\n함께 먹은 밥: " << s.pet.feedCount << L"번 · 간식: " << s.pet.snackCount
        << L"번\n\n첫 번째 동물과 생일·성장·성격·친밀도 기록이 따로 이어집니다.";
    UpdateMessage(text.str().c_str(), L"두 번째 동물의 기록", MB_OK | MB_ICONINFORMATION);
}
void FeedCompanionFood(cat::feeding::FoodKind kind) {
    if (!g.companionStore.enabled() || !g.companionModel.Bound()) return;
    const auto result = g.companionModel.FeedFood(UtcNow(), GetTickCount64(), kind, Random(0,1));
    if (result == cat::nutrition::FeedResult::Accepted) {
        SaveCompanion();
    } else Balloon(L"두 번째 동물", L"배부르거나 소화 중이면 조금 기다려 주세요. 공중에서는 착지한 뒤 먹어요.");
}
void FeedCompanion(bool treat) {
    FeedCompanionFood(treat ? cat::feeding::FoodKind::Treat : cat::feeding::FoodKind::Meal);
}
void ToggleCompanion();
void ShowCompanionMenu(POINT point) {
    if (g.menuOpen || g.closing || updates.applying || !g.companionStore.available()) return;
    HMENU menu = CreatePopupMenu();
    const auto& prefs = g.companionStore.state.preferences;
    AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"두 번째 동물");
    AppendMenuW(menu, MF_STRING, kCompanionFeed, L"이 동물에게 밥 주기");
    AppendMenuW(menu, MF_STRING, kCompanionTreat, L"이 동물에게 간식 주기");
    AppendMenuW(menu, MF_STRING | (cat::feeding::CanOfferFish(prefs.character) ? MF_ENABLED : MF_GRAYED),
        kCompanionFish, L"이 고양이에게 생선 주기");
    HMENU characters = CreatePopupMenu();
    for (const auto character:cat::kAvailableCharacters) {
        AppendMenuW(characters, MF_STRING | (character==prefs.character ? MF_CHECKED : 0),
            kCompanionCharacterFirst+static_cast<int>(character), cat::CharacterName(character));
    }
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(characters), L"캐릭터 선택");
    AppendMenuW(menu, MF_STRING | (g.companionStore.state.pet.roamingPaused ? MF_CHECKED : 0), kCompanionPause, L"이 자리에서 쉬기");
    AppendMenuW(menu, MF_STRING, kCompanionStatus, L"이 동물의 성장 기록");
    AppendMenuW(menu, MF_STRING, UpdateCheckCommand, L"프로그램 업데이트 확인…");
    AppendMenuW(menu, MF_STRING, kCompanionToggle, L"두 번째 동물 잠시 쉬게 하기");
    g.menuOpen = true;
    SetForegroundWindow(g.companionWindow.Handle());
    const UINT command=TrackPetContextMenu(menu,g.companionWindow.Handle(),point);
    PostMessageW(g.companionWindow.Handle(), WM_NULL, 0, 0);
    DestroyMenu(menu); g.menuOpen = false;
    if (command == kCompanionFeed) FeedCompanion(false);
    else if (command == kCompanionTreat) FeedCompanion(true);
    else if (command == kCompanionFish) FeedCompanionFood(cat::feeding::FoodKind::Fish);
    else if (command == kCompanionStatus) ShowCompanionStatus();
    else if (command == kCompanionToggle) ToggleCompanion();
    else if (command == UpdateCheckCommand) StartUpdateCheck(true);
    else if (command == kCompanionPause) {
        auto& paused = g.companionStore.state.pet.roamingPaused;
        const bool old = paused; paused = !paused;
        if (!SaveCompanion()) paused = old;
    } else if (command >= kCompanionCharacterFirst && command < kCompanionCharacterFirst+cat::kCharacterCount) {
        if(!cat::CharacterAvailable(static_cast<cat::Character>(command-kCompanionCharacterFirst)))return;
        auto& p = g.companionStore.state.preferences; const auto old = p;
        const auto character=static_cast<cat::Character>(command-kCompanionCharacterFirst);
        if(!EnsureCareCoats(g.preferences.character,character))return;
        p.character=character;
        if (!SaveCompanion()) {p=old;EnsureCareCoats(g.preferences.character,old.character);}
    }
}
bool EnsureCompanionWindow() {
    if (!g.companionStore.available()) return false;
    if (!g.companionWindow.Handle() && !g.companionWindow.Create(g.instance, g.window, PaintCompanion,
        [](const cat::companion::Input& input) {
            if (g.menuOpen || g.closing || updates.applying) return;
            using I = cat::companion::InputKind;
            if (input.kind == I::Menu) ShowCompanionMenu({input.screenX,input.screenY});
            else if (input.kind == I::Pet) {
                if (g.companionModel.Pet(UtcNow(),GetTickCount64())) SaveCompanion();
            } else {
                g.companionModel.DragTo((input.screenX-g.work.left)/g.dpiScale);
                if (input.kind == I::DragEnd) SaveCompanion();
            }
        })) return false;
    if (g.companionDpi != g.dpiScale || !EqualRect(&g.companionWork, &g.work)) {
        const double width = (g.work.right-g.work.left)/g.dpiScale;
        const double margin = std::min(96.0, width*.4);
        const double ceiling = std::max(1.0,(g.work.bottom-g.work.top)/g.dpiScale-160);
        if (g.companionDpi == 0) g.companionModel.Reset(margin,std::max(margin+1,width-margin),ceiling);
        else g.companionModel.Relocate(margin,std::max(margin+1,width-margin),ceiling);
        g.companionWork = g.work; g.companionDpi = g.dpiScale;
    }
    return true;
}
void ToggleCompanion() {
    if (g.companionStore.outcome == cat::companion::Outcome::Blocked) {
        UpdateMessage(L"두 번째 동물의 저장 기록을 읽지 못했어요. companion.ini와 companion.bak를 보존하고 새 동물로 덮어쓰지 않습니다.", kTitle, MB_OK | MB_ICONWARNING); return;
    }
    const bool adopting = g.companionStore.outcome == cat::companion::Outcome::Missing;
    const bool previous = g.companionStore.available() && g.companionStore.state.enabled;
    const auto previousCoat=previous?g.companionStore.state.preferences.character:cat::Character::Cream;
    const auto nextCoat=(adopting||!previous)?
        (adopting?cat::companion::State{}.preferences.character:g.companionStore.state.preferences.character):cat::Character::Cream;
    if(!EnsureCareCoats(g.preferences.character,nextCoat))return;
    if (adopting) {
        if (!g.companionStore.Adopt(UtcNow())) {EnsureCareCoats(g.preferences.character,previousCoat);return;}
    } else g.companionStore.state.enabled = !previous;
    if (!SaveCompanion()) {
        if (adopting) LoadCompanion(); else g.companionStore.state.enabled = previous;
        EnsureCareCoats(g.preferences.character,previousCoat);
        return;
    }
    if (adopting) { g.companionModel.Bind(g.companionStore.state); g.companionDpi = 0; }
    if (g.companionStore.enabled()) {
        if (!EnsureCompanionWindow()) Balloon(L"두 번째 동물", L"표시 창을 만들지 못했어요. 기록은 저장되어 있어요.", true);
    } else if (g.companionWindow.Handle()) ShowWindow(g.companionWindow.Handle(),SW_HIDE);
}
void TickCompanion(ULONGLONG ticks, double elapsed) {
    if (!g.companionStore.available() || !EnsureCompanionWindow()) return;
    auto context = CompanionContext(ticks,elapsed);
    if (g.companionStore.enabled()) {
        if (g.interactionSettings.seasonalEvents) {
            SYSTEMTIME local{}; GetLocalTime(&local);
            cat::life::EventContext event;
            event.nowUtc = UtcNow();
            event.localDay = cat::ToUnix({local.wYear,local.wMonth,local.wDay})/86400;
            event.seasonalEnabled = true;
            cat::life::UnlockSeasonal(g.companionStore.state.life,event);
        }
        g.companionModel.Tick(context);
        const auto event = g.companionModel.TakeEvent();
        using E = cat::companion::Interaction;
        if (!context.paused && !context.primaryBusy &&
            (g.motion==Motion::Idle || g.motion==Motion::Watch || g.motion==Motion::Walk)) {
            if (event==E::Sniff || event==E::Paw)
                g.direction=g.companionModel.Physical().x<context.primary.x ? -1 : 1;
            bool reacted=false;
            if (event==E::Sniff) {SetMotion(Motion::Sniff,2200);reacted=true;}
            else if (event==E::Paw) {SetMotion(Motion::ToyPaw,1500);reacted=true;}
            else if (event==E::SharedSleep) {SetMotion(Motion::Sleep,30000);reacted=true;}
            else if (event==E::RunTogether && !g.pet.roamingPaused) {
                g.direction = g.companionModel.Physical().velocityX < 0 ? -1 : 1;
                g.speed = 105*g.dpiScale; SetMotion(Motion::Zoomies,5500);reacted=true;
            }
            if(reacted)g.companionReaction=true;
        }
    }
    RECT support{};
    const auto hwnd = reinterpret_cast<HWND>(g.companionModel.SupportId());
    const bool supported = hwnd && QueryPerchRect(hwnd,support);
    g.companionWindow.SetSupportRect(supported ? &support : nullptr);
    const auto snapshot=g.companionModel.View(ticks);
    g.companionWindow.Present(snapshot,g.work.left,g.work.bottom,g.dpiScale,g.hidden,
        CompanionSpriteBottomInset(snapshot,g.dpiScale));
}
void NotifyCompanionFood() {
    if (g.companionStore.enabled() && g.companionModel.Bound())
        g.companionModel.FoodOffered(CompanionContext(GetTickCount64(),0));
}
