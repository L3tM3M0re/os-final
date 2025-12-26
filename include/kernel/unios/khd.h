#include <unios/hd.h>

static inline void out_byte(uint16_t port, uint8_t value) {
    __asm__ __volatile__ ("outb %1, %0" : : "dN" (port), "a" (value));
}

static inline uint8_t in_byte(uint16_t port) {
    uint8_t ret;
    __asm__ __volatile__ ("inb %1, %0" : "=a" (ret) : "dN" (port));
    return ret;
}

static inline void port_read(uint16_t port, void *buf, int n) {
    __asm__ __volatile__ ("cld; rep; insw"
        : "+D" (buf), "+c" (n)
        : "d" (port)
        : "memory");
}

void wait_disk(void);

void read_sector_pio(uint32_t lba, void *buf);

void raw_read_font(uint32_t start_lba, uint32_t bytes, uint8_t *buffer);
