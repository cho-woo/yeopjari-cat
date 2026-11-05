// Numeric inspection of the actual exported preview asset through the same
// portable loader and skinning path used by the Windows renderer.
// g++ -std=c++17 -O2 tools/audit_3d_model.cpp -o build/audit_3d_model
// build/audit_3d_model model.bin report.json [--skip FIRST COUNT] [--head-bone INDEX]
//   [--group NAME JOINT,JOINT,...]
#include "../src/cat_3d_model.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using cat::three_d::RenderVertex;
struct Range { std::size_t first, count; };
struct JointGroup { std::string name; std::vector<std::size_t> joints, vertices; };
struct Stats {
    double minimumZ = 1e9, structuralMinimumZ = 1e9, highestContact = -1e9;
    double normalError = 0, maximumStep = 0, worstTime = 0, maxHeadScaleError = 0;
    std::size_t samples = 0;
};
struct FootProbe {
    std::vector<std::size_t> sole;
    double offset = 0, previousWorldY = 0, previousPhase = 0;
    bool previousStance = false;
};

std::string Escape(const std::string& value) {
    std::ostringstream output;
    for (const auto c : value) {
        if (c == '"' || c == '\\') output << '\\' << c;
        else if (c == '\n') output << "\\n";
        else if (static_cast<unsigned char>(c) >= 32) output << c;
    }
    return output.str();
}

bool Excluded(std::size_t index, const std::vector<Range>& ranges) {
    for (const auto& range : ranges) if (index >= range.first && index - range.first < range.count) return true;
    return false;
}

double Inspect(const std::vector<RenderVertex>& vertices, const std::vector<RenderVertex>& previous,
               double time, const std::vector<Range>& skipped, Stats& stats) {
    double structuralMin = 1e9;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const auto& v = vertices[i];
        for (const auto p : v.position) if (!std::isfinite(p)) throw std::runtime_error("nonfinite skinned position");
        for (const auto n : v.normal) if (!std::isfinite(n)) throw std::runtime_error("nonfinite skinned normal");
        stats.normalError = std::max(stats.normalError,
            std::abs(std::sqrt(double(v.normal[0]) * v.normal[0] + double(v.normal[1]) * v.normal[1] +
                               double(v.normal[2]) * v.normal[2]) - 1));
        stats.minimumZ = std::min(stats.minimumZ, double(v.position[2]));
        if (!Excluded(i, skipped)) structuralMin = std::min(structuralMin, double(v.position[2]));
        if (previous.size() == vertices.size()) {
            double distance = 0;
            for (int axis = 0; axis < 3; ++axis) distance += std::pow(double(v.position[axis]) - previous[i].position[axis], 2);
            stats.maximumStep = std::max(stats.maximumStep, std::sqrt(distance));
        }
    }
    if (structuralMin < stats.structuralMinimumZ) { stats.structuralMinimumZ = structuralMin; stats.worstTime = time; }
    stats.highestContact = std::max(stats.highestContact, structuralMin);
    ++stats.samples;
    return structuralMin;
}

double HeadScaleError(const cat::three_d::Model& model, const cat::three_d::Clip& clip,
                      double time, std::size_t head) {
    if (head >= model.Bones().size()) return 0;
    const double duration = clip.Duration();
    const double localTime = clip.loop ? std::fmod(time, duration) : std::min(time, duration);
    const double frame = std::clamp(localTime * clip.fps, 0.0, double(clip.frameCount - 1));
    const auto from = static_cast<std::size_t>(frame), to = std::min(from + 1, std::size_t(clip.frameCount - 1));
    std::vector<cat::three_d::Matrix> world(model.Bones().size());
    for (std::size_t i = 0; i <= head; ++i) {
        const auto pose = cat::three_d::model_detail::Interpolate(
            clip.frames[from * model.Bones().size() + i], clip.frames[to * model.Bones().size() + i],
            static_cast<float>(frame - from));
        const auto local = cat::three_d::model_detail::ToMatrix(pose);
        world[i] = model.Bones()[i].parent < 0 ? local : cat::three_d::model_detail::Multiply(world[model.Bones()[i].parent], local);
    }
    const auto skin = cat::three_d::model_detail::Multiply(world[head], model.Bones()[head].inverseBind);
    double error = 0;
    for (int column = 0; column < 3; ++column) {
        double squared = 0;
        for (int row = 0; row < 3; ++row) squared += double(skin.values[row * 4 + column]) * skin.values[row * 4 + column];
        error = std::max(error, std::abs(std::sqrt(squared) - 1));
    }
    return error;
}

void PrintStats(std::ostream& output, const Stats& stats) {
    output << "{\"samples\":" << stats.samples << ",\"all_vertices_min_z\":" << stats.minimumZ
           << ",\"structural_min_z\":" << stats.structuralMinimumZ << ",\"highest_structural_contact\":" << stats.highestContact
           << ",\"worst_contact_time\":" << stats.worstTime << ",\"normal_length_error\":" << stats.normalError
           << ",\"max_vertex_step_between_samples\":" << stats.maximumStep
           << ",\"head_scale_error\":" << stats.maxHeadScaleError << '}';
}
} // namespace

int main(int argc, char** argv) {
    std::ostringstream report;
    report << std::setprecision(9);
    try {
        if (argc < 3) throw std::runtime_error("Usage: audit_3d_model model.bin report.json [--skip FIRST COUNT] [--head-bone INDEX]");
        const auto start = std::chrono::steady_clock::now();
        std::vector<Range> skipped;
        std::vector<JointGroup> groups;
        std::size_t headBone = std::numeric_limits<std::size_t>::max();
        bool requireBellyHeadContact = false, requireBellyBodyContact = false;
        for (int i = 3; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--skip" && i + 2 < argc) {
                const auto first = std::stoull(argv[++i]), count = std::stoull(argv[++i]);
                skipped.push_back({static_cast<std::size_t>(first), static_cast<std::size_t>(count)});
            } else if (argument == "--head-bone" && i + 1 < argc) headBone = static_cast<std::size_t>(std::stoull(argv[++i]));
            else if (argument == "--group" && i + 2 < argc) {
                JointGroup group;
                group.name = argv[++i];
                std::stringstream list(argv[++i]);
                std::string item;
                while (std::getline(list, item, ',')) group.joints.push_back(static_cast<std::size_t>(std::stoull(item)));
                if (group.joints.empty()) throw std::runtime_error("Empty joint group");
                groups.push_back(std::move(group));
            }
            else if (argument == "--require-belly-head-contact") requireBellyHeadContact = true;
            else if (argument == "--require-belly-body-contact") requireBellyBodyContact = true;
            else throw std::runtime_error("Invalid audit option");
        }
        cat::three_d::Model model;
        std::string error;
        if (!model.LoadFile(argv[1], error)) throw std::runtime_error(error);
        for (const auto& range : skipped)
            if (range.first >= model.Vertices().size() || range.count > model.Vertices().size() - range.first)
                throw std::runtime_error("Excluded vertex range is out of bounds");
        if (headBone != std::numeric_limits<std::size_t>::max() && headBone >= model.Bones().size())
            throw std::runtime_error("Head bone is out of bounds");
        for (auto& group : groups) {
            for (const auto joint : group.joints) if (joint >= model.Bones().size()) throw std::runtime_error("Group joint is out of bounds");
            for (std::size_t i = 0; i < model.Vertices().size(); ++i) {
                if (Excluded(i, skipped)) continue;
                const auto& vertex = model.Vertices()[i];
                float weight = 0;
                for (int influence = 0; influence < 4; ++influence)
                    if (std::find(group.joints.begin(), group.joints.end(), vertex.joints[influence]) != group.joints.end())
                        weight += vertex.weights[influence];
                if (weight >= 0.8f) group.vertices.push_back(i);
            }
            if (group.vertices.empty()) throw std::runtime_error("Joint group has no structural vertices: " + group.name);
        }
        report << "{\n\"loader_passed\":true,\"vertices\":" << model.Vertices().size()
               << ",\"triangles\":" << model.Indices().size() / 3 << ",\"bones\":" << model.Bones().size()
               << ",\"excluded_decorative_ranges\":" << skipped.size()
               << ",\"head_bone_checked\":" << (headBone == std::numeric_limits<std::size_t>::max() ? -1 : static_cast<int>(headBone))
               << ",\n\"clips\":{\n";
        bool geometryPassed = true, contactPassed = true, stancePassed = true, bellyHeadPassed = true;
        bool bellyBodyPassed = true, airborneContactPassed = true;
        std::size_t bellyHeadSamples = 0, bellyBodySamples = 0, airborneContactSamples = 0;
        std::vector<RenderVertex> vertices, previous;
        for (std::size_t clipIndex = 0; clipIndex < model.Clips().size(); ++clipIndex) {
            const auto& clip = model.Clips()[clipIndex];
            Stats stats;
            const bool walking = clip.name == "walk" || clip.name == "run" || clip.name == "stalk";
            const double stride = clip.name == "run" ? .72 : clip.name == "stalk" ? .36 : .48;
            const double duty = clip.name == "run" ? .55 : clip.name == "stalk" ? .78 : .68;
            double maxStanceDrift = 0, maxRelativeStanceSpeed = 0;
            std::size_t stanceChecks = 0;
            std::vector<FootProbe> feet;
            if (walking) for (const auto& group : groups) {
                if (group.name != "front_left" && group.name != "front_right" &&
                    group.name != "back_left" && group.name != "back_right") continue;
                FootProbe foot;
                if (clip.name == "run") foot.offset = group.name == "back_left" || group.name == "front_right" ? 0 : .5;
                else foot.offset = group.name == "back_left" ? 0 : group.name == "front_left" ? .25 : group.name == "back_right" ? .5 : .75;
                float low = 1e9f;
                for (const auto index : group.vertices) low = std::min(low, model.Vertices()[index].position[2]);
                for (const auto index : group.vertices) if (model.Vertices()[index].position[2] <= low + .006f) foot.sole.push_back(index);
                if (!foot.sole.empty()) feet.push_back(std::move(foot));
            }
            previous.clear();
            for (std::uint32_t sample = 0; sample <= (clip.frameCount - 1) * 2; ++sample) {
                const double time = sample / (2.0 * clip.fps);
                if (!model.Sample(clipIndex, time, vertices, &error)) throw std::runtime_error(clip.name + ": " + error);
                const double structuralMin = Inspect(vertices, previous, time, skipped, stats);
                stats.maxHeadScaleError = std::max(stats.maxHeadScaleError, HeadScaleError(model, clip, time, headBone));
                const double phase = time / clip.Duration();
                // The host clamps the airborne pose to phases .05..95 as
                // vertical speed changes. Feet must approach physical contact
                // near these endpoints, not jump downward on the landing tick.
                if (clip.name == "airborne" && (phase <= .051 || phase >= .949)) {
                    airborneContactPassed = airborneContactPassed && std::abs(structuralMin) <= .015;
                    ++airborneContactSamples;
                }
                for (auto& foot : feet) {
                    const double footPhase = std::fmod(phase + foot.offset, 1.0);
                    const bool stance = footPhase < duty;
                    double y = 0;
                    for (const auto index : foot.sole) y += vertices[index].position[1];
                    const double worldY = y / foot.sole.size() - stride * phase;
                    if (stance && foot.previousStance && phase > foot.previousPhase) {
                        const double drift = std::abs(worldY - foot.previousWorldY);
                        maxStanceDrift = std::max(maxStanceDrift, drift);
                        maxRelativeStanceSpeed = std::max(maxRelativeStanceSpeed, drift / (stride * (phase - foot.previousPhase)));
                        ++stanceChecks;
                    }
                    foot.previousStance = stance;
                    foot.previousWorldY = worldY;
                    foot.previousPhase = phase;
                }
                previous.swap(vertices);
            }
            geometryPassed = geometryPassed && stats.normalError < 1e-4 && stats.maxHeadScaleError < 1e-3;
            if (clip.name != "airborne")
                contactPassed = contactPassed && stats.structuralMinimumZ >= -.015 && stats.highestContact <= .015;
            if (!feet.empty()) stancePassed = stancePassed && maxStanceDrift <= .002;
            if (clipIndex) report << ",\n";
            report << '"' << Escape(clip.name) << "\":";
            PrintStats(report, stats);
            if (walking) {
                report << ",\n\"" << Escape(clip.name) << "_stance\":{\"checks\":" << stanceChecks
                       << ",\"max_drift_per_half_frame\":" << maxStanceDrift
                       << ",\"max_slip_speed_fraction_of_root_speed\":" << maxRelativeStanceSpeed << '}';
            }
            std::cerr << clip.name << ": " << stats.samples << " samples, minZ=" << stats.structuralMinimumZ
                      << ", max contact=" << stats.highestContact << '\n';
        }
        report << "\n},\n\"idle_transition_grids\":{\n";
        const int idle = model.FindClip("idle");
        bool first = true;
        if (idle >= 0) for (const std::string name : {"walk", "run", "stalk", "belly", "landing"}) {
            const int to = model.FindClip(name);
            if (to < 0) continue;
            Stats stats;
            // Grid includes starts, midpoint and settled belly, plus blends
            // after an interruption. Reverse transition has the same poses.
            for (int phase = 0; phase <= 8; ++phase) for (int weight = 1; weight < 10; ++weight) {
                const double time = model.Duration(to) * phase / 8.0;
                if (!model.SampleBlended(idle, 0, to, time, weight / 10.0f, vertices, &error))
                    throw std::runtime_error("idle->" + name + ": " + error);
                Inspect(vertices, {}, time, skipped, stats);
            }
            geometryPassed = geometryPassed && stats.normalError < 1e-4;
            if (!first) report << ",\n";
            first = false;
            report << '"' << name << "\":";
            PrintStats(report, stats);
        }
        report << "\n},\n\"support_snapshots\":[\n";
        first = true;
        if (!groups.empty()) for (const std::string clipName : {"idle", "walk", "belly", "landing"}) {
            const int clip = model.FindClip(clipName);
            if (clip < 0) continue;
            for (const double phase : {0.0, 0.22, 0.4, 0.5, 0.63, 0.73, 1.0}) {
                if (!model.Sample(clip, phase * model.Duration(clip), vertices, &error)) throw std::runtime_error(error);
                if (!first) report << ",\n";
                first = false;
                report << "{\"clip\":\"" << Escape(clipName) << "\",\"phase\":" << phase << ",\"group_min_z\":{";
                for (std::size_t i = 0; i < groups.size(); ++i) {
                    double minimum = 1e9;
                    for (const auto vertex : groups[i].vertices) minimum = std::min(minimum, double(vertices[vertex].position[2]));
                    if (requireBellyHeadContact && clipName == "belly" && groups[i].name == "head" && (phase == .63 || phase == .73)) {
                        bellyHeadPassed = bellyHeadPassed && minimum >= -.015 && minimum <= .04;
                        ++bellyHeadSamples;
                    }
                    if (requireBellyBodyContact && clipName == "belly" && groups[i].name == "torso" && (phase == .63 || phase == .73)) {
                        bellyBodyPassed = bellyBodyPassed && minimum >= -.015 && minimum <= .04;
                        ++bellyBodySamples;
                    }
                    if (i) report << ',';
                    report << '"' << Escape(groups[i].name) << "\":" << minimum;
                }
                report << "}}";
            }
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (requireBellyHeadContact && bellyHeadSamples != 2) bellyHeadPassed = false;
        if (requireBellyBodyContact && bellyBodySamples != 2) bellyBodyPassed = false;
        const bool passed = geometryPassed && contactPassed && stancePassed && bellyHeadPassed && bellyBodyPassed && airborneContactPassed;
        report << "\n],\n\"finite_geometry_and_normals_passed\":" << (geometryPassed ? "true" : "false")
               << ",\"ground_contact_passed\":" << (contactPassed ? "true" : "false")
               << ",\"stance_drift_passed\":" << (stancePassed ? "true" : "false")
               << ",\"settled_belly_head_contact_passed\":" << (bellyHeadPassed ? "true" : "false")
               << ",\"settled_belly_head_contact_required\":" << (requireBellyHeadContact ? "true" : "false")
               << ",\"settled_belly_body_contact_passed\":" << (bellyBodyPassed ? "true" : "false")
               << ",\"settled_belly_body_contact_required\":" << (requireBellyBodyContact ? "true" : "false")
               << ",\"airborne_contact_passed\":" << (airborneContactPassed ? "true" : "false")
               << ",\"airborne_contact_samples\":" << airborneContactSamples
               << ",\"passed\":" << (passed ? "true" : "false")
               << ",\"thresholds\":{\"absolute_ground_error\":0.015,\"stance_half_frame_drift\":0.002,\"settled_head_and_torso_height\":0.04,\"airborne_endpoint_gap\":0.015,\"normal_length_error\":0.0001,\"head_scale_error\":0.001}"
               << ",\"grounding_note\":\"Raw skinned coordinates before app ground correction; contact metrics require decorative exclusion ranges. Visual intersection and anatomical quality require separate review.\""
               << ",\"elapsed_seconds\":" << elapsed << "\n}\n";
        std::ofstream output(argv[2]);
        if (!output) throw std::runtime_error("Cannot write numeric audit report");
        output << report.str();
        std::cout << "Numeric audit written to " << argv[2] << '\n';
        return passed ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
