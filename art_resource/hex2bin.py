import gzip
import sys
import os

# 配置
INPUT_FILE = "unifont_jp-17.0.03.hex.gz"
OUTPUT_FILE = "unifont.bin"
FONT_WIDTH = 16
FONT_HEIGHT = 16
BYTES_PER_GLYPH = (FONT_WIDTH * FONT_HEIGHT) // 8  # 32 bytes

def convert_hex_to_bin(input_path, output_path):
    print(f"Processing {input_path} -> {output_path} ...")

    # 1. 准备一个全零的缓冲区，大小覆盖 BMP (U+0000 ~ U+FFFF)
    # 大小 = 65536 * 32 bytes = 2 MB
    total_chars = 65536
    buffer_size = total_chars * BYTES_PER_GLYPH
    font_buffer = bytearray(buffer_size)

    count = 0

    try:
        # 使用 gzip 打开文件
        with gzip.open(input_path, 'rt', encoding='utf-8') as f:
            for line in f:
                line = line.strip()
                if not line: continue

                parts = line.split(':')
                if len(parts) != 2: continue

                # 解析 Unicode 码点
                try:
                    codepoint = int(parts[0], 16)
                except ValueError:
                    continue

                # 忽略超出 BMP 范围的字符 (我们只分配了 2MB 内存)
                if codepoint >= total_chars:
                    continue

                hex_data = parts[1]
                data_len = len(hex_data)

                # 计算在 buffer 中的起始位置
                offset = codepoint * BYTES_PER_GLYPH

                if data_len == 32:
                    # === 8x16 字符 (16 字节数据) ===
                    # 我们需要将其扩展为 16x16 (32 字节)
                    # 策略：放在左侧，右侧补零
                    for i in range(16):
                        # 读取原始的 1 字节
                        val = int(hex_data[i*2 : i*2+2], 16)

                        # 写入目标 buffer (2 字节/行)
                        font_buffer[offset + i*2]     = val  # 左半边
                        font_buffer[offset + i*2 + 1] = 0x00 # 右半边 (补零)

                elif data_len == 64:
                    # === 16x16 字符 (32 字节数据) ===
                    # 直接拷贝
                    for i in range(32):
                        val = int(hex_data[i*2 : i*2+2], 16)
                        font_buffer[offset + i] = val

                else:
                    print(f"Warning: Skipping malformed line for U+{parts[0]}")
                    continue

                count += 1

    except FileNotFoundError:
        print(f"Error: File '{input_path}' not found.")
        return
    except Exception as e:
        print(f"Error: {e}")
        return

    # 2. 写入二进制文件
    with open(output_path, 'wb') as f:
        f.write(font_buffer)

    print(f"Done! Converted {count} glyphs.")
    print(f"Output size: {len(font_buffer)} bytes ({len(font_buffer)/1024/1024:.2f} MB)")

if __name__ == "__main__":
    # 如果命令行提供了参数，则使用参数，否则使用默认值
    in_file = sys.argv[1] if len(sys.argv) > 1 else INPUT_FILE
    out_file = sys.argv[2] if len(sys.argv) > 2 else OUTPUT_FILE

    convert_hex_to_bin(in_file, out_file)
