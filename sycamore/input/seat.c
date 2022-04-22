#include <stdlib.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/util/log.h>
#include "sycamore/server.h"
#include "sycamore/input/cursor.h"
#include "sycamore/input/keyboard.h"
#include "sycamore/input/libinput.h"
#include "sycamore/input/pointer.h"
#include "sycamore/input/seat.h"

static void handle_request_start_drag(struct wl_listener *listener, void *data) {
    struct sycamore_seat *seat = wl_container_of(listener, seat, request_start_drag);
    struct wlr_seat_request_start_drag_event *event = data;

    if (wlr_seat_validate_pointer_grab_serial(seat->wlr_seat,
                                              event->origin, event->serial)) {
        wlr_seat_start_pointer_drag(seat->wlr_seat, event->drag, event->serial);
        return;
    }

    struct wlr_touch_point *point;
    if (wlr_seat_validate_touch_grab_serial(seat->wlr_seat,
                                            event->origin, event->serial, &point)) {
        wlr_seat_start_touch_drag(seat->wlr_seat,
                                  event->drag, event->serial, point);
        return;
    }

    /* TODO: tablet grabs */

    wlr_data_source_destroy(event->drag->source);
}

static void handle_sycamore_drag_destroy(struct wl_listener *listener, void *data) {
    struct sycamore_drag *drag = wl_container_of(listener, drag, destroy);

    drag->wlr_drag->data = NULL;
    wl_list_remove(&drag->destroy.link);
    free(drag);
}

static void handle_start_drag(struct wl_listener *listener, void *data) {
    struct sycamore_seat *seat = wl_container_of(listener, seat, start_drag);
    struct wlr_drag *wlr_drag = data;
    struct sycamore_drag *drag = calloc(1, sizeof(struct sycamore_drag));
    if (!drag) {
        wlr_log(WLR_ERROR, "Unable to allocate sycamore_drag");
        return;
    }

    drag->seat = seat;
    drag->wlr_drag = wlr_drag;
    wlr_drag->data = drag;

    drag->destroy.notify = handle_sycamore_drag_destroy;
    wl_signal_add(&wlr_drag->events.destroy, &drag->destroy);

    struct wlr_drag_icon *wlr_drag_icon = wlr_drag->icon;
    if (wlr_drag_icon != NULL) {
        /* TODO: drag icon */
    }
}

static void handle_seat_device_destroy(struct wl_listener *listener, void *data) {
    struct sycamore_seat_device *seat_device = wl_container_of(listener, seat_device, destroy);
    struct sycamore_seat *seat = seat_device->seat;
    seat_device_destroy(seat_device);
    seat_update_capabilities(seat);
}

struct sycamore_seat_device *seat_device_create(struct sycamore_seat *seat, struct wlr_input_device *wlr_device,
        void *derived_device, void (*derived_destroy)(struct sycamore_seat_device *seat_device)) {
    struct sycamore_seat_device *seat_device = calloc(1, sizeof(struct sycamore_seat_device));
    if (!seat_device) {
        return NULL;
    }

    seat_device->wlr_device = wlr_device;
    seat_device->derived_device = derived_device;
    seat_device->derived_destroy = derived_destroy;
    seat_device->seat = seat;

    seat_device->destroy.notify = handle_seat_device_destroy;
    wl_signal_add(&wlr_device->events.destroy, &seat_device->destroy);

    return seat_device;
}

void seat_device_destroy(struct sycamore_seat_device *seat_device) {
    if (!seat_device) {
        return;
    }

    if (seat_device->derived_destroy) {
        seat_device->derived_destroy(seat_device);
    }

    wl_list_remove(&seat_device->destroy.link);
    wl_list_remove(&seat_device->link);

    free(seat_device);
}

void seat_update_capabilities(struct sycamore_seat *seat) {
    uint32_t caps = 0;
    struct sycamore_seat_device *seat_device;
    wl_list_for_each(seat_device, &seat->devices, link) {
        switch (seat_device->wlr_device->type) {
            case WLR_INPUT_DEVICE_KEYBOARD:
                caps |= WL_SEAT_CAPABILITY_KEYBOARD;
                break;
            case WLR_INPUT_DEVICE_POINTER:
                caps |= WL_SEAT_CAPABILITY_POINTER;
                break;
            case WLR_INPUT_DEVICE_TOUCH:
                caps |= WL_SEAT_CAPABILITY_TOUCH;
                break;
            case WLR_INPUT_DEVICE_TABLET_TOOL:
                caps |= WL_SEAT_CAPABILITY_POINTER;
                break;
            case WLR_INPUT_DEVICE_SWITCH:
            case WLR_INPUT_DEVICE_TABLET_PAD:
                break;
        }
    }

    wlr_seat_set_capabilities(seat->wlr_seat, caps);

    // Disable cursor if seat doesn't have pointer capability.
    if ((caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
        cursor_disable(seat->cursor);
    }
}

static void seat_configure_keyboard(struct sycamore_seat *seat,
                                    struct wlr_input_device *device) {
    struct sycamore_keyboard *keyboard =
            sycamore_keyboard_create(seat, device);
    if (!keyboard) {
        wlr_log(WLR_ERROR, "Unable to create sycamore_keyboard");
        return;
    }

    sycamore_keyboard_configure(keyboard);
    wlr_seat_set_keyboard(seat->wlr_seat, keyboard->wlr_keyboard);
    wl_list_insert(&seat->devices, &keyboard->base->link);
}

static void seat_configure_pointer(struct sycamore_seat *seat,
                                   struct wlr_input_device *device) {
    wlr_log(WLR_DEBUG, "new pointer device: %s", device->name);

    struct sycamore_pointer *pointer =
            sycamore_pointer_create(seat, device);
    if (!pointer) {
        wlr_log(WLR_ERROR, "Unable to create sycamore_pointer");
        return;
    }

    wlr_cursor_attach_input_device(seat->cursor->wlr_cursor, device);
    wl_list_insert(&seat->devices, &pointer->base->link);

    touchpad_set_tap_to_click(device);
    touchpad_set_natural_scroll(device);
    touchpad_set_accel_speed(device, 0.3);
}

static void seat_configure_touch(struct sycamore_seat *seat,
                                 struct wlr_input_device *device) {
    wlr_log(WLR_DEBUG, "new touch device: %s", device->name);
    /* TODO */
}

static void seat_configure_tablet_tool(struct sycamore_seat *seat,
                                       struct wlr_input_device *device) {
    wlr_log(WLR_DEBUG, "new tablet tool device: %s", device->name);
    wlr_cursor_attach_input_device(seat->cursor->wlr_cursor, device);
    /* TODO */
}

static void seat_configure_tablet_pad(struct sycamore_seat *seat,
                                      struct wlr_input_device *device) {
    wlr_log(WLR_DEBUG, "new tablet pad device: %s", device->name);
    /* TODO */
}

static void seat_configure_switch(struct sycamore_seat *seat,
                                  struct wlr_input_device *device) {
    wlr_log(WLR_DEBUG, "new switch device: %s", device->name);
    /* TODO */
}

static void (*seat_configure_device[])(struct sycamore_seat *seat, struct wlr_input_device *device) = {
        seat_configure_keyboard,
        seat_configure_pointer,
        seat_configure_touch,
        seat_configure_tablet_tool,
        seat_configure_tablet_pad,
        seat_configure_switch,
};

void handle_backend_new_input(struct wl_listener *listener, void *data) {
    /* This event is raised by the backend when a new input device becomes
     * available. */
    struct sycamore_server *server =
            wl_container_of(listener, server, backend_new_input);
    struct wlr_input_device *device = data;
    struct sycamore_seat *seat = server->seat;

    seat_configure_device[device->type](seat, device);

    seat_update_capabilities(seat);
}

static void handle_seat_request_set_cursor(struct wl_listener *listener, void *data) {
    struct sycamore_seat *seat = wl_container_of(listener, seat, request_set_cursor);
    if (!seat->cursor->enabled) {
        return;
    }

    struct wlr_seat_pointer_request_set_cursor_event *event = data;
    struct wlr_seat_client *focused_client = seat->wlr_seat->pointer_state.focused_client;
    if (focused_client != event->seat_client) {
        return;
    }

    /* Once we've vetted the client, we can tell the cursor to use the
     * provided surface as the cursor image. It will set the hardware cursor
     * on the output that it's currently on and continue to do so as the
     * cursor moves between outputs. */
    cursor_set_image_surface(seat->cursor, event);
}

static void handle_seat_request_set_selection(struct wl_listener *listener, void *data) {
    /* This event is raised by the seat when a client wants to set the selection,
     * usually when the user copies something. wlroots allows compositors to
     * ignore such requests if they so choose, but in sycamore we always honor
     */
    struct sycamore_seat *seat = wl_container_of(
            listener, seat, request_set_selection);
    struct wlr_seat_request_set_selection_event *event = data;
    wlr_seat_set_selection(seat->wlr_seat, event->source, event->serial);
}

static void handle_seat_request_set_primary_selection(struct wl_listener *listener, void *data) {
    struct sycamore_seat *seat = wl_container_of(
            listener, seat, request_set_primary_selection);
    struct wlr_seat_request_set_primary_selection_event *event = data;
    wlr_seat_set_primary_selection(seat->wlr_seat, event->source, event->serial);
}

static void handle_seat_destroy(struct wl_listener *listener, void *data) {
    struct sycamore_seat *seat = wl_container_of(listener, seat, destroy);

    seat->wlr_seat = NULL;
    seat->server->seat = NULL;
    sycamore_seat_destroy(seat);
}

void sycamore_seat_destroy(struct sycamore_seat *seat) {
    if (!seat) {
        return;
    }

    wl_list_remove(&seat->request_set_cursor.link);
    wl_list_remove(&seat->request_set_selection.link);
    wl_list_remove(&seat->request_set_primary_selection.link);
    wl_list_remove(&seat->request_start_drag.link);
    wl_list_remove(&seat->start_drag.link);
    wl_list_remove(&seat->destroy.link);

    if (seat->wlr_seat) {
        wlr_seat_destroy(seat->wlr_seat);
    }
    if (seat->cursor) {
        sycamore_cursor_destroy(seat->cursor);
    }

    free(seat);
}

struct sycamore_seat *sycamore_seat_create(struct sycamore_server *server,
        struct wl_display *display, struct wlr_output_layout *output_layout) {
    struct sycamore_seat *seat = calloc(1, sizeof(struct sycamore_seat));
    if (!seat) {
        wlr_log(WLR_ERROR, "Unable to allocate sycamore_seat");
        return NULL;
    }

    seat->wlr_seat = wlr_seat_create(display, "seat0");
    if (!seat->wlr_seat) {
        wlr_log(WLR_ERROR, "Unable to create wlr_seat");
        free(seat);
        return NULL;
    }

    seat->cursor = sycamore_cursor_create(seat, display, output_layout);
    if (!seat->cursor) {
        wlr_log(WLR_ERROR, "Unable to create sycamore_cursor");
        sycamore_seat_destroy(seat);
        return NULL;
    }

    wl_list_init(&seat->devices);
    seat->server = server;

    seat->grabbed_view = NULL;

    seat->request_set_cursor.notify = handle_seat_request_set_cursor;
    wl_signal_add(&seat->wlr_seat->events.request_set_cursor,
                  &seat->request_set_cursor);
    seat->request_set_selection.notify = handle_seat_request_set_selection;
    wl_signal_add(&seat->wlr_seat->events.request_set_selection,
                  &seat->request_set_selection);
    seat->request_set_primary_selection.notify = handle_seat_request_set_primary_selection;
    wl_signal_add(&seat->wlr_seat->events.request_set_primary_selection,
                  &seat->request_set_primary_selection);
    seat->request_start_drag.notify = handle_request_start_drag;
    wl_signal_add(&seat->wlr_seat->events.request_start_drag,
                  &seat->request_start_drag);
    seat->start_drag.notify = handle_start_drag;
    wl_signal_add(&seat->wlr_seat->events.start_drag,
                  &seat->start_drag);
    seat->destroy.notify = handle_seat_destroy;
    wl_signal_add(&seat->wlr_seat->events.destroy,
                  &seat->destroy);

    seatop_begin_default(seat);

    return seat;
}

