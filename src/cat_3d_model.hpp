#pragma once

// Portable, opt-in skeletal preview asset. This format never reads or writes
// pet preferences. All disk fields are decoded explicitly as little-endian;
// the in-memory C++ layout is not the file format.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace cat::three_d {

struct RenderVertex {
    float position[3]{};
    float normal[3]{0, 0, 1};
    float uv[2]{};
    float color[4]{1, 1, 1, 1}; // Linear RGB, alpha coverage.
    float roughness = 0.8f;
};
static_assert(sizeof(RenderVertex) == 52, "Renderer vertex contract changed");

struct Vertex : RenderVertex {
    std::uint16_t joints[4]{};
    float weights[4]{1, 0, 0, 0};
};

struct Transform {
    float translation[3]{};
    float rotation[4]{0, 0, 0, 1}; // Quaternion xyzw.
    float scale[3]{1, 1, 1};
};

struct Matrix {
    // Row-major storage, column vectors: translation lives at 3, 7 and 11.
    std::array<float, 16> values{1, 0, 0, 0, 0, 1, 0, 0,
                                0, 0, 1, 0, 0, 0, 0, 1};
};

struct Bone {
    std::int32_t parent = -1;
    Matrix inverseBind;
    Transform rest;
};

struct Clip {
    std::string name;
    std::uint32_t frameCount = 0;
    float fps = 0;
    bool loop = false;
    std::vector<Transform> frames; // frame-major, then parent-first bone order.
    double Duration() const { return frameCount > 1 && fps > 0 ? (frameCount - 1) / double(fps) : 0; }
};

struct Bounds3 {
    std::array<float, 3> minimum{};
    std::array<float, 3> maximum{};
};

inline Bounds3 BoundsOf(const std::vector<RenderVertex>& vertices) {
    Bounds3 result;
    if (vertices.empty()) return result;
    for (int axis = 0; axis < 3; ++axis) result.minimum[axis] = result.maximum[axis] = vertices[0].position[axis];
    for (const auto& vertex : vertices) for (int axis = 0; axis < 3; ++axis) {
        result.minimum[axis] = std::min(result.minimum[axis], vertex.position[axis]);
        result.maximum[axis] = std::max(result.maximum[axis], vertex.position[axis]);
    }
    return result;
}

namespace model_detail {

inline Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix out;
    for (int row = 0; row < 4; ++row) for (int col = 0; col < 4; ++col) {
        double sum = 0;
        for (int k = 0; k < 4; ++k) sum += double(a.values[row * 4 + k]) * b.values[k * 4 + col];
        out.values[row * 4 + col] = static_cast<float>(sum);
    }
    return out;
}

inline Matrix ToMatrix(const Transform& t) {
    const float x = t.rotation[0], y = t.rotation[1], z = t.rotation[2], w = t.rotation[3];
    Matrix result;
    result.values = {
        (1 - 2 * (y * y + z * z)) * t.scale[0], 2 * (x * y - z * w) * t.scale[1],
        2 * (x * z + y * w) * t.scale[2], t.translation[0],
        2 * (x * y + z * w) * t.scale[0], (1 - 2 * (x * x + z * z)) * t.scale[1],
        2 * (y * z - x * w) * t.scale[2], t.translation[1],
        2 * (x * z - y * w) * t.scale[0], 2 * (y * z + x * w) * t.scale[1],
        (1 - 2 * (x * x + y * y)) * t.scale[2], t.translation[2], 0, 0, 0, 1};
    return result;
}

inline Transform Interpolate(const Transform& a, const Transform& b, float weight) {
    Transform result;
    const float t = std::clamp(weight, 0.0f, 1.0f);
    for (int axis = 0; axis < 3; ++axis) {
        result.translation[axis] = a.translation[axis] + t * (b.translation[axis] - a.translation[axis]);
        result.scale[axis] = a.scale[axis] + t * (b.scale[axis] - a.scale[axis]);
    }
    double dot = 0;
    for (int axis = 0; axis < 4; ++axis) dot += double(a.rotation[axis]) * b.rotation[axis];
    const float sign = dot < 0 ? -1.0f : 1.0f;
    dot = std::clamp(std::abs(dot), 0.0, 1.0);
    double from = 1 - t, to = t;
    if (dot < 0.9995) {
        const double angle = std::acos(dot), denominator = std::sin(angle);
        from = std::sin((1 - t) * angle) / denominator;
        to = std::sin(t * angle) / denominator;
    }
    double lengthSquared = 0;
    for (int axis = 0; axis < 4; ++axis) {
        result.rotation[axis] = static_cast<float>(from * a.rotation[axis] + to * sign * b.rotation[axis]);
        lengthSquared += double(result.rotation[axis]) * result.rotation[axis];
    }
    const double inverseLength = 1 / std::sqrt(lengthSquared);
    for (auto& value : result.rotation) value = static_cast<float>(value * inverseLength);
    return result;
}

class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t length) : data_(data), length_(length) {}
    std::size_t Remaining() const { return length_ - position_; }
    bool U32(std::uint32_t& value) {
        if (Remaining() < 4) return false;
        value = std::uint32_t(data_[position_]) | (std::uint32_t(data_[position_ + 1]) << 8) |
                (std::uint32_t(data_[position_ + 2]) << 16) | (std::uint32_t(data_[position_ + 3]) << 24);
        position_ += 4;
        return true;
    }
    bool I32(std::int32_t& value) {
        std::uint32_t bits;
        if (!U32(bits)) return false;
        std::memcpy(&value, &bits, 4);
        return true;
    }
    bool U16(std::uint16_t& value) {
        if (Remaining() < 2) return false;
        value = std::uint16_t(data_[position_]) | std::uint16_t(std::uint16_t(data_[position_ + 1]) << 8);
        position_ += 2;
        return true;
    }
    bool F32(float& value) {
        std::uint32_t bits;
        if (!U32(bits)) return false;
        std::memcpy(&value, &bits, 4);
        return std::isfinite(value);
    }
    bool Bytes(void* output, std::size_t count) {
        if (count > Remaining()) return false;
        if (count) std::memcpy(output, data_ + position_, count);
        position_ += count;
        return true;
    }
private:
    const std::uint8_t* data_;
    std::size_t length_, position_ = 0;
};

inline bool ReadTransform(Reader& reader, Transform& t) {
    for (auto& value : t.translation) if (!reader.F32(value) || std::abs(value) > 10000) return false;
    double squared = 0;
    for (auto& value : t.rotation) {
        if (!reader.F32(value)) return false;
        squared += double(value) * value;
    }
    if (std::abs(squared - 1) > 0.005) return false;
    for (auto& value : t.rotation) value = static_cast<float>(value / std::sqrt(squared));
    for (auto& value : t.scale) if (!reader.F32(value) || value < 0.0001f || value > 100) return false;
    return true;
}

inline bool SamePose(const Transform& a, const Transform& b) {
    for (int axis = 0; axis < 3; ++axis)
        if (std::abs(a.translation[axis] - b.translation[axis]) > 0.002f ||
            std::abs(a.scale[axis] - b.scale[axis]) > 0.002f) return false;
    double dot = 0;
    for (int axis = 0; axis < 4; ++axis) dot += double(a.rotation[axis]) * b.rotation[axis];
    return std::abs(dot) > 0.9999;
}

inline bool ValidName(const std::string& name) {
    // UTF-8 with no controls, embedded NUL, slash, or backslash.
    std::size_t offset = 0;
    while (offset < name.size()) {
        const auto c = static_cast<unsigned char>(name[offset++]);
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return false;
            continue;
        }
        int count;
        std::uint32_t code, minimum;
        if ((c & 0xe0) == 0xc0) { count = 1; code = c & 0x1f; minimum = 0x80; }
        else if ((c & 0xf0) == 0xe0) { count = 2; code = c & 0x0f; minimum = 0x800; }
        else if ((c & 0xf8) == 0xf0) { count = 3; code = c & 0x07; minimum = 0x10000; }
        else return false;
        for (int i = 0; i < count; ++i) {
            if (offset == name.size()) return false;
            const auto next = static_cast<unsigned char>(name[offset++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return !name.empty();
}

} // namespace model_detail

class Model {
public:
    static constexpr std::size_t kMaximumBytes = 128u * 1024u * 1024u;
    static constexpr std::uint32_t kMaximumVertices = 200000, kMaximumIndices = 1200000;
    static constexpr std::uint32_t kMaximumBones = 128, kMaximumClips = 64, kMaximumFrames = 4096;

    // An invalid reload leaves a previously loaded, valid model intact.
    bool Load(const std::uint8_t* bytes, std::size_t size, std::string& error) {
        error.clear();
        const auto fail = [&](const char* why) { error = why; return false; };
        if (!bytes || size < 28 || size > kMaximumBytes) return fail("3D asset size is invalid");
        model_detail::Reader reader(bytes, size);
        char magic[8];
        std::uint32_t version, vertexCount, indexCount, boneCount, clipCount;
        if (!reader.Bytes(magic, 8) || std::memcmp(magic, "YPC3DM01", 8) != 0 ||
            !reader.U32(version) || version != 1 || !reader.U32(vertexCount) ||
            !reader.U32(indexCount) || !reader.U32(boneCount) || !reader.U32(clipCount))
            return fail("3D asset header/version is invalid");
        if (vertexCount < 3 || vertexCount > kMaximumVertices || indexCount < 3 ||
            indexCount > kMaximumIndices || indexCount % 3 || boneCount == 0 ||
            boneCount > kMaximumBones || clipCount == 0 || clipCount > kMaximumClips)
            return fail("3D asset counts exceed supported limits");
        const std::uint64_t required = std::uint64_t(vertexCount) * 76 + std::uint64_t(indexCount) * 4 +
                                       std::uint64_t(boneCount) * 108 + std::uint64_t(clipCount) * 17;
        if (required > reader.Remaining()) return fail("3D asset is truncated");
        Model candidate;
        candidate.vertices_.resize(vertexCount);
        for (auto& vertex : candidate.vertices_) {
            for (auto& v : vertex.position) if (!reader.F32(v) || std::abs(v) > 10000) return fail("Invalid vertex position");
            double normalLength = 0;
            for (auto& v : vertex.normal) {
                if (!reader.F32(v) || std::abs(v) > 2) return fail("Invalid vertex normal");
                normalLength += double(v) * v;
            }
            if (normalLength < 1e-8) return fail("Zero vertex normal");
            for (auto& v : vertex.normal) v = static_cast<float>(v / std::sqrt(normalLength));
            for (auto& v : vertex.uv) if (!reader.F32(v) || std::abs(v) > 10000) return fail("Invalid vertex UV");
            for (auto& v : vertex.color) if (!reader.F32(v) || v < 0 || v > 1) return fail("Invalid vertex color");
            if (!reader.F32(vertex.roughness) || vertex.roughness < 0 || vertex.roughness > 1)
                return fail("Invalid material roughness");
            for (auto& v : vertex.joints) if (!reader.U16(v) || v >= boneCount) return fail("Vertex joint is out of range");
            double sum = 0;
            for (auto& v : vertex.weights) {
                if (!reader.F32(v) || v < 0 || v > 1) return fail("Invalid skin weight");
                sum += v;
            }
            if (std::abs(sum - 1) > 0.001) return fail("Skin weights must sum to one");
            for (auto& v : vertex.weights) v = static_cast<float>(v / sum);
        }
        candidate.indices_.resize(indexCount);
        for (auto& index : candidate.indices_) if (!reader.U32(index) || index >= vertexCount)
            return fail("Triangle index is out of range");
        candidate.bones_.resize(boneCount);
        std::vector<Matrix> global(boneCount);
        for (std::uint32_t i = 0; i < boneCount; ++i) {
            auto& bone = candidate.bones_[i];
            if (!reader.I32(bone.parent) || (i == 0 ? bone.parent != -1 : bone.parent < 0 || std::uint32_t(bone.parent) >= i))
                return fail("Skeleton must have one root and parent-first ordering");
            for (auto& v : bone.inverseBind.values)
                if (!reader.F32(v) || std::abs(v) > 1000000) return fail("Invalid inverse bind matrix");
            if (std::abs(bone.inverseBind.values[12]) > 1e-5 || std::abs(bone.inverseBind.values[13]) > 1e-5 ||
                std::abs(bone.inverseBind.values[14]) > 1e-5 || std::abs(bone.inverseBind.values[15] - 1) > 1e-5)
                return fail("Inverse bind matrix is not affine");
            if (!model_detail::ReadTransform(reader, bone.rest)) return fail("Invalid bone rest transform");
            const Matrix local = model_detail::ToMatrix(bone.rest);
            global[i] = bone.parent < 0 ? local : model_detail::Multiply(global[bone.parent], local);
            const auto identity = model_detail::Multiply(global[i], bone.inverseBind);
            for (std::size_t k = 0; k < 16; ++k)
                if (!std::isfinite(identity.values[k]) || std::abs(identity.values[k] - (k % 5 == 0 ? 1.0f : 0.0f)) > 0.005f)
                    return fail("Inverse bind matrix does not match rest skeleton");
        }
        candidate.clips_.resize(clipCount);
        for (std::uint32_t clipIndex = 0; clipIndex < clipCount; ++clipIndex) {
            auto& clip = candidate.clips_[clipIndex];
            std::uint32_t nameLength, loop;
            if (!reader.U32(nameLength) || nameLength == 0 || nameLength > 63 || nameLength > reader.Remaining())
                return fail("Invalid animation name length");
            clip.name.resize(nameLength);
            if (!reader.Bytes(clip.name.data(), nameLength) || !model_detail::ValidName(clip.name))
                return fail("Invalid animation name");
            for (std::uint32_t previous = 0; previous < clipIndex; ++previous)
                if (candidate.clips_[previous].name == clip.name) return fail("Duplicate animation name");
            if (!reader.U32(clip.frameCount) || clip.frameCount < 2 || clip.frameCount > kMaximumFrames ||
                !reader.F32(clip.fps) || clip.fps < 1 || clip.fps > 240 || !reader.U32(loop) || loop > 1)
                return fail("Invalid animation frame metadata");
            clip.loop = loop != 0;
            const std::uint64_t transformCount = std::uint64_t(clip.frameCount) * boneCount;
            if (transformCount * 40 > reader.Remaining()) return fail("Animation frames are truncated");
            clip.frames.resize(static_cast<std::size_t>(transformCount));
            for (auto& frame : clip.frames)
                if (!model_detail::ReadTransform(reader, frame)) return fail("Invalid animation transform");
            if (clip.loop) for (std::size_t i = 0; i < boneCount; ++i)
                if (!model_detail::SamePose(clip.frames[i], clip.frames[(clip.frameCount - 1) * boneCount + i]))
                    return fail("Loop animation endpoints do not match");
        }
        if (reader.Remaining() != 0) return fail("Unexpected trailing 3D asset data");
        for (int axis = 0; axis < 3; ++axis)
            candidate.bounds_.minimum[axis] = candidate.bounds_.maximum[axis] = candidate.vertices_[0].position[axis];
        for (const auto& vertex : candidate.vertices_) for (int axis = 0; axis < 3; ++axis) {
            candidate.bounds_.minimum[axis] = std::min(candidate.bounds_.minimum[axis], vertex.position[axis]);
            candidate.bounds_.maximum[axis] = std::max(candidate.bounds_.maximum[axis], vertex.position[axis]);
        }
        *this = std::move(candidate);
        return true;
    }

    bool Load(const std::vector<std::uint8_t>& bytes, std::string& error) { return Load(bytes.data(), bytes.size(), error); }

    bool LoadFile(const std::filesystem::path& path, std::string& error) {
        error.clear();
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) { error = "Unable to open 3D asset"; return false; }
        const auto length = stream.tellg();
        if (length < 0 || static_cast<std::uint64_t>(length) > kMaximumBytes) {
            error = "3D asset file exceeds size limit";
            return false;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        stream.seekg(0);
        if (!bytes.empty() && !stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
            error = "Unable to read complete 3D asset";
            return false;
        }
        return Load(bytes, error);
    }

    const std::vector<Vertex>& Vertices() const { return vertices_; }
    const std::vector<std::uint32_t>& Indices() const { return indices_; }
    const std::vector<Bone>& Bones() const { return bones_; }
    const std::vector<Clip>& Clips() const { return clips_; }
    const Bounds3& Bounds() const { return bounds_; }
    bool Empty() const { return vertices_.empty(); }
    int FindClip(const std::string& name) const {
        for (std::size_t i = 0; i < clips_.size(); ++i) if (clips_[i].name == name) return static_cast<int>(i);
        return -1;
    }
    double Duration(std::size_t clip) const { return clip < clips_.size() ? clips_[clip].Duration() : 0; }

    bool Sample(std::size_t clip, double seconds, std::vector<RenderVertex>& output, std::string* error = nullptr) const {
        if (error) error->clear();
        std::vector<Transform> pose;
        if (!SamplePose(clip, seconds, pose, error)) return false;
        return Skin(pose, output, error);
    }

    bool SampleBlended(std::size_t from, double fromSeconds, std::size_t to, double toSeconds,
                       float blend, std::vector<RenderVertex>& output, std::string* error = nullptr) const {
        if (error) error->clear();
        if (!std::isfinite(blend)) return Error(error, "Animation blend is not finite");
        std::vector<Transform> a, b;
        if (!SamplePose(from, fromSeconds, a, error) || !SamplePose(to, toSeconds, b, error)) return false;
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = model_detail::Interpolate(a[i], b[i], blend);
        return Skin(a, output, error);
    }

private:
    static bool Error(std::string* error, const char* why) { if (error) *error = why; return false; }

    bool SamplePose(std::size_t clipIndex, double seconds, std::vector<Transform>& pose, std::string* error) const {
        if (clipIndex >= clips_.size() || bones_.empty()) return Error(error, "Animation index is invalid");
        if (!std::isfinite(seconds)) return Error(error, "Animation time is not finite");
        const auto& clip = clips_[clipIndex];
        const double duration = clip.Duration();
        double time = std::max(0.0, seconds);
        if (clip.loop) time = std::fmod(time, duration);
        else time = std::min(time, duration);
        const double frame = std::clamp(time * clip.fps, 0.0, double(clip.frameCount - 1));
        const auto first = static_cast<std::size_t>(frame);
        const auto second = std::min(first + 1, std::size_t(clip.frameCount - 1));
        const auto weight = static_cast<float>(frame - first);
        pose.resize(bones_.size());
        for (std::size_t i = 0; i < bones_.size(); ++i)
            pose[i] = model_detail::Interpolate(clip.frames[first * bones_.size() + i],
                                               clip.frames[second * bones_.size() + i], weight);
        return true;
    }

    bool Skin(const std::vector<Transform>& pose, std::vector<RenderVertex>& output, std::string* error) const {
        std::vector<Matrix> world(bones_.size()), skin(bones_.size());
        for (std::size_t i = 0; i < bones_.size(); ++i) {
            const Matrix local = model_detail::ToMatrix(pose[i]);
            world[i] = bones_[i].parent < 0 ? local : model_detail::Multiply(world[bones_[i].parent], local);
            skin[i] = model_detail::Multiply(world[i], bones_[i].inverseBind);
        }
        output.resize(vertices_.size());
        for (std::size_t i = 0; i < vertices_.size(); ++i) {
            const auto& source = vertices_[i];
            auto& target = output[i];
            target = static_cast<const RenderVertex&>(source);
            // Blend matrices first. Normals use the inverse transpose of the
            // actual blended transform, including nonuniform bone scales.
            double m[12]{};
            for (int joint = 0; joint < 4; ++joint) {
                if (source.weights[joint] == 0) continue;
                const auto& transform = skin[source.joints[joint]].values;
                for (int k = 0; k < 12; ++k) m[k] += double(transform[k]) * source.weights[joint];
            }
            for (int axis = 0; axis < 3; ++axis) {
                const int row = axis * 4;
                const double value = m[row] * source.position[0] + m[row + 1] * source.position[1] +
                                     m[row + 2] * source.position[2] + m[row + 3];
                if (!std::isfinite(value) || std::abs(value) > 1e8) return Error(error, "Skinned position exceeds limits");
                target.position[axis] = static_cast<float>(value);
            }
            const double cofactors[9] = {
                m[5] * m[10] - m[6] * m[9], m[6] * m[8] - m[4] * m[10], m[4] * m[9] - m[5] * m[8],
                m[2] * m[9] - m[1] * m[10], m[0] * m[10] - m[2] * m[8], m[1] * m[8] - m[0] * m[9],
                m[1] * m[6] - m[2] * m[5], m[2] * m[4] - m[0] * m[6], m[0] * m[5] - m[1] * m[4]};
            const double determinant = m[0] * cofactors[0] + m[1] * cofactors[1] + m[2] * cofactors[2];
            if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12)
                return Error(error, "Skinning encountered a singular joint transform");
            double normal[3], lengthSquared = 0;
            for (int axis = 0; axis < 3; ++axis) {
                normal[axis] = (cofactors[axis * 3] * source.normal[0] + cofactors[axis * 3 + 1] * source.normal[1] +
                                cofactors[axis * 3 + 2] * source.normal[2]) / determinant;
                lengthSquared += normal[axis] * normal[axis];
            }
            if (!std::isfinite(lengthSquared) || lengthSquared < 1e-20) return Error(error, "Invalid skinned normal");
            const double inverseLength = 1 / std::sqrt(lengthSquared);
            for (int axis = 0; axis < 3; ++axis) target.normal[axis] = static_cast<float>(normal[axis] * inverseLength);
        }
        return true;
    }

    std::vector<Vertex> vertices_;
    std::vector<std::uint32_t> indices_;
    std::vector<Bone> bones_;
    std::vector<Clip> clips_;
    Bounds3 bounds_;
};

} // namespace cat::three_d
