#ifndef SYCAMORE_LAYER_H
#define SYCAMORE_LAYER_H

#include "sycamore/server.h"
#include "sycamore/desktop/desktop.h"
#include <wlr/types/wlr_layer_shell_v1.h>

struct sycamore_layer {
    enum desktop_element_type element_type;
    struct wl_list link;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer_surface;
    enum zwlr_layer_shell_v1_layer layer_type;

    bool mapped;

    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;

    struct sycamore_server* server;
};

void sycamore_layer_map(struct sycamore_layer* layer);
void sycamore_layer_unmap(struct sycamore_layer* layer);
struct sycamore_layer* sycamore_layer_create(struct sycamore_server* server, struct wlr_layer_surface_v1* layer_surface);
void sycamore_layer_destroy(struct sycamore_layer* layer);

#endif //SYCAMORE_LAYER_H
