#include "input_state.h"

namespace sk {

void InputState::newFrame() {
    for (int i = 0; i < kKeyCount; ++i) pressed_[i] = false;
}

void InputState::onKeyDown(int vk) {
    if (vk < 0 || vk >= kKeyCount) return;
    if (!down_[vk]) pressed_[vk] = true;
    down_[vk] = true;
}

void InputState::onKeyUp(int vk) {
    if (vk < 0 || vk >= kKeyCount) return;
    down_[vk] = false;
}

void InputState::clearKeys() {
    for (int i = 0; i < kKeyCount; ++i) {
        down_[i] = false;
        pressed_[i] = false;
    }
}

} // namespace sk
