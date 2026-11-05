#pragma once

// Included by main.cpp only in a separately named experimental build. These
// helpers deliberately reuse its Box2D state and transparent-window compositor.

struct ThreeDCommand {
    bool preview = false, selfTest = false, smoke = false, warp = false, valid = true;
    std::wstring modelPath;
};

ThreeDCommand ParseThreeDCommand() {
    ThreeDCommand command;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { command.valid = false; return command; }
    for (int i = 1; i < argc; ++i) {
        if (wcscmp(argv[i], L"--3d-preview") == 0) command.preview = true;
        else if (wcscmp(argv[i], L"--3d-preview-smoke") == 0) command.preview = command.smoke = true;
        else if (wcscmp(argv[i], L"--3d-render-self-test") == 0) command.selfTest = true;
        else if (wcscmp(argv[i], L"--3d-warp") == 0) command.warp = true;
        else if (wcscmp(argv[i], L"--3d-model") == 0) {
            if (i + 1 == argc || wcsncmp(argv[i + 1], L"--", 2) == 0) command.valid = false;
            else command.modelPath = argv[++i];
        }
    }
    LocalFree(argv);
    if (command.modelPath.empty())
        command.modelPath = (std::filesystem::path(cat::update::CurrentExePath()).parent_path() / L"cat3d.bin").wstring();
    return command;
}

bool InitializeThreeDPreview(const std::wstring& path, bool forceWarp) {
    preview3d.modelPath = path;
    if (!preview3d.model.LoadFile(std::filesystem::path(path), preview3d.error)) return false;
    for (const char* clip : {"idle", "walk", "run", "stalk", "belly", "airborne", "landing", "ear", "paw"}) {
        if (preview3d.model.FindClip(clip) < 0) {
            preview3d.error = std::string("Missing required preview animation: ") + clip;
            return false;
        }
    }
    const auto bounds = preview3d.model.Bounds();
    preview3d.modelHeight = bounds.maximum[2] - bounds.minimum[2];
    if (!(preview3d.modelHeight > .01f)) { preview3d.error = "The model has no usable height"; return false; }
    if (!preview3d.renderer.Initialize(forceWarp)) {
        preview3d.error = preview3d.renderer.LastError(); return false;
    }
    preview3d.initialized = true;
    return true;
}

void GroundThreeDVertices(std::vector<cat::three_d::RenderVertex>& vertices) {
    // Clip sampling blends local bone transforms. Re-establish the supporting
    // plane after interpolation, rather than allowing that blend to hover.
    // This affects rendered geometry only; Box2D remains the owner of lift.
    if (vertices.empty()) return;
    const float low = cat::three_d::BoundsOf(vertices).minimum[2];
    for (auto& vertex : vertices) vertex.position[2] -= low;
}

bool DrawThreeDPreview(Graphics& graphics, const cat::animation::Pose& pose,
                      REAL center, REAL bottom, REAL scale) {
    if (!preview3d.enabled || !preview3d.initialized || preview3d.failed ||
            g.preferences.character != cat::Character::Cream) return false;
    const auto ticks = GetTickCount64();
    const auto physical = g.physics.RenderPose();
    if (physical.hanging) return false;
    const double elapsed = ticks >= g.motionStart ? (ticks - g.motionStart) / 1000.0 : 0;
    const float pixelsPerUnit = std::max(1.0f, CurrentArt().base.standingHeight * scale / preview3d.modelHeight);
    const double dt = preview3d.lastTick && ticks >= preview3d.lastTick
        ? std::min(.25, (ticks - preview3d.lastTick) / 1000.0) : 0;
    const bool observedGait = physical.grounded && pose.walkingRig;
    const float previousYaw = preview3d.yaw;
    const float targetYaw = (pose.flip ? 1.0f : -1.0f) * (observedGait ? 1.05f : .55f);
    if (!dt) preview3d.yaw = targetYaw;
    else preview3d.yaw += static_cast<float>(1 - std::exp(-dt / .12)) * (targetYaw - preview3d.yaw);
    const double travel = physical.relativeTravelX - preview3d.lastTravel;
    // The model walks along -Y. At a three-quarter camera angle only sin(yaw)
    // of that stride projects horizontally. Account for that per sample, not
    // by rescaling the accumulated phase when the cat changes direction.
    const double projectedStride = std::max(.3, .5 * (std::abs(std::sin(previousYaw)) + std::abs(std::sin(preview3d.yaw))));
    if (observedGait && preview3d.lastTick && dt > 0 && std::abs(travel) <= std::max(12.0, 500.0 * dt))
        preview3d.distance += std::abs(travel) / (pixelsPerUnit / std::max(.1, g.dpiScale) * projectedStride);
    preview3d.lastTravel = physical.relativeTravelX;
    preview3d.lastTick = ticks;

    const char* name = nullptr;
    double time = elapsed;
    if (!physical.grounded) {
        name = "airborne";
        const int clip = preview3d.model.FindClip(name);
        time = std::clamp(.5 - physical.velocityY / 650.0, .05, .95) * preview3d.model.Duration(clip);
    } else if (pose.walkingRig) {
        const Motion moving = cat::animation::Walking(g.motion) ? g.motion : g.walkingMotion;
        name = moving == Motion::Zoomies ? "run" : moving == Motion::Stalk ? "stalk" : "walk";
        const double stride = moving == Motion::Zoomies ? .72 : moving == Motion::Stalk ? .36 : .48;
        time = preview3d.distance / stride * preview3d.model.Duration(preview3d.model.FindClip(name));
    } else if (g.landedAt && ticks >= g.landedAt && ticks - g.landedAt < 450) {
        name = "landing";
        time = (ticks - g.landedAt) / 450.0 * preview3d.model.Duration(preview3d.model.FindClip(name));
    } else {
        switch (g.motion) {
        case Motion::Idle: case Motion::Watch: case Motion::Sniff: name = "idle"; break;
        case Motion::BellyRoll:
            name = "belly";
            time = elapsed / (cat::animation::kBellyRollDurationMs / 1000.0) * preview3d.model.Duration(preview3d.model.FindClip(name));
            break;
        case Motion::EarFlick:
            name = "ear";
            time = elapsed / (cat::animation::kEarFlickDurationMs / 1000.0) * preview3d.model.Duration(preview3d.model.FindClip(name));
            break;
        case Motion::Punch: case Motion::ToyPaw: name = "paw"; break;
        case Motion::Jump: case Motion::Pounce: name = "idle"; break;
        default: break;
        }
    }
    if (!name) { preview3d.lastClip = -1; return false; }
    const int clip = preview3d.model.FindClip(name);
    if (clip != preview3d.lastClip) {
        preview3d.blendFromClip = preview3d.lastClip;
        preview3d.blendFromTime = preview3d.lastClipTime;
        preview3d.blendStart = ticks;
        preview3d.lastClip = clip;
    }
    const float blend = static_cast<float>(std::clamp((ticks - preview3d.blendStart) / 160.0, 0.0, 1.0));
    const bool sampled = preview3d.blendFromClip >= 0 && blend < 1
        ? preview3d.model.SampleBlended(preview3d.blendFromClip, preview3d.blendFromTime,
                                       clip, time, blend * blend * (3 - 2 * blend), preview3d.vertices, &preview3d.error)
        : preview3d.model.Sample(clip, time, preview3d.vertices, &preview3d.error);
    if (!sampled) {
        preview3d.failed = true;
        g.bubble = L"3D 동작을 읽지 못했어요. 시험판 기록 폴더를 확인해 주세요.";
        g.bubbleUntil = ticks + 10000;
        WriteBytes(g.directory + L"\\3d-render-error.txt", preview3d.error + "\n");
        return false;
    }
    preview3d.lastClipTime = time;
    if (physical.grounded) GroundThreeDVertices(preview3d.vertices);
    // Rotate through a real view of the mesh. Never mirror with negative scale.
    cat::render3d::Params params;
    params.width = g.surface.width; params.height = g.surface.height;
    params.centerX = center; params.groundY = bottom;
    params.pixelsPerUnit = pixelsPerUnit; params.yawRadians = preview3d.yaw;
    if (!preview3d.renderer.Render(preview3d.vertices, preview3d.model.Indices(), params, preview3d.frame)) {
        preview3d.error = preview3d.renderer.LastError(); preview3d.failed = true;
        g.bubble = L"3D 표시를 중단했어요. 시험판 기록 폴더를 확인해 주세요.";
        g.bubbleUntil = ticks + 10000;
        WriteBytes(g.directory + L"\\3d-render-error.txt", preview3d.error + "\n");
        return false;
    }
    Bitmap image(preview3d.frame.width, preview3d.frame.height, preview3d.frame.width * 4,
                 PixelFormat32bppPARGB, reinterpret_cast<BYTE*>(preview3d.frame.bgra.data()));
    const bool drawn = graphics.DrawImage(&image, 0, 0) == Ok;
    if (drawn) ++preview3d.renderedFrames;
    return drawn;
}

bool SaveThreeDFrame(const wchar_t* path, const cat::render3d::Frame& frame) {
    UINT count = 0, bytes = 0;
    if (GetImageEncodersSize(&count, &bytes) != Ok || !bytes) return false;
    std::vector<BYTE> storage(bytes);
    auto* encoders = reinterpret_cast<ImageCodecInfo*>(storage.data());
    if (GetImageEncoders(count, bytes, encoders) != Ok) return false;
    for (UINT i = 0; i < count; ++i) if (wcscmp(encoders[i].MimeType, L"image/png") == 0) {
        Bitmap image(frame.width, frame.height, frame.width * 4, PixelFormat32bppPARGB,
                     reinterpret_cast<BYTE*>(const_cast<std::uint32_t*>(frame.bgra.data())));
        return image.Save(path, &encoders[i].Clsid, nullptr) == Ok;
    }
    return false;
}

void FinishThreeDHostSmoke(ULONGLONG ticks) {
    if (!preview3d.smoke || preview3d.smokeFinished || !g.initialized ||
            ticks < preview3d.smokeStart || ticks - preview3d.smokeStart < 3000) return;
    preview3d.smokeFinished = true;
    const double travel = std::abs(g.physics.RenderPose().relativeTravelX - preview3d.smokeStartTravel);
    const bool isolated = std::filesystem::path(g.directory).filename() == L"YeopjariCat-3D-Preview";
    const bool updatesOff = !updates.automatic && updates.feed.empty();
    cat::render3d::Frame composite;
    composite.width = g.surface.width; composite.height = g.surface.height;
    const auto* pixels = reinterpret_cast<const std::uint32_t*>(g.surface.pixels);
    composite.bgra.assign(pixels, pixels + static_cast<std::size_t>(composite.width) * composite.height);
    const bool captured = SaveThreeDFrame(L"3d-host-smoke.png", composite);
    const bool passed = captured && isolated && updatesOff && !preview3d.failed &&
        preview3d.layerPresented && preview3d.renderedFrames >= 3 && travel > 2;
    preview3d.smokeExit = passed ? 0 : 189;
    std::ostringstream report;
    report << "{\"exit\":" << preview3d.smokeExit << ",\"rendered_3d_frames\":" << preview3d.renderedFrames
           << ",\"physical_travel_px\":" << travel << ",\"layer_presented\":"
           << (preview3d.layerPresented ? "true" : "false") << ",\"profile_isolated\":"
           << (isolated ? "true" : "false") << ",\"updates_disabled\":"
           << (updatesOff ? "true" : "false") << "}\n";
    WriteBytes(L"3d-host-smoke.json", report.str());
    PostMessageW(g.window, WM_CLOSE, 0, 0);
}

int ThreeDRenderSelfTest(const std::wstring& path) {
    // This entry precedes all AppData/profile/update and visible-window paths.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token = 0;
    GdiplusStartupInput startup;
    if (GdiplusStartup(&token, &startup, nullptr) != Ok) return 180;
    std::ostringstream report;
    int result = 0, frames = 0;
    std::string rendererTest;
    if (!cat::render3d::NativeRendererSelfTest(rendererTest)) {
        report << "renderer regression: " << rendererTest << '\n'; result = 188;
    } else if (!InitializeThreeDPreview(path, true)) {
        report << preview3d.error << '\n'; result = 181;
    } else {
        cat::render3d::Params params;
        params.width = 512; params.height = 384; params.centerX = 256; params.groundY = 360;
        params.pixelsPerUnit = 150 / preview3d.modelHeight;
        params.yawRadians = -.8f;
        for (const std::string name : {"idle", "walk", "run", "stalk", "belly", "airborne", "landing", "ear", "paw"}) {
            if (result) break;
            const auto clip = static_cast<std::size_t>(preview3d.model.FindClip(name));
            unsigned imageNumber = 0;
            for (double phase : {0.0, .25, .5, .75, .999}) {
                if (!preview3d.model.Sample(clip, phase * preview3d.model.Duration(clip), preview3d.vertices, &preview3d.error)) {
                    report << name << ": " << preview3d.error << '\n'; result = 182; break;
                }
                const bool grounded = name != "airborne";
                if (grounded) GroundThreeDVertices(preview3d.vertices);
                if (!preview3d.renderer.Render(preview3d.vertices, preview3d.model.Indices(), params, preview3d.frame)) {
                    report << name << ": " << preview3d.renderer.LastError() << '\n'; result = 183; break;
                }
                unsigned visible = 0;
                int lowest = -1;
                for (int y = 0; y < params.height && !result; ++y) for (int x = 0; x < params.width; ++x) {
                    const auto pixel = preview3d.frame.bgra[static_cast<std::size_t>(y) * params.width + x];
                    const unsigned a = pixel >> 24;
                    if (a) { ++visible; lowest = std::max(lowest, y); }
                    if (((pixel >> 16) & 255) > a || ((pixel >> 8) & 255) > a || (pixel & 255) > a) result = 184;
                    if ((x == 0 || y == 0 || x == params.width - 1 || y == params.height - 1) && a) result = 185;
                }
                if (visible < 500 || (grounded && (lowest < params.groundY - 4 || lowest > params.groundY + 1))) result = 186;
                report << name << " phase=" << phase << " visible=" << visible << " bottom=" << lowest << " exit=" << result << '\n';
                ++frames;
                if (!SaveThreeDFrame((L"3d-" + std::wstring(name.begin(), name.end()) + L"-" +
                        std::to_wstring(imageNumber++) + L".png").c_str(), preview3d.frame) && !result) result = 187;
                if (result) break;
            }
        }
        report << "warp=" << preview3d.renderer.UsingWarp() << " frames=" << frames
               << " vertices=" << preview3d.model.Vertices().size()
               << " indices=" << preview3d.model.Indices().size() << '\n';
        // Dense sequences use the very same runtime skinning and D3D drawing
        // as the app; the exported file is not judged from Blender stills only.
        if (!result) {
            params.width = 384; params.height = 256;
            params.centerX = 192; params.groundY = 236;
            for (const std::string name : {"walk", "belly", "landing"}) {
                const auto clip = static_cast<std::size_t>(preview3d.model.FindClip(name));
                const double duration = preview3d.model.Duration(clip);
                const unsigned samples = static_cast<unsigned>(std::lround(duration * 30)) + 1;
                const auto directory = std::filesystem::path(L"3d-motion-review") / name;
                std::error_code ec;
                std::filesystem::create_directories(directory, ec);
                if (ec) { result = 190; break; }
                params.yawRadians = name == "walk" ? -1.05f : -.55f;
                for (unsigned sample = 0; sample < samples; ++sample) {
                    const double time = std::min(duration, sample / 30.0);
                    if (!preview3d.model.Sample(clip, time, preview3d.vertices, &preview3d.error)) { result = 191; break; }
                    GroundThreeDVertices(preview3d.vertices);
                    if (!preview3d.renderer.Render(preview3d.vertices, preview3d.model.Indices(), params, preview3d.frame)) {
                        result = 192; break;
                    }
                    wchar_t filename[32]{};
                    swprintf(filename, 32, L"frame-%04u.png", sample);
                    if (!SaveThreeDFrame((directory / filename).c_str(), preview3d.frame)) { result = 193; break; }
                }
                report << "sequence=" << name << " samples=" << samples << " fps=30 exit=" << result << '\n';
                if (result) break;
            }
        }
    }
    report << "exit=" << result << '\n';
    WriteBytes(L"3d-render-self-test.txt", report.str());
    WriteBytes(L"3d-render-self-test.json", "{\"exit\":" + std::to_string(result) +
        ",\"frames\":" + std::to_string(frames) + ",\"warp\":" +
        (preview3d.initialized && preview3d.renderer.UsingWarp() ? "true" : "false") +
        ",\"app_profile_loaded\":false,\"screenshots\":\"3d-<clip>-<0..4>.png\"}\n");
    GdiplusShutdown(token);
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
