#include "sycamore/desktop/shell/layer_shell.h"
#include <wlr/util/log.h>
#include <stdlib.h>

#include "sycamore/desktop/layer.h"

void handle_sycamore_layer_map(struct wl_listener *listener, void *data) {
    struct sycamore_layer* layer = wl_container_of(listener, layer, map);
    sycamore_layer_map(layer);
}

void handle_sycamore_layer_unmap(struct wl_listener *listener, void *data) {
    struct sycamore_layer* layer = wl_container_of(listener, layer, unmap);
    sycamore_layer_unmap(layer);
}

void handle_sycamore_layer_destroy(struct wl_listener *listener, void *data) {
    struct sycamore_layer* layer = wl_container_of(listener, layer, destroy);
    layer->wlr_layer_surface = NULL;
    sycamore_layer_destroy(layer);
}

static void handle_new_layer_shell_surface(struct wl_listener *listener, void *data) {
    struct sycamore_layer_shell* layer_shell =
            wl_container_of(listener, layer_shell, new_layer_shell_surface);
    struct wlr_layer_surface_v1* layer_surface = data;

    if (!sycamore_layer_create(layer_shell->server, layer_surface)) {
        wlr_log(WLR_ERROR, "Unable to create sycamore_layer");
        return;
    }
}

void sycamore_layer_shell_destroy(struct sycamore_layer_shell* layer_shell) {
    if (!layer_shell) {
        return;
    }

    wl_list_remove(&layer_shell->new_layer_shell_surface.link);

    free(layer_shell);
}

struct sycamore_layer_shell* sycamore_layer_shell_create(
        struct sycamore_server* server, struct wl_display* display) {
    struct sycamore_layer_shell* layer_shell =
            calloc(1, sizeof(struct sycamore_layer_shell));
    if (!layer_shell) {
        wlr_log(WLR_ERROR, "Unable to allocate sycamore_layer_shell");
        return NULL;
    }

    layer_shell->server = server;

    layer_shell->wlr_layer_shell = wlr_layer_shell_v1_create(display);
    if (!layer_shell->wlr_layer_shell) {
        wlr_log(WLR_ERROR, "Unable to create wlr_layer_shell");
        sycamore_layer_shell_destroy(layer_shell);
        return NULL;
    }

    layer_shell->new_layer_shell_surface.notify = handle_new_layer_shell_surface;
    wl_signal_add(&layer_shell->wlr_layer_shell->events.new_surface,
                  &layer_shell->new_layer_shell_surface);

    return layer_shell;
}

