#pragma once

#include <memory>

namespace cat::physics {

// Coordinates and velocities are logical pixels, independent of monitor DPI.
// x is relative to the work area; lift is the feet's height above the taskbar.
struct Pose {
    double x = 0.0;
    double lift = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    bool grounded = true;
    bool onPlatform = false;
    bool hanging = false; // supported by a real ledge joint, not ground contact
    double supportVelocityX = 0.0; // world px/s of the contacted ledge, else zero
    double supportTravelX = 0.0; // ledge travel while supported in this Update
    // Cumulative displacement relative to actual contacted support. RenderPose
    // interpolates this with x, so visible footfalls follow visible root travel.
    // Viewport relocation and reset do not manufacture walking distance.
    double relativeTravelX = 0.0;
    // An event during the most recent Update, cleared by the next Update.
    // Getters do not consume it. landingSpeed is the incoming speed in px/s.
    bool landed = false;
    double landingSpeed = 0.0;
};

inline constexpr double kFixedStepSeconds = 1.0 / 60.0;
// Per-animal plans may use a smaller rise budget. Absolute altitude follows
// the work-area ceiling, so a high source can reach another high ledge.
inline constexpr double kMaximumLaunchRisePixels = 1100.0;

// Owns a real Box2D world, a dynamic capsule and static floor/edge colliders.
// No simulation state is persisted; reset when dragging or changing monitors.
class Controller {
public:
    Controller();
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    // minX/maxX are the permitted CENTER positions, after the sprite margin.
    // Reset places the feet on the floor and clears velocity and pending time.
    void Reset(double x, double minX, double maxX);
    void SetBounds(double minX, double maxX);
    // Highest permitted feet position, leaving room for the rendered body.
    // A physical ceiling stops fast upward support motion without teleporting.
    void SetCeiling(double maxFeetLift);
    // Explicit viewport relocation, preserving in-flight height and time.
    // This translates world x for a coordinate change; physical window motion
    // must use SetPlatform instead. Clipping stops horizontal velocity only.
    void RelocateBounds(double minX, double maxX, double deltaX);
    // A finite, one-way kinematic ledge in taskbar-relative coordinates.
    // SetPlatform moves the collider through fixed-step velocities; it never
    // translates the cat. Removal preserves the cat's airborne momentum.
    void SetPlatform(double left, double right, double height);
    void RemovePlatform();
    bool OnPlatform() const;
    bool CanLaunchTo(double targetX, double targetHeight, double clearance = 45.0) const;
    bool LaunchTo(double targetX, double targetHeight, double clearance = 45.0);
    // Plan against a different finite ledge without moving the current support.
    // A successful launch replaces that support only after the takeoff impulse.
    bool CanLaunchToPlatform(double left, double right, double height,
                             double targetX, double clearance = 45.0) const;
    bool LaunchToPlatform(double left, double right, double height,
                          double targetX, double clearance = 45.0);
    bool BeginHang(double edgeX, double height);
    bool EndHang(bool climb = true);
    bool Hanging() const;
    void SetTargetVelocity(double logicalPixelsPerSecond);
    bool Jump(double apexPixels = 55.0);
    bool Pounce(int direction, double apexPixels = 30.0,
                double speedPixelsPerSecond = 72.0);

    // 60 Hz fixed step with four Box2D substeps; no more than fifteen catch-up
    // steps. Pauses and stalls >250 ms discard elapsed time, not pet age.
    void Update(double elapsedSeconds, bool paused = false);
    Pose CurrentPose() const;
    Pose RenderPose() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cat::physics
