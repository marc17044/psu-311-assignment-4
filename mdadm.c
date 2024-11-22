#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"

int is_mounted = 0;
int write_permission = 0;

int mdadm_write(uint32_t start_address, uint32_t length_to_write, const uint8_t *buffer_to_write) {
    // Validate input
    if ((buffer_to_write == NULL) && (length_to_write == 0)) {
        return 0;
    }
    if (start_address + length_to_write > 65536 * 16) {
        return -1;
    }
    if (length_to_write > 1024) {
        return -2;
    }
    if (!is_mounted) {
        return -3;
    }
    if ((buffer_to_write == NULL) && (length_to_write > 0)) {
        return -4;
    }
    if (!write_permission) {
        return -5;
    }

    // Calculate disk and block addresses
    int disk_index = start_address / JBOD_DISK_SIZE;
    int block_index = (start_address % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
    int offset_within_block = start_address % JBOD_BLOCK_SIZE;

    int remaining_bytes = length_to_write;
    int total_bytes_written = 0;
    uint8_t block_buffer[256];

    while (remaining_bytes > 0) {
        // Seek to the starting block
        jbod_operation(JBOD_SEEK_TO_DISK << 12 | disk_index, NULL);
        jbod_operation((JBOD_SEEK_TO_BLOCK << 12) | (block_index << 4), NULL);

        // Read the current block
        jbod_operation((JBOD_READ_BLOCK << 12), block_buffer);

        // Modify the block buffer
        for (int i = offset_within_block; i < JBOD_BLOCK_SIZE && remaining_bytes > 0; i++) {
            block_buffer[i] = *buffer_to_write;
            buffer_to_write++;
            remaining_bytes--;
            total_bytes_written++;
        }

        // Seek to the correct disk and block again before writing
        jbod_operation((JBOD_SEEK_TO_BLOCK << 12) | (block_index << 4) | disk_index, NULL);
        jbod_operation((JBOD_WRITE_BLOCK << 12), block_buffer);

        // Increment block and disk indices
        block_index++;
        if (block_index >= JBOD_NUM_BLOCKS_PER_DISK) {
            disk_index++;
            block_index = 0; // Reset block index if end of disk is reached
        }
        offset_within_block = 0; // Reset offset for subsequent blocks
    }
    return total_bytes_written;
}

int mdadm_write_permission(void) {
    write_permission = 1;
    return jbod_operation(JBOD_WRITE_PERMISSION << 12, NULL);
}

int mdadm_revoke_write_permission(void) {
    return jbod_operation(JBOD_REVOKE_WRITE_PERMISSION << 12, NULL);
}

// Mount the drive
int mdadm_mount(void) {
    if (is_mounted) {
        return -1;
    }

    //JBOD_MOUNT << 12 is a bitwsie left shift operator effectively multiplies the number by 2^12
    //This declares a 32-bit unsigned integer op is short for "operation code
    uint32_t op = JBOD_MOUNT << 12;
    if (!jbod_operation(op, NULL)) {
        is_mounted = 1;
        return 1;
    } else {
        return -1;
    }
}

// Unmount the drive
int mdadm_unmount(void) {
    if (!is_mounted) {
        return -1;
    }
    uint32_t op = JBOD_UNMOUNT << 12;
    if (!jbod_operation(op, NULL)) {
        is_mounted = 0;
        return 1;
    } else {
        return -1;
    }
}

// Read from the disk
int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf) {
    int remaining_bytes = read_len;

    // Check if the read would be out of bounds
    if (start_addr + read_len > 65536 * 16) {
        printf("Read out of bounds\n");
        return -1;
    }

    // Check if the read length exceeds the maximum allowed size
    if (read_len > 1024) {
        printf("Read length exceeds maximum allowed size\n");
        return -2;
    }

    // Ensure the drive is mounted
    if (!is_mounted) {
        printf("Drive not mounted\n");
        return -3;
    }

    // Ensure the buffer is valid
    if ((read_buf == NULL) && (read_len > 0)) {
        printf("Invalid buffer\n");
        return -4;
    }

    uint8_t temp_block[256];
    uint8_t *block_pointer = temp_block;

    // Calculate disk and block IDs from the starting address
    int disk_index = start_addr / JBOD_DISK_SIZE;
    int base_address_of_calculated_disk = disk_index * JBOD_DISK_SIZE;
    int offset_by_the_start_addr = start_addr - base_address_of_calculated_disk;
    int block_index_in_disk = (offset_by_the_start_addr) / JBOD_BLOCK_SIZE;


    // Seek to the appropriate disk and block
    jbod_operation(JBOD_SEEK_TO_DISK << 12 | disk_index | block_index_in_disk << 4, NULL);
    jbod_operation(JBOD_SEEK_TO_BLOCK << 12 | disk_index | block_index_in_disk << 4, NULL);

    uint8_t *destination_memory_buffer_pointer = read_buf;
    int block_offset = start_addr % 256;
    int is_first_block = 1;
    int disk_switched = 0;
    int current_addr = start_addr;

    // Read the initial block
    jbod_operation(disk_index | block_index_in_disk << 4 | JBOD_READ_BLOCK << 12, temp_block);
    block_index_in_disk++;

    while (remaining_bytes > 0) {

        // Handle switching disks if needed
        if ((current_addr % JBOD_DISK_SIZE == 0) && (current_addr > 0) && !disk_switched) {
            disk_index = current_addr / JBOD_DISK_SIZE;
            block_index_in_disk = (current_addr - disk_index * JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;


            jbod_operation(JBOD_SEEK_TO_DISK << 12 | disk_index | block_index_in_disk << 4, NULL);
            jbod_operation(JBOD_SEEK_TO_BLOCK << 12 | disk_index | block_index_in_disk << 4, NULL);

            jbod_operation(disk_index | block_index_in_disk << 4 | JBOD_READ_BLOCK << 12, temp_block);
            block_index_in_disk++;
            block_pointer = temp_block;
            disk_switched = 1;
        }
        // Handle reading for the first block if offset exists and exceeds the block size
        else if (block_offset + remaining_bytes > JBOD_BLOCK_SIZE && is_first_block) {
            block_pointer += block_offset;
            memcpy(destination_memory_buffer_pointer, block_pointer, 256 - block_offset);

            destination_memory_buffer_pointer += 256 - block_offset;
            current_addr += 256 - block_offset;
            remaining_bytes -= 256 - block_offset;

            block_offset = 0;
            is_first_block = 0;

            // Read the next block
            jbod_operation(disk_index | block_index_in_disk << 4 | JBOD_READ_BLOCK << 12, temp_block);
            block_index_in_disk++;
            block_pointer = temp_block;
        }
        // Handle reading a full block
        else if (!is_first_block && remaining_bytes > 256) {
            memcpy(destination_memory_buffer_pointer, block_pointer, 256);

            destination_memory_buffer_pointer += 256;
            current_addr += 256;
            remaining_bytes -= 256;

            // Read the next block
            jbod_operation(disk_index | block_index_in_disk << 4 | JBOD_READ_BLOCK << 12, temp_block);
            block_index_in_disk++;
            block_pointer = temp_block;
        }
        // Handle reading the last block or a partial block
        else {
            block_pointer += block_offset;
            memcpy(destination_memory_buffer_pointer, block_pointer, remaining_bytes);

            remaining_bytes = 0;
        }
    }

    return read_len;
}

