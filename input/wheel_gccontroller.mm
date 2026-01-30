/*
 * wheel_gccontroller.mm - macOS/iOS GCController steering wheel implementation (stub)
 *
 * TODO: Implement proper steering wheel support via GCController framework.
 * This is a placeholder that creates a non-functional WheelManager.
 */

#include "input_wheel.hpp"

#ifdef __APPLE__

namespace window {
namespace input {

//=============================================================================
// Platform Implementation (Stub)
//=============================================================================

struct WheelManager::Impl {
    WheelEventDispatcher dispatcher;
    WheelState states[MAX_WHEELS];
    float deadzone = 0.02f;
    float ff_gain[MAX_WHEELS] = { 1.0f, 1.0f, 1.0f, 1.0f };
    float rotation_range[MAX_WHEELS] = { 900.0f, 900.0f, 900.0f, 900.0f };

    Impl() {
        for (int i = 0; i < MAX_WHEELS; i++) {
            states[i].reset();
        }
    }
};

WheelManager::WheelManager() : impl_(nullptr) {}
WheelManager::~WheelManager() { destroy(); }

WheelManager* WheelManager::create() {
    WheelManager* mgr = new WheelManager();
    mgr->impl_ = new WheelManager::Impl();
    return mgr;
}

void WheelManager::destroy() {
    if (impl_) {
        delete impl_;
        impl_ = nullptr;
    }
}

void WheelManager::update() {
    // TODO: Poll GCController for racing wheel devices
}

bool WheelManager::add_handler(IWheelHandler* handler) {
    if (!impl_) return false;
    return impl_->dispatcher.add_handler(handler);
}

bool WheelManager::remove_handler(IWheelHandler* handler) {
    if (!impl_) return false;
    return impl_->dispatcher.remove_handler(handler);
}

bool WheelManager::remove_handler(const char* handler_id) {
    if (!impl_) return false;
    return impl_->dispatcher.remove_handler(handler_id);
}

WheelEventDispatcher* WheelManager::get_dispatcher() {
    return impl_ ? &impl_->dispatcher : nullptr;
}

int WheelManager::get_wheel_count() const {
    return 0;
}

bool WheelManager::is_connected(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;
    return impl_->states[index].connected;
}

const WheelState* WheelManager::get_state(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return nullptr;
    return &impl_->states[index];
}

bool WheelManager::get_capabilities(int index, WheelCaps* caps) const {
    if (!impl_ || !caps || index < 0 || index >= MAX_WHEELS) return false;
    if (!impl_->states[index].connected) return false;
    *caps = WheelCaps();
    return true;
}

float WheelManager::get_steering(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->states[index].steering;
}

float WheelManager::get_steering_degrees(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->states[index].steering_degrees;
}

float WheelManager::get_throttle(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->states[index].throttle;
}

float WheelManager::get_brake(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->states[index].brake;
}

float WheelManager::get_clutch(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 0.0f;
    return impl_->states[index].clutch;
}

GearPosition WheelManager::get_gear(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return GearPosition::Neutral;
    return impl_->states[index].gear;
}

bool WheelManager::is_button_down(int index, WheelButton button) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;
    int btn = static_cast<int>(button);
    if (btn < 0 || btn >= MAX_WHEEL_BUTTONS) return false;
    return impl_->states[index].buttons[btn];
}

void WheelManager::set_rotation_range(int index, float degrees) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return;
    impl_->rotation_range[index] = degrees;
}

float WheelManager::get_rotation_range(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 900.0f;
    return impl_->rotation_range[index];
}

void WheelManager::set_deadzone(float deadzone) {
    if (!impl_) return;
    impl_->deadzone = deadzone;
}

float WheelManager::get_deadzone() const {
    return impl_ ? impl_->deadzone : 0.02f;
}

void WheelManager::set_linearity(int index, float linearity) {
    (void)index;
    (void)linearity;
}

// Force Feedback (stubs)
bool WheelManager::supports_force_feedback(int index) const {
    (void)index;
    return false;
}

bool WheelManager::get_ff_capabilities(int index, WheelCaps* caps) const {
    (void)index;
    (void)caps;
    return false;
}

bool WheelManager::set_constant_force(int index, float force) {
    (void)index;
    (void)force;
    return false;
}

bool WheelManager::set_spring_force(int index, float strength, float center) {
    (void)index;
    (void)strength;
    (void)center;
    return false;
}

bool WheelManager::set_damper_force(int index, float strength) {
    (void)index;
    (void)strength;
    return false;
}

bool WheelManager::set_friction_force(int index, float strength) {
    (void)index;
    (void)strength;
    return false;
}

bool WheelManager::set_sine_effect(int index, float magnitude, float frequency_hz) {
    (void)index;
    (void)magnitude;
    (void)frequency_hz;
    return false;
}

bool WheelManager::stop_all_forces(int index) {
    (void)index;
    return false;
}

WheelFFHandle WheelManager::play_effect(int index, const WheelFFEffect& effect) {
    (void)index;
    (void)effect;
    return INVALID_WHEEL_FF_HANDLE;
}

bool WheelManager::stop_effect(int index, WheelFFHandle handle) {
    (void)index;
    (void)handle;
    return false;
}

bool WheelManager::update_effect(int index, WheelFFHandle handle, const WheelFFEffect& effect) {
    (void)index;
    (void)handle;
    (void)effect;
    return false;
}

bool WheelManager::set_ff_gain(int index, float gain) {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return false;
    impl_->ff_gain[index] = gain;
    return true;
}

float WheelManager::get_ff_gain(int index) const {
    if (!impl_ || index < 0 || index >= MAX_WHEELS) return 1.0f;
    return impl_->ff_gain[index];
}

bool WheelManager::set_ff_autocenter(int index, bool enabled, float strength) {
    (void)index;
    (void)enabled;
    (void)strength;
    return false;
}

bool WheelManager::pause_ff(int index) {
    (void)index;
    return false;
}

bool WheelManager::resume_ff(int index) {
    (void)index;
    return false;
}

} // namespace input
} // namespace window

#endif // __APPLE__
