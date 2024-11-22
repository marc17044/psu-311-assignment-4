#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "cache.h"
#include "jbod.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
    if (cache != NULL || num_entries < 2 || num_entries > 4096) {
        return -1; // Cache already created or invalid size.
    }
    cache = malloc(num_entries * sizeof(cache_entry_t));
    if (!cache) {
        return -1; // Memory allocation failure.
    }
    cache_size = num_entries;
    for (int i = 0; i < cache_size; i++) {
        cache[i].valid = false;
    }
    clock = 0;
    return 1; // Success.
}

int cache_destroy(void) {
    if (cache == NULL) {
        return -1; // Cache not created or already destroyed.
    }
    free(cache);
    cache = NULL;
    cache_size = 0;
    return 1; // Success.
}

int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
    if (cache == NULL || buf == NULL) {
        return -1; // Cache not enabled or invalid buffer.
    }
    num_queries++;
    for (int i = 0; i < cache_size; i++) {
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            memcpy(buf, cache[i].block, JBOD_BLOCK_SIZE);
            cache[i].clock_accesses = clock++; // Update clock.
            num_hits++;
            return 1; // Block found.
        }
    }
    return -1; // Block not found.
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
    if (cache == NULL || buf == NULL) {
        return; // Cache not enabled or invalid buffer.
    }
    for (int i = 0; i < cache_size; i++) {
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            memcpy(cache[i].block, buf, JBOD_BLOCK_SIZE);
            cache[i].clock_accesses = clock++; // Update clock.
            return; // Updated.
        }
    }
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
    if (cache == NULL || buf == NULL) {
        return -1; // Cache not enabled or invalid buffer.
    }

    // Validate disk_num and block_num
    if (disk_num < 0 || disk_num >= JBOD_NUM_DISKS || block_num < 0 || block_num >= JBOD_NUM_BLOCKS_PER_DISK) {
        return -1; // Invalid disk or block number.
    }

    // Check if entry already exists
    for (int i = 0; i < cache_size; i++) {
        if (cache[i].valid && cache[i].disk_num == disk_num && cache[i].block_num == block_num) {
            return -1; // Entry already exists.
        }
    }

    // Find an invalid entry or the most recently used (MRU) entry.
    int mru_index = -1;
    int most_recent_access = -1;
    for (int i = 0; i < cache_size; i++) {
        if (!cache[i].valid) {
            mru_index = i;
            break; // Use the first invalid entry.
        }
        if (cache[i].clock_accesses > most_recent_access) {
            most_recent_access = cache[i].clock_accesses;
            mru_index = i;
        }
    }

    // Insert or replace the entry.
    cache[mru_index].valid = true;
    cache[mru_index].disk_num = disk_num;
    cache[mru_index].block_num = block_num;
    memcpy(cache[mru_index].block, buf, JBOD_BLOCK_SIZE);
    cache[mru_index].clock_accesses = clock++; // Update clock.
    return 1; // Success.
}


bool cache_enabled(void) {
    return cache != NULL;
}

void cache_print_hit_rate(void) {
    fprintf(stderr, "num_hits: %d, num_queries: %d\n", num_hits, num_queries);
    if (num_queries > 0) {
        fprintf(stderr, "Hit rate: %5.1f%%\n", 100.0 * (float) num_hits / num_queries);
    } else {
        fprintf(stderr, "Hit rate: N/A\n");
    }
}

int cache_resize(int new_size) {
    if (new_size < 2 || new_size > 4096 || cache == NULL) {
        return -1; // Invalid size or cache not enabled.
    }

    cache_entry_t *new_cache = malloc(new_size * sizeof(cache_entry_t));
    if (!new_cache) {
        return -1; // Memory allocation failure.
    }

    // Copy valid entries into the new cache.
    int copy_count = (new_size < cache_size) ? new_size : cache_size;
    for (int i = 0; i < copy_count; i++) {
        new_cache[i] = cache[i];
    }

    // Initialize remaining entries if resizing to a larger size.
    for (int i = copy_count; i < new_size; i++) {
        new_cache[i].valid = false;
    }

    free(cache);
    cache = new_cache;
    cache_size = new_size;
    return 1; // Success.
}
