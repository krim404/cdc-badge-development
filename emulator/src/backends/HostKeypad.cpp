#include "HostKeypad.h"

namespace emu {

HostKeypad& HostKeypad::instance()
{
    static HostKeypad keypad;
    return keypad;
}

bool HostKeypad::init()
{
    state_ = cdc::core::ServiceState::INITIALIZED;
    return true;
}

void HostKeypad::injectKey(char key, bool longPress)
{
    using cdc::hal::Key;
    if (longPress && longPressEnabled_ && longPressCallback_) {
        longPressCallback_(static_cast<Key>(key));
        return;
    }
    if (callback_) {
        callback_(static_cast<Key>(key), true);
        callback_(static_cast<Key>(key), false);
    }
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.push_back(key);
}

bool HostKeypad::isKeyPressed(cdc::hal::Key key) const
{
    (void)key;  // injected keys are instantaneous press+release
    return false;
}

cdc::hal::Key HostKeypad::getNextKey()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (buffer_.empty()) {
        return cdc::hal::Key::KEY_NONE;
    }
    const char key = buffer_.front();
    buffer_.pop_front();
    return static_cast<cdc::hal::Key>(key);
}

bool HostKeypad::hasKey() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !buffer_.empty();
}

void HostKeypad::setLongPressEnabled(bool enabled, uint32_t thresholdMs)
{
    (void)thresholdMs;
    longPressEnabled_ = enabled;
}

void HostKeypad::clearBuffer()
{
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
}

}  // namespace emu

namespace cdc::hal {

IKeypad* getKeypadInstance()
{
    return &emu::HostKeypad::instance();
}

}  // namespace cdc::hal
