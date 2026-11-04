#pragma once

#include <box2d/box2d.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace cat::toy {

struct BallPose {
    double x = 0, velocity = 0, lift = 0, verticalVelocity = 0;
    double angle = 0, angularVelocity = 0; // Box2D counterclockwise radians
    bool grounded = true;
};

// An independent Box2D toy world. x/bounds are logical desktop coordinates;
// lift is the bottom of the ball above the taskbar, not its centre height.
// No cat position, save record or operating-system window is modified here.
class PhysicsBall {
public:
    static constexpr double Radius = 12.0;
    static constexpr double FixedStep = 1.0 / 60.0;
    double x = 0, velocity = 0, lift = 0, verticalVelocity = 0;
    double angle = 0, angularVelocity = 0;
    bool grounded = true;

    explicit PhysicsBall(double initialX = 0) { Reset(initialX); }
    PhysicsBall(const PhysicsBall&) = delete;
    PhysicsBall& operator=(const PhysicsBall&) = delete;
    ~PhysicsBall() { Destroy(); }

    void Reset(double initialX, double initialLift = 0,
               double velocityX = 0, double velocityY = 0) {
        Destroy();
        initialX = std::clamp(FiniteOr(initialX, 0), -100000.0, 100000.0);
        initialLift = std::clamp(FiniteOr(initialLift, 0), 0.0, 600.0);
        velocityX = std::clamp(FiniteOr(velocityX, 0), -500.0, 500.0);
        velocityY = std::clamp(FiniteOr(velocityY, 0), -600.0, 600.0);
        low_ = initialX - 4096; high_ = initialX + 4096;
        auto definition = b2DefaultWorldDef();
        definition.gravity = {0, -9};
        definition.restitutionThreshold = .3f;
        world_ = b2CreateWorld(&definition);
        CreateBoundary();
        auto body = b2DefaultBodyDef();
        body.type = b2_dynamicBody;
        body.position = {static_cast<float>(initialX / PixelsPerMeter),
                         static_cast<float>((initialLift + Radius) / PixelsPerMeter)};
        body.linearVelocity = {static_cast<float>(velocityX / PixelsPerMeter),
                               static_cast<float>(velocityY / PixelsPerMeter)};
        body.linearDamping = .10f;
        body.angularDamping = .08f;
        body.isBullet = true;
        body_ = b2CreateBody(world_, &body);
        auto shape = b2DefaultShapeDef();
        shape.density = 1;
        shape.material.friction = .55f;
        shape.material.rollingResistance = .035f;
        shape.material.restitution = .55f;
        const b2Circle circle{{0,0}, static_cast<float>(Radius / PixelsPerMeter)};
        b2CreateCircleShape(body_, &shape, &circle);
        accumulator_ = 0;
        current_ = {initialX, velocityX, initialLift, velocityY, 0, 0,
                    initialLift == 0 && velocityY == 0};
        previous_ = current_;
        Sync();
    }

    void Kick(int direction, double speed, double upwardSpeed = 90) {
        if (!direction || !std::isfinite(speed) || speed <= 0 ||
                !std::isfinite(upwardSpeed) || upwardSpeed < 0) return;
        const double desired = (direction < 0 ? -1 : 1) * std::min(speed, 500.0);
        const auto before = b2Body_GetLinearVelocity(body_);
        const auto position = b2Body_GetPosition(body_);
        const auto mass = b2Body_GetMass(body_);
        const double up = std::max(static_cast<double>(before.y),
                                   std::min(upwardSpeed, 300.0) / PixelsPerMeter);
        // A real impulse above the centre supplies both translation and spin.
        b2Body_ApplyLinearImpulse(body_,
            {mass * static_cast<float>(desired / PixelsPerMeter - before.x),
             mass * static_cast<float>(up - before.y)},
            {position.x, position.y + static_cast<float>(Radius * .35 / PixelsPerMeter)}, true);
        ReadPose();
        current_.grounded = false;
        previous_ = current_;
        Sync();
    }

    void Advance(double seconds, double low, double high, bool paused = false) {
        if (paused || !std::isfinite(seconds) || seconds < 0 || seconds > .25 ||
                !std::isfinite(low) || !std::isfinite(high)) {
            accumulator_ = 0; previous_ = current_; Sync(); return;
        }
        SetBounds(low, high);
        accumulator_ += seconds;
        int steps = 0;
        while (accumulator_ + 1e-10 >= FixedStep && steps < 15) {
            previous_ = current_;
            b2World_Step(world_, static_cast<float>(FixedStep), 4);
            ReadPose();
            accumulator_ = std::max(0.0, accumulator_ - FixedStep);
            ++steps;
        }
        Sync();
    }

    BallPose CurrentPose() const { return current_; }
    BallPose RenderPose() const {
        auto pose = current_;
        const double t = std::clamp(accumulator_ / FixedStep, 0.0, 1.0);
        const auto mix = [t](double a, double b) { return a + (b-a)*t; };
        pose.x = mix(previous_.x, pose.x);
        pose.lift = mix(previous_.lift, pose.lift);
        pose.velocity = mix(previous_.velocity, pose.velocity);
        pose.verticalVelocity = mix(previous_.verticalVelocity, pose.verticalVelocity);
        // A wrapped b2Rot must interpolate through pi, never spin backwards
        // almost a full revolution when crossing the angle representation seam.
        pose.angle = previous_.angle + std::remainder(pose.angle - previous_.angle, 2 * Pi) * t;
        pose.angularVelocity = mix(previous_.angularVelocity, pose.angularVelocity);
        return pose;
    }

private:
    static constexpr double PixelsPerMeter = 100, Pi = 3.14159265358979323846;
    static constexpr double CeilingLift = 1200;
    b2WorldId world_ = b2_nullWorldId;
    b2BodyId body_ = b2_nullBodyId, boundary_ = b2_nullBodyId;
    b2ShapeId floor_ = b2_nullShapeId;
    double low_ = 0, high_ = 1, accumulator_ = 0;
    BallPose previous_, current_;

    static double FiniteOr(double value, double fallback) {
        return std::isfinite(value) ? value : fallback;
    }
    void Destroy() {
        if (B2_IS_NON_NULL(world_)) b2DestroyWorld(world_);
        world_ = b2_nullWorldId; body_ = boundary_ = b2_nullBodyId;
        floor_ = b2_nullShapeId;
    }
    void CreateBoundary() {
        if (B2_IS_NON_NULL(boundary_)) b2DestroyBody(boundary_);
        auto body = b2DefaultBodyDef(); boundary_ = b2CreateBody(world_, &body);
        auto shape = b2DefaultShapeDef();
        shape.material.friction = .55f;
        shape.material.rollingResistance = .035f;
        shape.material.restitution = .45f;
        const float low = static_cast<float>(low_ / PixelsPerMeter);
        const float high = static_cast<float>(high_ / PixelsPerMeter);
        const float radius = static_cast<float>(Radius / PixelsPerMeter);
        const float middle = (low + high) * .5f;
        const float roof = static_cast<float>((CeilingLift + 2 * Radius) / PixelsPerMeter);
        const auto floor = b2MakeOffsetBox((high-low)*.5f + 1, .25f, {middle,-.25f}, b2Rot_identity);
        floor_ = b2CreatePolygonShape(boundary_, &shape, &floor);
        shape.material.restitution = .65f;
        const auto left = b2MakeOffsetBox(.25f, (roof+1)*.5f,
            {low-radius-.25f, (roof-1)*.5f}, b2Rot_identity);
        const auto right = b2MakeOffsetBox(.25f, (roof+1)*.5f,
            {high+radius+.25f, (roof-1)*.5f}, b2Rot_identity);
        const auto ceiling = b2MakeOffsetBox((high-low)*.5f + 1, .25f,
            {middle,roof+.25f}, b2Rot_identity);
        b2CreatePolygonShape(boundary_, &shape, &left);
        b2CreatePolygonShape(boundary_, &shape, &right);
        b2CreatePolygonShape(boundary_, &shape, &ceiling);
    }
    void SetBounds(double low, double high) {
        low = std::clamp(low, -100000.0, 100000.0);
        high = std::clamp(high, -100000.0, 100000.0);
        if (low > high) low = high = low + (high-low)*.5;
        if (low == low_ && high == high_) return;
        low_ = low; high_ = high;
        CreateBoundary();
        const auto position = b2Body_GetPosition(body_);
        const double clipped = std::clamp(position.x * PixelsPerMeter, low_, high_);
        // Only a changed work-area boundary can relocate a now-outside ball.
        // Ordinary movement is resolved by the circle and static colliders.
        if (std::abs(clipped - position.x * PixelsPerMeter) > .001) {
            b2Body_SetTransform(body_, {static_cast<float>(clipped / PixelsPerMeter), position.y},
                                b2Body_GetRotation(body_));
            const auto motion = b2Body_GetLinearVelocity(body_);
            b2Body_SetLinearVelocity(body_, {0, motion.y});
        }
        b2Body_SetAwake(body_, true);
        ReadPose(); previous_ = current_;
    }
    void ReadPose() {
        const auto position = b2Body_GetPosition(body_);
        const auto motion = b2Body_GetLinearVelocity(body_);
        current_.x = position.x * PixelsPerMeter;
        current_.lift = std::max(0.0, position.y * PixelsPerMeter - Radius);
        current_.velocity = motion.x * PixelsPerMeter;
        current_.verticalVelocity = motion.y * PixelsPerMeter;
        current_.angle = b2Rot_GetAngle(b2Body_GetRotation(body_));
        current_.angularVelocity = b2Body_GetAngularVelocity(body_);
        current_.grounded = false;
        if (current_.lift <= 2.5 && std::abs(current_.verticalVelocity) <= 20) {
            std::array<b2ContactData, 8> contacts{};
            const int count = b2Body_GetContactData(body_, contacts.data(), static_cast<int>(contacts.size()));
            for (int i = 0; i < count; ++i) {
                const auto& contact = contacts[static_cast<std::size_t>(i)];
                const bool floorA = B2_ID_EQUALS(contact.shapeIdA, floor_);
                const bool floorB = B2_ID_EQUALS(contact.shapeIdB, floor_);
                if (!(floorA || floorB)) continue;
                const double normal = floorA ? contact.manifold.normal.y : -contact.manifold.normal.y;
                if (normal < .5) continue;
                for (int j = 0; j < contact.manifold.pointCount; ++j)
                    if (contact.manifold.points[j].separation <= .01f) current_.grounded = true;
            }
        }
    }
    void Sync() {
        x = current_.x; velocity = current_.velocity; lift = current_.lift;
        verticalVelocity = current_.verticalVelocity; angle = current_.angle;
        angularVelocity = current_.angularVelocity; grounded = current_.grounded;
    }
};

} // namespace cat::toy
