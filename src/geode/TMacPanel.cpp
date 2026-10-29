// =============================================================================
// TMac window and pause-menu entry point.
//
// Geode SDK v5.11.1 API, read from the SDK sources at tag v5.11.1
// (loader/include/Geode/ui/Popup.hpp, loader/src/ui/nodes/Popup.cpp):
//   * geode::Popup is a plain, non-templated FLAlertLayer subclass.
//   * Popup::init(width, height) is protected; call it from a derived method and
//     autorelease in create().
//   * The close handler is `virtual void onClose(CCObject*)`, protected. The
//     built-in X button and ESC both go through it, so overriding it is the only
//     way to catch every close.
//   * Popup::init creates m_mainLayer (content size = popup size, centred) and
//     m_buttonMenu (z-order 100, kept the same size by CopySizeLayout). Content
//     goes in those with coordinates relative to the window box; buttons go in
//     m_buttonMenu so they inherit the popup's forced touch priority.
//
// Still unverified: exact pixel placement has not been seen in game.
// =============================================================================
#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/Log.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/utils/Keyboard.hpp>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "TMacPanel.hpp"
#include "TMacRuntime.hpp"
#include "tmac/PanelView.hpp"

using namespace geode::prelude;

class TMacPanel;

namespace {

TMacPanel* g_panel = nullptr;

// Compact on purpose: this is a tool window, not a screen. 300x290 leaves the
// level visible behind it.
constexpr float kPanelWidth = 300.f;
constexpr float kPanelHeight = 290.f;
constexpr float kBarWidth = 240.f;
constexpr float kBarHeight = 6.f;
constexpr float kSmall = .5f;

}  // namespace

class TMacPanel : public geode::Popup {
public:
    static TMacPanel* create();
    static void open();

    void closeNow();

    void onModeOff(CCObject*);
    void onModeRecord(CCObject*);
    void onModePlay(CCObject*);
    void onSave(CCObject*);
    void onLoad(CCObject*);
    void onDelete(CCObject*);
    void onNew(CCObject*);
    void onRestart(CCObject*);
    void onPrevMacro(CCObject*);
    void onNextMacro(CCObject*);

protected:
    void onClose(CCObject* sender) override;

    bool setupPanel();
    void tick(float dt);
    void refresh();
    void refreshEntries();
    void updatePickerLabel();
    void addButton(const std::string& id, const char* label, SEL_MenuHandler handler, float x,
                   float y, float scale);
    void setButtonColor(const std::string& id, const cocos2d::ccColor3B& color);

    std::map<std::string, CCMenuItemSpriteExtra*> m_buttons;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_timer = nullptr;
    CCLabelBMFont* m_pick = nullptr;
    CCLabelBMFont* m_info = nullptr;
    CCLayerColor* m_barFill = nullptr;
    geode::TextInput* m_name = nullptr;

    std::vector<tmac::StoredMacro> m_entries;
    size_t m_pickIndex = 0;
    bool m_saved = false;
};

namespace {

bool isPanelShown() {
    return g_panel != nullptr && g_panel->getParent() != nullptr;
}

// Drops our reference. May be the last one, so nothing may touch the window after.
void releaseOpenPanel() {
    TMacPanel* panel = g_panel;
    g_panel = nullptr;
    if (panel) panel->release();
}

}  // namespace

TMacPanel* TMacPanel::create() {
    auto* ret = new TMacPanel();
    if (ret && ret->setupPanel()) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void TMacPanel::open() {
    if (g_panel) {
        if (isPanelShown()) return;  // one window only
        releaseOpenPanel();
    }
    TMacPanel* panel = create();
    if (!panel) return;
    panel->retain();
    g_panel = panel;
    panel->show();
}

void TMacPanel::closeNow() {
    this->onClose(nullptr);
}

void TMacPanel::onClose(CCObject* sender) {
    if (m_name) m_name->defocus();
    unschedule(schedule_selector(TMacPanel::tick));
    tmacRuntime().toggle.setOpen(false);
    geode::Popup::onClose(sender);
    if (g_panel == this) releaseOpenPanel();  // can free `this`, so it goes last
}

void tmacOnToggleKey(bool down, bool repeat) {
    TMacRuntime& rt = tmacRuntime();
    const tmac::ToggleController::Action action = rt.toggle.onKey(down, repeat);
    if (action == tmac::ToggleController::Action::None) return;

    geode::log::info("[TMac] toggle action={} inLevel={}",
                     action == tmac::ToggleController::Action::Open ? "open" : "close",
                     PlayLayer::get() != nullptr);

    if (action == tmac::ToggleController::Action::Open) {
        // Opening outside a level is allowed so the window can be inspected and
        // macros can be listed; recording and playback report that they need a
        // level instead of failing silently.
        TMacPanel::open();
        rt.toggle.setOpen(isPanelShown());
        if (!isPanelShown()) geode::log::warn("[TMac] panel did not open");
    } else {
        if (g_panel) g_panel->closeNow();
        rt.toggle.setOpen(false);
    }
}

void tmacCloseOpenPanel() {
    if (g_panel) g_panel->closeNow();
    tmacRuntime().toggle.setOpen(false);
}

// TAB toggles the window. This listens to the keyboard directly instead of to
// the "toggle-key" keybind setting: a keybind default Geode cannot parse, or an
// older settings file pointing at a missing setting, both leave the keybind
// unregistered and the listener silent. The setting is still honoured when it
// reads cleanly; Tab is the fallback.
$on_mod(Loaded) {
    geode::log::info("[TMac] register keyboard toggle listener");
    [[maybe_unused]] static auto toggleListener = geode::KeyboardInputEvent().listen(
        [](geode::KeyboardInputData& data) -> geode::ListenerResult {
            const bool down = data.action != geode::KeyboardInputData::Action::Release;
            const bool repeat = data.action == geode::KeyboardInputData::Action::Repeat;

            cocos2d::enumKeyCodes expected = cocos2d::KEY_Tab;
            try {
                if (geode::Mod::get()->hasSetting("toggle-key")) {
                    const std::vector<geode::Keybind> binds =
                        geode::Mod::get()->getSettingValue<std::vector<geode::Keybind>>("toggle-key");
                    if (!binds.empty() && binds.front().key != cocos2d::KEY_None) {
                        expected = binds.front().key;
                    }
                }
            } catch (...) {
                // Keep the Tab default.
            }

            geode::log::debug("[TMac] key={} action={} expected={}", static_cast<int>(data.key),
                              static_cast<int>(data.action), static_cast<int>(expected));

            if (data.key == expected && data.modifiers == geode::KeyboardModifier::None) {
                tmacOnToggleKey(down, repeat);
            }
            return geode::ListenerResult::Propagate;
        });
}

bool TMacPanel::setupPanel() {
    if (!geode::Popup::init(kPanelWidth, kPanelHeight)) return false;
    setTitle("TMac");

    // Modes.
    addButton("off", "Off", menu_selector(TMacPanel::onModeOff), -95.f, -44.f, kSmall);
    addButton("rec", "Rec", menu_selector(TMacPanel::onModeRecord), 0.f, -44.f, kSmall);
    addButton("play", "Play", menu_selector(TMacPanel::onModePlay), 95.f, -44.f, kSmall);

    // Macro name for saving.
    m_name = geode::TextInput::create(210.f, "Macro name");
    m_name->setMaxCharCount(32);
    m_mainLayer->addChildAtPosition(m_name, geode::Anchor::Top, ccp(0.f, -84.f));

    // Saved macro picker: < name >
    addButton("prev", "<", menu_selector(TMacPanel::onPrevMacro), -118.f, -122.f, kSmall);
    addButton("next", ">", menu_selector(TMacPanel::onNextMacro), 118.f, -122.f, kSmall);
    m_pick = CCLabelBMFont::create("(no saved macros)", "chatFont.fnt");
    m_pick->setScale(.55f);
    m_mainLayer->addChildAtPosition(m_pick, geode::Anchor::Top, ccp(0.f, -122.f));

    // File actions.
    addButton("save", "Save", menu_selector(TMacPanel::onSave), -62.f, -160.f, kSmall);
    addButton("load", "Load", menu_selector(TMacPanel::onLoad), 62.f, -160.f, kSmall);
    addButton("new", "New", menu_selector(TMacPanel::onNew), -62.f, -192.f, kSmall);
    addButton("delete", "Delete", menu_selector(TMacPanel::onDelete), 62.f, -192.f, kSmall);
    addButton("restart", "Restart", menu_selector(TMacPanel::onRestart), 0.f, -218.f, kSmall);

    // Status.
    m_status = CCLabelBMFont::create("Idle", "chatFont.fnt");
    m_status->setScale(.6f);
    m_mainLayer->addChildAtPosition(m_status, geode::Anchor::Top, ccp(0.f, -238.f));

    m_timer = CCLabelBMFont::create("", "chatFont.fnt");
    m_timer->setScale(.5f);
    m_mainLayer->addChildAtPosition(m_timer, geode::Anchor::Top, ccp(0.f, -254.f));

    auto* track = CCLayerColor::create(ccc4(0, 0, 0, 110), kBarWidth, kBarHeight);
    track->ignoreAnchorPointForPosition(false);
    track->setAnchorPoint(ccp(.5f, .5f));
    m_mainLayer->addChildAtPosition(track, geode::Anchor::Bottom, ccp(0.f, 26.f));

    m_barFill = CCLayerColor::create(ccc4(95, 215, 130, 255), 0.f, kBarHeight);
    m_barFill->ignoreAnchorPointForPosition(false);
    m_barFill->setAnchorPoint(ccp(0.f, .5f));
    m_barFill->setPosition(ccp(0.f, kBarHeight / 2.f));
    track->addChild(m_barFill);

    m_info = CCLabelBMFont::create("", "chatFont.fnt");
    m_info->setScale(.45f);
    m_info->setColor(ccc3(175, 175, 175));
    m_mainLayer->addChildAtPosition(m_info, geode::Anchor::Bottom, ccp(0.f, 8.f));

    schedule(schedule_selector(TMacPanel::tick), 0.05f);
    refreshEntries();
    refresh();
    return true;
}

void TMacPanel::addButton(const std::string& id, const char* label, SEL_MenuHandler handler,
                          float x, float y, float scale) {
    auto* sprite = ButtonSprite::create(label, scale);
    sprite->setCascadeOpacityEnabled(true);
    auto* button = CCMenuItemSpriteExtra::create(sprite, this, handler);
    button->setCascadeOpacityEnabled(true);
    m_buttonMenu->addChildAtPosition(button, geode::Anchor::Center, ccp(x, y));
    m_buttons[id] = button;
}

void TMacPanel::setButtonColor(const std::string& id, const cocos2d::ccColor3B& color) {
    auto it = m_buttons.find(id);
    if (it != m_buttons.end()) it->second->setColor(color);
}

void TMacPanel::tick(float) {
    refresh();
}

void TMacPanel::refreshEntries() {
    tmac::MacroStore* store = tmacStore();
    m_entries = store ? store->list() : std::vector<tmac::StoredMacro>{};
    if (m_pickIndex >= m_entries.size()) m_pickIndex = 0;
    updatePickerLabel();
}

void TMacPanel::updatePickerLabel() {
    if (!m_pick) return;
    if (m_entries.empty()) {
        m_pick->setString("(no saved macros)");
        return;
    }
    m_pick->setString(m_entries[m_pickIndex].name.c_str());
    m_pick->limitLabelWidth(150.f, .55f, .2f);
}

void TMacPanel::refresh() {
    TMacRuntime& rt = tmacRuntime();
    const tmac::PanelView v = tmac::buildPanelView(rt.session, tmacCurrentFrame(), m_saved);

    m_status->setString(v.status.c_str());
    if (v.recordingActive) {
        m_status->setColor(ccc3(255, 95, 95));
    } else if (v.paused) {
        m_status->setColor(ccc3(255, 215, 110));
    } else if (v.playbackActive) {
        m_status->setColor(ccc3(110, 230, 145));
    } else {
        m_status->setColor(ccc3(255, 255, 255));
    }

    std::string timer = v.elapsed + " / " + v.duration;
    if (v.eventCount > 0) timer += "  -  " + std::to_string(v.eventCount) + " inputs";
    m_timer->setString(timer.c_str());
    m_info->setString(v.statusDetail.c_str());

    const double clamped = std::clamp(v.progress, 0.0, 1.0);
    m_barFill->setContentSize(CCSize{static_cast<float>(kBarWidth * clamped), kBarHeight});

    // Selected mode is highlighted; the rest stay neutral.
    const ccColor3B neutral = ccc3(170, 170, 170);
    const bool playing = v.playbackActive || v.paused;
    setButtonColor("rec", v.recordingActive ? ccc3(255, 95, 95) : neutral);
    setButtonColor("play", playing ? ccc3(110, 230, 145) : neutral);
    setButtonColor("off",
                   (!v.recordingActive && !playing) ? ccc3(255, 255, 255) : neutral);

    // Nothing to load or delete without a saved macro, nothing to save without
    // one in memory.
    const bool hasEntries = !m_entries.empty();
    const bool hasMacro = rt.session.hasMacro();
    if (auto it = m_buttons.find("load"); it != m_buttons.end()) {
        it->second->setEnabled(hasEntries);
        it->second->setOpacity(hasEntries ? 255 : 90);
    }
    if (auto it = m_buttons.find("delete"); it != m_buttons.end()) {
        it->second->setEnabled(hasEntries);
        it->second->setOpacity(hasEntries ? 255 : 90);
    }
    if (auto it = m_buttons.find("save"); it != m_buttons.end()) {
        it->second->setEnabled(hasMacro);
        it->second->setOpacity(hasMacro ? 255 : 90);
    }
    for (const char* id : {"prev", "next"}) {
        if (auto it = m_buttons.find(id); it != m_buttons.end()) {
            it->second->setEnabled(hasEntries);
            it->second->setOpacity(hasEntries ? 255 : 90);
        }
    }
    if (auto it = m_buttons.find("restart"); it != m_buttons.end()) {
        it->second->setEnabled(hasMacro);
        it->second->setOpacity(hasMacro ? 255 : 90);
    }
}

void TMacPanel::onModeOff(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (rt.session.isRecording()) rt.session.stopRecording(tmacCurrentFrame());
    if (rt.session.isPlaybackActive()) dispatchToGame(rt.session.stop());
    rt.session.setMessage("Stopped.");
    refresh();
}

void TMacPanel::onModeRecord(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (!PlayLayer::get()) {
        rt.session.setMessage("Open a level to record.");
        refresh();
        return;
    }
    if (rt.session.startRecording(tmacCurrentFrame(), tmac::MacroMeta{})) {
        m_saved = false;
    }
    refresh();
}

void TMacPanel::onModePlay(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (!PlayLayer::get()) {
        rt.session.setMessage("Open a level to play a macro.");
        refresh();
        return;
    }
    if (rt.session.playerState() == tmac::Player::State::Paused) {
        dispatchToGame(rt.session.resume());
    } else if (rt.session.playerState() == tmac::Player::State::Playing) {
        dispatchToGame(rt.session.pause());
    } else {
        rt.session.play();
    }
    refresh();
}

void TMacPanel::onSave(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    tmac::MacroStore* store = tmacStore();
    if (!store) {
        rt.session.setMessage("The save folder is unavailable.");
        refresh();
        return;
    }
    std::string base = m_name ? m_name->getString().c_str() : std::string();
    if (base.empty()) base = "Macro";

    std::string fileName;
    std::string message;
    tmac::StoreError err = store->save(base, rt.session.currentMacro(), fileName, false, message);
    if (err == tmac::StoreError::AlreadyExists) {
        // Typing an existing name is an explicit overwrite request.
        err = store->save(base, rt.session.currentMacro(), fileName, true, message);
    }
    if (err == tmac::StoreError::Ok) {
        m_saved = true;
        refreshEntries();
        for (size_t i = 0; i < m_entries.size(); ++i) {
            if (m_entries[i].fileName == fileName) m_pickIndex = i;
        }
        updatePickerLabel();
    }
    rt.session.setMessage(message);
    refresh();
}

void TMacPanel::onLoad(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    tmac::MacroStore* store = tmacStore();
    if (!store || m_entries.empty()) return;
    if (m_pickIndex >= m_entries.size()) m_pickIndex = 0;

    tmac::Macro macro;
    std::string message;
    if (store->load(m_entries[m_pickIndex].fileName, macro, message) == tmac::StoreError::Ok &&
        rt.session.setCurrentMacro(std::move(macro))) {
        m_saved = true;
        if (m_name) m_name->setString(m_entries[m_pickIndex].name.c_str());
        rt.session.setMessage("Loaded " + m_entries[m_pickIndex].name + ".");
    } else {
        rt.session.setMessage(message);
    }
    refresh();
}

void TMacPanel::onDelete(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    tmac::MacroStore* store = tmacStore();
    if (!store || m_entries.empty()) return;
    const std::string name = m_entries[m_pickIndex].name;
    if (store->remove(m_entries[m_pickIndex].fileName) == tmac::StoreError::Ok) {
        rt.session.setMessage("Deleted " + name + ".");
        refreshEntries();
    } else {
        rt.session.setMessage("Could not delete " + name + ".");
    }
    refresh();
}

void TMacPanel::onNew(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (rt.session.clearCurrentMacro()) {
        m_saved = false;
        rt.session.setMessage("New macro. Press Rec to start.");
    }
    refresh();
}

void TMacPanel::onRestart(CCObject*) {
    dispatchToGame(tmacRuntime().session.restart());
    refresh();
}

void TMacPanel::onPrevMacro(CCObject*) {
    if (m_entries.empty()) return;
    m_pickIndex = (m_pickIndex + m_entries.size() - 1) % m_entries.size();
    updatePickerLabel();
}

void TMacPanel::onNextMacro(CCObject*) {
    if (m_entries.empty()) return;
    m_pickIndex = (m_pickIndex + 1) % m_entries.size();
    updatePickerLabel();
}

// Opens TMac from the pause menu. The button placement is unverified.
class $modify(TMacPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        TMacRuntime& rt = tmacRuntime();
        dispatchToGame(rt.session.onLevelEvent(tmac::LevelEvent::GamePaused, tmacCurrentFrame()));

        CCMenu* menu = CCMenu::create();
        auto* button = CCMenuItemSpriteExtra::create(ButtonSprite::create("TMac"), this,
                                                     menu_selector(TMacPauseLayer::onOpenTMac));
        const CCSize size = CCDirector::sharedDirector()->getWinSize();
        menu->setPosition(ccp(size.width - 60.f, 40.f));
        button->setPosition(ccp(0.f, 0.f));
        menu->addChild(button);
        addChild(menu);
    }

    void onResume(CCObject* sender) {
        TMacRuntime& rt = tmacRuntime();
        dispatchToGame(rt.session.onLevelEvent(tmac::LevelEvent::GameResumed, tmacCurrentFrame()));
        PauseLayer::onResume(sender);
    }

    void onOpenTMac(CCObject*) { TMacPanel::open(); }
};
