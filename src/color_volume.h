#pragma once
#include <libplacebo/colorspace.h>
// Private VP fork implementation. Table RGB is normalized to [black, peak].
#define VP_VOLUME_Y 129
#define VP_VOLUME_C 33
#define VP_VOLUME_H 513
struct vp_color_volume {
    struct pl_raw_primaries prim;
    float black, peak; // physical nits, never the virtual dynamic target
    bool failed;
};
bool vp_color_volume_generate(float *data, const struct vp_color_volume *params);
