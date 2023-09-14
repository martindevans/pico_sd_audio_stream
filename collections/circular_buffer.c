#include <stdio.h>
#include <string.h>

#include "circular_buffer.h"

// The hidden definition of our circular buffer structure
struct circular_buf_t {
	void* buffer;
	size_t head;
	size_t tail;
	size_t max;
};