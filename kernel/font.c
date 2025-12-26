#include <unios/font.h>
#include <unios/assert.h>
#include <unios/memory.h>
#include <unios/tracing.h>
#include <unios/khd.h>
#include <stddef.h>
#include <stdint.h>

#define GLYPH_SIZE 32

#define FONT_LBA_START  54000
#define FONT_SIZE_BYTES (2 * 1024 * 1024)
#define SECTOR_SIZE     512

static uint8_t *font_data = NULL;
static size_t   font_size = 0;

int font_init(void)
{
    font_size = FONT_SIZE_BYTES;
    font_data = kmalloc(font_size);

    if (font_data == NULL) {
        panic("font_init: out of memory for font");
        return -1;
    }


    uint32_t sectors_to_read = font_size / SECTOR_SIZE;

    raw_read_font(FONT_LBA_START, font_size, font_data);

    kinfo("font loaded raw from LBA %d: %d bytes", FONT_LBA_START, font_size);
    return 0;
}

int utf8_decode(const char *str, uint32_t *out_code) {
    const uint8_t *p = (const uint8_t *)str;
    if (!p || !*p) return 0;

    // 1字节 (ASCII): 0xxxxxxx
    if (p[0] < 0x80) {
        *out_code = p[0];
        return 1;
    }
    // 2字节: 110xxxxx 10xxxxxx
    else if ((p[0] & 0xE0) == 0xC0) {
        *out_code = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    // 3字节 (大多数汉字): 1110xxxx 10xxxxxx 10xxxxxx
    else if ((p[0] & 0xF0) == 0xE0) {
        *out_code = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    // 4字节: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
    else if ((p[0] & 0xF8) == 0xF0) {
        *out_code = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }

    // 非法序列，当做 '?' (0x3F) 处理，跳过1字节
    *out_code = 0x3F;
    return 1;
}

const uint8_t* get_glyph_bitmap(uint32_t codepoint) {
    size_t offset = codepoint * GLYPH_SIZE;
    if (offset + GLYPH_SIZE > font_size)
        return NULL;
    return font_data + offset;
}
