#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_output.h>
#include <wlr/util/log.h>

#include "sycamore/desktop/view.h"

void view_map(struct sycamore_view* view) {
    wl_list_insert(&view->server->mapped_views, &view->link);

    focus_view(view);
}

void view_unmap(struct sycamore_view* view) {
    wl_list_remove(&view->link);
    if (view->server->seat->cursor->grabbed_view == view) {
        view->server->seat->cursor->grabbed_view = NULL;
    }
    if (view->server->desktop_focused_view == view) {
        view->server->desktop_focused_view = NULL;
    }

    double sx, sy;
    update_pointer_focus(view->server->seat->cursor, &sx, &sy);
}

void focus_view(struct sycamore_view *view) {
    /* Note: this function only deals with keyboard focus. */
    if (view == NULL || view->type == VIEW_TYPE_UNKNOWN) {
        return;
    }
    struct sycamore_server *server = view->server;
    struct sycamore_view *prev_view = server->desktop_focused_view;
    if (prev_view == view) {
        /* Don't re-focus an already focused view. */
        return;
    }

    if (prev_view) {
        /*
         * Deactivate the previously focused view. This lets the client know
         * it no longer has focus and the client will repaint accordingly, e.g.
         * stop displaying a caret.
         */
        prev_view->interface->set_activated(prev_view, false);
    }

    /* Move the view to the front */
    wlr_scene_node_raise_to_top(view->scene_node);
    wl_list_remove(&view->link);
    wl_list_insert(&server->mapped_views, &view->link);

    /* Activate the new view */
    view->interface->set_activated(view, true);

    /* Tell the seat to have the keyboard enter this surface. wlroots will keep
     * track of this and automatically send key events to the appropriate
     * clients without additional work on your part. */
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(server->seat->wlr_seat);
    wlr_seat_keyboard_notify_enter(server->seat->wlr_seat, view->interface->get_wlr_surface(view),
                                   keyboard->keycodes,
                                   keyboard->num_keycodes,
                                   &keyboard->modifiers);

    server->desktop_focused_view = view;
}

struct sycamore_view* desktop_view_at(
        struct sycamore_server *server, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy) {
    /* This returns the topmost node in the scene at the given layout coords.
     * we only care about surface nodes as we are specifically looking for a
     * surface in the surface tree of a sycamore_view. */
    struct wlr_scene_node *node = wlr_scene_node_at(
            &server->scene->node, lx, ly, sx, sy);
    if (node == NULL || node->type != WLR_SCENE_NODE_SURFACE) {
        return NULL;
    }
    *surface = wlr_scene_surface_from_node(node)->surface;
    /* Find the node corresponding to the sycamore_view at the root of this
     * surface tree, it is the only one for which we set the data field. */
    while (node != NULL && node->data == NULL) {
        node = node->parent;
    }
    return node->data;
}

void view_set_fullscreen(struct sycamore_view* view, struct wlr_output* output, bool fullscreen) {
    if (fullscreen == view->is_fullscreen) {
        return;
    }

    view->is_fullscreen = fullscreen;
    view->interface->set_fullscreen(view, fullscreen);

    if (fullscreen) {
        view->restore.x = view->x;
        view->restore.y = view->y;

        struct wlr_box window_box;
        view->interface->get_geometry(view, &window_box);
        view->restore.width = window_box.width;
        view->restore.height = window_box.height;

        struct wlr_box fullscreen_box;
        wlr_output_layout_get_box(view->server->output_layout,
                                  output, &fullscreen_box);
        view->x = fullscreen_box.x;
        view->y = fullscreen_box.y;

        wlr_scene_node_set_position(view->scene_node , fullscreen_box.x, fullscreen_box.y);
        view->interface->set_size(view, fullscreen_box.width, fullscreen_box.height);
    } else {
        /* Restore from fullscreen mode */
        view->x = view->restore.x;
        view->y = view->restore.y;

        view->interface->set_size(view, view->restore.width, view->restore.height);
        wlr_scene_node_set_position(view->scene_node , view->restore.x, view->restore.y);
    }
}


