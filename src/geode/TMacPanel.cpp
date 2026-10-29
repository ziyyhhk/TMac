// =============================================================================
// TMac window and pause-menu entry point.
//
// Geode SDK v5.11.1 API used here (from the Geode v5 documentation):
//   * geode::Popup is NOT templated. Subclass it as `public geode::Popup`.
//   * Initialise with Popup::init(width, height) inside a derived method that
//     returns bool, then autorelease the object in create().
//   * Popup::setTitle, Popup::show (FLAlertLayer), Popup::onClose (virtual, no args).
//   * There is no setup() hook and no m_mainLayer in v5. Content is added as
//     children of the popup itself.
//   * Include paths: Geode/ui/Popup.hpp, Geode/binding/CCMenuItemSpriteExtra.hpp,
//     Geode/binding/ButtonSprite.hpp, Geode/loader/SettingV3.hpp.
//
// Still needs a compiled check: child positions are computed from the window
// centre, and the pause-menu button placement is unverified visually.
// =============================================================================
#include <Geode/Geode.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/CCMenuItemSpriteExtra.hpp>
#include <Geode/loader/SettingV3.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>

#include <map>
#include <string>

#include "TMacPanel.hpp"
#include "TMacRuntime.hpp"
#include "tmac/PanelView.hpp"

using namespace geode::prelude;

class TMacPanel;

namespace {

// The one open window. It is retained while it is held here, so a window the
// game removed without telling us can never be a dangling pointer.
TMacPanel* g_panel = nullptr;

}  // namespace

class TMacPanel : public geode::Popup {
public:
    static TMacPanel* create();
    static void open();

    // Popup's close handler, virtual in v5 and takes no arguments.
    void onClose() override;

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
    bool setupPanel();
    void tick(float dt);
    void refresh();
    void addButton(CCMenu* menu, const std::string& id, const char* label, SEL_MenuHandler handler,
                   float x, float y);
    void setButtonEnabled(const std::string& id, bool enabled);

    std::map<std::string, CCMenuItemSpriteExtra*> m_buttons;
    CCLabelBMFont* m_status = nullptr;
    CCLabelBMFont* m_timer = nullptr;
    CCLabelBMFont* m_info = nullptr;
    bool m_saved = false;  // whether the current macro has been written to disk
};

namespace {

bool isPanelShown() {
    return g_panel != nullptr && g_panel->getParent() != nullptr;
}

// Drops our reference. The autorelease pool and the scene still hold theirs.
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

void TMacPanel::onClose() {
    geode::Popup::onClose();  // base removes the popup from the scene
    unschedule(schedule_selector(TMacPanel::tick));
    if (g_panel == this) releaseOpenPanel();
    tmacRuntime().toggle.setOpen(false);
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
            if (g_panel) g_panel->onClose();
            rt.toggle.setOpen(false);
            break;
        case tmac::ToggleController::Action::None:
            break;
    }
}

void tmacCloseOpenPanel() {
    if (g_panel) g_panel->onClose();
    tmacRuntime().toggle.setOpen(false);
}

// TAB is registered as a keybind setting in mod.json ("toggle-key").
// UNVERIFIED: the keybind default JSON shape; the Geode VS Code extension validates mod.json.
$on_mod(Loaded) {
    [[maybe_unused]] static auto* toggleListener = geode::listenForKeybindSettingPresses(
        "toggle-key",
        [](auto const&, bool down, bool repeat, double) -> bool {
            tmacOnToggleKey(down, repeat);
            return true;
        },
        geode::Mod::get());
}

bool TMacPanel::setupPanel() {
    if (!geode::Popup::init(380.f, 340.f)) return false;
    setTitle("TMac");

    // Children of the popup are placed in screen coordinates, relative to the centre.
    const CCSize win = CCDirector::sharedDirector()->getWinSize();
    const float cx = win.width / 2.f;
    const float cy = win.height / 2.f;

    m_status = CCLabelBMFont::create("Idle", "bigFont.fnt");
    m_status->setPosition(ccp(cx, cy + 100.f));
    addChild(m_status);

    m_timer = CCLabelBMFont::create("", "chatFont.fnt");
    m_timer->setPosition(ccp(cx, cy + 72.f));
    addChild(m_timer);

    m_info = CCLabelBMFont::create("", "chatFont.fnt");
    m_info->setPosition(ccp(cx, cy - 150.f));
    addChild(m_info);

    CCMenu* menu = CCMenu::create();
    menu->setPosition(ccp(0.f, 0.f));
    addChild(menu);

    const float colX[2] = {cx - 85.f, cx + 85.f};
    const float rowY[4] = {cy + 30.f, cy - 10.f, cy - 50.f, cy - 90.f};
    addButton(menu, "record", "Record", menu_selector(TMacPanel::onRecord), colX[0], rowY[0]);
    addButton(menu, "stopRecord", "Stop Rec", menu_selector(TMacPanel::onStopRecording), colX[1], rowY[0]);
    addButton(menu, "play", "Play", menu_selector(TMacPanel::onPlay), colX[0], rowY[1]);
    addButton(menu, "restart", "Restart", menu_selector(TMacPanel::onRestart), colX[1], rowY[1]);
    addButton(menu, "pause", "Pause", menu_selector(TMacPanel::onPause), colX[0], rowY[2]);
    addButton(menu, "resume", "Resume", menu_selector(TMacPanel::onResume), colX[1], rowY[2]);
    addButton(menu, "stop", "Stop", menu_selector(TMacPanel::onStop), colX[0], rowY[3]);
    addButton(menu, "save", "Save", menu_selector(TMacPanel::onSave), colX[1], rowY[3]);
    addButton(menu, "load", "Load", menu_selector(TMacPanel::onLoad), cx, cy - 130.f);

    schedule(schedule_selector(TMacPanel::tick), 0.1f);
    refresh();
    return true;
}

void TMacPanel::addButton(CCMenu* menu, const std::string& id, const char* label, SEL_MenuHandler handler,
                          float x, float y) {
    auto* button = CCMenuItemSpriteExtra::create(ButtonSprite::create(label), this, handler);
    button->setPosition(ccp(x, y));
    menu->addChild(button);
    m_buttons[id] = button;
}

void TMacPanel::setButtonEnabled(const std::string& id, bool enabled) {
    auto it = m_buttons.find(id);
    if (it != m_buttons.end()) it->second->setEnabled(enabled);
}

void TMacPanel::tick(float) {
    refresh();  // keeps progress and timer current while playing
}

void TMacPanel::refresh() {
    TMacRuntime& rt = tmacRuntime();
    const tmac::PanelView v = tmac::buildPanelView(rt.session, rt.clock.frame(), m_saved);

    m_status->setString(v.status.c_str());
    m_timer->setString((v.elapsed + " / " + v.duration).c_str());
    m_info->setString(v.statusDetail.c_str());

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
    if (rt.session.startRecording(rt.clock.frame(), tmac::MacroMeta{})) m_saved = false;
    refresh();
}

void TMacPanel::onStopRecording(CCObject*) {
    TMacRuntime& rt = tmacRuntime();
    if (rt.session.stopRecording(rt.clock.frame())) m_saved = false;
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
        dispatchToGame(rt.session.onLevelEvent(tmac::LevelEvent::GamePaused, rt.clock.frame()));

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
        dispatchToGame(rt.session.onLevelEvent(tmac::LevelEvent::GameResumed, rt.clock.frame()));
        PauseLayer::onResume(sender);
    }

    void onOpenTMac(CCObject*) { TMacPanel::open(); }
};
