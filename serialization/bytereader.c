#include <string.h>

#include "bytereader.h"

void bytereader_skip(bytereader_t* reader, int bytes)
{
    reader->offset += bytes;
}

uint8_t bytereader_read_u8(bytereader_t* reader)
{
    char* ptr = reader->bytes + reader->offset;
    reader->offset += 1;

    uint8_t result = 0;
    memcpy(&result, ptr, 1);
    return result;
}

uint16_t bytereader_read_u16(bytereader_t* reader)
{
    char* ptr = reader->bytes + reader->offset;
    reader->offset += 2;

    uint16_t result = 0;
    memcpy(&result, ptr, 2);
    return result;
}

uint32_t bytereader_read_u32(bytereader_t* reader)
{
    char* ptr = reader->bytes + reader->offset;
    reader->offset += 4;
    
    uint32_t result = 0;
    memcpy(&result, ptr, 4);
    return result;
}