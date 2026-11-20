#include "Screen.hpp"

Screen::Screen(UiContext& context)
    : context(context) {
}

Screen::~Screen() {
    for (lv_timer_t* timer : timers) {
        lv_timer_delete(timer);
    }
}

lv_timer_t* Screen::addTimer(lv_timer_cb_t callback, uint32_t periodMs) {
    lv_timer_t* timer = lv_timer_create(callback, periodMs, this);
    timers.push_back(timer);

    return timer;
}
