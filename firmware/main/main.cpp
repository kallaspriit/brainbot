#include "App.hpp"

extern "C" void app_main(void) {
    // Static, so the app's members don't live on the main task's stack
    static App app;
    app.begin();
}
