#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "vibe/render/Camera.h"
#include "vibe/node/Node.h"

namespace {

int failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

constexpr float kPi = 3.14159265358979323846f;

bool near(float a, float b, float epsilon = 1e-4f) {
    return std::fabs(a - b) <= epsilon;
}

bool near(vibe::Vec3 a, vibe::Vec3 b, float epsilon = 1e-4f) {
    return near(a.x, b.x, epsilon) && near(a.y, b.y, epsilon) && near(a.z, b.z, epsilon);
}

std::vector<std::string> journal;

class Probe : public vibe::Node {
public:
    explicit Probe(std::string name, bool consumes = false) : Node(std::move(name)), consumes_(consumes) {}

    std::vector<vibe::Node*> spawnOnUpdate;
    bool freeSelfOnUpdate = false;
    int updates = 0;

protected:
    void onEnterTree() override { journal.push_back("enter " + name()); }
    void onExitTree() override { journal.push_back("exit " + name()); }
    bool onEvent(const SDL_Event& /*event*/) override {
        journal.push_back("event " + name());
        return consumes_;
    }
    void onFixedUpdate(float /*stepSeconds*/) override {
        ++updates;
        journal.push_back("update " + name());
        if (freeSelfOnUpdate) {
            queueFree();
        }
        if (!spawned_ && !spawnOnUpdate.empty()) {
            spawned_ = true;
            emplaceChild<Probe>(name() + ".spawn");
        }
    }

private:
    bool consumes_ = false;
    bool spawned_ = false;
};

void testMatrices() {
    const vibe::Mat4 m = vibe::Mat4::compose(vibe::Vec3{3.0f, -2.0f, 5.0f}, vibe::Quat::aroundZ(kPi * 0.5f),
                                             vibe::Vec3{2.0f, 2.0f, 2.0f});
    CHECK(near(m.transformPoint(vibe::Vec3{1.0f, 0.0f, 0.0f}), vibe::Vec3{3.0f, 0.0f, 5.0f}));
    CHECK(near(m.affineInverse().transformPoint(vibe::Vec3{3.0f, 0.0f, 5.0f}), vibe::Vec3{1.0f, 0.0f, 0.0f}));
    CHECK(near(m.rotationZ(), kPi * 0.5f));
    CHECK(near(m.planarScaleX(), 2.0f));
    CHECK(near(m.planarScaleY(), 2.0f));

    const vibe::Mat4 yaw = vibe::Mat4::compose({}, vibe::Quat::fromAxisAngle({0.0f, 1.0f, 0.0f}, kPi * 0.5f),
                                               {1.0f, 1.0f, 1.0f});
    CHECK(near(yaw.transformPoint(vibe::Vec3{0.0f, 0.0f, -1.0f}), vibe::Vec3{-1.0f, 0.0f, 0.0f}));

    const vibe::Quat halfway = vibe::Quat::nlerp(vibe::Quat::aroundZ(kPi * 0.9f), vibe::Quat::aroundZ(-kPi * 0.9f), 0.5f);
    const vibe::Mat4 turn = vibe::Mat4::compose({}, halfway, {1.0f, 1.0f, 1.0f});
    CHECK(near(std::fabs(turn.rotationZ()), kPi, 1e-3f));
}

void testHierarchy() {
    vibe::Node root("root");
    vibe::Node& arm = root.emplaceChild<vibe::Node>("arm");
    arm.setPosition(10.0f, 0.0f);
    arm.setRotationZ(kPi * 0.5f);
    vibe::Node& hand = arm.emplaceChild<vibe::Node>("hand");
    hand.setPosition(2.0f, 0.0f, 1.0f);
    CHECK(near(hand.toWorld({}), vibe::Vec3{10.0f, 2.0f, 1.0f}));
    CHECK(near(hand.toLocal(vibe::Vec3{10.0f, 2.0f, 1.0f}), vibe::Vec3{}));
    CHECK(root.findDescendant("hand") == &hand);
    CHECK(root.findChild("hand") == nullptr);
    CHECK(hand.parent() == &arm);

    std::unique_ptr<vibe::Node> detached = hand.detach();
    CHECK(detached.get() == &hand);
    CHECK(hand.parent() == nullptr);
    CHECK(arm.childCount() == 0);
    root.addChild(std::move(detached));
    CHECK(root.childCount() == 2);
    CHECK(near(hand.toWorld({}), vibe::Vec3{2.0f, 0.0f, 1.0f}));
}

void testTreeLifecycle() {
    journal.clear();
    vibe::Node root("root");
    Probe& a = root.emplaceChild<Probe>("a");
    a.emplaceChild<Probe>("a1");
    CHECK(journal.empty());
    root.enterTree();
    CHECK((journal == std::vector<std::string>{"enter a", "enter a1"}));
    journal.clear();
    root.emplaceChild<Probe>("b");
    CHECK((journal == std::vector<std::string>{"enter b"}));
    journal.clear();
    root.exitTree();
    CHECK((journal == std::vector<std::string>{"exit b", "exit a1", "exit a"}));
}

void testEvents() {
    journal.clear();
    vibe::Node root("root");
    root.emplaceChild<Probe>("back");
    Probe& front = root.emplaceChild<Probe>("front", true);
    front.emplaceChild<Probe>("front.child");
    SDL_Event event{};
    CHECK(root.dispatchEvent(event));
    CHECK((journal == std::vector<std::string>{"event front.child", "event front"}));
    journal.clear();
    front.setVisible(false);
    CHECK(!root.dispatchEvent(event));
    CHECK((journal == std::vector<std::string>{"event back"}));
}

void testUpdatesAndFree() {
    journal.clear();
    vibe::Node root("root");
    root.enterTree();
    Probe& a = root.emplaceChild<Probe>("a");
    Probe& b = root.emplaceChild<Probe>("b");
    Probe& c = root.emplaceChild<Probe>("c");
    a.freeSelfOnUpdate = true;
    b.spawnOnUpdate.push_back(&b);
    c.setActive(false);
    journal.clear();
    root.fixedUpdate(0.025f);
    CHECK((journal == std::vector<std::string>{"update a", "update b", "enter b.spawn", "update b.spawn"}));
    CHECK(c.updates == 0);
    CHECK(root.childCount() == 3);
    CHECK(a.isQueuedForFree());
    root.flushPending();
    CHECK(root.childCount() == 2);
    CHECK(root.findChild("a") == nullptr);
    CHECK(root.childAt(0) == &b);
}

void testInterpolation() {
    vibe::Node root("root");
    root.enterTree();
    vibe::Node& mover = root.emplaceChild<vibe::Node>("mover");
    mover.setPosition(0.0f, 0.0f);
    root.fixedUpdate(0.025f);
    mover.setPosition(4.0f, 0.0f);
    root.fixedUpdate(0.025f);
    mover.setPosition(8.0f, 0.0f);
    CHECK(near(mover.position(), vibe::Vec3{8.0f, 0.0f, 0.0f}));
    vibe::Node& late = root.emplaceChild<vibe::Node>("late");
    late.setPosition(5.0f, 5.0f);
    CHECK(near(late.toWorld({}), vibe::Vec3{5.0f, 5.0f, 0.0f}));
}

void testCamera() {
    vibe::Camera camera;
    camera.position = vibe::Vec3{3.0f, 4.0f, 0.0f};
    const vibe::Viewport viewport{1280, 720};
    const vibe::ScreenPoint planar = vibe::worldToScreen(camera, viewport, 5.0f, 1.0f);
    vibe::ScreenPoint projected;
    CHECK(vibe::projectToScreen(camera, viewport, vibe::Vec3{5.0f, 1.0f, 0.0f}, projected));
    CHECK(near(planar.x, projected.x, 1e-2f) && near(planar.y, projected.y, 1e-2f));

    camera.projection = vibe::Projection::Perspective;
    camera.position = vibe::Vec3{0.0f, 0.0f, 10.0f};
    float depth = -1.0f;
    CHECK(vibe::projectToScreen(camera, viewport, vibe::Vec3{}, projected, &depth));
    CHECK(near(projected.x, 640.0f, 1e-2f) && near(projected.y, 360.0f, 1e-2f));
    CHECK(depth > 0.0f && depth < 1.0f);
    CHECK(!vibe::projectToScreen(camera, viewport, vibe::Vec3{0.0f, 0.0f, 20.0f}, projected));

    vibe::Vec3 origin;
    vibe::Vec3 direction;
    vibe::screenToWorldRay(camera, viewport, 640.0f, 360.0f, origin, direction);
    CHECK(near(origin, camera.position));
    CHECK(near(direction, vibe::Vec3{0.0f, 0.0f, -1.0f}));
}

} // namespace

int main() {
    testMatrices();
    testHierarchy();
    testTreeLifecycle();
    testEvents();
    testUpdatesAndFree();
    testInterpolation();
    testCamera();
    if (failures == 0) {
        std::printf("node-tests: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
