#pragma once

// A short, entirely visual cracked-glass gag. This window never receives input,
// captures the screen, moves another window, or changes anything underneath it.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

namespace cat {

class ScreenPrank {
public:
    ScreenPrank() = default;
    ScreenPrank(const ScreenPrank&) = delete;
    ScreenPrank& operator=(const ScreenPrank&) = delete;
    ~ScreenPrank() { Shutdown(); }

    bool Initialize(HINSTANCE instance) {
        if (window_) return true;
        instance_ = instance;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WindowProc;
        wc.hInstance = instance;
        wc.lpszClassName = kClass;
        if (RegisterClassExW(&wc)) registered_ = true;
        else if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        window_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW |
                                     WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                                 kClass, L"Yeopjari Cat playful glass", WS_POPUP,
                                 0, 0, 1, 1, nullptr, nullptr, instance, nullptr);
        return window_ != nullptr;
    }

    void Trigger(POINT impact, RECT monitorWork, double dpiScale) {
        Clear();
        if (!window_ || monitorWork.right <= monitorWork.left || monitorWork.bottom <= monitorWork.top) return;
        const long long workWidth = static_cast<long long>(monitorWork.right) - monitorWork.left;
        const long long workHeight = static_cast<long long>(monitorWork.bottom) - monitorWork.top;
        if (workWidth < 48 || workHeight < 48) return;
        // The surface remains below 9 MiB even on extreme DPI configurations.
        scale_ = static_cast<float>(std::clamp(std::isfinite(dpiScale) ? dpiScale : 1.0, 0.75, 4.0));
        const int width = static_cast<int>(std::min(workWidth, static_cast<long long>(std::lround(430 * scale_))));
        const int height = static_cast<int>(std::min(workHeight, static_cast<long long>(std::lround(330 * scale_))));
        impact.x = std::clamp(impact.x, monitorWork.left, monitorWork.right - 1);
        impact.y = std::clamp(impact.y, monitorWork.top, monitorWork.bottom - 1);
        left_ = static_cast<int>(std::clamp(static_cast<long long>(impact.x) - width / 2,
                                         static_cast<long long>(monitorWork.left),
                                         static_cast<long long>(monitorWork.right) - width));
        top_ = static_cast<int>(std::clamp(static_cast<long long>(impact.y) - std::lround(278 * scale_),
                                        static_cast<long long>(monitorWork.top),
                                        static_cast<long long>(monitorWork.bottom) - height));
        if (!CreateSurface(width, height)) return;
        origin_ = Gdiplus::PointF(static_cast<float>(impact.x - left_), static_cast<float>(impact.y - top_));
        started_ = GetTickCount64();
        random_.seed(static_cast<std::uint32_t>(started_ ^ (started_ >> 32) ^
                        (static_cast<std::uint32_t>(impact.x) * 2654435761U) ^ static_cast<std::uint32_t>(impact.y)));
        BuildFractures();
        active_ = true;
        Paint(0);
        if (active_) {
            SetWindowPos(window_, HWND_TOPMOST, left_, top_, width_, height_,
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
            ShowWindow(window_, SW_SHOWNOACTIVATE);
        }
    }

    void Tick(ULONGLONG ticks) {
        if (!active_) return;
        // A caller may pass a tick captured just before Trigger in this same frame.
        if (ticks < started_) return;
        if (ticks - started_ >= kDurationMs) { Clear(); return; }
        Paint(static_cast<double>(ticks - started_));
    }

    void Clear() {
        active_ = false;
        if (window_) ShowWindow(window_, SW_HIDE);
    }

    bool Active() const { return active_; }

    void Shutdown() {
        Clear();
        if (window_) { DestroyWindow(window_); window_ = nullptr; }
        ReleaseSurface();
        if (registered_ && instance_) UnregisterClassW(kClass, instance_);
        registered_ = false;
        instance_ = nullptr;
    }

private:
    static constexpr wchar_t kClass[] = L"YeopjariCat.ScreenPrank.v1";
    static constexpr ULONGLONG kDurationMs = 2850;
    struct Fracture {
        std::vector<Gdiplus::PointF> points;
        float delay = 0;
        float weight = 1;
    };
    struct Shard { float angle, speed, size, spin; };
    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr, oldBitmap_ = nullptr;
    BYTE* pixels_ = nullptr;
    int width_ = 0, height_ = 0, left_ = 0, top_ = 0;
    bool registered_ = false, active_ = false;
    float scale_ = 1;
    ULONGLONG started_ = 0;
    Gdiplus::PointF origin_{};
    std::vector<Fracture> fractures_;
    std::array<Shard, 7> shards_{};
    std::mt19937 random_{0x59704361U};

    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_NCHITTEST: return HTTRANSPARENT;
            case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
            case WM_ERASEBKGND: return 1;
            case WM_CLOSE: return 0; // The owner controls the brief lifetime.
            default: return DefWindowProcW(window, message, wparam, lparam);
        }
    }

    float Random(float low, float high) { return std::uniform_real_distribution<float>(low, high)(random_); }

    void ReleaseSurface() {
        if (dc_ && oldBitmap_) SelectObject(dc_, oldBitmap_);
        if (bitmap_) DeleteObject(bitmap_);
        if (dc_) DeleteDC(dc_);
        dc_ = nullptr; bitmap_ = oldBitmap_ = nullptr; pixels_ = nullptr; width_ = height_ = 0;
    }

    bool CreateSurface(int width, int height) {
        if (dc_ && pixels_ && width == width_ && height == height_) return true;
        ReleaseSurface();
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        HDC screen = GetDC(nullptr);
        if (!screen) return false;
        dc_ = CreateCompatibleDC(screen);
        bitmap_ = CreateDIBSection(screen, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels_), nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (!dc_ || !bitmap_ || !pixels_) { ReleaseSurface(); return false; }
        oldBitmap_ = static_cast<HBITMAP>(SelectObject(dc_, bitmap_));
        if (!oldBitmap_ || oldBitmap_ == reinterpret_cast<HBITMAP>(HGDI_ERROR)) { oldBitmap_ = nullptr; ReleaseSurface(); return false; }
        width_ = width; height_ = height;
        return true;
    }

    Gdiplus::PointF Along(float angle, float length) const {
        return {origin_.X + std::cos(angle) * length, origin_.Y + std::sin(angle) * length};
    }

    float BoundaryLength(float angle, float wanted) const {
        const float dx = std::cos(angle), dy = std::sin(angle);
        float length = wanted;
        if (dx > 0.001f) length = std::min(length, (width_ - 3.f - origin_.X) / dx);
        else if (dx < -0.001f) length = std::min(length, (2.f - origin_.X) / dx);
        if (dy > 0.001f) length = std::min(length, (height_ - 3.f - origin_.Y) / dy);
        else if (dy < -0.001f) length = std::min(length, (2.f - origin_.Y) / dy);
        return std::max(0.f, length);
    }

    void BuildFractures() {
        fractures_.clear();
        constexpr float pi = 3.14159265359f;
        constexpr int rays = 15;
        std::array<float, rays> angles{}, lengths{};
        for (int i = 0; i < rays; ++i) {
            const float angle = i * 2 * pi / rays + Random(-0.10f, 0.10f);
            const float downward = std::max(0.f, std::sin(angle));
            const float wanted = Random(175.f, 265.f) * scale_ / (1.f + 3.7f * downward);
            const float length = BoundaryLength(angle, wanted);
            angles[i] = angle; lengths[i] = length;
            if (length < 6 * scale_) continue;
            Fracture ray;
            ray.weight = Random(0.8f, 1.12f);
            ray.points.push_back(origin_);
            for (int j = 1; j <= 5; ++j) {
                const float part = j / 5.f;
                const float wobble = Random(-6.f, 6.f) * scale_ * part;
                auto p = Along(angle, length * part);
                p.X += -std::sin(angle) * wobble;
                p.Y += std::cos(angle) * wobble;
                ray.points.push_back(p);
            }
            fractures_.push_back(ray);
            if (length > 60 * scale_ && Random(0, 1) < 0.78f) {
                const auto start = ray.points[3];
                const float split = angle + Random(-0.7f, 0.7f);
                const float branchLength = length * Random(0.18f, 0.34f);
                fractures_.push_back({{start,
                    {start.X + std::cos(split) * branchLength * 0.5f, start.Y + std::sin(split) * branchLength * 0.5f},
                    {start.X + std::cos(split + 0.10f) * branchLength, start.Y + std::sin(split + 0.10f) * branchLength}},
                    70, 0.65f});
            }
        }
        // Imperfect cross-links make the radiating cracks read as broken glass.
        for (float fraction : {0.20f, 0.43f, 0.66f}) {
            for (int i = 0; i < rays; ++i) {
                const int next = (i + 1) % rays;
                if (Random(0, 1) < 0.26f || std::min(lengths[i], lengths[next]) < 14 * scale_) continue;
                const float radius = std::min(lengths[i], lengths[next]) * fraction;
                const auto a = Along(angles[i], radius * Random(0.94f, 1.08f));
                const auto b = Along(angles[next], radius * Random(0.94f, 1.08f));
                const Gdiplus::PointF mid((a.X + b.X) * 0.5f + Random(-3.f, 3.f) * scale_,
                                           (a.Y + b.Y) * 0.5f + Random(-3.f, 3.f) * scale_);
                fractures_.push_back({{a, mid, b}, 55 + fraction * 80, 0.62f});
            }
        }
        for (auto& shard : shards_) shard = {Random(-2.95f, -0.18f), Random(35.f, 100.f), Random(2.1f, 4.7f), Random(-4.f, 4.f)};
    }

    static void DrawPartial(Gdiplus::Graphics& graphics, Gdiplus::Pen& pen,
                            const std::vector<Gdiplus::PointF>& points, float fraction) {
        if (fraction <= 0 || points.size() < 2) return;
        if (fraction >= 1) { graphics.DrawLines(&pen, points.data(), static_cast<INT>(points.size())); return; }
        float total = 0;
        for (std::size_t i = 1; i < points.size(); ++i) total += std::hypot(points[i].X - points[i - 1].X, points[i].Y - points[i - 1].Y);
        float remaining = total * fraction;
        for (std::size_t i = 1; i < points.size() && remaining > 0; ++i) {
            const float dx = points[i].X - points[i - 1].X, dy = points[i].Y - points[i - 1].Y;
            const float length = std::hypot(dx, dy);
            if (length <= 0.001f) continue;
            const float part = std::min(1.f, remaining / length);
            graphics.DrawLine(&pen, points[i - 1], {points[i - 1].X + dx * part, points[i - 1].Y + dy * part});
            remaining -= length;
        }
    }

    void Paint(double elapsedMs) {
        if (!active_ || !dc_ || !pixels_) return;
        const float fade = static_cast<float>(std::clamp((kDurationMs - elapsedMs) / 1450.0, 0.0, 1.0));
        const float opacity = fade * fade;
        std::memset(pixels_, 0, static_cast<std::size_t>(width_) * height_ * 4);
        {
            Gdiplus::Bitmap target(width_, height_, width_ * 4, PixelFormat32bppPARGB, pixels_);
            if (target.GetLastStatus() != Gdiplus::Ok) { Clear(); return; }
            Gdiplus::Graphics graphics(&target);
            graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
            graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            for (const auto& fracture : fractures_) {
                const float reveal = static_cast<float>(std::clamp((elapsedMs - fracture.delay) / 155.0, 0.0, 1.0));
                if (reveal <= 0) continue;
                Gdiplus::Pen edge(Gdiplus::Color(static_cast<BYTE>(125 * opacity), 28, 36, 48), 2.2f * scale_ * fracture.weight);
                Gdiplus::Pen light(Gdiplus::Color(static_cast<BYTE>(225 * opacity), 238, 250, 255), 0.85f * scale_ * fracture.weight);
                edge.SetLineJoin(Gdiplus::LineJoinRound); light.SetLineJoin(Gdiplus::LineJoinRound);
                const auto state = graphics.Save();
                graphics.TranslateTransform(0.65f * scale_, 0.65f * scale_);
                DrawPartial(graphics, edge, fracture.points, reveal);
                graphics.Restore(state);
                DrawPartial(graphics, light, fracture.points, reveal);
            }
            const float seconds = static_cast<float>(elapsedMs / 1000.0);
            if (seconds < 1.3f) {
                const BYTE shardAlpha = static_cast<BYTE>(155 * std::max(0.f, 1.f - seconds / 1.3f));
                Gdiplus::Pen outline(Gdiplus::Color(shardAlpha, 233, 248, 255), 0.85f * scale_);
                Gdiplus::SolidBrush glass(Gdiplus::Color(static_cast<BYTE>(shardAlpha / 4), 226, 245, 255));
                for (const auto& shard : shards_) {
                    const float x = origin_.X + std::cos(shard.angle) * shard.speed * seconds * scale_;
                    const float y = origin_.Y + (std::sin(shard.angle) * shard.speed * seconds + 32 * seconds * seconds) * scale_;
                    std::array<Gdiplus::PointF, 3> vertices;
                    for (int i = 0; i < 3; ++i) {
                        const float angle = shard.angle + shard.spin * seconds + i * 2.0943951f;
                        const float size = shard.size * scale_ * (i == 1 ? 1.4f : 0.8f);
                        vertices[i] = {x + std::cos(angle) * size, y + std::sin(angle) * size};
                    }
                    graphics.FillPolygon(&glass, vertices.data(), 3);
                    graphics.DrawPolygon(&outline, vertices.data(), 3);
                }
            }
            if (elapsedMs < 200) {
                const float radius = static_cast<float>((7 + elapsedMs * 0.07) * scale_);
                Gdiplus::Pen ripple(Gdiplus::Color(static_cast<BYTE>(90 * (1 - elapsedMs / 200)), 255, 242, 222), 1.2f * scale_);
                graphics.DrawEllipse(&ripple, origin_.X - radius, origin_.Y - radius, radius * 2, radius * 2);
            }
        }
        HDC screen = GetDC(nullptr);
        if (!screen) { Clear(); return; }
        POINT destination{left_, top_}, source{};
        SIZE size{width_, height_};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const BOOL updated = UpdateLayeredWindow(window_, screen, &destination, &size, dc_, &source, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
        if (!updated) Clear();
    }
};

} // namespace cat
