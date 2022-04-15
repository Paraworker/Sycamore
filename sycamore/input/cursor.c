#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_pointer_gestures_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/box.h>
#include <wlr/util/log.h>
#include "sycamore/desktop/view.h"
#include "sycamore/desktop/scene.h"
#include "sycamore/input/cursor.h"
#include "sycamore/output/output.h"

static uint32_t get_current_time_msec() {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

void cursor_image_update(struct sycamore_cursor *cursor, struct wlr_surface *surface) {
    if (!surface && cursor->set_image_default == false) {
        /* If there's no surface under the cursor, set the cursor image to a default */
        cursor_set_image(cursor, "left_ptr");
        cursor->set_image_default = true;
    } else if (surface && cursor->set_image_default == true){
        cursor->set_image_default = false;
    }
}

void pointer_focus_update(struct sycamore_cursor *cursor, struct wlr_surface *surface, double sx, double sy, uint32_t time_msec) {
    struct wlr_seat* seat = cursor->seat->wlr_seat;
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(cursor->seat->wlr_seat, time_msec, sx, sy);
    } else if (seat->pointer_state.focused_surface) {
        wlr_seat_pointer_clear_focus(seat);
    }
}

void cursor_set_image(struct sycamore_cursor *cursor, const char *name) {
    wlr_xcursor_manager_set_cursor_image(cursor->xcursor_manager,
                                         name, cursor->wlr_cursor);
}

void cursor_warp_to_output(struct sycamore_cursor *cursor, struct sycamore_output *output) {
    struct wlr_box box;
    wlr_output_layout_get_box(cursor->seat->server->output_layout,
                              output->wlr_output, &box);

    cursor->wlr_cursor->x = box.x + box.width / 2.0;
    cursor->wlr_cursor->y = box.y + box.height / 2.0;

    wlr_cursor_warp(cursor->wlr_cursor, NULL,
                    cursor->wlr_cursor->x, cursor->wlr_cursor->y);
}

void cursor_enable(struct sycamore_cursor *cursor, bool enabled) {
    if (cursor->enabled == enabled) {
        return;
    }

    cursor->enabled = enabled;
    if (enabled) {
        cursor_rebase(cursor);
    } else {
        wlr_seat_pointer_notify_clear_focus(cursor->seat->wlr_seat);
        wlr_cursor_set_image(cursor->wlr_cursor, NULL, 0, 0, 0, 0, 0, 0);
    }
}

void cursor_rebase(struct sycamore_cursor *cursor) {
    if (!cursor || !cursor->enabled) {
        return;
    }

    enum seatop_mode mode = cursor->seat->seatop_impl->mode;
    if (mode == SEATOP_POINTER_MOVE || mode == SEATOP_POINTER_RESIZE) {
        return;
    }

    double sx, sy;
    struct wlr_surface *surface = surface_under(cursor->seat->server->scene,
            cursor->wlr_cursor->x, cursor->wlr_cursor->y, &sx, &sy);

    pointer_focus_update(cursor, surface, sx, sy, get_current_time_msec());
    cursor_image_update(cursor, surface);
}

static void handle_cursor_motion_relative(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits a _relative_
     * pointer motion event (i.e. a delta) */
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, cursor_motion);
    struct wlr_pointer_motion_event *event = data;
    cursor_enable(cursor, true);
    wlr_cursor_move(cursor->wlr_cursor, &event->pointer->base,
                    event->delta_x, event->delta_y);
    cursor->seat->seatop_impl->pointer_motion(cursor->seat, event->time_msec);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits an _absolute_
     * motion event, from 0..1 on each axis. This happens, for example, when
     * wlroots is running under a Wayland window rather than KMS+DRM, and you
     * move the mouse over the window. You could enter the window from any edge,
     * so we have to warp the mouse there. There is also some hardware which
     * emits these events. */
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = data;
    cursor_enable(cursor, true);
    wlr_cursor_warp_absolute(cursor->wlr_cursor, &event->pointer->base, event->x, event->y);
    cursor->seat->seatop_impl->pointer_motion(cursor->seat, event->time_msec);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits a button event. */
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, cursor_button);
    struct wlr_pointer_button_event *event = data;
    cursor_enable(cursor, true);
    cursor->seat->seatop_impl->pointer_button(cursor->seat, event);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits an axis event,
     * for example when you move the scroll wheel. */
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, cursor_axis);
    struct wlr_pointer_axis_event *event = data;
    cursor_enable(cursor, true);
    /* Notify the client with pointer focus of the axis event. */
    wlr_seat_pointer_notify_axis(cursor->seat->wlr_seat,
                                 event->time_msec, event->orientation, event->delta,
                                 event->delta_discrete, event->source);
}

static void handle_cursor_frame(struct wl_listener *listener, void *data) {
    /* This event is forwarded by the cursor when a pointer emits an frame
     * event. Frame events are sent after regular pointer events to group
     * multiple events together. For instance, two axis events may happen at the
     * same time, in which case a frame event won't be sent in between. */
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, cursor_frame);
    /* Notify the client with pointer focus of the frame event. */
    wlr_seat_pointer_notify_frame(cursor->seat->wlr_seat);
}

static void handle_swipe_begin(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, swipe_begin);
    struct wlr_pointer_swipe_begin_event *event = data;
    cursor_enable(cursor, true);
    wlr_pointer_gestures_v1_send_swipe_begin(cursor->gestures, cursor->seat->wlr_seat,
                                             event->time_msec, event->fingers);
}

static void handle_swipe_update(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, swipe_update);
    struct wlr_pointer_swipe_update_event *event = data;

    wlr_pointer_gestures_v1_send_swipe_update(cursor->gestures, cursor->seat->wlr_seat,
                                              event->time_msec, event->dx, event->dy);
}

static void handle_swipe_end(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, swipe_end);
    struct wlr_pointer_swipe_end_event *event = data;

    wlr_pointer_gestures_v1_send_swipe_end(cursor->gestures, cursor->seat->wlr_seat,
                                           event->time_msec, event->cancelled);
}

static void handle_pinch_begin(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, pinch_begin);
    struct wlr_pointer_pinch_begin_event *event = data;
    cursor_enable(cursor, true);
    wlr_pointer_gestures_v1_send_pinch_begin(cursor->gestures, cursor->seat->wlr_seat,
                                             event->time_msec, event->fingers);
}

static void handle_pinch_update(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, pinch_update);
    struct wlr_pointer_pinch_update_event *event = data;

    wlr_pointer_gestures_v1_send_pinch_update(cursor->gestures, cursor->seat->wlr_seat,
                                              event->time_msec, event->dx, event->dy,
                                              event->scale, event->rotation);
}

static void handle_pinch_end(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, pinch_end);
    struct wlr_pointer_pinch_end_event *event = data;

    wlr_pointer_gestures_v1_send_pinch_end(cursor->gestures, cursor->seat->wlr_seat,
                                           event->time_msec, event->cancelled);
}

static void handle_hold_begin(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, hold_begin);
    struct wlr_pointer_hold_begin_event *event = data;
    cursor_enable(cursor, true);
    wlr_pointer_gestures_v1_send_hold_begin(cursor->gestures, cursor->seat->wlr_seat,
                                            event->time_msec, event->fingers);
}

static void handle_hold_end(struct wl_listener *listener, void *data) {
    struct sycamore_cursor *cursor = wl_container_of(listener, cursor, hold_end);
    struct wlr_pointer_hold_end_event *event = data;

    wlr_pointer_gestures_v1_send_hold_end(cursor->gestures, cursor->seat->wlr_seat,
                                          event->time_msec, event->cancelled);
}

void sycamore_cursor_destroy(struct sycamore_cursor *cursor) {
    if (!cursor) {
        return;
    }

    if (cursor->xcursor_manager) {
        wlr_xcursor_manager_destroy(cursor->xcursor_manager);
    }
    if (cursor->wlr_cursor) {
        wlr_cursor_destroy(cursor->wlr_cursor);
    }

    wl_list_remove(&cursor->cursor_axis.link);
    wl_list_remove(&cursor->cursor_button.link);
    wl_list_remove(&cursor->cursor_motion.link);
    wl_list_remove(&cursor->cursor_motion_absolute.link);
    wl_list_remove(&cursor->cursor_frame.link);

    wl_list_remove(&cursor->swipe_begin.link);
    wl_list_remove(&cursor->swipe_update.link);
    wl_list_remove(&cursor->swipe_end.link);
    wl_list_remove(&cursor->pinch_begin.link);
    wl_list_remove(&cursor->pinch_update.link);
    wl_list_remove(&cursor->hold_begin.link);
    wl_list_remove(&cursor->hold_end.link);

    free(cursor);
}

struct sycamore_cursor *sycamore_cursor_create(struct sycamore_seat *seat,
        struct wl_display *display, struct wlr_output_layout *output_layout) {
    struct sycamore_cursor *cursor = calloc(1, sizeof(struct sycamore_cursor));
    if (!cursor) {
        wlr_log(WLR_ERROR, "Unable to allocate sycamore_cursor");
        return NULL;
    }

    cursor->wlr_cursor = wlr_cursor_create();
    if (!cursor->wlr_cursor) {
        free(cursor);
        return NULL;
    }

    wlr_cursor_attach_output_layout(cursor->wlr_cursor, output_layout);

    cursor->gestures = wlr_pointer_gestures_v1_create(display);
    if (!cursor->gestures) {
        sycamore_cursor_destroy(cursor);
        return NULL;
    }

    cursor->xcursor_manager = wlr_xcursor_manager_create(NULL, 24);
    if (!cursor->xcursor_manager) {
        sycamore_cursor_destroy(cursor);
        return NULL;
    }

    wlr_xcursor_manager_load(cursor->xcursor_manager, 1);

    cursor->enabled = false;
    cursor->set_image_default = false;
    cursor->seat = seat;

    cursor->cursor_motion.notify = handle_cursor_motion_relative;
    wl_signal_add(&cursor->wlr_cursor->events.motion, &cursor->cursor_motion);
    cursor->cursor_motion_absolute.notify = handle_cursor_motion_absolute;
    wl_signal_add(&cursor->wlr_cursor->events.motion_absolute, &cursor->cursor_motion_absolute);
    cursor->cursor_button.notify = handle_cursor_button;
    wl_signal_add(&cursor->wlr_cursor->events.button, &cursor->cursor_button);
    cursor->cursor_axis.notify = handle_cursor_axis;
    wl_signal_add(&cursor->wlr_cursor->events.axis, &cursor->cursor_axis);
    cursor->cursor_frame.notify = handle_cursor_frame;
    wl_signal_add(&cursor->wlr_cursor->events.frame, &cursor->cursor_frame);

    cursor->swipe_begin.notify = handle_swipe_begin;
    wl_signal_add(&cursor->wlr_cursor->events.swipe_begin, &cursor->swipe_begin);
    cursor->swipe_update.notify = handle_swipe_update;
    wl_signal_add(&cursor->wlr_cursor->events.swipe_update, &cursor->swipe_update);
    cursor->swipe_end.notify = handle_swipe_end;
    wl_signal_add(&cursor->wlr_cursor->events.swipe_end, &cursor->swipe_end);
    cursor->pinch_begin.notify = handle_pinch_begin;
    wl_signal_add(&cursor->wlr_cursor->events.pinch_begin, &cursor->pinch_begin);
    cursor->pinch_update.notify = handle_pinch_update;
    wl_signal_add(&cursor->wlr_cursor->events.pinch_update, &cursor->pinch_update);
    cursor->pinch_end.notify = handle_pinch_end;
    wl_signal_add(&cursor->wlr_cursor->events.pinch_end, &cursor->pinch_end);
    cursor->hold_begin.notify = handle_hold_begin;
    wl_signal_add(&cursor->wlr_cursor->events.hold_begin, &cursor->hold_begin);
    cursor->hold_end.notify = handle_hold_end;
    wl_signal_add(&cursor->wlr_cursor->events.hold_end, &cursor->hold_end);

    return cursor;
}

