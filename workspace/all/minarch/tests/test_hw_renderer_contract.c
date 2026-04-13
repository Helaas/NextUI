#include <assert.h>
#include <stdint.h>

#include "../../common/api.h"

int main(void)
{
    GFX_Renderer renderer = {0};
    renderer.source_type = GFX_SOURCE_HW_TEXTURE;
    renderer.src_texture = 42;
    renderer.src_texture_flipped = 1;

    assert(renderer.source_type == GFX_SOURCE_HW_TEXTURE);
    assert(renderer.src_texture == 42);
    assert(renderer.src_texture_flipped == 1);
    return 0;
}
