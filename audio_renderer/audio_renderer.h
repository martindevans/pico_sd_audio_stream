#include "pico/audio_i2s.h"

typedef struct audio_library_track_sector_record {
    uint16_t track_id;
    uint32_t track_sector;
} audio_library_track_sector_record_t;

// Initialise the audio system and return a producer queue
// Returns NULL if audio initialisation fails!
audio_buffer_pool_t *init_audio_sys();

// Update the audio system
void pump_audio_sys();

// Set the map which defines where on the SD card audio should be fetched from
bool set_audio_sd_map(char* audio_map_data);

// Remove all music from the playlist
void music_playlist_clear();

// Add a track to the music playlist
void music_playlist_enqueue(uint16_t track_id);

// Set if the playlist loops
void music_playlist_set_loop(bool loop);

// Enqueue a sound effect for immediate playback
bool sfx_play(uint16_t sfx_id, uint8_t priority);