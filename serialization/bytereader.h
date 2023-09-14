#include "pico/stdlib.h"

typedef struct bytereader {
    char* bytes;
    int offset;
} bytereader_t;

void bytereader_skip(bytereader_t* reader, int bytes);

uint8_t bytereader_read_u8(bytereader_t* reader);

uint16_t bytereader_read_u16(bytereader_t* reader);

uint32_t bytereader_read_u32(bytereader_t* reader);