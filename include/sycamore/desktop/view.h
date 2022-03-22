#ifndef SYCAMORE_VIEW_H
#define SYCAMORE_VIEW_H

#include "sycamore/server.h"
#include <wlr/types/wlr_xdg_shell.h>

enum sycamore_view_type {
    VIEW_TYPE_UNKNOWN,
    VIEW_TYPE_XDG_SHELL,
    VIEW_TYPE_XWAYLAND,
};

struct sycamore_view;

struct sycamore_view_interface{
    void (*destroy)(struct sycamore_view *view);
    void (*set_activated)(struct sycamore_view *view, bool activated);
    void (*set_size)(struct sycamore_view *view, uint32_t width, uint32_t height);
    void (*set_fullscreen)(struct sycamore_view *view, bool fullscreen);
    void (*set_maximized)(struct sycamore_view *view, bool maximized);
    struct wlr_surface* (*get_wlr_surface)(struct sycamore_view *view);
    void (*get_geometry)(struct sycamore_view *view, struct wlr_box* box);
};

/* the base view */
struct sycamore_view {
    struct wl_list link;
    const struct sycamore_view_interface* interface;
    struct wlr_scene_node *scene_node;
    enum sycamore_view_type type;
    int x, y;

    bool is_maximized;
    bool is_fullscreen;

    struct wlr_box maximize_restore;
    struct wlr_box fullscreen_restore;

    struct sycamore_server *server;
};

struct sycamore_xdg_shell_view {
    struct sycamore_view base_view;

    struct wlr_xdg_toplevel *xdg_toplevel;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_fullscreen;
    struct wl_listener request_maximize;
};

void view_map(struct sycamore_view* view, struct wlr_output* output, bool maximized, bool fullscreen);
void view_unmap(struct sycamore_view* view);
void view_set_fullscreen(struct sycamore_view* view, struct wlr_output* output, bool fullscreen);
void view_set_maximized(struct sycamore_view* view, bool maximized);
void focus_view(struct sycamore_view *view);
struct sycamore_view *desktop_view_at(struct sycamore_server *server, double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy);

#endif //SYCAMORE_VIEW_H
