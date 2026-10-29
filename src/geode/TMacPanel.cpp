// =============================================================================
// TMac window and pause-menu entry point.
//
// Geode SDK v5.11.1 API, read from the SDK sources at tag v5.11.1
// (loader/include/Geode/ui/Popup.hpp and loader/src/ui/nodes/Popup.cpp):
//   * geode::Popup is a plain, non-templated FLAlertLayer subclass.
//   * Popup::init(width, height) is protected, so it is called from a derived
//     method and the object is autoreleased in create().
//   * The close handler is `virtual void onClose(CCObject*)` - it takes the
//     sender and is protected. The base version sends CloseEvent, disables
//     touch and keypad input, then removes the popup from its parent. The
//     built-in X button and keyBackClicked() (ESC) both go through it, so
//     overriding it is the only way to catch every close.
//   * Popup::init creates m_mainLayer (content size = popup size, centred on
//     screen) and m_buttonMenu (z-order 100, kept the same size as m_mainLayer
//     by CopySizeLayout). Content belongs in those nodes, with coordinates
//     relative to the window box, not in screen coordinates.
//   * Buttons go in m_buttonMenu so they inherit the popup's forced touch
//     priority; a hand-made CCMenu added to the popup is not guaranteed to
//     receive touches while the popup is swallowing them.
//
// Still unverified: exact pixel placement has not been checked in-game, and
// the pause-menu button position is a guess.
// =============================================================================
#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>
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

// The one open window. It is retained while it is held here, so a window the
// game removed without telling us can never be a dangling pointer.
TMacPanel* g_panel = nullptr;

constexpr float kPanelWidth = 380.f;
constexpr float kPanelHeight = 340.f;
constexpr float kBarWidth = 300.f;
constexpr float kBarHeight = 8.f;

}  // namespace

class TMacPanel : public geode::Popup {
public:
    static TMacPanel* create();
    static void open();

    // Closes the window from outside the class (toggle keybind, level exit).
    void closeNow();

    void onRecord(CCObject*);
    void onStopRecording(CCObject*);
    void onPlay(CCObject*);
    void onPause(CCObject*);
    void onResume(CCObject*);
    void onStop(CCObject*);
    void onRestart(CCObject*);
    void onSave(CCObject*);
    void onLoad(CCObject*);

protected:
    // Popup's close handler. `sender` is the X button, or null when the close
    // came from ESC or from closeNow().
    void onClose(CCObject* sender) override;

    bool setupPanel();
    void tick(float dt);
    void refresh();
    void addButton(const std::string& id, const char* label, SEL_MenuHandler handler, float x,
                   float y);
    void setButtonEnabled(const std::string& id, bool enabled);

    std::map<std::string, CCMenuItemSpriteExtra*> m_buttons;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_timer = nullptr;
    CCLabelBMFont* m_counts = nullptr;
    CCLabelBMFont* m_info = nullptr;
    CCLayerColor* m_barFill = nullptr;
    bool m_saved = false;  // whether the current macro has been written to disk
};

namespace {

bool isPanelShown() {
    return g_panel != nullptr && g_panel->getParent() != nullptr;
}

// Drops our reference. May be the last one, so nothing must touch the window
// after this returns.
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
        releaseOpenPanel();          // removed by the game without a close: forget it
    }
    TMacPanel* panel = create();
    if (!panel) return;
    panel->retain();  // held while it is open
    g_panel = panel;
    panel->show();
}

void TMacPanel::closeNow() {
    this->onClose(nullptr);
}

void TMacPanel::onClose(CCObject* sender) {
    unschedule(schedule_selector(TMacPanel::tick));
    tmacRuntime().toggle.setOpen(false);
    // Base removes the popup from the scene; our own retain keeps `this` alive
    // across the call.
    geode::Popup::onClose(sender);
    // Last statement on purpose: this can free the window.
    if (g_panel == this) releaseOpenPanel();
}

void tmacOnToggleKey(bool down, bool repeat) {
    TMacRuntime& rt = tmacRuntime();
    switch (rt.toggle.onKey(down, repeat)) {
        case tmac::ToggleController::Action::Open:
            // Only inside a level, because recording and playback need one.
            if (!PlayLayer::get()) {
                rt.toggle.setOpen(false);
                return;
            }
            TMacPanel::open();
            rt.toggle.setOpen(isPanelShown());
            break;
        case tmac::ToggleController::Action::Close:
            if (g_panel) g_panel->closeNow();
            rt.toggle.setOpen(false);
            break;
        case tmac::ToggleController::Action::None:
            break;
    }
}

void tmacCloseOpenPanel() {
    if (g_panel) g_panel->closeNow();
    tmacRuntime().toggle.setOpen(false);
}

// TAB toggles the window.
//
// This listens to the keyboard directly rather than to the "toggle-key"
// keybind setting, because the keybind path has two ways to fail silently: a
// keybind default that Geode cannot parse leaves the setting unregistered, and
// an older settings file on disk can keep pointing at a setting that no longer
// exists ("Missing setting"). Neither leaves the mod broken here: the setting
// is still honoured when it is readable, and Tab always works.
//
// Geode's own keybind dispatch (LoaderImpl) maps a press to
// KeyboardInputData::Action::Press, so `down` below means "not a release".
$on_mod(Loaded) {
    [[maybe_unused]] static auto toggleListener = geode::KeyboardInputEvent().listen(
        [](geode::KeyboardInputData& data) -> geode::ListenerResult {
            const bool down = data.action != geode::KeyboardInputData::Action::Release;
            const bool repeat = data.action == geode::KeyboardInputData::Action::Repeat;

            cocos2d::enumKeyCodes expected = cocos2d::KEY_Tab;
            try {
                // A keybind setting's value type is std::vector<Keybind>, one
                // entry per bound key. An absent or unparsable setting reports
                // false here or throws; either way the Tab default stands.
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

            if (data.key == expected && data.modifiers == geode::KeyboardModifier::None) {
                tmacOnToggleKey(down, repeat);
            }
            return geode::ListenerResult::Propagate;
        });
}

bool TMacPanel::setupPanel() {
    if (!geode::Popup::init(kPanelWidth, kPanelHeight)) return false;
    setTitle("TMac");

    // Offsets are relative to the window box (m_mainLayer), so the layout does
    // not depend on the screen resolution.
    m_status = CCLabelBMFont::create("Idle", "bigFont.fnt");
    m_status->setScale(.55f);
    m_mainLayer->addChildAtPosition(m_status, geode::Anchor::Top, ccp(0.f, -50.f));

    m_timer = CCLabelBMFont::create("", "chatFont.fnt");
    m_timer->setScale(.7f);
    m_mainLayer->addChildAtPosition(m_timer, geode::Anchor::Top, ccp(0.f, -74.f));

    // Playback progress. The track is a plain dark bar; the fill is a child of
    // it anchored on the left edge, so only its width has to change.
    auto* track = CCLayerColor::create(ccc4(0, 0, 0, 110), kBarWidth, kBarHeight);
    track->ignoreAnchorPointForPosition(false);
    track->setAnchorPoint(ccp(.5f, .5f));
    m_mainLayer->addChildAtPosition(track, geode::Anchor::Top, ccp(0.f, -92.f));

    m_barFill = CCLayerColor::create(ccc4(95, 215, 130, 255), 0.f, kBarHeight);
    m_barFill->ignoreAnchorPointForPosition(false);
    m_barFill->setAnchorPoint(ccp(0.f, .5f));
    m_barFill->setPosition(ccp(0.f, kBarHeight / 2.f));
    track->addChild(m_barFill);

    m_counts = CCLabelBMFont::create("", "chatFont.fnt");
    m_counts->setScale(.5f);
    m_counts->setColor(ccc3(170, 170, 170));
    m_mainLayer->addChildAtPosition(m_counts, geode::Anchor::Top, ccp(0.f, -108.f));

    m_info = CCLabelBMFont::create("", "chatFont.fnt");
    m_info->setScale(.6f);
    m_mainLayer->addChildAtPosition(m_info, geode::Anchor::Bottom, ccp(0.f, 16.f));

    const float colX[2] = {-85.f, 85.f};
    const float rowY[4] = {26.f, -12.f, -50.f, -88.f};
    addButton("record", "Record", menu_selector(TMacPanel::onRecord), colX[0], rowY[0]);
    addButton("stopRecord", "Stop Rec", menu_selector(TMacPanel::onStopRecording), colX[1], rowY[0]);
    addButton("play", "Play", menu_selector(TMacPanel::onPlay), colX[0], rowY[1]);
    addButton("pause", "Pause", menu_selector(TMacPanel::onPause), colX[1], rowY[1]);
    addButton("resume", "Resume", menu_selector(TMacPanel::onResume), colX[0], rowY[2]);
    addButton("stop", "Stop", menu_selector(TMacPanel::onStop), colX[1], rowY[2]);
    addButton("restart", "Restart", menu_selector(TMacPanel::onRestart), colX[0], rowY[3]);
    addButton("save", "Save", menu_selector(TMacPanel::onSave), colX[1], rowY[3]);
    addButton("load", "Load", menu_selector(TMacPanel::onLoad), 0.f, -126.f);

    schedule(schedule_selector(TMacPanel::tick), 0.05f);
    refresh();
    return true;
}

void TMacPanel::addButton(const std::string& id, const char* label, SEL_MenuHandler handler,
                          float x, float y) {
    auto* sprite = ButtonSprite::create(label);
    sprite->setCascadeOpacityEnabled(true);
    auto* button = CCMenuItemSpriteExtra::create(sprite, this, handler);
    button->setCascadeOpacityEnabled(true);
    // m_buttonMenu already carries the popup's touch priority.
    m_buttonMenu->addChildAtPosition(button, geode::Anchor::Center, ccp(x, y));
    m_buttons[id] = button;
}

void TMacPanel::setButtonEnabled(const std::string& id, bool enabled) {
    auto it = m_buttons.find(id);
    if (it == m_buttons.end()) return;
    it->second->setEnabled(enabled);
    // setEnabled alone leaves the sprite looking clickable, so dim it too.
    it->second->setOpacity(enabled ? 255 : 90);
}

void TMacPanel::tick(float) {
    refresh();  // keeps progress and timer current while playing
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

    m_timer->setString((v.elapsed + " / " + v.duration).c_str());
    m_info->setString(v.statusDetail.c_str());
    std::string counts = std::to_string(v.eventCount) + " inputs";
    if (v.eventCount > 0 && !m_saved) counts += " - unsaved";
    m_counts->setString(counts.c_str());

    const double clamped = std::clamp(v.progress, 0.0, 1.0);
    m_barFill->setContentSize(CCSize{static_cast<float>(kBarWidth * clamped), kBarHeight});

    setButtonEnabled("record", v.canRecord);
    setButtonEnabled("stopRecord", v.canStopRecording);
    setButtonEnabled("play", v.canPlay);
    setButtonEnabled("pause", v.canPause);
    setButtonEnabled("resume", v.canResume);
    setButtonEnabled("stop", v.canStopPlayback);
    setButtonEnabled("restart", v.canRestart);
    setButtonEnabled("save", v.canSave);
    setButtonEnabled("load", v.canLoad);
}

void TMacPanel::onRecord(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (rt.session.startRecording(tmacCurrentFrame(), tmac::MacroMeta{})) m_saved = false;
    refresh();
}

void TMacPanel::onStopRecording(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (rt.session.stopRecording(tmacCurrentFrame())) m_saved = false;
    refresh();
}

void TMacPanel::onPlay(CCObject*) {
    tmacRuntime().session.play();
    refresh();
}

void TMacPanel::onPause(CCObject*) {
    dispatchToGame(tmacRuntime().session.pause());
    refresh();
}

void TMacPanel::onResume(CCObject*) {
    dispatchToGame(tmacRuntime().session.resume());
    refresh();
}

void TMacPanel::onStop(CCObject*) {
    dispatchToGame(tmacRuntime().session.stop());
    refresh();
}

void TMacPanel::onRestart(CCObject*) {
    dispatchToGame(tmacRuntime().session.restart());
    refresh();
}

void TMacPanel::onSave(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    tmac::MacroStore* store = tmacStore();
    if (!store) {
        rt.session.setMessage("The save folder is unavailable.");
    } else {
        std::string fileName;
        std::string message;
        // Names are generated so an existing macro is never overwritten silently.
        const tmac::StoreError err =
            store->save(store->uniqueName("Macro"), rt.session.currentMacro(), fileName, false, message);
        if (err == tmac::StoreError::Ok) m_saved = true;
        rt.session.setMessage(message);
    }
    refresh();
}

void TMacPanel::onLoad(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (rt.session.hasMacro() && !m_saved) {
        rt.session.setMessage("Save the current macro before loading another.");
        refresh();
        return;
    }
    tmac::MacroStore* store = tmacStore();
    if (!store) return;
    const auto entries = store->list();
    if (entries.empty()) {
        rt.session.setMessage("No saved macros yet.");
        refresh();
        return;
    }
    if (rt.nextLoadIndex >= entries.size()) rt.nextLoadIndex = 0;
    const tmac::StoredMacro& entry = entries[rt.nextLoadIndex++];

    tmac::Macro macro;
    std::string message;
    if (store->load(entry.fileName, macro, message) == tmac::StoreError::Ok &&
        rt.session.setCurrentMacro(std::move(macro))) {
        m_saved = true;
    } else {
        rt.session.setMessage(message);
    }
    refresh();
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
