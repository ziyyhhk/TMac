#pragma once

namespace tmac {

// Decides what a toggle-key event means for the TMac window. Pure logic, so it
// is tested without the game.
//
// Rules:
//   * Only a fresh press (down and not a key-repeat) toggles. Holding the key,
//     and releasing it, do nothing.
//   * Each fresh press flips the expected state immediately, so two presses in
//     a row cannot ask for two windows at once.
//   * setOpen() is the truth once the UI has actually opened or closed. Call it
//     whenever the window opens or closes for any reason, including its own
//     close button and scene teardown.
class ToggleController {
public:
    enum class Action { None, Open, Close };

    Action onKey(bool down, bool repeat) {
        if (!down || repeat) return Action::None;
        open_ = !open_;
        return open_ ? Action::Open : Action::Close;
    }

    void setOpen(bool open) { open_ = open; }
    bool isOpen() const { return open_; }

private:
    bool open_ = false;
};

}  // namespace tmac
