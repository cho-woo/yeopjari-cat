#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include "cat_3d_renderer.hpp"

namespace cat::render3d {
namespace {

template<class T> void Release(T*& value) {
    if (value) value->Release();
    value = nullptr;
}

constexpr std::size_t kMaxVertices = 2'000'000;
constexpr std::size_t kMaxIndices = 6'000'000;
constexpr int kMaxDimension = 4096;

// No texture or external shader dependency. RGB is supplied in linear space;
// lighting is linear and converted to display sRGB before premultiplication.
constexpr char kShader[] = R"hlsl(
cbuffer Camera : register(b0) {
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraView;
    float4 screenTransform;
    float4 depthSettings;
};
struct VertexIn {
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float roughness : TEXCOORD1;
};
struct VertexOut {
    float4 position : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    float roughness : TEXCOORD1;
};
VertexOut VSMain(VertexIn v) {
    VertexOut o;
    o.position = float4(
        screenTransform.x + dot(v.position, cameraRight.xyz) * screenTransform.z,
        screenTransform.y + dot(v.position, cameraUp.xyz) * screenTransform.w,
        0.5 - dot(v.position, cameraView.xyz) * depthSettings.x, 1);
    o.normal = v.normal;
    o.uv = v.uv;
    o.color = v.color;
    o.roughness = v.roughness;
    return o;
}
float3 unitVector(float3 value) {
    return value * rsqrt(max(dot(value, value), 1e-12));
}
float coatNoise(float2 p) {
    return frac(sin(dot(p, float2(12.9898, 78.233))) * 43758.5453);
}
float3 displayColor(float3 rgb) {
    rgb = saturate(rgb);
    return lerp(12.92 * rgb, 1.055 * pow(rgb, 1.0 / 2.4) - 0.055,
                step(float3(0.0031308, 0.0031308, 0.0031308), rgb));
}
float4 PSMain(VertexOut v) : SV_TARGET {
    float alpha = saturate(v.color.a);
    clip(alpha - 0.001);
    float3 n = unitVector(v.normal);
    float3 view = cameraView.xyz;
    float3 key = unitVector(float3(-0.5, -0.8, 1.15));
    float3 fill = unitVector(float3(0.8, -0.4, 0.5));
    float roughness = clamp(v.roughness, 0.08, 1.0);
    float diffuse = 0.47 + 0.40 * saturate(dot(n, key))
                         + 0.14 * saturate(dot(n, fill));
    float3 base = max(v.color.rgb, 0.0);
    // This is only very small coat grain; detailed fur still belongs to the
    // model. Filtering damps subpixel grain instead of shimmering at pet size.
    if (roughness > 0.65) {
        float2 uv = v.uv * float2(180.0, 420.0);
        float coverage = saturate(1.0 - max(length(ddx(uv)), length(ddy(uv))));
        base *= 1.0 + (coatNoise(floor(uv)) - 0.5) * 0.045 * coverage;
    }
    float exponent = lerp(120.0, 8.0, roughness);
    float spec = pow(saturate(dot(n, unitVector(key + view))), exponent)
                 * (1.0 - roughness) * 0.33;
    float rim = pow(1.0 - saturate(dot(n, view)), 3.0)
                * saturate(n.z * 0.6 + 0.4) * roughness * 0.035;
    float3 lit = base * diffuse + float3(1.0, 0.95, 0.88) * spec + base * rim;
    return float4(displayColor(lit) * alpha, alpha);
}
)hlsl";

struct CameraConstants {
    float right[4], up[4], view[4], transform[4], depth[4];
};
static_assert(sizeof(CameraConstants) % 16 == 0);
static_assert(sizeof(Vertex) == 52, "Renderer vertex ABI must match the model loader");

} // namespace

struct Renderer::Impl {
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    ID3D11VertexShader* vertexShader = nullptr;
    ID3D11PixelShader* pixelShader = nullptr;
    ID3D11InputLayout* layout = nullptr;
    ID3D11RasterizerState* raster = nullptr;
    ID3D11BlendState* blend = nullptr;
    ID3D11DepthStencilState* depthState = nullptr;
    ID3D11Buffer* constants = nullptr;
    ID3D11Buffer* vertices = nullptr;
    ID3D11Buffer* indices = nullptr;
    ID3D11Texture2D* target = nullptr;
    ID3D11Texture2D* resolved = nullptr;
    ID3D11Texture2D* staging = nullptr;
    ID3D11Texture2D* depthTexture = nullptr;
    ID3D11RenderTargetView* targetView = nullptr;
    ID3D11DepthStencilView* depthView = nullptr;
    std::size_t vertexCapacity = 0, indexCapacity = 0;
    int width = 0, height = 0;
    unsigned samples = 1;
    bool warp = false;
    std::string error;

    ~Impl() { Reset(); }

    bool Fail(const char* operation, HRESULT result) {
        char text[192]{};
        std::snprintf(text, sizeof text, "%s failed (HRESULT 0x%08lX)",
                      operation, static_cast<unsigned long>(result));
        error = text;
        return false;
    }

    void ResetTargets() {
        if (context) context->OMSetRenderTargets(0, nullptr, nullptr);
        Release(targetView); Release(depthView); Release(depthTexture);
        Release(target); Release(resolved); Release(staging);
        width = height = 0;
    }

    void Reset() {
        if (context) context->ClearState();
        ResetTargets();
        Release(vertices); Release(indices); Release(constants);
        Release(vertexShader); Release(pixelShader); Release(layout);
        Release(raster); Release(blend); Release(depthState);
        Release(context); Release(device);
        vertexCapacity = indexCapacity = 0;
    }

    bool Compile(const char* entry, const char* profile, ID3DBlob** code) {
        ID3DBlob* messages = nullptr;
        HRESULT hr = D3DCompile(kShader, sizeof(kShader) - 1, "cat_3d_renderer",
                               nullptr, nullptr, entry, profile,
                               D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3,
                               0, code, &messages);
        if (FAILED(hr)) {
            Fail(entry, hr);
            if (messages) {
                error += ": ";
                error.append(static_cast<const char*>(messages->GetBufferPointer()),
                             std::min<std::size_t>(messages->GetBufferSize(), 2048));
            }
        }
        Release(messages);
        return SUCCEEDED(hr);
    }

    bool Initialize(bool forceWarp) {
        if (device && (!forceWarp || warp)) return true;
        Reset();
        error.clear();
        constexpr D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL selected{};
        HRESULT hr = E_FAIL;
        warp = forceWarp;
        if (!forceWarp) {
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                   &device, &selected, &context);
        }
        if (forceWarp || FAILED(hr)) {
            Release(context); Release(device);
            warp = true;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels,
                                   static_cast<UINT>(std::size(levels)), D3D11_SDK_VERSION,
                                   &device, &selected, &context);
        }
        if (FAILED(hr)) return Fail("D3D11CreateDevice hardware/WARP", hr);

        ID3DBlob* vs = nullptr;
        ID3DBlob* ps = nullptr;
        if (!Compile("VSMain", "vs_4_0", &vs)) { Reset(); return false; }
        if (!Compile("PSMain", "ps_4_0", &ps)) { Release(vs); Reset(); return false; }
        hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &vertexShader);
        if (FAILED(hr)) { Release(vs); Release(ps); Reset(); return Fail("CreateVertexShader", hr); }
        hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(), nullptr, &pixelShader);
        Release(ps);
        if (FAILED(hr)) { Release(vs); Reset(); return Fail("CreatePixelShader", hr); }

        const D3D11_INPUT_ELEMENT_DESC elements[] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, offsetof(Vertex, normal), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, uv), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"TEXCOORD", 1, DXGI_FORMAT_R32_FLOAT, 0, offsetof(Vertex, roughness), D3D11_INPUT_PER_VERTEX_DATA, 0}};
        hr = device->CreateInputLayout(elements, static_cast<UINT>(std::size(elements)),
                                       vs->GetBufferPointer(), vs->GetBufferSize(), &layout);
        Release(vs);
        if (FAILED(hr)) { Reset(); return Fail("CreateInputLayout", hr); }

        D3D11_RASTERIZER_DESC rasterDesc{};
        rasterDesc.FillMode = D3D11_FILL_SOLID;
        rasterDesc.CullMode = D3D11_CULL_NONE;
        rasterDesc.DepthClipEnable = TRUE;
        rasterDesc.MultisampleEnable = TRUE;
        hr = device->CreateRasterizerState(&rasterDesc, &raster);
        if (FAILED(hr)) { Reset(); return Fail("CreateRasterizerState", hr); }
        D3D11_BLEND_DESC blendDesc{};
        auto& rt = blendDesc.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        hr = device->CreateBlendState(&blendDesc, &blend);
        if (FAILED(hr)) { Reset(); return Fail("CreateBlendState", hr); }
        D3D11_DEPTH_STENCIL_DESC depthDesc{};
        depthDesc.DepthEnable = TRUE;
        depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
        depthDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
        hr = device->CreateDepthStencilState(&depthDesc, &depthState);
        if (FAILED(hr)) { Reset(); return Fail("CreateDepthStencilState", hr); }
        D3D11_BUFFER_DESC cb{};
        cb.ByteWidth = sizeof(CameraConstants);
        cb.Usage = D3D11_USAGE_DEFAULT;
        cb.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        hr = device->CreateBuffer(&cb, nullptr, &constants);
        if (FAILED(hr)) { Reset(); return Fail("Create camera constants", hr); }

        samples = 1;
        for (unsigned candidate : {4U, 2U}) {
            UINT colorLevels = 0, depthLevels = 0;
            if (SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, candidate, &colorLevels)) &&
                    SUCCEEDED(device->CheckMultisampleQualityLevels(DXGI_FORMAT_D24_UNORM_S8_UINT, candidate, &depthLevels)) &&
                    colorLevels && depthLevels) {
                samples = candidate;
                break;
            }
        }
        return true;
    }

    bool Resize(int w, int h) {
        if (width == w && height == h) return true;
        ResetTargets();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(w);
        desc.Height = static_cast<UINT>(h);
        desc.MipLevels = desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = samples;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        HRESULT hr = device->CreateTexture2D(&desc, nullptr, &target);
        if (FAILED(hr)) return Fail("Create render target", hr);
        hr = device->CreateRenderTargetView(target, nullptr, &targetView);
        if (FAILED(hr)) return Fail("Create render target view", hr);
        desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        hr = device->CreateTexture2D(&desc, nullptr, &depthTexture);
        if (FAILED(hr)) return Fail("Create depth texture", hr);
        hr = device->CreateDepthStencilView(depthTexture, nullptr, &depthView);
        if (FAILED(hr)) return Fail("Create depth view", hr);
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BindFlags = 0;
        if (samples > 1) {
            hr = device->CreateTexture2D(&desc, nullptr, &resolved);
            if (FAILED(hr)) return Fail("Create MSAA resolve texture", hr);
        }
        desc.Usage = D3D11_USAGE_STAGING;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        hr = device->CreateTexture2D(&desc, nullptr, &staging);
        if (FAILED(hr)) return Fail("Create readback texture", hr);
        width = w;
        height = h;
        return true;
    }

    bool Buffer(ID3D11Buffer*& buffer, std::size_t& capacity, std::size_t size, UINT flags) {
        if (capacity >= size && buffer) return true;
        Release(buffer);
        capacity = 0;
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = static_cast<UINT>(size);
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.BindFlags = flags;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        const HRESULT hr = device->CreateBuffer(&desc, nullptr, &buffer);
        if (FAILED(hr)) return Fail("Create mesh buffer", hr);
        capacity = size;
        return true;
    }

    bool Upload(ID3D11Buffer* buffer, const void* data, std::size_t size) {
        D3D11_MAPPED_SUBRESOURCE map{};
        const HRESULT hr = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
        if (FAILED(hr)) return Fail("Map mesh upload", hr);
        std::memcpy(map.pData, data, size);
        context->Unmap(buffer, 0);
        return true;
    }

    bool Render(const std::vector<Vertex>& mesh, const std::vector<std::uint32_t>& faces,
                const Params& p, Frame& frame) {
        frame.width = frame.height = 0;
        frame.bgra.clear();
        error.clear();
        if (p.width <= 0 || p.height <= 0 || p.width > kMaxDimension || p.height > kMaxDimension ||
                !std::isfinite(p.centerX) || !std::isfinite(p.groundY) ||
                !std::isfinite(p.pixelsPerUnit) || p.pixelsPerUnit <= 0 || p.pixelsPerUnit > 10000 ||
                !std::isfinite(p.yawRadians) || !std::isfinite(p.pitchRadians) ||
                !std::isfinite(p.depthSpan) || p.depthSpan <= 0 ||
                mesh.empty() || faces.empty() || mesh.size() > kMaxVertices ||
                faces.size() > kMaxIndices || faces.size() % 3 != 0) {
            error = "Invalid 3D frame, camera or mesh dimensions";
            return false;
        }
        for (const auto index : faces) {
            if (index >= mesh.size()) { error = "3D triangle index is outside the mesh"; return false; }
        }
        // Positions can be influenced by live animation, not only the loader.
        // Reject invalid values before they reach the graphics driver.
        for (const auto& vertex : mesh) {
            for (float value : vertex.position) if (!std::isfinite(value)) {
                error = "Non-finite animated 3D vertex"; return false;
            }
            for (float value : vertex.normal) if (!std::isfinite(value)) {
                error = "Non-finite animated 3D normal"; return false;
            }
            for (float value : vertex.color) if (!std::isfinite(value)) {
                error = "Non-finite 3D color"; return false;
            }
            for (float value : vertex.uv) if (!std::isfinite(value)) {
                error = "Non-finite 3D UV"; return false;
            }
            if (!std::isfinite(vertex.roughness)) { error = "Non-finite 3D roughness"; return false; }
        }
        if (!Initialize(false) || !Resize(p.width, p.height)) return false;
        const std::size_t vertexBytes = mesh.size() * sizeof(Vertex);
        const std::size_t indexBytes = faces.size() * sizeof(std::uint32_t);
        if (!Buffer(vertices, vertexCapacity, vertexBytes, D3D11_BIND_VERTEX_BUFFER) ||
                !Buffer(indices, indexCapacity, indexBytes, D3D11_BIND_INDEX_BUFFER) ||
                !Upload(vertices, mesh.data(), vertexBytes) ||
                !Upload(indices, faces.data(), indexBytes)) return false;

        const float cy = std::cos(p.yawRadians), sy = std::sin(p.yawRadians);
        const float cp = std::cos(p.pitchRadians), sp = std::sin(p.pitchRadians);
        const CameraConstants camera{
            {cy, sy, 0, 0}, {-sy * sp, cy * sp, cp, 0}, {sy * cp, -cy * cp, sp, 0},
            {2 * p.centerX / p.width - 1, 1 - 2 * p.groundY / p.height,
             2 * p.pixelsPerUnit / p.width, 2 * p.pixelsPerUnit / p.height},
            {1 / p.depthSpan, 0, 0, 0}};
        context->UpdateSubresource(constants, 0, nullptr, &camera, 0, 0);
        const float clear[4]{};
        context->ClearRenderTargetView(targetView, clear);
        context->ClearDepthStencilView(depthView, D3D11_CLEAR_DEPTH, 1, 0);
        context->OMSetRenderTargets(1, &targetView, depthView);
        context->OMSetDepthStencilState(depthState, 0);
        const float factor[4]{};
        context->OMSetBlendState(blend, factor, 0xffffffff);
        context->RSSetState(raster);
        const D3D11_VIEWPORT viewport{0, 0, static_cast<float>(p.width), static_cast<float>(p.height), 0, 1};
        context->RSSetViewports(1, &viewport);
        const UINT stride = sizeof(Vertex), offset = 0;
        context->IASetInputLayout(layout);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->IASetVertexBuffers(0, 1, &vertices, &stride, &offset);
        context->IASetIndexBuffer(indices, DXGI_FORMAT_R32_UINT, 0);
        context->VSSetShader(vertexShader, nullptr, 0);
        context->PSSetShader(pixelShader, nullptr, 0);
        context->VSSetConstantBuffers(0, 1, &constants);
        context->PSSetConstantBuffers(0, 1, &constants);
        context->DrawIndexed(static_cast<UINT>(faces.size()), 0, 0);
        context->OMSetRenderTargets(0, nullptr, nullptr);
        ID3D11Texture2D* source = target;
        if (samples > 1) {
            context->ResolveSubresource(resolved, 0, target, 0, DXGI_FORMAT_B8G8R8A8_UNORM);
            source = resolved;
        }
        context->CopyResource(staging, source);
        D3D11_MAPPED_SUBRESOURCE pixels{};
        const HRESULT hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &pixels);
        if (FAILED(hr)) return Fail("Map rendered 3D frame", hr);
        frame.bgra.resize(static_cast<std::size_t>(p.width) * p.height);
        const auto* row = static_cast<const std::uint8_t*>(pixels.pData);
        for (int y = 0; y < p.height; ++y) {
            std::memcpy(frame.bgra.data() + static_cast<std::size_t>(y) * p.width,
                        row + static_cast<std::size_t>(y) * pixels.RowPitch,
                        static_cast<std::size_t>(p.width) * 4);
        }
        context->Unmap(staging, 0);
        frame.width = p.width;
        frame.height = p.height;
        return true;
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;
bool Renderer::Initialize(bool forceWarp) { return impl_->Initialize(forceWarp); }
bool Renderer::Render(const std::vector<Vertex>& vertices, const std::vector<std::uint32_t>& indices,
                      const Params& params, Frame& frame) {
    return impl_->Render(vertices, indices, params, frame);
}
const std::string& Renderer::LastError() const { return impl_->error; }
bool Renderer::UsingWarp() const { return impl_->warp; }

} // namespace cat::render3d
