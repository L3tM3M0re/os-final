#include <stdint.h>
#include <string.h>

#define GLYPH_SIZE 32

#define FONT_LBA_START  54000
#define FONT_SIZE_BYTES (2 * 1024 * 1024)
#define SECTOR_SIZE     512
#define FONT_WIDTH 16
#define FONT_HEIGHT 16
#define ASCII_WIDTH 8

int font_init(void);
/*!
 * \brief 从 str 中解码一个 UTF-8 字符
 * \param 返回值: 该字符占用的字节数 (1-4). out_code: 解码后的 Unicode Codepoint
 */
int utf8_decode(const char *str, uint32_t *out_code);
/*!
 * \brief 获取指定 Unicode 码点的字形位图数据指针
 *
 * \param codepoint Unicode 码点
 * \return 指向字形位图数据的指针，若无对应字形则返回 NULL
 */
const uint8_t* get_glyph_bitmap(uint32_t codepoint);
