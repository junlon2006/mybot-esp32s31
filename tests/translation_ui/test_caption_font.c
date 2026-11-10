/* SPDX-License-Identifier: MIT */
#include "lvgl.h"
#include "src/core/lv_global.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !LV_USE_FONT_COMPRESSED
#error "Caption fonts require the production LVGL compressed-font decoder"
#endif

LV_FONT_DECLARE(mybot_caption_font_20);
LV_FONT_DECLARE(mybot_caption_font_28);
LV_FONT_DECLARE(mybot_caption_font_ko_20);

lv_global_t lv_global;
static unsigned stride_alignment = 1;
static size_t temporary_bytes, peak_temporary_bytes;
static unsigned cache_flushes;

void lv_memset(void *destination, uint8_t value, size_t size) {
    memset(destination, value, size);
}

/* Keep LVGL's font and RLE code real; replace only its host allocation/draw
 * environment. No display or board is needed to decode an A8 glyph. */
void *lv_malloc(size_t size) {
    size_t *allocation = malloc(sizeof(*allocation) + size);
    assert(allocation);
    *allocation = size;
    temporary_bytes += size;
    if (temporary_bytes > peak_temporary_bytes)
        peak_temporary_bytes = temporary_bytes;
    return allocation + 1;
}

void lv_free(void *memory) {
    if (!memory)
        return;
    size_t *allocation = (size_t *)memory - 1;
    assert(temporary_bytes >= *allocation);
    temporary_bytes -= *allocation;
    free(allocation);
}

uint32_t lv_draw_buf_width_to_stride(uint32_t width, lv_color_format_t format) {
    assert(format == LV_COLOR_FORMAT_A8);
    return (width + stride_alignment - 1) / stride_alignment * stride_alignment;
}

void lv_draw_buf_flush_cache(const lv_draw_buf_t *buffer, const lv_area_t *area) {
    assert(buffer && buffer->data && !area);
    ++cache_flushes;
}

static unsigned decode_font(const lv_font_t *font, const char *output_path) {
    const lv_font_fmt_txt_dsc_t *format = font->dsc;
    assert(format->bpp == 2 && format->bitmap_format == LV_FONT_FMT_TXT_COMPRESSED);
    assert(format->stride == 0 && format->cmap_num == 1);
    const lv_font_fmt_txt_cmap_t *cmap = format->cmaps;
    assert(cmap->type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY && cmap->glyph_id_start == 1);
    FILE *output = fopen(output_path, "wb");
    assert(output);
    unsigned levels = 0;
    for (unsigned gid = 1; gid <= cmap->list_length; ++gid) {
        uint32_t codepoint = cmap->range_start + cmap->unicode_list[gid - 1];
        lv_font_glyph_dsc_t glyph = {.resolved_font = font};
        assert(font->get_glyph_dsc(font, &glyph, codepoint, 0));
        assert(glyph.gid.index == gid);
        if (!glyph.box_w || !glyph.box_h)
            continue;
        size_t pixels = (size_t)glyph.box_w * glyph.box_h;
        uint8_t *canonical = malloc(pixels);
        assert(canonical);
        for (unsigned alignment = 1; alignment <= 8; alignment *= 8) {
            stride_alignment = alignment;
            unsigned stride = lv_draw_buf_width_to_stride(glyph.box_w, LV_COLOR_FORMAT_A8);
            size_t bytes = (size_t)stride * glyph.box_h;
            uint8_t *allocation = malloc(bytes + 16);
            assert(allocation);
            memset(allocation, 0xa5, bytes + 16);
            lv_draw_buf_t buffer = {.data = allocation + 8, .data_size = (uint32_t)bytes};
            unsigned flushes_before = cache_flushes;
            assert(font->get_glyph_bitmap(&glyph, &buffer) == &buffer);
            assert(cache_flushes == flushes_before + 1 && !temporary_bytes);
            for (unsigned row = 0; row < glyph.box_h; ++row) {
                const uint8_t *visible = buffer.data + (size_t)row * stride;
                uint8_t *expected = canonical + (size_t)row * glyph.box_w;
                if (alignment == 1)
                    memcpy(expected, visible, glyph.box_w);
                else
                    assert(!memcmp(expected, visible, glyph.box_w));
                for (unsigned column = 0; column < glyph.box_w; ++column) {
                    assert(visible[column] % 85 == 0);
                    levels |= 1U << (visible[column] / 85);
                }
                for (unsigned column = glyph.box_w; column < stride; ++column)
                    assert(visible[column] == 0xa5);
            }
            for (unsigned guard = 0; guard < 8; ++guard)
                assert(allocation[guard] == 0xa5 && allocation[bytes + 8 + guard] == 0xa5);
            free(allocation);
        }
        /* The generator hashes this exact sequence before compression: visible
         * A8 pixels, in glyph order, without row padding or metric fields. */
        assert(fwrite(canonical, 1, pixels, output) == pixels);
        free(canonical);
    }
    assert(fclose(output) == 0);
    assert(levels == 15);
    return cmap->list_length;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    assert(sizeof(lv_font_fmt_txt_glyph_dsc_t) == 8);
    unsigned glyphs = decode_font(&mybot_caption_font_20, argv[1]);
    glyphs += decode_font(&mybot_caption_font_28, argv[2]);
    glyphs += decode_font(&mybot_caption_font_ko_20, argv[3]);
    /* Exercise LVGL's actual public fallback lookup used by both caption sizes,
     * including the first/last modern syllables and standalone Jamo. */
    const uint32_t korean[] = {0xac00, 0xd7a3, 0xd55c, 0xad6d, 0xc5b4, 0x1100,
                               0x1161, 0x11a8, 0x3131, 0x314f, 0xffa1, 0xffc2};
    const lv_font_t *fonts[] = {&mybot_caption_font_20, &mybot_caption_font_28};
    for (unsigned index = 0; index < sizeof(fonts) / sizeof(fonts[0]); ++index) {
        for (unsigned letter = 0; letter < sizeof(korean) / sizeof(korean[0]); ++letter) {
            lv_font_glyph_dsc_t glyph;
            assert(lv_font_get_glyph_dsc(fonts[index], &glyph, korean[letter], 0));
            assert(glyph.resolved_font == &mybot_caption_font_ko_20 && !glyph.is_placeholder);
            assert(glyph.box_w && glyph.box_h && glyph.adv_w);
        }
    }
    assert(!temporary_bytes);
    printf("PASS native LVGL caption decoder: %u glyphs, A8 stride 1/8, guards, "
           "peak temporary bytes=%zu\n",
           glyphs, peak_temporary_bytes);
    return 0;
}
