#include "Helper/CRC32Helper.h"

uint32_t CRC32Helper::crc_table[256];
bool CRC32Helper::table_initialized = false;

void CRC32Helper::init_table()
{
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
        {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }
        crc_table[i] = c;
    }
    table_initialized = true;
}

uint32_t CRC32Helper::calculate(const uint8_t *data, size_t len,
                   uint32_t crc)
{
    if (!table_initialized)
    {
        init_table();
    }

    crc = crc ^ 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++)
    {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

// 增量计算（适用于流式数据）
uint32_t CRC32Helper::update(uint32_t crc, const uint8_t *data, size_t len)
{
    if (!table_initialized)
    {
        init_table();
    }

    for (size_t i = 0; i < len; i++)
    {
        crc = crc_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}
