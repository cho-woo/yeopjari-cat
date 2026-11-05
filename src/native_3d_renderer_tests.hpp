#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include "cat_3d_renderer.hpp"

// Runs real D3D11 shaders and readback on Windows WARP. This is intentionally
// independent of a particular cat model so malformed camera/alpha/depth paths
// are tested even when a model appears plausible in a still image.
namespace cat::render3d {

inline bool NativeRendererSelfTest(std::string& report) {
    Renderer renderer;
    if (!renderer.Initialize(true)) { report = renderer.LastError(); return false; }
    if (!renderer.UsingWarp()) { report = "Explicit WARP test did not select WARP"; return false; }
    std::vector<Vertex> mesh(3);
    const float positions[][3] = {{-.6f, 0, .2f}, {.6f, 0, .2f}, {0, 0, 1.2f}};
    for (std::size_t i = 0; i < mesh.size(); ++i) {
        std::copy(std::begin(positions[i]), std::end(positions[i]), mesh[i].position);
        mesh[i].normal[0] = mesh[i].normal[2] = 0;
        mesh[i].normal[1] = -1;
        mesh[i].uv[0] = mesh[i].uv[1] = 0;
        mesh[i].color[0] = .75f; mesh[i].color[1] = .05f;
        mesh[i].color[2] = .015f; mesh[i].color[3] = 1;
        mesh[i].roughness = .8f;
    }
    Params params;
    params.width = params.height = 64;
    params.centerX = 32; params.groundY = 56;
    params.pixelsPerUnit = 32;
    const std::vector<std::uint32_t> triangle{0, 1, 2};
    Frame frame;
    if (!renderer.Render(mesh, triangle, params, frame)) { report = renderer.LastError(); return false; }
    if (frame.width != 64 || frame.height != 64 || frame.bgra.size() != 4096) {
        report = "Wrong readback dimensions"; return false;
    }
    auto alpha = [](std::uint32_t pixel) { return pixel >> 24; };
    const auto center = frame.bgra[40 * 64 + 32];
    if (alpha(center) != 255 || ((center >> 16) & 255) <= (center & 255)) {
        report = "Triangle shading or top-down BGRA orientation is incorrect"; return false;
    }
    std::size_t opaque = 0;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 64; ++x) {
        const auto pixel = frame.bgra[y * 64 + x];
        const auto a = alpha(pixel);
        if (a) ++opaque;
        if ((x < 3 || x >= 61 || y < 3 || y >= 61) && pixel != 0) {
            report = "Transparent render border contains color or alpha"; return false;
        }
        if ((pixel & 255) > a || ((pixel >> 8) & 255) > a || ((pixel >> 16) & 255) > a) {
            report = "Readback is not premultiplied BGRA"; return false;
        }
    }
    if (opaque < 250 || opaque > 1000) { report = "Triangle coverage is implausible"; return false; }

    // Render near red first, then far blue. Depth, not submission order, must
    // keep the near surface visible. -Y is toward a zero-yaw camera.
    auto overlapping = mesh;
    overlapping.insert(overlapping.end(), mesh.begin(), mesh.end());
    for (std::size_t i = 0; i < 6; ++i) {
        overlapping[i].position[1] = i < 3 ? -.2f : .2f;
        if (i >= 3) {
            overlapping[i].color[0] = .01f;
            overlapping[i].color[2] = .8f;
        }
    }
    if (!renderer.Render(overlapping, {0, 1, 2, 3, 4, 5}, params, frame)) {
        report = renderer.LastError(); return false;
    }
    const auto depthPixel = frame.bgra[40 * 64 + 32];
    if (((depthPixel >> 16) & 255) <= (depthPixel & 255)) {
        report = "Far blue triangle overwrote near red triangle"; return false;
    }

    for (auto& v : mesh) v.color[3] = .5f;
    if (!renderer.Render(mesh, triangle, params, frame)) { report = renderer.LastError(); return false; }
    const auto translucent = frame.bgra[40 * 64 + 32];
    if (alpha(translucent) < 126 || alpha(translucent) > 129 ||
            ((translucent >> 16) & 255) > alpha(translucent)) {
        report = "Half-opacity material was not read back premultiplied"; return false;
    }
    params.width = 96; params.height = 48;
    params.centerX = 48; params.groundY = 47; params.pixelsPerUnit = 25;
    if (!renderer.Render(mesh, triangle, params, frame) || frame.bgra.size() != 96 * 48) {
        report = "DPI-style render-target resize failed: " + renderer.LastError(); return false;
    }
    auto broken = mesh;
    broken[0].position[0] = std::numeric_limits<float>::quiet_NaN();
    if (renderer.Render(broken, triangle, params, frame) || !frame.bgra.empty() || renderer.LastError().empty()) {
        report = "Non-finite animation was accepted or retained a stale frame"; return false;
    }
    if (renderer.Render(mesh, {0, 1, 99}, params, frame) || !frame.bgra.empty()) {
        report = "Out-of-range triangle index was accepted"; return false;
    }
    params.width = 0;
    if (renderer.Render(mesh, triangle, params, frame) || !frame.bgra.empty()) {
        report = "Zero-width render target was accepted"; return false;
    }
    params.width = 96;
    if (!renderer.Render(mesh, triangle, params, frame)) {
        report = "Renderer did not recover from rejected input: " + renderer.LastError(); return false;
    }
    report = "D3D11 WARP: shaders, depth order, transparent border, premultiplied alpha, "
             "resize, malformed input and recovery passed";
    return true;
}

} // namespace cat::render3d
