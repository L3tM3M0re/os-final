#include <unios/khd.h>

void wait_disk(void) {
    while ((in_byte(REG_STATUS) & 0xC0) != 0x40);
}

// 读取一个扇区
void read_sector_pio(uint32_t lba, void *buf) {
    wait_disk();

    out_byte(REG_NSECTOR, 1);
    out_byte(REG_LBA_LOW,  (uint8_t)(lba & 0xFF));
    out_byte(REG_LBA_MID,  (uint8_t)((lba >> 8) & 0xFF));
    out_byte(REG_LBA_HIGH, (uint8_t)((lba >> 16) & 0xFF));

    // LBA 模式，主盘 (Master)
    out_byte(REG_DEVICE, 0xE0 | ((lba >> 24) & 0x0F));

    // 发送读命令
    out_byte(REG_CMD, 0x20); // ATA_READ

    wait_disk(); // 等待数据准备好

    // 读取数据 (in_word 一次读2字节，循环256次 = 512字节)
    port_read(REG_DATA, buf, 256);
}

// 批量读取函数
void raw_read_font(uint32_t start_lba, uint32_t bytes, uint8_t *buffer) {
    uint32_t sectors = (bytes + SECTOR_SIZE - 1) / SECTOR_SIZE;

    for (uint32_t i = 0; i < sectors; i++) {
        // 逐个扇区读取，计算当前内存偏移
        read_sector_pio(start_lba + i, buffer + (i * SECTOR_SIZE));
    }
}
