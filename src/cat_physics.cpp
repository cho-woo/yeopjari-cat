#include "cat_physics.hpp"

#include <box2d/box2d.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace cat::physics {
namespace {
constexpr double kPixelsPerMeter = 100.0;
constexpr float kGravity = 9.0f; // 55 px apex gives about 0.70 seconds airborne.
constexpr float kRadius = 0.18f;
constexpr float kCapsuleHalfSegment = 0.12f;
constexpr float kFootOffset = kRadius + kCapsuleHalfSegment;
constexpr int kSubsteps = 4;
constexpr int kMaxCatchUpSteps = 6;
constexpr double kStallSeconds = 0.25;

double FiniteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

double Lerp(double a, double b, double t) { return a + (b - a) * t; }
} // namespace

struct Controller::Impl {
    b2WorldId world = b2_nullWorldId;
    b2BodyId body = b2_nullBodyId;
    b2BodyId boundary = b2_nullBodyId;
    b2ShapeId ground = b2_nullShapeId;
    double minX = 0.0, maxX = 100.0;
    double targetVelocity = 0.0;
    double accumulator = 0.0;
    Pose previous, current;

    ~Impl() { Destroy(); }

    void Destroy() {
        if (B2_IS_NON_NULL(world)) b2DestroyWorld(world);
        world = b2_nullWorldId;
        body = b2_nullBodyId;
        boundary = b2_nullBodyId;
        ground = b2_nullShapeId;
    }

    void Bounds(double low, double high) {
        minX = std::clamp(FiniteOr(low, 0.0), -100000.0, 100000.0);
        maxX = std::clamp(FiniteOr(high, minX), minX, 100000.0);
    }

    void CreateBoundary() {
        if (B2_IS_NON_NULL(boundary)) b2DestroyBody(boundary);
        b2BodyDef boundaryDef = b2DefaultBodyDef();
        boundary = b2CreateBody(world, &boundaryDef);
        b2ShapeDef shape = b2DefaultShapeDef();
        shape.material.friction = 0.0f;
        shape.material.restitution = 0.0f;
        const float low = static_cast<float>(minX / kPixelsPerMeter);
        const float high = static_cast<float>(maxX / kPixelsPerMeter);
        const float middle = (low + high) * 0.5f;
        const b2Polygon floor = b2MakeOffsetBox((high-low)*0.5f + 1.0f,
            0.25f, {middle, -0.25f}, b2Rot_identity);
        ground = b2CreatePolygonShape(boundary, &shape, &floor);
        // The inside edges touch the capsule at the allowed center positions.
        const b2Polygon left = b2MakeOffsetBox(0.25f, 5.0f,
            {low-kRadius-0.25f, 4.75f}, b2Rot_identity);
        const b2Polygon right = b2MakeOffsetBox(0.25f, 5.0f,
            {high+kRadius+0.25f, 4.75f}, b2Rot_identity);
        b2CreatePolygonShape(boundary, &shape, &left);
        b2CreatePolygonShape(boundary, &shape, &right);
    }

    void Reset(double x, double low, double high) {
        Destroy();
        Bounds(low, high);
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {0.0f, -kGravity};
        world = b2CreateWorld(&worldDef);
        CreateBoundary();

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {static_cast<float>(std::clamp(FiniteOr(x, minX),
            minX, maxX) / kPixelsPerMeter), kFootOffset};
        bodyDef.fixedRotation = true;
        bodyDef.name = "Yeopjari cat";
        body = b2CreateBody(world, &bodyDef);
        b2ShapeDef shape = b2DefaultShapeDef();
        shape.density = 1.0f;
        shape.material.friction = 0.0f;
        shape.material.restitution = 0.0f;
        const b2Capsule capsule{{0.0f, -kCapsuleHalfSegment},
            {0.0f, kCapsuleHalfSegment}, kRadius};
        b2CreateCapsuleShape(body, &shape, &capsule);
        current = {};
        current.x = bodyDef.position.x * kPixelsPerMeter;
        previous = current;
        accumulator = 0.0;
        targetVelocity = 0.0;
    }

    bool HasFloorContact(b2Vec2 position, b2Vec2 velocity) const {
        // Contact manifolds can include speculative points. Do not classify a
        // capsule that is still rising or approaching the floor as landed.
        if (std::abs(velocity.y) > 0.12f || position.y-kFootOffset > 0.02f)
            return false;
        std::array<b2ContactData, 8> contacts{};
        const int count = b2Body_GetContactData(body, contacts.data(),
            static_cast<int>(contacts.size()));
        for (int i = 0; i < count; ++i) {
            const auto& contact = contacts[static_cast<std::size_t>(i)];
            if (!B2_ID_EQUALS(contact.shapeIdA, ground) &&
                !B2_ID_EQUALS(contact.shapeIdB, ground)) continue;
            for (int point = 0; point < contact.manifold.pointCount; ++point)
                if (contact.manifold.points[point].separation <= 0.01f) return true;
        }
        return false;
    }

    void Step() {
        previous = current;
        const b2Vec2 before = b2Body_GetLinearVelocity(body);
        const double vx = before.x * kPixelsPerMeter;
        // A force-controlled motor gives smooth acceleration and deceleration;
        // Box2D owns integration and resolves floor and edge collisions.
        const bool braking = std::abs(targetVelocity) < std::abs(vx) ||
            targetVelocity * vx < 0.0;
        const double response = braking ? 0.10 : 0.13;
        const double maxAcceleration = current.grounded ? (braking ? 680.0 : 460.0) : 180.0;
        const double acceleration = std::clamp((targetVelocity-vx)/response,
            -maxAcceleration, maxAcceleration);
        const float mass = b2Body_GetMass(body);
        if (std::abs(acceleration) > 0.001) {
            b2Body_ApplyForceToCenter(body,
                {mass * static_cast<float>(acceleration/kPixelsPerMeter), 0.0f}, true);
        }
        b2World_Step(world, static_cast<float>(kFixedStepSeconds), kSubsteps);
        const b2Vec2 position = b2Body_GetPosition(body);
        const b2Vec2 velocity = b2Body_GetLinearVelocity(body);
        const bool grounded = HasFloorContact(position, velocity);
        if (!current.grounded && grounded) {
            current.landed = true;
            current.landingSpeed = std::max(0.0, -before.y*kPixelsPerMeter);
        }
        current.x = std::clamp(position.x*kPixelsPerMeter, minX, maxX);
        current.lift = std::max(0.0, (position.y-kFootOffset)*kPixelsPerMeter);
        current.velocityX = velocity.x*kPixelsPerMeter;
        current.velocityY = velocity.y*kPixelsPerMeter;
        current.grounded = grounded;
    }
};

Controller::Controller() : impl_(std::make_unique<Impl>()) { impl_->Reset(0, 0, 100); }
Controller::~Controller() = default;

void Controller::Reset(double x, double minX, double maxX) { impl_->Reset(x, minX, maxX); }

void Controller::SetBounds(double minX, double maxX) {
    MovePlatform(minX, maxX, 0.0);
}

void Controller::MovePlatform(double minX, double maxX, double deltaX) {
    const double oldMin = impl_->minX, oldMax = impl_->maxX;
    impl_->Bounds(minX, maxX);
    deltaX = std::clamp(FiniteOr(deltaX, 0.0), -200000.0, 200000.0);
    const bool changedBounds = oldMin != impl_->minX || oldMax != impl_->maxX;
    if (!changedBounds && deltaX == 0.0) return;
    if (changedBounds) impl_->CreateBoundary();
    b2Vec2 position = b2Body_GetPosition(impl_->body);
    const double wanted = position.x*kPixelsPerMeter + deltaX;
    const double clamped = std::clamp(wanted, impl_->minX, impl_->maxX);
    const bool clipped = std::abs(clamped-wanted) > 0.001;
    const double previousX = impl_->current.x;
    position.x = static_cast<float>(clamped/kPixelsPerMeter);
    b2Body_SetTransform(impl_->body, position, b2Rot_identity);
    if (clipped) {
        b2Vec2 velocity = b2Body_GetLinearVelocity(impl_->body);
        velocity.x = 0.0f;
        b2Body_SetLinearVelocity(impl_->body, velocity);
        impl_->current.velocityX = 0.0;
        impl_->previous.velocityX = 0.0;
    }
    impl_->current.x = clamped;
    // Keep the interpolation fraction and height/vertical velocity. Resetting
    // the accumulator every frame of a window drag can freeze the simulation;
    // resetting the entire body would cancel a jump in midair.
    impl_->previous.x = std::clamp(impl_->previous.x + clamped-previousX,
        impl_->minX, impl_->maxX);
    impl_->current.landed = false;
    impl_->current.landingSpeed = 0.0;
}

void Controller::SetTargetVelocity(double pixelsPerSecond) {
    impl_->targetVelocity = std::clamp(FiniteOr(pixelsPerSecond, 0.0), -450.0, 450.0);
}

bool Controller::Jump(double apexPixels) {
    if (!impl_->current.grounded || !std::isfinite(apexPixels) || apexPixels <= 0.0)
        return false;
    const float height = static_cast<float>(std::clamp(apexPixels, 1.0, 180.0)/kPixelsPerMeter);
    const float speed = std::sqrt(2.0f*kGravity*height);
    const float currentY = b2Body_GetLinearVelocity(impl_->body).y;
    const float mass = b2Body_GetMass(impl_->body);
    b2Body_ApplyLinearImpulseToCenter(impl_->body, {0.0f, mass*(speed-currentY)}, true);
    impl_->current.velocityY = speed*kPixelsPerMeter;
    impl_->current.grounded = false;
    impl_->current.landed = false;
    impl_->current.landingSpeed = 0.0;
    impl_->previous = impl_->current;
    return true;
}

bool Controller::Pounce(int direction, double apexPixels, double speedPixelsPerSecond) {
    if (!Jump(apexPixels)) return false;
    SetTargetVelocity((direction < 0 ? -1.0 : 1.0)*std::abs(FiniteOr(speedPixelsPerSecond, 0.0)));
    // A pounce has a horizontal take-off impulse. Ordinary walking uses forces.
    const float wanted = static_cast<float>(impl_->targetVelocity/kPixelsPerMeter);
    const float before = b2Body_GetLinearVelocity(impl_->body).x;
    const float mass = b2Body_GetMass(impl_->body);
    b2Body_ApplyLinearImpulseToCenter(impl_->body, {mass*(wanted-before), 0.0f}, true);
    impl_->current.velocityX = impl_->targetVelocity;
    impl_->previous = impl_->current;
    return true;
}

void Controller::Update(double elapsedSeconds, bool paused) {
    impl_->current.landed = false;
    impl_->current.landingSpeed = 0.0;
    if (paused || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
        elapsedSeconds > kStallSeconds) {
        impl_->accumulator = 0.0;
        impl_->previous = impl_->current;
        return;
    }
    impl_->accumulator += std::min(elapsedSeconds, kMaxCatchUpSteps*kFixedStepSeconds);
    int steps = 0;
    while (impl_->accumulator + 1e-10 >= kFixedStepSeconds && steps < kMaxCatchUpSteps) {
        impl_->Step();
        impl_->accumulator = std::max(0.0, impl_->accumulator-kFixedStepSeconds);
        ++steps;
    }
    // Keep only the interpolation fraction; never accumulate a work backlog.
    if (impl_->accumulator >= kFixedStepSeconds)
        impl_->accumulator = std::fmod(impl_->accumulator, kFixedStepSeconds);
}

Pose Controller::CurrentPose() const { return impl_->current; }

Pose Controller::RenderPose() const {
    Pose pose = impl_->current;
    const double alpha = std::clamp(impl_->accumulator/kFixedStepSeconds, 0.0, 1.0);
    pose.x = Lerp(impl_->previous.x, pose.x, alpha);
    pose.lift = Lerp(impl_->previous.lift, pose.lift, alpha);
    pose.velocityX = Lerp(impl_->previous.velocityX, pose.velocityX, alpha);
    pose.velocityY = Lerp(impl_->previous.velocityY, pose.velocityY, alpha);
    return pose;
}

} // namespace cat::physics
