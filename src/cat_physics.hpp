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
    // An event during the most recent Update, cleared by the next Update.
    // Getters do not consume it. landingSpeed is the incoming speed in px/s.
    bool landed = false;
    double landingSpeed = 0.0;
};

inline constexpr double kFixedStepSeconds = 1.0 / 60.0;

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
    // Move/resize a supporting window without resetting a jump or the motor.
    // deltaX translates the cat along with the platform; only horizontal
    // velocity is stopped if the new allowed interval actually clips it.
    void MovePlatform(double minX, double maxX, double deltaX);
    void SetTargetVelocity(double logicalPixelsPerSecond);
    bool Jump(double apexPixels = 55.0);
    bool Pounce(int direction, double apexPixels = 30.0,
                double speedPixelsPerSecond = 72.0);

    // 60 Hz fixed step with four Box2D substeps; no more than six catch-up
    // steps. Pauses and stalls >250 ms discard elapsed time, not pet age.
    void Update(double elapsedSeconds, bool paused = false);
    Pose CurrentPose() const;
    Pose RenderPose() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace cat::physics
