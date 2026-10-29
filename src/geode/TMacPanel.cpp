// =============================================================================
// UI: NOT COMPILED OR RUN IN THIS ENVIRONMENT.
// Every label and enabled state comes from tmac::buildPanelView(), so the window
// shows only what the engine reports. The Geode calls below are unverified.
//   * Popup<>, initAnchored, setTitle, m_mainLayer
//   * ButtonSprite, CCMenuItemSpriteExtra, CCLabelBMFont
//   * PauseLayer::customSetup and PauseLayer::onResume
// Expect to fix these against the v5.11.1 headers on the first build.
// Known gaps: no name entry, and load cycles through saved macros in order.
// =============================================================================
#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

#include <map>
#include <string>

#include "TMacRuntime.hpp"
#include "tmac/PanelView.hpp"

using namespace geode::prelude;

class TMacPanel : public geode::Popup<> {
public:
    static TMacPanel* create();
    static void open();

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
    bool setup() override;
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

TMacPanel* TMacPanel::create() {
    auto* ret = new TMacPanel();
    if (ret && ret->initAnchored(380.f, 300.f)) {
        ret->autorelease();
        return ret;
    }
    CC_SAFE_DELETE(ret);
    return nullptr;
}

void TMacPanel::open() {
    if (TMacPanel* panel = TMacPanel::create()) panel->show();
}

bool TMacPanel::setup() {
    setTitle("TMac");
    const CCSize size = m_mainLayer->getContentSize();

    m_status = CCLabelBMFont::create("Idle", "bigFont.fnt");
    m_status->setPosition(ccp(size.width / 2, size.height - 50.f));
    m_mainLayer->addChild(m_status);

    m_timer = CCLabelBMFont::create("", "chatFont.fnt");
    m_timer->setPosition(ccp(size.width / 2, size.height - 80.f));
    m_mainLayer->addChild(m_timer);

    m_info = CCLabelBMFont::create("", "chatFont.fnt");
    m_info->setPosition(ccp(size.width / 2, 40.f));
    m_mainLayer->addChild(m_info);

    CCMenu* menu = CCMenu::create();
    menu->setPosition(ccp(0.f, 0.f));
    m_mainLayer->addChild(menu);

    const float colX[2] = {size.width / 2 - 85.f, size.width / 2 + 85.f};
    const float rowY[5] = {size.height - 120.f, size.height - 160.f, size.height - 200.f,
                           size.height - 240.f, 100.f};
    addButton(menu, "record", "Record", menu_selector(TMacPanel::onRecord), colX[0], rowY[0]);
    addButton(menu, "stopRecord", "Stop Rec", menu_selector(TMacPanel::onStopRecording), colX[1], rowY[0]);
    addButton(menu, "play", "Play", menu_selector(TMacPanel::onPlay), colX[0], rowY[1]);
    addButton(menu, "restart", "Restart", menu_selector(TMacPanel::onRestart), colX[1], rowY[1]);
    addButton(menu, "pause", "Pause", menu_selector(TMacPanel::onPause), colX[0], rowY[2]);
    addButton(menu, "resume", "Resume", menu_selector(TMacPanel::onResume), colX[1], rowY[2]);
    addButton(menu, "stop", "Stop", menu_selector(TMacPanel::onStop), colX[0], rowY[3]);
    addButton(menu, "save", "Save", menu_selector(TMacPanel::onSave), colX[1], rowY[3]);
    addButton(menu, "load", "Load", menu_selector(TMacPanel::onLoad), size.width / 2, rowY[4]);

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
