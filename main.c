#include <stdio.h>
#include <math.h>
#include <string.h>
#include <tusb.h>

#include "hardware/clocks.h"
#include "hardware/structs/clocks.h"

#include "pico/stdlib.h"

#include "hardware/dma.h"
#include "pico/sd_card.h"

#include "audio_renderer/audio_renderer.h"
#include "serialization/bytereader.h"

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

char sd_data_buffer_0[SD_SECTOR_SIZE] __attribute__ ((aligned(32)));
char sd_data_buffer_1[SD_SECTOR_SIZE] __attribute__ ((aligned(32)));

void read_sd_block(int id, uint32_t *buffer)
{
    int err = sd_readblocks_sync(buffer, id, 1);
    
    // Check for errors in SD reading
    if (err != SD_OK) {
        printf("SD ERR %ld\n", err);
    }
}

void play_track(uint16_t track_id, audio_buffer_pool_t *buffer_pool)
{
    read_sd_block(0, (uint32_t*)sd_data_buffer_0);

    // Check the magic header
    int magic_header_cmp = strncmp(sd_data_buffer_0, "RETROFIST_AUDIO!", 16);
    printf("Audio Library Magic Header Validation: %d\n", magic_header_cmp);

    // Create a read to read data from the SD card buffer
    bytereader_t reader = {
        bytes: sd_data_buffer_0,
        offset: 16,
    };

    // Read track count and check validity
    uint16_t track_count = bytereader_read_u16(&reader);
    printf("Tracks Count: %d\n", track_count);
    if (track_id > track_count) {
        printf("%d is an invalid track id\n", track_id);
        return;
    }

    // Skip to the relevant track record
    bytereader_skip(&reader, track_id * 8);

    // Read the record
    uint16_t record_track_id = bytereader_read_u16(&reader);
    uint32_t record_track_sector = bytereader_read_u32(&reader);
    uint32_t record_track_sector_count = bytereader_read_u16(&reader);
    printf("track %d is at blocks %d => %d\n", record_track_id, record_track_sector, record_track_sector + record_track_sector_count);

    // Read and play sectors
    for (int i = 0; i < record_track_sector_count; i++)
    {
        gpio_put(LED_PIN, i % 10 == 1);

        // Get an audio buffer
        audio_buffer_t *buffer = take_audio_buffer(buffer_pool, true);

        // Read the sector directly into the audio buffer
        read_sd_block((int)record_track_sector + i, (uint32_t*)buffer->buffer->bytes);

        // Read sample count from end of the buffer
        reader.bytes = buffer->buffer->bytes;
        reader.offset = SD_SECTOR_SIZE - 4;
        uint16_t sample_count = bytereader_read_u8(&reader);

        // Tell the audio system how many samples are available
        buffer->sample_count = sample_count;

        // Enqueue data for playback
        give_audio_buffer(buffer_pool, buffer);

        printf("Buffer %d, samples %d\n", i, sample_count);
    }

    printf("Completed Playback");
}

int main()
{
    // Prepare to blink LED
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, true);

    // Wait for serial connection
    stdio_init_all();
    int i = 0;
    while (!tud_cdc_connected()) {
        sleep_ms(250);
        gpio_put(LED_PIN, i++ % 2 == 0);
    }
    printf("tud_cdc_connected()\n");
    printf("AUDIO FROM SD\n");

    gpio_put(LED_PIN, 1);

    // Init SD card
    sd_init_4pins();
    printf("Initialised SD\n");

    // Create audio buffer system
    audio_buffer_pool_t *buffer_pool = init_audio_sys();
    if (buffer_pool == NULL) {
        panic("Cannot Initialise Audio\n");
        return 0;
    }
    printf("Initialised Audio Renderer\n");

    // Read in the audio map from SD
    uint32_t *audio_map = malloc(sizeof(uint32_t[SD_SECTOR_SIZE]));
    read_sd_block(0, audio_map);
    if (!set_audio_sd_map((char*)audio_map)) {
        panic("Failed To Load Audio Map!");
    }

    // Play the three tracks
    music_playlist_enqueue(0);
    music_playlist_enqueue(1);
    music_playlist_enqueue(2);
    music_playlist_set_loop(true);

    // Pump audio system
    while (true)
    {
        pump_audio_sys(buffer_pool);

        // Interrupt with some sound effects when a key is pressed
        int c = getchar_timeout_us(0);
        if (c >= 0)
        {
            if (c == '1') { sfx_play(3, 1); }
            if (c == '2') { sfx_play(4, 1); }
            if (c == '3') { sfx_play(5, 1); }
            if (c == '4') { sfx_play(6, 1); }
            if (c == '5') { sfx_play(7, 1); }
        }
    }

    // // Play all three tracks
    // play_track(0, buffer_pool);
    // play_track(1, buffer_pool);
    // play_track(2, buffer_pool);

    // // Flash LED
    // while (true) {
    //     gpio_put(LED_PIN, i++ % 2 == 0);
    //     sleep_ms(250);
    // }

    printf("x_x\n");
    return 0;
}