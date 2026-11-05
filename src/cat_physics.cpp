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
constexpr int kMaxCatchUpSteps = 15;
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
    b2BodyId platform = b2_nullBodyId;
    b2ShapeId platformShape = b2_nullShapeId;
    b2JointId hangJoint = b2_nullJointId;
    int hangSide = 1;
    double platformLeft = 0, platformRight = 0, platformHeight = 0;
    double platformTargetX = 0, platformTargetY = 0;
    bool platformCanCollide = false, ballistic = false;
    double minX = 0.0, maxX = 100.0;
    double ceiling = 100000.0;
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
        platform = b2_nullBodyId;
        platformShape = b2_nullShapeId;
        hangJoint = b2_nullJointId;
        platformCanCollide = ballistic = false;
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
        // A fast raised ledge can launch the body above ordinary jump height.
        // Join the side walls to the configured roof, including tall monitors.
        const float wallTop = static_cast<float>(ceiling/kPixelsPerMeter) + 2*kFootOffset + .5f;
        const float wallHalfHeight = (wallTop + .25f)*.5f;
        const float wallCenterY = (wallTop - .25f)*.5f;
        const b2Polygon left = b2MakeOffsetBox(0.25f, wallHalfHeight,
            {low-kRadius-0.25f, wallCenterY}, b2Rot_identity);
        const b2Polygon right = b2MakeOffsetBox(0.25f, wallHalfHeight,
            {high+kRadius+0.25f, wallCenterY}, b2Rot_identity);
        b2CreatePolygonShape(boundary, &shape, &left);
        b2CreatePolygonShape(boundary, &shape, &right);
        const b2Polygon roof = b2MakeOffsetBox((high-low)*.5f + 1,
            .25f, {middle, static_cast<float>(ceiling/kPixelsPerMeter) + 2*kFootOffset + .25f}, b2Rot_identity);
        b2CreatePolygonShape(boundary, &shape, &roof);
    }

    void Reset(double x, double low, double high) {
        Destroy();
        Bounds(low, high);
        ceiling = 100000.0;
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {0.0f, -kGravity};
        world = b2CreateWorld(&worldDef);
        CreateBoundary();
        b2World_SetPreSolveCallback(world, [](b2ShapeId a, b2ShapeId b, b2Manifold* manifold, void* context) {
            const auto& self = *static_cast<Impl*>(context);
            const bool platformA = B2_ID_EQUALS(a, self.platformShape);
            const bool platformB = B2_ID_EQUALS(b, self.platformShape);
            if (!platformA && !platformB) return true;
            const float upwardNormal = platformA ? manifold->normal.y : -manifold->normal.y;
            return !self.current.hanging && self.platformCanCollide && upwardNormal > .5f;
        }, this);

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {static_cast<float>(std::clamp(FiniteOr(x, minX),
            minX, maxX) / kPixelsPerMeter), kFootOffset};
        bodyDef.fixedRotation = true;
        bodyDef.isBullet = true;
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

    bool HasSupportContact(b2Vec2 position, b2Vec2 velocity, bool& onPlatform) const {
        onPlatform = false;
        std::array<b2ContactData, 8> contacts{};
        const int count = b2Body_GetContactData(body, contacts.data(), static_cast<int>(contacts.size()));
        for (int i = 0; i < count; ++i) {
            const auto& contact = contacts[static_cast<std::size_t>(i)];
            const bool floorA = B2_ID_EQUALS(contact.shapeIdA, ground);
            const bool floorB = B2_ID_EQUALS(contact.shapeIdB, ground);
            const bool platformA = B2_IS_NON_NULL(platformShape) && B2_ID_EQUALS(contact.shapeIdA, platformShape);
            const bool platformB = B2_IS_NON_NULL(platformShape) && B2_ID_EQUALS(contact.shapeIdB, platformShape);
            const bool ledge = platformA || platformB;
            if (!(floorA || floorB || ledge) || (ledge && !platformCanCollide)) continue;
            const float top = ledge ? b2Body_GetPosition(platform).y : 0;
            const float supportVy = ledge ? b2Body_GetLinearVelocity(platform).y : 0;
            const float normal = (floorA || platformA) ? contact.manifold.normal.y : -contact.manifold.normal.y;
            if (normal < .5f || std::abs(velocity.y - supportVy) > .20f ||
                    std::abs(position.y - kFootOffset - top) > .025f) continue;
            for (int point = 0; point < contact.manifold.pointCount; ++point) {
                if (contact.manifold.points[point].separation > .01f) continue;
                onPlatform = ledge;
                return true;
            }
        }
        return false;
    }

    bool LaunchVelocity(double targetX, double targetHeight, double clearance,
                        double& vx, double& vy, bool checkCurrentPlatform = true, bool allowHanging = false) const {
        if ((!current.grounded && !(allowHanging && current.hanging)) || !std::isfinite(targetX) || !std::isfinite(targetHeight) ||
                !std::isfinite(clearance) || targetX < minX || targetX > maxX ||
                targetHeight < 0 || targetHeight > ceiling || clearance < 10 || clearance > 120)
            return false;
        const double gravity = kGravity * kPixelsPerMeter;
        const double rise = targetHeight - current.lift;
        if (rise > kMaximumLaunchRisePixels) return false;
        const double apex = std::max(current.lift, targetHeight) + clearance;
        if (apex > ceiling) return false;
        vy = std::sqrt(2 * gravity * (apex - current.lift));
        const double duration = (vy + std::sqrt(vy * vy - 2 * gravity * rise)) / gravity;
        vx = (targetX - current.x) / duration;
        if (!std::isfinite(vx) || duration <= 0 || duration > 3 || std::abs(vx) > 450) return false;
        if (checkCurrentPlatform && B2_IS_NON_NULL(platform) && std::abs(targetHeight - platformHeight) < .5) {
            // Landing retains momentum. Require enough top surface for the
            // ordinary ground motor to brake instead of aiming off an edge.
            const double stopX = targetX + std::copysign(vx*vx/(2*680.0) + 12, vx);
            if (targetX < platformLeft + 24 || targetX > platformRight - 24 ||
                    stopX < platformLeft + 24 || stopX > platformRight - 24) return false;
        }
        return true;
    }

    bool PlatformLaunchVelocity(double left, double right, double height, double targetX,
                                double clearance, double& vx, double& vy) const {
        if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(height) ||
                height <= 0 || height > ceiling || right - left < 72)
            return false;
        left = std::max(left, minX - 96); right = std::min(right, maxX + 96);
        if (right - left < 72 || !LaunchVelocity(targetX, height, clearance, vx, vy, false)) return false;
        const double stopX = targetX + std::copysign(vx*vx/(2*680.0) + 12, vx);
        return targetX >= left + 24 && targetX <= right - 24 &&
            stopX >= left + 24 && stopX <= right - 24;
    }

    void ApplyLaunch(double vx, double vy) {
        const auto before = b2Body_GetLinearVelocity(body);
        const auto mass = b2Body_GetMass(body);
        b2Body_ApplyLinearImpulseToCenter(body,
            {mass * static_cast<float>(vx/kPixelsPerMeter-before.x),
             mass * static_cast<float>(vy/kPixelsPerMeter-before.y)}, true);
        current.velocityX = vx; current.velocityY = vy;
        current.grounded = current.onPlatform = current.landed = false;
        current.supportVelocityX = current.supportTravelX = 0;
        current.landingSpeed = 0;
        previous = current;
        ballistic = true;
    }

    void Step(int remainingSteps) {
        previous = current;
        if (B2_IS_NON_NULL(hangJoint)) {
            // Shorten a physical constraint gradually. The solver, gravity and
            // momentum move the body; no pose/position override performs the catch.
            b2DistanceJoint_SetLength(hangJoint,
                std::max(.08f, b2DistanceJoint_GetLength(hangJoint) - .6f*static_cast<float>(kFixedStepSeconds)));
        }
        const b2Vec2 before = b2Body_GetLinearVelocity(body);
        b2Vec2 platformVelocity{};
        const double platformBeforeX = B2_IS_NON_NULL(platform) ? b2Body_GetPosition(platform).x : 0;
        const bool topContactWasEligible = platformCanCollide;
        platformCanCollide = false;
        if (B2_IS_NON_NULL(platform)) {
            const auto position = b2Body_GetPosition(platform);
            const double remaining = std::max(1, remainingSteps) * kFixedStepSeconds;
            platformVelocity = {
                static_cast<float>(std::clamp((platformTargetX - position.x) / remaining, -15.0, 15.0)),
                static_cast<float>(std::clamp((platformTargetY - position.y) / remaining, -15.0, 15.0))};
            b2Body_SetLinearVelocity(platform, platformVelocity);
            const auto catPosition = b2Body_GetPosition(body);
            // Pass through from below/upward. Only a top contact reached from
            // above while descending relative to the ledge can support the cat.
            // Box2D can first report a new contact after the feet cross the
            // top, then resolve its small penetration over subsequent steps.
            // Keep that already armed contact until resolution, while the
            // capsule center is above the top. Otherwise a fixed 2 px cutoff
            // disables the very contact that must finish the landing. A body
            // approaching from underneath never arms this allowance.
            const bool reachedFromAbove = catPosition.y - kFootOffset >= position.y - .02f ||
                (topContactWasEligible && catPosition.y >= position.y);
            platformCanCollide = !current.hanging && reachedFromAbove &&
                before.y <= platformVelocity.y + .05f;
        }
        const double vx = before.x * kPixelsPerMeter;
        const double supportVx = current.onPlatform ? platformVelocity.x * kPixelsPerMeter : 0;
        const double desired = targetVelocity + supportVx;
        const bool braking = std::abs(desired) < std::abs(vx) || desired * vx < 0.0;
        const double response = braking ? 0.10 : 0.13;
        const double maxAcceleration = current.grounded ? (braking ? 680.0 : 460.0) : 180.0;
        const double acceleration = current.hanging || (ballistic && !current.grounded) ? 0.0 :
            std::clamp((desired-vx)/response, -maxAcceleration, maxAcceleration);
        const float mass = b2Body_GetMass(body);
        if (std::abs(acceleration) > .001)
            b2Body_ApplyForceToCenter(body,
                {mass * static_cast<float>(acceleration/kPixelsPerMeter), 0}, true);
        b2World_Step(world, static_cast<float>(kFixedStepSeconds), kSubsteps);
        const b2Vec2 position = b2Body_GetPosition(body);
        const b2Vec2 velocity = b2Body_GetLinearVelocity(body);
        bool onPlatform = false;
        const bool grounded = HasSupportContact(position, velocity, onPlatform);
        if (!current.grounded && grounded) {
            current.landed = true;
            current.landingSpeed = std::max(0.0, -(before.y - (onPlatform ? platformVelocity.y : 0)) * kPixelsPerMeter);
        }
        if (grounded) ballistic = false;
        current.x = std::clamp(position.x*kPixelsPerMeter, minX, maxX);
        current.lift = std::max(0.0, (position.y-kFootOffset)*kPixelsPerMeter);
        current.velocityX = velocity.x*kPixelsPerMeter;
        current.velocityY = velocity.y*kPixelsPerMeter;
        current.grounded = grounded;
        const double supportTravel = current.onPlatform && onPlatform ?
            (b2Body_GetPosition(platform).x - platformBeforeX) * kPixelsPerMeter : 0.0;
        current.supportTravelX += supportTravel;
        current.relativeTravelX += current.x-previous.x-supportTravel;
        current.onPlatform = onPlatform;
        current.supportVelocityX = onPlatform ? platformVelocity.x*kPixelsPerMeter : 0;
    }

};

Controller::Controller() : impl_(std::make_unique<Impl>()) { impl_->Reset(0, 0, 100); }
Controller::~Controller() = default;

void Controller::Reset(double x, double minX, double maxX) { impl_->Reset(x, minX, maxX); }

void Controller::SetBounds(double minX, double maxX) {
    RelocateBounds(minX, maxX, 0.0);
}

void Controller::SetCeiling(double maxFeetLift) {
    if (!std::isfinite(maxFeetLift) || maxFeetLift < 1) return;
    maxFeetLift = std::min(maxFeetLift, 100000.0);
    if (impl_->ceiling == maxFeetLift) return;
    impl_->ceiling = maxFeetLift;
    impl_->CreateBoundary();
    if (B2_IS_NON_NULL(impl_->platform) && impl_->platformHeight > maxFeetLift)
        RemovePlatform();
    b2Body_SetAwake(impl_->body, true);
}

void Controller::RelocateBounds(double minX, double maxX, double deltaX) {
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
    // A viewport coordinate change preserves the interpolation fraction and
    // height/vertical velocity. Physical support motion never uses this path.
    impl_->previous.x = std::clamp(impl_->previous.x + clamped-previousX,
        impl_->minX, impl_->maxX);
    impl_->current.landed = false;
    impl_->current.landingSpeed = 0.0;
}

void Controller::SetPlatform(double left, double right, double height) {
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(height) ||
            right-left < 36 || height <= 0 || height > impl_->ceiling) {
        RemovePlatform();
        return;
    }
    left = std::max(left, impl_->minX - 96);
    right = std::min(right, impl_->maxX + 96);
    if (right-left < 36) { RemovePlatform(); return; }
    bool create = B2_IS_NULL(impl_->platform);
    const bool changed = create || left != impl_->platformLeft || right != impl_->platformRight || height != impl_->platformHeight;
    bool resize = create || std::abs((right-left) - (impl_->platformRight-impl_->platformLeft)) > .01;
    const bool supported = impl_->current.onPlatform || impl_->current.hanging;
    const double contactRadius = kRadius*kPixelsPerMeter;
    const bool supportMovedAway = !impl_->current.hanging &&
        (impl_->current.x + contactRadius < left || impl_->current.x - contactRadius > right);
    // A resized window is new geometry, not a wide ledge sweeping from its
    // previous centre. A window moved entirely out from under the feet also
    // ceases to carry the animal. Release only the support; preserve velocity.
    if (!create && changed && supported && (resize || supportMovedAway)) {
        RemovePlatform();
        create = resize = true;
    }
    impl_->platformLeft = left; impl_->platformRight = right; impl_->platformHeight = height;
    impl_->platformTargetX = (left+right)*.5/kPixelsPerMeter;
    impl_->platformTargetY = height/kPixelsPerMeter;
    if (create) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_kinematicBody;
        bodyDef.position = {static_cast<float>(impl_->platformTargetX), static_cast<float>(impl_->platformTargetY)};
        impl_->platform = b2CreateBody(impl_->world, &bodyDef);
    } else if (changed && !supported) {
        // Airborne targets must match the observed window immediately. Chasing
        // the new rectangle with a speed-capped kinematic body left an invisible
        // old ledge behind, allowing a missed jump to land in empty space.
        // Move only the collider; the cat keeps its Box2D ballistic trajectory.
        b2Body_SetTransform(impl_->platform,
            {static_cast<float>(impl_->platformTargetX), static_cast<float>(impl_->platformTargetY)}, b2Rot_identity);
        b2Body_SetLinearVelocity(impl_->platform, {0, 0});
        impl_->platformCanCollide = false;
    }
    if (resize) {
        if (B2_IS_NON_NULL(impl_->platformShape)) b2DestroyShape(impl_->platformShape, false);
        b2ShapeDef shape = b2DefaultShapeDef();
        shape.enablePreSolveEvents = true;
        shape.material.friction = 0;
        shape.material.restitution = 0;
        const b2Polygon ledge = b2MakeOffsetBox(static_cast<float>((right-left)*.5/kPixelsPerMeter),
            .10f, {0,-.10f}, b2Rot_identity);
        impl_->platformShape = b2CreatePolygonShape(impl_->platform, &shape, &ledge);
    }
    if (changed) b2Body_SetAwake(impl_->body, true);
}

void Controller::RemovePlatform() {
    const bool supported = impl_->current.onPlatform || impl_->current.hanging;
    if (B2_IS_NON_NULL(impl_->hangJoint)) b2DestroyJoint(impl_->hangJoint);
    impl_->hangJoint = b2_nullJointId;
    impl_->current.hanging = false;
    if (supported) b2Body_SetLinearDamping(impl_->body, 0);
    if (B2_IS_NON_NULL(impl_->platform)) b2DestroyBody(impl_->platform);
    impl_->platform = b2_nullBodyId; impl_->platformShape = b2_nullShapeId;
    impl_->platformCanCollide = false;
    if (supported) {
        impl_->current.grounded = impl_->current.onPlatform = false;
        impl_->current.supportVelocityX = impl_->current.supportTravelX = 0;
        impl_->current.landed = false; impl_->current.landingSpeed = 0;
        impl_->previous = impl_->current;
        impl_->ballistic = true;
        b2Body_SetAwake(impl_->body, true);
    }
}

bool Controller::OnPlatform() const { return impl_->current.onPlatform; }

bool Controller::Hanging() const { return impl_->current.hanging; }

bool Controller::BeginHang(double edgeX, double height) {
    if (!impl_->current.onPlatform || impl_->current.hanging || !std::isfinite(edgeX) ||
            !std::isfinite(height) || height < 90 || std::abs(height - impl_->platformHeight) > 2 ||
            std::abs(edgeX - impl_->current.x) > 90 || edgeX < impl_->minX + 30 || edgeX > impl_->maxX - 30)
        return false;
    const double leftDistance = std::abs(edgeX - impl_->platformLeft);
    const double rightDistance = std::abs(edgeX - impl_->platformRight);
    if (std::min(leftDistance, rightDistance) > 2) return false;
    impl_->hangSide = leftDistance < rightDistance ? -1 : 1;
    const auto platformPosition = b2Body_GetPosition(impl_->platform);
    const float halfWidth = static_cast<float>((impl_->platformRight - impl_->platformLeft)*.5/kPixelsPerMeter);
    if (std::abs((platformPosition.x + impl_->hangSide*halfWidth)*kPixelsPerMeter - impl_->current.x) > 90)
        return false;
    b2DistanceJointDef joint = b2DefaultDistanceJointDef();
    joint.bodyIdA = impl_->platform; joint.bodyIdB = impl_->body;
    joint.localAnchorA = {impl_->hangSide*halfWidth, 0};
    joint.localAnchorB = {impl_->hangSide*.25f, .24f};
    const auto hand = b2Body_GetWorldPoint(impl_->body, joint.localAnchorB);
    const float dx = hand.x - (platformPosition.x + joint.localAnchorA.x);
    const float dy = hand.y - platformPosition.y;
    joint.length = std::max(.08f, std::sqrt(dx*dx + dy*dy));
    joint.collideConnected = false;
    impl_->hangJoint = b2CreateDistanceJoint(impl_->world, &joint);
    // Forepaws absorb swing energy during the catch. Without grip damping,
    // shortening the tether conserves angular momentum and spins the capsule.
    b2Body_SetLinearDamping(impl_->body, 10.0f);
    impl_->current.hanging = true;
    impl_->current.grounded = impl_->current.onPlatform = impl_->current.landed = false;
    impl_->current.supportVelocityX = impl_->current.supportTravelX = impl_->current.landingSpeed = 0;
    impl_->targetVelocity = 0; impl_->ballistic = true;
    impl_->previous = impl_->current;
    b2Body_SetAwake(impl_->body, true);
    return true;
}

bool Controller::EndHang(bool climb) {
    if (!impl_->current.hanging || B2_IS_NULL(impl_->hangJoint)) return false;
    double vx = 0, vy = 0;
    const auto platformPosition = b2Body_GetPosition(impl_->platform);
    const double halfWidth = (impl_->platformRight - impl_->platformLeft)*.5;
    const double center = platformPosition.x*kPixelsPerMeter;
    const double low = std::max(impl_->minX, center-halfWidth+30);
    const double high = std::min(impl_->maxX, center+halfWidth-30);
    const double target = low <= high ? std::clamp(center + impl_->hangSide*(halfWidth - 90), low, high) : center;
    const bool launched = climb && low <= high && impl_->LaunchVelocity(target, platformPosition.y*kPixelsPerMeter,
        35, vx, vy, false, true);
    b2DestroyJoint(impl_->hangJoint); impl_->hangJoint = b2_nullJointId;
    b2Body_SetLinearDamping(impl_->body, 0);
    impl_->current.hanging = false;
    if (launched) impl_->ApplyLaunch(vx, vy);
    else {
        impl_->current.grounded = impl_->current.onPlatform = false;
        impl_->ballistic = true;
        impl_->previous = impl_->current;
    }
    b2Body_SetAwake(impl_->body, true);
    return launched;
}

bool Controller::CanLaunchTo(double targetX, double targetHeight, double clearance) const {
    double vx = 0, vy = 0;
    return impl_->LaunchVelocity(targetX, targetHeight, clearance, vx, vy);
}

bool Controller::LaunchTo(double targetX, double targetHeight, double clearance) {
    double vx = 0, vy = 0;
    if (!impl_->LaunchVelocity(targetX, targetHeight, clearance, vx, vy)) return false;
    impl_->ApplyLaunch(vx, vy);
    return true;
}

bool Controller::CanLaunchToPlatform(double left, double right, double height,
                                     double targetX, double clearance) const {
    double vx = 0, vy = 0;
    return impl_->PlatformLaunchVelocity(left, right, height, targetX, clearance, vx, vy);
}

bool Controller::LaunchToPlatform(double left, double right, double height,
                                  double targetX, double clearance) {
    double vx = 0, vy = 0;
    if (!impl_->PlatformLaunchVelocity(left, right, height, targetX, clearance, vx, vy)) return false;
    impl_->ApplyLaunch(vx, vy);
    RemovePlatform();
    SetPlatform(left, right, height);
    return true;
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
    impl_->current.onPlatform = false;
    impl_->current.supportVelocityX = impl_->current.supportTravelX = 0;
    impl_->ballistic = false;
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
    impl_->current.supportTravelX = 0.0;
    if (paused || !std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0 ||
        elapsedSeconds > kStallSeconds) {
        // Discard only the new elapsed time. Keeping the existing fraction
        // and previous pose freezes the exact displayed location; collapsing
        // them to current used to jump a partly rendered step on opening a menu.
        return;
    }
    impl_->accumulator += std::min(elapsedSeconds, kMaxCatchUpSteps*kFixedStepSeconds);
    int steps = 0;
    while (impl_->accumulator + 1e-10 >= kFixedStepSeconds && steps < kMaxCatchUpSteps) {
        const int remaining = std::min(kMaxCatchUpSteps-steps, static_cast<int>((impl_->accumulator + 1e-10)/kFixedStepSeconds));
        impl_->Step(remaining);
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
    pose.supportVelocityX = Lerp(impl_->previous.supportVelocityX, pose.supportVelocityX, alpha);
    pose.relativeTravelX = Lerp(impl_->previous.relativeTravelX, pose.relativeTravelX, alpha);
    return pose;
}

} // namespace cat::physics
