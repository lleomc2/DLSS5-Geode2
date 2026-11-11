// Minimal stand-in for the Geode/cocos API that Pipeline.cpp touches, so the REAL Pipeline.cpp can be
// compiled and executed on a desktop (Mesa software GLES2) for previews and self-tests.
#pragma once
#include <cstdio>
#include <format>
#include <functional>
#include <string>
#include <utility>

namespace cocos2d {
struct CCPoint { float x = 0, y = 0; };
struct CCSize { float width = 0, height = 0; };
struct CCNode {
    CCPoint pos;
    CCNode* parent = nullptr;
    bool visible = true;
    bool isVisible() const { return visible; }
    CCNode* getParent() const { return parent; }
    CCPoint getPosition() const { return pos; }
    CCPoint convertToWorldSpace(CCPoint const& p) const { return p; }
};
struct CCDirector {
    unsigned frames = 0;
    CCSize win{1600.f, 720.f};
    double interval = 1.0 / 60.0;
    static CCDirector* sharedDirector() { static CCDirector d; return &d; }
    unsigned getTotalFrames() const { return frames; }
    CCSize getWinSize() const { return win; }
    double getAnimationInterval() const { return interval; }
};
} // namespace cocos2d

struct PlayerObject : cocos2d::CCNode {};
struct PlayLayer {
    PlayerObject* m_player1 = nullptr;
    static PlayLayer*& slot() { static PlayLayer* p = nullptr; return p; }
    static PlayLayer* get() { return slot(); }
};
struct LevelEditorLayer {
    static LevelEditorLayer* get() { return nullptr; }
};

namespace geode {
namespace log {
template <class... A> void info(std::format_string<A...> f, A&&... a) { std::puts(("[info]  " + std::format(f, std::forward<A>(a)...)).c_str()); }
template <class... A> void warn(std::format_string<A...> f, A&&... a) { std::puts(("[warn]  " + std::format(f, std::forward<A>(a)...)).c_str()); }
template <class... A> void error(std::format_string<A...> f, A&&... a) { std::puts(("[error] " + std::format(f, std::forward<A>(a)...)).c_str()); }
} // namespace log
enum class NotificationIcon { None, Loading, Success, Warning, Error, Info };
struct Notification {
    static Notification* create(std::string const& t, NotificationIcon, float) { std::printf("[toast] %s\n", t.c_str()); static Notification n; return &n; }
    void show() {}
};
struct Loader {
    static Loader* get() { static Loader l; return &l; }
    void queueInMainThread(std::function<void()> f) { f(); }
};
namespace prelude {
using namespace geode;
using namespace cocos2d;
} // namespace prelude
} // namespace geode
