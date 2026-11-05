#include "../src/cat_3d_model.hpp"

#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) + " " #condition); } while (false)
bool Near(double a, double b, double tolerance = 1e-5) { return std::abs(a - b) < tolerance; }

struct Bytes {
    std::vector<std::uint8_t> data;
    void U32(std::uint32_t value) { for (int i = 0; i < 4; ++i) data.push_back(static_cast<std::uint8_t>(value >> (i * 8))); }
    void U16(std::uint16_t value) { data.push_back(value & 255); data.push_back(value >> 8); }
    void Float(float value) { std::uint32_t bits; std::memcpy(&bits, &value, 4); U32(bits); }
    void Text(const std::string& value) { data.insert(data.end(), value.begin(), value.end()); }
    void PatchU32(std::size_t offset, std::uint32_t value) { for (int i = 0; i < 4; ++i) data.at(offset + i) = static_cast<std::uint8_t>(value >> (i * 8)); }
    void PatchFloat(std::size_t offset, float value) { std::uint32_t bits; std::memcpy(&bits, &value, 4); PatchU32(offset, bits); }
    void Pose(float x = 0, float zRotation = 0, float xScale = 1) {
        Float(x); Float(0); Float(0);
        Float(0); Float(0); Float(std::sin(zRotation / 2)); Float(std::cos(zRotation / 2));
        Float(xScale); Float(1); Float(1);
    }
};

struct Fixture {
    Bytes bytes;
    std::size_t childBone = 0, turnFrames = 0, loopFrames = 0;
    Fixture() {
        auto& b = bytes;
        b.Text("YPC3DM01"); b.U32(1); b.U32(3); b.U32(3); b.U32(2); b.U32(3);
        const float positions[3][3] = {{2, 0, 0}, {1, 1, 0}, {1, 0, 1}};
        for (const auto& p : positions) {
            for (float v : p) b.Float(v);
            b.Float(std::sqrt(0.5f)); b.Float(std::sqrt(0.5f)); b.Float(0);
            b.Float(0); b.Float(0);
            for (int i = 0; i < 4; ++i) b.Float(1);
            b.Float(0.8f);
            b.U16(1); b.U16(0); b.U16(0); b.U16(0);
            b.Float(1); b.Float(0); b.Float(0); b.Float(0);
        }
        b.U32(0); b.U32(1); b.U32(2);
        b.U32(0xffffffffu);
        for (int i = 0; i < 16; ++i) b.Float(i % 5 == 0 ? 1.0f : 0.0f);
        b.Pose();
        childBone = b.data.size();
        b.U32(0);
        for (int i = 0; i < 16; ++i) b.Float(i % 5 == 0 ? 1.0f : i == 3 ? -1.0f : 0.0f);
        b.Pose(1);
        b.U32(4); b.Text("turn"); b.U32(2); b.Float(1); b.U32(0);
        turnFrames = b.data.size();
        b.Pose(); b.Pose(1);
        b.Pose(); b.Pose(1, 3.1415926f);
        b.U32(5); b.Text("scale"); b.U32(2); b.Float(1); b.U32(0);
        b.Pose(); b.Pose(1);
        b.Pose(); b.Pose(1, 0, 2);
        b.U32(4); b.Text("loop"); b.U32(3); b.Float(2); b.U32(1);
        loopFrames = b.data.size();
        b.Pose(); b.Pose(1);
        b.Pose(); b.Pose(1, 3.1415926f / 2);
        b.Pose(); b.Pose(1);
    }
};

void SkinningMovesAroundTheJointAndSlerpPreservesLength() {
    const Fixture f;
    cat::three_d::Model model;
    std::string error;
    CHECK(model.Load(f.bytes.data, error));
    CHECK(error.empty());
    CHECK(model.Vertices().size() == 3 && model.Indices().size() == 3 && model.Bones().size() == 2);
    CHECK(model.FindClip("turn") == 0 && model.FindClip("missing") == -1);
    CHECK(model.Duration(0) == 1 && model.Duration(999) == 0);
    CHECK(model.Bounds().minimum[0] == 1 && model.Bounds().maximum[0] == 2);
    std::vector<cat::three_d::RenderVertex> v;
    CHECK(model.Sample(0, 0, v, &error));
    CHECK(Near(v[0].position[0], 2) && Near(v[0].position[1], 0));
    CHECK(model.Sample(0, 0.5, v, &error));
    CHECK(Near(v[0].position[0], 1) && Near(v[0].position[1], 1));
    CHECK(Near(v[0].normal[0], -std::sqrt(0.5)) && Near(v[0].normal[1], std::sqrt(0.5)));
    CHECK(model.SampleBlended(0, 0, 0, 1, 0.5f, v, &error));
    CHECK(Near(v[0].position[0], 1) && Near(v[0].position[1], 1));
    for (int i = 0; i <= 120; ++i) {
        CHECK(model.Sample(0, i / 120.0, v));
        const double x = v[0].position[0] - 1, y = v[0].position[1];
        CHECK(Near(x * x + y * y, 1));
    }
    CHECK(model.Sample(0, 123456, v));
    CHECK(Near(v[0].position[0], 0));
    CHECK(model.Sample(0, -1, v));
    CHECK(Near(v[0].position[0], 2));
    CHECK(!model.Sample(99, 1, v, &error) && !error.empty());
    CHECK(!model.Sample(0, std::numeric_limits<double>::infinity(), v));
    CHECK(!model.SampleBlended(0, 0, 1, 0, std::numeric_limits<float>::quiet_NaN(), v));
}

void NonuniformScaleKeepsNormalPerpendicularToSurface() {
    const Fixture f;
    cat::three_d::Model model;
    std::string error;
    CHECK(model.Load(f.bytes.data, error));
    std::vector<cat::three_d::RenderVertex> v;
    CHECK(model.Sample(1, 1, v));
    CHECK(Near(v[0].position[0], 3));
    CHECK(Near(v[0].normal[0], 1 / std::sqrt(5.0)));
    CHECK(Near(v[0].normal[1], 2 / std::sqrt(5.0)));
    CHECK(Near(v[0].normal[0] * 2 - v[0].normal[1], 0));
    // Check the normal against the actual transformed triangle tangent.
    const double dx = v[1].position[0] - v[0].position[0], dy = v[1].position[1] - v[0].position[1];
    CHECK(Near(v[0].normal[0] * dx + v[0].normal[1] * dy, 0));
}

void LoopsWrapWithoutPoseDiscontinuities() {
    const Fixture f;
    cat::three_d::Model model;
    std::string error;
    CHECK(model.Load(f.bytes.data, error));
    std::vector<cat::three_d::RenderVertex> a, b;
    CHECK(model.Sample(2, 0.25, a));
    CHECK(model.Sample(2, 27.25, b));
    for (int axis = 0; axis < 3; ++axis) CHECK(Near(a[0].position[axis], b[0].position[axis]));
    CHECK(model.Sample(2, 0.9999999, a));
    CHECK(model.Sample(2, 1.0000001, b));
    for (int axis = 0; axis < 3; ++axis) CHECK(Near(a[0].position[axis], b[0].position[axis]));
    CHECK(model.Sample(2, 1, a));
    CHECK(Near(a[0].position[0], 2));
}

void InvalidDataNeverReplacesTheLastValidModel() {
    const Fixture original;
    cat::three_d::Model model;
    std::string error;
    CHECK(model.Load(original.bytes.data, error));
    for (std::size_t size = 0; size < original.bytes.data.size(); ++size) {
        CHECK(!model.Load(original.bytes.data.data(), size, error));
        CHECK(!error.empty());
        CHECK(model.FindClip("turn") == 0 && model.Vertices().size() == 3);
    }
    auto rejects = [&](const Bytes& bytes) {
        CHECK(!model.Load(bytes.data, error));
        CHECK(!error.empty());
        CHECK(model.FindClip("turn") == 0);
    };
    for (const auto& patch : std::vector<std::pair<std::size_t, std::uint32_t>>{
        {8, 2}, {12, 0xffffffffu}, {16, 2}, {20, 0}, {24, 65},
        {28 + 3 * 76, 3}, // Triangle index.
        {original.childBone, 1}, // Parent cycle/self-reference.
        {original.childBone, 0xffffffffu}, // Second root.
        {28 + 52, 0xffffu}, // Joint index.
        {original.turnFrames - 8, 0}, // Invalid fps.
        {original.turnFrames - 4, 3}}) { // Unsupported loop flag.
        auto corrupt = original.bytes; corrupt.PatchU32(patch.first, patch.second); rejects(corrupt);
    }
    for (const auto& patch : std::vector<std::pair<std::size_t, float>>{
        {28, std::numeric_limits<float>::quiet_NaN()},
        {28 + 48, 1.5f}, // Roughness.
        {28 + 60, 0.5f}, // Weight sum.
        {original.childBone + 4 + 12, 0}, // Incorrect inverse bind translation.
        {original.turnFrames + 3 * 40 + 28, 0}, // Singular animation scale.
        {original.loopFrames + 4 * 40, 0.5f}}) { // Loop root endpoint differs.
        auto corrupt = original.bytes; corrupt.PatchFloat(patch.first, patch.second); rejects(corrupt);
    }
    auto trailing = original.bytes; trailing.data.push_back(0); rejects(trailing);
    auto signature = original.bytes; signature.data[0] = 'X'; rejects(signature);
    CHECK(!model.Load(nullptr, 100, error));
    CHECK(!model.Load(original.bytes.data.data(), cat::three_d::Model::kMaximumBytes + 1, error));
    CHECK(!model.LoadFile("this-model-does-not-exist.yp3d", error));
    CHECK(model.Vertices().size() == 3);
}

} // namespace

int main() {
    try {
        SkinningMovesAroundTheJointAndSlerpPreservesLength();
        NonuniformScaleKeepsNormalPerpendicularToSurface();
        LoopsWrapWithoutPoseDiscontinuities();
        InvalidDataNeverReplacesTheLastValidModel();
        std::cout << "cat_3d_model: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
