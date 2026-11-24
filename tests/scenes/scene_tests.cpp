#include <cstdio>
#include <string>
#include <vector>

#include "vibe/core/SceneStack.h"

namespace {

int failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

std::vector<std::string> journal;

class Probe : public vibe::Scene {
public:
    Probe(std::string name, bool opaque = true) : name_(std::move(name)), opaque_(opaque) {}

    vibe::SceneStack* popOnEvent = nullptr;

    bool opaque() const override { return opaque_; }
    void onEnter() override { journal.push_back(name_ + ".enter"); }
    void onLeave() override { journal.push_back(name_ + ".leave"); }
    void onEvent(const vibe::InputEvent& /*event*/) override {
        journal.push_back(name_ + ".event");
        if (popOnEvent) {
            popOnEvent->pop();
        }
    }
    void frameStart() override { journal.push_back(name_ + ".start"); }
    void fixedUpdate(float /*stepSeconds*/) override { journal.push_back(name_ + ".step"); }
    void render(float /*alpha*/) override { journal.push_back(name_ + ".render"); }
    void frameEnd() override { journal.push_back(name_ + ".end"); }

private:
    std::string name_;
    bool opaque_;
};

bool journalIs(const std::vector<std::string>& expected) {
    const bool same = journal == expected;
    if (!same) {
        for (const std::string& line : journal) {
            std::printf("  %s\n", line.c_str());
        }
    }
    journal.clear();
    return same;
}

void frame(vibe::SceneStack& stack) {
    stack.frameStart();
    stack.fixedUpdate(0.025f);
    stack.render(0.5f);
    stack.frameEnd();
}

void testDeferredChanges() {
    vibe::SceneStack stack;
    stack.push<Probe>("a");
    CHECK(stack.empty());
    CHECK(stack.hasPending());
    CHECK(journalIs({}));
    stack.apply();
    CHECK(stack.size() == 1);
    CHECK(journalIs({"a.enter"}));

    stack.replace<Probe>("b");
    stack.apply();
    CHECK(stack.size() == 1);
    CHECK(journalIs({"a.leave", "b.enter"}));

    stack.push<Probe>("c");
    stack.push<Probe>("d");
    stack.apply();
    CHECK(stack.size() == 3);
    stack.clear();
    stack.apply();
    CHECK(stack.empty());
    CHECK(journalIs({"c.enter", "d.enter", "d.leave", "c.leave", "b.leave"}));
}

void testFrameOrder() {
    vibe::SceneStack stack;
    stack.push<Probe>("world");
    stack.push<Probe>("pause", false);
    stack.apply();
    journal.clear();
    frame(stack);
    CHECK(journalIs({"world.start", "pause.start", "pause.step", "world.render", "pause.render", "pause.end",
                     "world.end"}));

    stack.push<Probe>("menu");
    stack.apply();
    journal.clear();
    stack.render(0.0f);
    CHECK(journalIs({"menu.render"}));
}

void testPopDuringEvent() {
    vibe::SceneStack stack;
    stack.push<Probe>("world");
    Probe& pause = stack.push<Probe>("pause", false);
    pause.popOnEvent = &stack;
    stack.apply();
    journal.clear();
    stack.onEvent(vibe::InputEvent{});
    CHECK(stack.size() == 2);
    CHECK(journalIs({"pause.event"}));
    stack.apply();
    CHECK(stack.size() == 1);
    CHECK(journalIs({"pause.leave"}));
    stack.onEvent(vibe::InputEvent{});
    CHECK(journalIs({"world.event"}));
}

void testDestructorLeaves() {
    {
        vibe::SceneStack stack;
        stack.push<Probe>("a");
        stack.push<Probe>("b");
        stack.apply();
        stack.push<Probe>("never");
        journal.clear();
    }
    CHECK(journalIs({"b.leave", "a.leave"}));
}

} // namespace

int main() {
    testDeferredChanges();
    testFrameOrder();
    testPopDuringEvent();
    testDestructorLeaves();
    if (failures == 0) {
        std::printf("scene-tests: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}
