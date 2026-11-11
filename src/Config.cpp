#include "Config.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <initializer_list>
#include <mutex>
#include <string>

using namespace geode::prelude;

namespace dlss5 {

Config& config() {
    static Config c;
    return c;
}

namespace {

int indexOf(std::string const& value, std::initializer_list<char const*> names, int fallback) {
    int i = 0;
    for (auto const* name : names) {
        if (value == name) return i;
        ++i;
    }
    return fallback;
}

float percent(double v, double lo, double hi) {
    return static_cast<float>(std::clamp(v, lo, hi) / 100.0);
}

void reload() {
    auto* mod = Mod::get();
    auto& c = config();
    c.enabled = mod->getSettingValue<bool>("enabled");
    c.quality = indexOf(mod->getSettingValue<std::string>("quality"), {"Rendimiento", "Equilibrado", "Ultra", "MAX", "Experimental"}, 1);
    c.experimental = mod->getSettingValue<std::string>("quality") == "Experimental";
    c.look = indexOf(mod->getSettingValue<std::string>("look"), {"Cinematico", "Natural", "Calido", "Frio", "Neon"}, 0);
    c.intensity = percent(mod->getSettingValue<double>("intensity"), 0.0, 130.0);
    c.detail = percent(mod->getSettingValue<double>("detail"), 0.0, 150.0);
    c.bloom = percent(mod->getSettingValue<double>("bloom"), 0.0, 150.0);
    c.relight = percent(mod->getSettingValue<double>("relight"), 0.0, 150.0);
    c.shadows = percent(mod->getSettingValue<double>("shadows"), 0.0, 150.0);
    c.film = percent(mod->getSettingValue<double>("film"), 0.0, 150.0);
    c.reflect = percent(mod->getSettingValue<double>("reflect"), 0.0, 150.0);
    c.gi = percent(mod->getSettingValue<double>("gi"), 0.0, 150.0);
    c.autoQuality = mod->getSettingValue<bool>("auto-quality");
    c.playerLight = mod->getSettingValue<bool>("player-light");
    c.menus = mod->getSettingValue<bool>("menus");
    c.compare = mod->getSettingValue<bool>("compare");
    c.comparePos = percent(mod->getSettingValue<double>("compare-pos"), 5.0, 95.0);
}

} // namespace

void initConfig() {
    static std::once_flag once;
    std::call_once(once, [] {
        reload();
        listenForAllSettingChanges([](std::string_view, std::shared_ptr<SettingV3>) {
            reload();
        });
    });
}

} // namespace dlss5
