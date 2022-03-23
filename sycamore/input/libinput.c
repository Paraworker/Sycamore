#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>

#include "sycamore/input/libinput.h"

static bool set_accel_speed(struct libinput_device *device, double speed) {
    if (!libinput_device_config_accel_is_available(device) ||
        libinput_device_config_accel_get_speed(device) == speed) {
        return false;
    }
    wlr_log(WLR_DEBUG, "accel_set_speed(%f)", speed);
    libinput_device_config_accel_set_speed(device, speed);
    return true;
}

static bool set_accel_profile(struct libinput_device *device,
                              enum libinput_config_accel_profile profile) {
    if (!libinput_device_config_accel_is_available(device) ||
        libinput_device_config_accel_get_profile(device) == profile) {
        return false;
    }
    wlr_log(WLR_DEBUG, "accel_set_profile(%d)", profile);
    libinput_device_config_accel_set_profile(device, profile);
    return true;
}

static bool set_tap(struct libinput_device *device,
                    enum libinput_config_tap_state tap) {
    if (libinput_device_config_tap_get_finger_count(device) <= 0 ||
        libinput_device_config_tap_get_enabled(device) == tap) {
        return false;
    }
    wlr_log(WLR_DEBUG, "tap_set_enabled(%d)", tap);
    libinput_device_config_tap_set_enabled(device, tap);
    return true;
}

static bool set_natural_scroll(struct libinput_device *device, bool n) {
    if (!libinput_device_config_scroll_has_natural_scroll(device) ||
        libinput_device_config_scroll_get_natural_scroll_enabled(device) == n) {
        return false;
    }
    wlr_log(WLR_DEBUG, "scroll_set_natural_scroll(%d)", n);
    libinput_device_config_scroll_set_natural_scroll_enabled(device, n);
    return true;
}

bool scroll_methods_is_touchpad(struct libinput_device* device) {
    enum libinput_config_scroll_method method =
            libinput_device_config_scroll_get_methods(device);
    if (method & LIBINPUT_CONFIG_SCROLL_2FG & LIBINPUT_CONFIG_SCROLL_EDGE != 0) {
        return true;
    }
    return false;
}

void touchpad_set_natural_scroll(struct wlr_input_device* device) {
    if (!wlr_input_device_is_libinput(device)) {
        /* We can only do this on libinput devices */
        wlr_log(WLR_DEBUG, "%s is not a libinput_device", device->name);
        return;
    }
    struct libinput_device *libinput_device = wlr_libinput_get_device_handle(device);
    if (scroll_methods_is_touchpad(libinput_device)) {
        set_natural_scroll(libinput_device, true);
    }
}

void touchpad_set_tap_to_click(struct wlr_input_device* device) {
    if (!wlr_input_device_is_libinput(device)) {
        /* We can only do this on libinput devices */
        wlr_log(WLR_DEBUG, "%s is not a libinput_device", device->name);
        return;
    }
    struct libinput_device *libinput_device = wlr_libinput_get_device_handle(device);
    set_tap(libinput_device, LIBINPUT_CONFIG_TAP_ENABLED);
}

void touchpad_set_accel_speed(struct wlr_input_device* device, double speed) {
    if (!wlr_input_device_is_libinput(device)) {
        /* We can only do this on libinput devices */
        wlr_log(WLR_DEBUG, "%s is not a libinput_device", device->name);
        return;
    }
    struct libinput_device *libinput_device = wlr_libinput_get_device_handle(device);
    if (scroll_methods_is_touchpad(libinput_device)) {
        set_accel_speed(libinput_device, speed);
    }
}


