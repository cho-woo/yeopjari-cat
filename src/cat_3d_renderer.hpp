#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "cat_3d_model.hpp"

// Experimental native 3D drawing. The caller owns physics, animation, state,
// DPI and window placement; this renderer never reads or writes saved pet data.
namespace cat::render3d {

using Vertex = cat::three_d::RenderVertex;

struct Params {
    int width = 260, height = 210;
    float centerX = 130, groundY = 209;
    float pixelsPerUnit = 70;
    // Model axes: X lateral, forward -Y, Z up. Zero yaw faces the camera.
    // Positive yaw views the animal from its positive-X side. Zero pitch
    // puts every z=0 contact on groundY, independently of the paw's depth.
    float yawRadians = 0, pitchRadians = 0, depthSpan = 8;
};

struct Frame {
    int width = 0, height = 0;
    // Top-down, tightly packed premultiplied BGRA; compatible with GDI+
    // PixelFormat32bppPARGB and UpdateLayeredWindow's AC_SRC_ALPHA.
    std::vector<std::uint32_t> bgra;
};

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Hardware is preferred. WARP is also available explicitly for native
    // Windows tests. All methods must be called on the owning render thread.
    bool Initialize(bool forceWarp = false);
    bool Render(const std::vector<Vertex>& vertices,
                const std::vector<std::uint32_t>& indices,
                const Params& params, Frame& frame);
    const std::string& LastError() const;
    bool UsingWarp() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cat::render3d
