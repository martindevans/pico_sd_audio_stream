#include <stdio.h>
#include <string.h>

#include "audio_renderer.h"
#include "serialization/bytereader.h"
#include "collections/circular_buffer.h"

#include "pico/sd_card.h"
#include "hardware/dma.h"
#include "hardware/claim.h"

#define AUDIOSYS_SAMPLES_PER_BUFFER 256
#define AUDIOSYS_SAMPLE_RATE 12000
#define AUDIOSYS_BUFFER_SUBSYS_COUNT 3
const int AUDIOSYS_BUFFER_COUNT = AUDIOSYS_BUFFER_SUBSYS_COUNT * 2; // BGM + SFX

#pragma region music_playlist

typedef struct bgm_playlist_item bgm_playlist_item_t;
typedef struct bgm_playlist_item {
    // Track ID of the music
    uint16_t track_id;

    // Next track to play
    bgm_playlist_item_t *next;
} bgm_playlist_item_t;

// Linked list of available "async_buffer_read" objects
static bgm_playlist_item_t *music_playlist_head = NULL;

// Remove all music from the playlist
void music_playlist_clear()
{
    while (music_playlist_head != NULL)
    {
        bgm_playlist_item_t *item = music_playlist_head;
        music_playlist_head = item->next;
        free(item);
    }
}

// Add a track to the music playlist
void music_playlist_enqueue(uint16_t track_id)
{
    bgm_playlist_item_t* item = malloc(sizeof(bgm_playlist_item_t));
    item->track_id = track_id;
    item->next = music_playlist_head;
    music_playlist_head = item;
}

static bool music_playlist_loop = true;

// Set if the playlist loops
void music_playlist_set_loop(bool loop)
{
    music_playlist_loop = loop;
}

#pragma endregion

#pragma region sfx_queue

// Indicates if there is a pending SFX to play
static bool sfx_is_pending = false;

// Track ID of the pending sound effect
static uint16_t sfx_pending_item = 0;

// Priority of the queued sound effect
static uint8_t sfx_pending_priority = 0;

// Enqueue a sound effect for immediate playback
bool sfx_play(uint16_t sfx_id, uint8_t priority)
{
    // Early exit if a higher priority sfx is already waiting
    if (sfx_is_pending && sfx_pending_priority >= priority) {
        return false;
    }

    sfx_is_pending = true;
    sfx_pending_item = sfx_id;
    sfx_pending_priority = priority;
    return true;
}

#pragma endregion

#pragma region sd_audio_map
// Raw "Audio Map" data read from the SD card
// - 16 byte: "RETROFIST_AUDIO!"
// - 2 byte:  Track Count
// - Per Track:
//   - 2 byte: Track ID
//   - 4 byte: Track First Sector
//   - 2 byte: Track Sector Count
static char* global_audio_map = NULL;

// Set the map which defines where on the SD card audio should be fetched from
bool set_audio_sd_map(char* audio_map_data)
{
    // Check the magic header
    int magic_header_cmp = strncmp(audio_map_data, "RETROFIST_AUDIO!", 16);
    if (!magic_header_cmp) {
        return false;
    }

    // Free the old map
    if (global_audio_map != NULL) {
        free(global_audio_map);
    }

    // If validation succeeded, store it
    global_audio_map = audio_map_data;

    return true;
}

bool read_track_data(uint16_t id, uint32_t* track_sector, uint16_t* track_sector_count)
{
    if (global_audio_map == NULL) {
        return false;
    }

    // Create a read to read data from the audio map
    bytereader_t reader = {
        bytes: global_audio_map,
        offset: 16 + id * 8,
    };

    // Read the record
    uint16_t record_track_id = bytereader_read_u16(&reader);
    if (record_track_id != id) {
        return false;
    }

    *track_sector = bytereader_read_u32(&reader);
    *track_sector_count = bytereader_read_u16(&reader);
    return true;
}

#pragma endregion

#pragma region init_audio_sys

static audio_format_t audio_format = {
    .format = AUDIO_BUFFER_FORMAT_PCM_S16,
    .sample_freq = AUDIOSYS_SAMPLE_RATE,
    .channel_count = 1,
};

static audio_buffer_format_t producer_format = {
    .format = &audio_format,
    .sample_stride = 2
};

// Initialise the audio system and return a producer queue
// May return NULL if audio initialisation fails!
audio_buffer_pool_t *init_audio_sys()
{
    audio_buffer_pool_t *producer_pool = audio_new_producer_pool(&producer_format, AUDIOSYS_BUFFER_COUNT, AUDIOSYS_SAMPLES_PER_BUFFER);
    const audio_format_t *output_format;

    // audio system internally claims the channel. Claims a one now (to find a free one) and then unclaim it so the audio system can claim it.
    int channel = dma_claim_unused_channel(true);
    dma_channel_unclaim(channel);

    audio_i2s_config_t config = {
        .data_pin = PICO_AUDIO_I2S_DATA_PIN,
        .clock_pin_base = PICO_AUDIO_I2S_CLOCK_PIN_BASE,
        .dma_channel = channel,
        .pio_sm = 0,
    };

    output_format = audio_i2s_setup(&audio_format, &config);
    if (!output_format) {
        return NULL;
    }

    if (!audio_i2s_connect(producer_pool)) {
        return NULL;
    }
    audio_i2s_set_enabled(true);

    return producer_pool;
}

#pragma endregion init_audio_sys

#pragma region buffer_track_queue

// Indicates an "in flight" read from the SD card into an audio buffer
typedef struct async_buffer_read {
    // The buffer being read into from the SD card
    audio_buffer_t *buffer;

    // SD card async handle to pass into e.g. `sd_scatter_read_complete`
    int async_operation_handle;
} async_buffer_read_t;

typedef struct playing_track_buffer_queue {
    uint16_t track_id;
    uint32_t base_sector;
    uint16_t sector_count;
    uint16_t sector_index;

    async_buffer_read_t async_buffers[AUDIOSYS_BUFFER_SUBSYS_COUNT];
    uint8_t head;
} playing_track_buffer_queue_t;

bool track_buffer_pump(playing_track_buffer_queue_t *queue, audio_buffer_pool_t* buffer_pool)
{
    if (queue->count == 0) {
        return false;
    }

    async_buffer_read_t *head = &queue->async_buffers[queue->head];

    // Check if head of queue has finished reading from SD card
    int async_handle = head->async_operation_handle;
    if (!sd_scatter_read_complete(&async_handle)) {
        return false;
    }

    // Read sample count from end of the buffer
    audio_buffer_t *buffer = head->buffer;
    bytereader_t reader = {
        bytes: buffer->buffer->bytes,
        offset: SD_SECTOR_SIZE - 4,
    };
    buffer->sample_count = bytereader_read_u8(&reader);

    // Submit buffer for playback
    give_audio_buffer(buffer_pool, buffer);

    // Begin a new transfer in this slot
    if (queue->sector_index < queue->sector_count)
    {
        panic("read next sector into head buffer")
    }
    else
    {
        panic("Reached end of track");
    }

    // Move the queue to point to the next item
    queue->head++;
    if (queue->head >= AUDIOSYS_BUFFER_SUBSYS_COUNT) {
        queue->head = 0;
    }
}

#pragma endregion

static playing_track_buffer_queue_t bgm_playing_buffer_queue = {
    .track_id = 0,
    .base_sector = 0,
    .sector_count = 0,
    .sector_index = 1,

    .head = 0,
    .tail = 0,
    .count = 0,
};

void update_bgm_playlist()
{
    if (bgm_playing_buffer_queue.sector_index >= bgm_playing_buffer_queue.sector_count && music_playlist_head != NULL) {

        // dequeue item at start of playlist
        bgm_playlist_item_t *bgm = music_playlist_head;
        music_playlist_head = bgm->next;
        bgm->next = NULL;

        // Add to end of playlist if looping
        if (music_playlist_loop) {
            bgm_playlist_item_t *item = music_playlist_head;
            while (item->next != NULL) {
                item = item->next;
            }
            item->next = bgm;
        }

        // Read the track data
        uint32_t sector;
        uint16_t count;
        if (!read_track_data(bgm->track_id, &sector, &count)) {
            return;
        }

        bgm_playing_buffer_queue = (playing_track_buffer_queue_t) {
            .track_id = bgm->track_id,
            .base_sector = sector,
            .sector_count = count,
            .sector_index = 0,

            .head = 0,
            .tail = 0,
            .count = 0,
        };
    }
}

// void begin_playing_sfx()
// {
//     if (sfx_is_pending)
//     {
//         // Clear the request
//         sfx_is_pending = false;

//         // If the playing item is higher priority just cancel the request
//         if (sfx_is_playing && sfx_playing_priority > sfx_pending_priority) {
//             sfx_pending_item = 0;
//             sfx_pending_priority = 0;
//             return;
//         }

//         // Clear out the request data
//         uint16_t requested_track = sfx_pending_item;
//         uint8_t requested_priority = sfx_pending_priority;
//         sfx_pending_item = 0;
//         sfx_pending_priority = 0;

//         // Try to get track data, exit if invalid
//         uint32_t sector;
//         uint16_t count;
//         if (!read_track_data(requested_track, &sector, &count)) {
//             return;
//         }

//         sfx_is_playing = true;
//         sfx_playing_priority = sfx_pending_priority;
//         playing_sfx = (playing_track_sector_data_t) {
//             .track_id = sfx_pending_item,
//             .base_sector = sector,
//             .sector_count = count,
//             .sector_index = 0
//         };
//     }
// }


// Get an async_read_buffer, reading the given SD sector
async_buffer_read_t* begin_async_read(audio_buffer_pool_t* buffer_pool, uint32_t sector)
{
    // Try to get a buffer
    audio_buffer_t *buffer = take_audio_buffer(buffer_pool, false);
    if (buffer == NULL) {
        return NULL;
    }

    // Get a read buffer to represent the async load into the audio buffer
    async_buffer_read_t *async_read = get_async_read_buffer();
    async_read->buffer = buffer;

    // Begin async read operation
    async_read->async_operation_handle = sd_readblocks_async((uint32_t*)buffer->buffer->bytes, sector, 1);

    return async_read;
}

// Update the audio system
void pump_audio_sys(audio_buffer_pool_t* buffer_pool)
{
    // Cycle to the next BGM track if necessary
    //todo: this really only needs calling when `bgm_next_sector` is incremented
    update_bgm_playlist();

    // Start playing a sound effect if one is pending
    //begin_playing_sfx();

    // Play SFX if one is playing
    //if (sfx_is_playing)
    {
        panic("Not Implemented: sfx_is_playing");
        // todo: load first sector of audio clip and insert into linked list
    }
    //else
    {
        panic("setup SD read in `async_read`");
        panic("append `async_read` to bgm playback queue");
    }

    panic("Submit next sector from start of linked list if it's loaded");
}