#ifndef SYCAMORE_LAYER_H
#define SYCAMORE_LAYER_H

#include <wlr/types/wlr_layer_shell_v1.h>
#include "sycamore/output/scene.h"

#define LAYERS_ALL 4

struct sycamore_output;
struct sycamore_server;

struct sycamore_layer {
    enum scene_descriptor_type scene_descriptor;
    struct wl_list link;
    bool linked;
    struct wlr_layer_surface_v1 *layer_surface;
    struct wlr_scene_layer_surface_v1 *scene;
    enum zwlr_layer_shell_v1_layer layer_type;

    struct wl_listener destroy;
    struct wl_listener map;
    struct wl_listener unmap;

    struct sycamore_output *output;
    struct sycamore_server *server;
};

void layer_map(struct sycamore_layer *layer);

void layer_unmap(struct sycamore_layer *layer);

void arrange_layers(struct sycamore_output *output);

struct sycamore_layer *sycamore_layer_create(struct sycamore_server *server,
        struct wlr_layer_surface_v1 *layer_surface);

void sycamore_layer_destroy(struct sycamore_layer *layer);

struct wlr_scene_node *layer_get_scene_node(struct sycamore_scene *root,
        struct sycamore_layer *layer);

#endif //SYCAMORE_LAYER_H
