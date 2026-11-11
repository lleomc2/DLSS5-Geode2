#pragma once

#include <atomic>

namespace dlss5 {

constexpr int kTierCount = 4; // Rendimiento, Equilibrado, Ultra, MAX

inline char const* qualityName(int q) {
    constexpr char const* names[kTierCount] = {"Rendimiento", "Equilibrado", "Ultra", "MAX"};
    return names[q < 0 ? 0 : (q >= kTierCount ? kTierCount - 1 : q)];
}

// Cached copy of the mod settings. It is refreshed whenever the user changes a setting,
// so the render loop never has to do string lookups.
struct Config {
    std::atomic<bool> enabled{true};
    std::atomic<int> quality{1};   // 0 = Rendimiento, 1 = Equilibrado, 2 = Ultra, 3 = MAX
    std::atomic<bool> experimental{false};
    std::atomic<int> look{0};      // index into the style table (see Pipeline.cpp)
    std::atomic<float> intensity{1.f};
    std::atomic<float> detail{1.f};
    std::atomic<float> bloom{1.f};
    std::atomic<float> relight{1.f};
    std::atomic<float> shadows{1.f};
    std::atomic<float> film{1.f};
    std::atomic<float> reflect{1.f};  // MAX only
    std::atomic<float> gi{1.f};       // MAX only
    std::atomic<bool> playerLight{true};
    std::atomic<bool> menus{true};
    std::atomic<bool> autoQuality{false};
    std::atomic<bool> compare{false};
    std::atomic<float> comparePos{0.5f};
};

Config& config();
void initConfig(); // safe to call every frame; only does work the first time

} // namespace dlss5
