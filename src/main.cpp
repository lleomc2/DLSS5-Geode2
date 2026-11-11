#include <Geode/Geode.hpp>
#include <Geode/modify/CCDirector.hpp>
#include <Geode/modify/CCDisplayLinkDirector.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include "Config.hpp"
#include "Pipeline.hpp"

using namespace geode::prelude;

// The frame is complete (and still sitting in the back buffer) once drawScene() returns, so that is
// the moment to run the post-processing. mainLoop() is hooked too as a safety net in case
// drawScene() was inlined into it; Pipeline ignores a frame it has already processed.
class $modify(DLSS5DrawScene, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();
        dlss5::onFrameRendered();
    }
};

class $modify(DLSS5MainLoop, CCDisplayLinkDirector) {
    void mainLoop() {
        CCDisplayLinkDirector::mainLoop();
        dlss5::onFrameRendered();
    }
};

namespace {

std::string dlss5Label() {
    auto* mod = Mod::get();
    if (!mod->getSettingValue<bool>("enabled")) return "DLSS 5: Off";
    return "DLSS 5: " + mod->getSettingValue<std::string>("quality");
}

} // namespace

// Quick toggle in the pause menu: Off -> Rendimiento -> Equilibrado -> Ultra -> MAX -> Experimental -> Off
class $modify(DLSS5PauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        auto winSize = CCDirector::sharedDirector()->getWinSize();
        auto* menu = CCMenu::create();
        menu->setID("dlss5-menu"_spr);
        menu->setPosition({0.f, 0.f});

        auto* spr = ButtonSprite::create(dlss5Label().c_str(), "goldFont.fnt", "GJ_button_01.png", 0.6f);
        auto* btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(DLSS5PauseLayer::onDLSS5));
        btn->setID("dlss5-button"_spr);
        btn->setPosition({86.f, winSize.height - 24.f});
        menu->addChild(btn);

        this->addChild(menu, 100);
        geode::cocos::handleTouchPriority(this);
    }

    void onDLSS5(CCObject* sender) {
        auto* mod = Mod::get();
        bool enabled = mod->getSettingValue<bool>("enabled");
        auto quality = mod->getSettingValue<std::string>("quality");

        if (!enabled) {
            mod->setSettingValue<bool>("enabled", true);
            mod->setSettingValue<std::string>("quality", std::string("Rendimiento"));
        } else if (quality == "Rendimiento") {
            mod->setSettingValue<std::string>("quality", std::string("Equilibrado"));
        } else if (quality == "Equilibrado") {
            mod->setSettingValue<std::string>("quality", std::string("Ultra"));
        } else if (quality == "Ultra") {
            mod->setSettingValue<std::string>("quality", std::string("MAX"));
        } else if (quality == "MAX") {
            mod->setSettingValue<std::string>("quality", std::string("Experimental"));
        } else {
            mod->setSettingValue<bool>("enabled", false);
        }

        if (auto* item = typeinfo_cast<CCMenuItemSpriteExtra*>(sender)) {
            if (auto* label = typeinfo_cast<ButtonSprite*>(item->getNormalImage())) {
                label->setString(dlss5Label().c_str());
            }
        }
    }
};
