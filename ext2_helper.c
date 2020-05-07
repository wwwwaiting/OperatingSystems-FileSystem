#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "ext2.h"
#include "ext2_helper.h"

// Function for finding the *next* available free spot in the bitmap
// The size determine whether the function should find free spot for inode or block bitmap
// find_next_available will also check if there are any free spaces before looking.
int find_next_available(unsigned char *bitmap, int size) {
    if (gd->bg_free_blocks_count == 0 || gd->bg_free_inodes_count == 0) {
        fprintf(stderr, "ERROR: Not enough space in the file system\n");
        exit(ENOSPC);
    }

    int bit;
    for (int i = 0 ; i < size ; i++) {
        for (int j = 0; j < 8; j++) {
            bit = (bitmap[i] >> j) & 1;
            if ( bit == 0 ) {
                int num = i * 8 + (j + 1);
                set_bit(bitmap, num, size);
                return num;
            }
        }
    }
    return -1;
}

// Set/unset the specific bit in the bitmap
// The size determine whether the function should find free spot for inode or block bitmap
// It also adjusts the free block counts / free inode counts accordingly.
void set_bit(unsigned char* bitmap, int num, int size) {
    int byte = (num - 1) / 8;
    int bit = (num - 1) % 8;

    bitmap[byte] |= 1 << bit;
    if (size == 4) {
        sb->s_free_inodes_count--;
        gd->bg_free_inodes_count--;
    } else if (size == 16) {
        sb->s_free_blocks_count--;
        gd->bg_free_blocks_count--;
    }
}

void unset_bit(unsigned char* bitmap, int num, int size) {
    int byte = (num - 1) / 8;
    int bit = (num - 1) % 8;

    bitmap[byte] &= ~( 1 << bit); // unset
    if (size == 4) {
        sb->s_free_inodes_count++;
        gd->bg_free_inodes_count++;
        return;
    } else if (size == 16) {
        sb->s_free_blocks_count++;
        gd->bg_free_blocks_count++;
        return;
    }
}

// See whether a specific is set in the bit map
int is_set(unsigned char* bitmap, int num) {
    int byte = (num - 1) / 8;
    int bit = (num - 1) % 8;

    return (bitmap[byte] >> bit) & 1;
}

// actual_rec_len calculates the actual rec len for a dir entry
int actual_rec_len(int name_len) {
    int len = sizeof(struct ext2_dir_entry) + name_len;
    if (len % 4 != 0 ) {
        len += 4 - (len % 4);
    }

    return len;
}

// Given a name, check_exist will check whether a dir entry with the
// same name exists in the inode dir entry
// On success, check_exist will return the existing inode, and 0 otherwise.
int check_exist(char* name, int inode) {
    int block_num;
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;

    // Search for all data blocks
    int data_blocks = inode_table[inode - 1].i_blocks / 2;
    if ( inode_table[inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[inode - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }

    for (int i = 0; i < data_blocks ; i++) {
        if (i < 12) {
            block_num = inode_table[inode - 1].i_block[i];
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        }

        int len = strlen(name);
        struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num);
        if (len == entry->name_len && (strncmp(name, entry->name, len) == 0)) {
            return entry->inode;
        }
        struct ext2_dir_entry *next; 
        int rec_len = entry->rec_len;
    
        while (rec_len != EXT2_BLOCK_SIZE) {

            next = (struct ext2_dir_entry *)((char *)entry + rec_len);
            if (len == next->name_len && (strncmp(name, next->name, len) == 0)) {
                return next->inode;
            }
            rec_len += next->rec_len;
        }
    }
    return 0;
}

// inode_num returns the inode number of the last entry in the path if exists,
// Otherwise, it should return 0.
int inode_num(char* path, int* prev) {
    char buf[EXT2_NAME_LEN];
    strcpy(buf, path);

    int i = 0;
    int inode = EXT2_ROOT_INO;
    int is_dir;
    char* curr;
    char* token = strtok(buf, "/");
    while ( token != NULL ) {
        curr = token;
        is_dir = check_exist(curr, inode); 
        token = strtok(NULL, "/");
        i++;

        
        if ( token != NULL ) {    // Not last token
            if ( is_dir == 0) {
            fprintf(stderr, "ERROR: directoy %s does not exist\n", curr);
            exit(ENOENT);
            } 
            if (!IS_S_DIR(is_dir)) {
                fprintf(stderr, "ERROR: %s is not a directory\n", curr);
                exit(ENOENT);
            }
            inode = is_dir;
        } else {
            *prev = inode;
            return is_dir;
        }
    }

    return inode;
}

// Validate a path, if the path is an absolute path. It will check for leading slash. 
// validate_path will check for the following:
// 1. leading slashes
// 2. double or more slashes inbetween the path
// Note: trailing slashes are acceptable for absolute path
void validate_path(char* path, int flag) {
    int path_len = strlen(path);
    if (path_len > EXT2_NAME_LEN) {
        fprintf(stderr, "ERROR: %s's length is too long\n", path);
        exit(ENOENT);
    }

    // Check for leading slashes
    int len = strspn(path, "/");
    if (flag && len != 1) {
        fprintf(stderr, "ERROR: %s is not a valid path\n", path);
        exit(ENOENT);
    } else if (!flag && len > 1) {
        fprintf(stderr, "ERROR: %s is not a valid path\n", path);
        exit(ENOENT);
    }
    
    // Check for any double or more slashes inbetween
    int count = 0;
    for (int i = 0; i < path_len; i++) {
        if (path[i] != '/' && count <= 1) {
            count = 0;
        } else if (path[i] != '/' && count > 1) {
            fprintf(stderr, "ERROR: %s is not a valid path\n", path);
            exit(ENOENT);
        } else {
            count++;
        }
    }
}

// Insert new_inode after the parent_inode's last existing dir entry
// with the given name. It also sets the type for the new dir entry.
void insert_dir_entry(int new_inode, char* name, int parent_inode, int type) {
    if (new_inode == 0) {
        fprintf(stderr, "ERROR: insert_dir_entry: inode is not a not valid\n");
        exit(1);
    }

    int block_num;
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;

    // Get the last block nums in i_blocks array.
    int data_blocks = inode_table[parent_inode - 1].i_blocks / 2;
    if ( data_blocks > 12 ) {
        indirect_block_num = inode_table[parent_inode - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        indirect_idx = data_blocks - 13;
        memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        data_blocks--;
    } else {
        block_num = inode_table[parent_inode - 1].i_block[data_blocks  - 1];
    }

    struct ext2_dir_entry *base_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num);
    struct ext2_dir_entry *next; 
    struct ext2_dir_entry *new_entry;
    int len = strlen(name);
    int rec_len;

    // This allows insert_dir_entry to put new dir in the new data block if the first block
    // does not have enough space.
    if (data_blocks == 1) {
        rec_len = base_entry->rec_len;
    } else {
        rec_len = 0;
    }

    // Search it recusively
    while (rec_len != EXT2_BLOCK_SIZE) {
        next = (struct ext2_dir_entry *)((char *)base_entry + rec_len);
        
        // Last dir_entry
        if (rec_len + next->rec_len == EXT2_BLOCK_SIZE) {
            int actual_size = actual_rec_len(next->name_len);
            int offset = rec_len + actual_size;
            
            // Check if there is enoguh space for the new dir entry
            // Create the dir entry in a new block if not.
            int new_rec_len = actual_rec_len(len);
            int avail_space = EXT2_BLOCK_SIZE - offset;
            if (new_rec_len <= avail_space) {
                new_entry = (struct ext2_dir_entry *)((char *)base_entry + offset);
                new_entry->rec_len = avail_space;
                next->rec_len = actual_size;
            } else {
                block_num = find_next_available(block_bitmap, BLOCK_BITMAP_SIZE);
                new_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num); 
                new_entry->rec_len = EXT2_BLOCK_SIZE;
                inode_table[parent_inode-1].i_blocks += 2;
                int num_blocks = inode_table[parent_inode-1].i_blocks;
                inode_table[parent_inode-1].i_block[num_blocks / 2 - 1] = block_num;
                inode_table[parent_inode-1].i_size += EXT2_BLOCK_SIZE;
            }
            new_entry->inode = new_inode;
            new_entry->name_len = len;
            new_entry->file_type = 0;
            new_entry->file_type |= type;
            memcpy(new_entry->name, name, len);
            return;
        }
        rec_len += next->rec_len;
    }
}

// Remove a file/link dir entry in the parent_inode dir entry.
// It will search for the entry with the same name and inode number.
void remove_dir_entry(int inode, char* name,int parent_inode) {
    if (inode == 0) {
        fprintf(stderr, "ERROR: remove_dir_entry: inode is not a not valid\n");
        exit(1);
    }
    int len = strlen(name);
    int block_num;
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;

    // Get the maximum number of blocks
    int data_blocks = inode_table[parent_inode - 1].i_blocks / 2;
    if ( inode_table[parent_inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[parent_inode - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }

    for (int i = 0; i < data_blocks ; i++) {
        if (i < 12) {
            block_num = inode_table[parent_inode - 1].i_block[i];
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        }

        struct ext2_dir_entry *base_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num);
        if (base_entry->inode == inode && \
         (strncmp(name, base_entry->name, len) == 0)) {   // next is the target dir entry, remove it
            base_entry->inode = 0;
            inode_table[inode - 1].i_links_count--;
            if (inode_table[inode - 1].i_links_count == 0) {   
                // If this is the last link
                // Remove the inode from the filesystem altogether
                cleanup_inode(inode);
            }
            return;
        }

        struct ext2_dir_entry *curr;
        struct ext2_dir_entry *next; 
        int rec_len = 0;

        while (rec_len != EXT2_BLOCK_SIZE) {
            curr = (struct ext2_dir_entry *)((char *)base_entry + rec_len);
            next = (struct ext2_dir_entry *)((char *)base_entry + rec_len + curr->rec_len);
            if (next->inode == inode && \
                (strncmp(name, next->name, len) == 0)) {   
                // next is the target dir entry, remove it
                curr->rec_len += next->rec_len;
                inode_table[inode - 1].i_links_count--;
                if (inode_table[inode - 1].i_links_count == 0) {   
                    // If this is the last link
                    // Remove the inode from the filesystem altogether
                    cleanup_inode(inode);
                }
                return;
            }
            rec_len += curr->rec_len;
        }
    }
}

// cleanup_inode removes inode from the filesystem.
// It releases all the data blocks that it has claimed and also the
// inode number
// Set the dtime for deletion as well.
void cleanup_inode(int inode) {
    if (inode == 0) {
        fprintf(stderr, "ERROR: cleanup: inode is not a not valid\n");
        exit(ENOENT);
    }
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;
    int block_num;
    int data_blocks = inode_table[inode - 1].i_blocks / 2;
    if ( inode_table[inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[inode - 1].i_block[12];
        unset_bit(block_bitmap, indirect_block_num, BLOCK_BITMAP_SIZE);
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }
    for (int i = 0; i < data_blocks; i++) {
        if (i < 12) {
            unset_bit(block_bitmap, inode_table[inode - 1].i_block[i], BLOCK_BITMAP_SIZE);
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
            unset_bit(block_bitmap, block_num, BLOCK_BITMAP_SIZE);
        }
    }
    unset_bit(inode_bitmap, inode, INODE_BITMAP_SIZE);
    inode_table[inode - 1].i_dtime = time(NULL);
}

// For BONUS:
// Similar to remove_dir_entry, but this removes directory as well.
// remove_dir recursively removes all the file including directories in the dir_entry
// If the direcotry contains any file/link, it will call remove_dir_entry to remove it.
// and remove_dir for any subdirectories.
// Finally, removing the itself from the parent_inode dir entry
void remove_dir(int dir_inode, char* name, int parent_inode) {
    if (dir_inode == 0) {
        fprintf(stderr, "ERROR: remove_dir: inode is not a not valid\n");
        exit(ENOENT);
    }
    char buf[EXT2_NAME_LEN];
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;
    int block_num;
    struct ext2_dir_entry *base_entry;
    int data_blocks = inode_table[dir_inode - 1].i_blocks / 2;
    if ( inode_table[dir_inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[dir_inode - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }
    for (int i = 0; i < data_blocks; i++) {
        if (i < 12) {
            block_num = inode_table[dir_inode - 1].i_block[i];
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        }

        base_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num);

        // Set inode num to 0 for the first inode
        // Decrement the link count for this inode as well
        // the link count will not be zero since the dir_inode still exists
        if (base_entry->inode != 0 && (strncmp(base_entry->name, ".", 1) == 0)) {
            inode_table[base_entry->inode - 1].i_links_count--;
            base_entry->inode = 0;
        } else if (base_entry->inode != 0 && !IS_S_DIR(base_entry->inode)) {
            // case for the first file/link after the first data block
            strncpy(buf, base_entry->name, base_entry->name_len);
            buf[base_entry->name_len] = '\0';
            remove_dir_entry(base_entry->inode, buf, dir_inode);
            base_entry->inode = 0;
        } else if (base_entry->inode != 0 && IS_S_DIR(base_entry->inode)) {
            // case for the first directory after the first data block
            strncpy(buf, base_entry->name, base_entry->name_len);
            buf[base_entry->name_len] = '\0';
            remove_dir(base_entry->inode, buf, dir_inode);
        }

        struct ext2_dir_entry *next; 
        int rec_len = base_entry->rec_len;

        while (rec_len != EXT2_BLOCK_SIZE) {
            next = (struct ext2_dir_entry *)((char *)base_entry + rec_len);
            // If this is the hard link to the parent directory,
            // Just decrement its link count and leave it.
            if (next->inode != 0 && (strncmp(next->name, "..", 2) == 0 )) {
                inode_table[next->inode - 1].i_links_count--;
            } else if (next->inode != 0 && !IS_S_DIR(next->inode)) {
                strncpy(buf, next->name, next->name_len);
                buf[base_entry->name_len] = '\0';
                remove_dir_entry(next->inode, buf, dir_inode);
            } else if (next->inode != 0 && IS_S_DIR(next->inode)) {
                strncpy(buf, next->name, next->name_len);
                buf[base_entry->name_len] = '\0';
                remove_dir(next->inode, buf,dir_inode);
            }
            rec_len += next->rec_len;
        }
    }

    // Finally remove itself from the parent_inode dir entry
    // Also decrement the used dir count in the filesystem.
    remove_dir_entry(dir_inode, name,parent_inode);
    gd->bg_used_dirs_count--;
}

// check_restore checks whether a deleted file is recoverable in the dir entry
// If the file's orginally inode num has already been reallocated. It will return 0.
// If a deleted file's inode num is still unclaimed. It will return that specific inode num.
int check_restore(char* name, int parent_inode) {
    int block_num;
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;
    int data_blocks = inode_table[parent_inode - 1].i_blocks / 2;
    if ( inode_table[parent_inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[parent_inode - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }

    for (int i = 0; i < data_blocks ; i++) {
        if (i < 12) {
            block_num = inode_table[parent_inode - 1].i_block[i];
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        }

        struct ext2_dir_entry *base_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num);
        struct ext2_dir_entry *next;
        struct ext2_dir_entry *target;
        int rec_len = base_entry->rec_len;
        int name_len = strlen(name);
        int gap_len;
        while (rec_len != EXT2_BLOCK_SIZE) {
            next = (struct ext2_dir_entry *)((char *)base_entry + rec_len);
            int actual_size = actual_rec_len(next->name_len);
            // Gap exists
            if (actual_size != next->rec_len) {
                // Recursively search for the deleted file
                gap_len = actual_size;
                while (gap_len != next->rec_len) {
                    target = (struct ext2_dir_entry *)((char *)next + gap_len);
                    // Last dir_entry is reached
                    if (target->name_len == 0) {
                        return 0;
                    } else if (strncmp(name, target->name, name_len) == 0) {  // name matches
                        if (is_set(inode_bitmap, target->inode)) {   // But inode num is reallocated
                            fprintf(stderr, "ERROR: cannot restore file %s\n", name);
                            exit(ENOENT);
                        }
                        // Adjust the rec lens
                        target->rec_len = next->rec_len - gap_len;
                        next->rec_len = gap_len;
                        return target->inode;
                    }
                    gap_len += actual_rec_len(target->name_len);
                }
            }
            rec_len += next->rec_len;
        }
    }

    return 0;
}

// restore_dir_entry restores the inode num returned by check_restore
// It will also check if any of its data blocks has been reallocated to
// a different file as well
// Set dtime to 0 and increment its link count
void restore_dir_entry(int inode, char* name, int parent_inode) {
    if (inode == 0) {
        fprintf(stderr, "ERROR: restore_dir_entry: inode is not a not valid\n");
        exit(1);
    }

    set_bit(inode_bitmap, inode, 4);
    inode_table[inode - 1].i_links_count++;

    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;
    int block_num;
    int data_blocks = inode_table[inode - 1].i_blocks / 2;
    if ( inode_table[inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[inode - 1].i_block[12];
        if (is_set(block_bitmap, indirect_block_num)) {
            fprintf(stderr, "ERROR: cannot restore file %s\n", name);
            exit(ENOENT);
        }
        set_bit(block_bitmap, indirect_block_num, 16);
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }
    for (int i = 0; i < data_blocks; i++) {
        if (i < 12) {
            if (is_set(block_bitmap, inode_table[inode - 1].i_block[i])) {
                fprintf(stderr, "ERROR: cannot restore file %s\n", name);
                exit(ENOENT);
            }
            set_bit(block_bitmap, inode_table[inode - 1].i_block[i], 16);
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
            if (is_set(block_bitmap, block_num)) {
                fprintf(stderr, "ERROR: cannot restore file %s\n", name);
                exit(ENOENT);
            }
            set_bit(block_bitmap, block_num, 16);
        }
    }
    inode_table[inode - 1].i_dtime = 0;

}

// For BONUS:
// restore_dir will attempt to restore any file that is recoverable.
// For the first entry that are after the first block, they are not recoverable.
void restore_dir(int dir_inode, char* name, int parent_inode) {
    if (dir_inode == 0) {
        fprintf(stderr, "ERROR: restore_dir: inode is not a not valid\n");
        exit(1);
    }
    char buf[EXT2_NAME_LEN];
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;
    int block_num;
    struct ext2_dir_entry *base_entry;

    // Get the max block num
    int data_blocks = inode_table[dir_inode - 1].i_blocks / 2;
    if ( inode_table[dir_inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[dir_inode - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }

    // Restore all file in the data blocks
    for (int i = 0; i < data_blocks; i++) {
        if (i < 12) {
            block_num = inode_table[dir_inode - 1].i_block[i];
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        }

        // First entry in the first data block
        // Reset the inode num to dir_inode.
        // Readjust the rec len
        base_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num);
        if (base_entry->inode == 0 && (strncmp(base_entry->name, ".", 1) == 0)) {
            inode_table[dir_inode - 1].i_links_count = 1;
            base_entry->inode = dir_inode;
            base_entry->rec_len = actual_rec_len(base_entry->name_len);
        }

        struct ext2_dir_entry *next; 
        int rec_len = base_entry->rec_len;

        while (rec_len != EXT2_BLOCK_SIZE) {
            next = (struct ext2_dir_entry *)((char *)base_entry + rec_len);
            int actual_size = actual_rec_len(next->name_len);
            struct ext2_dir_entry *end = (struct ext2_dir_entry *)((char *)next + actual_size);
            // Check if next is the last dir entry in the data block
            if (end->name_len != 0) {
                next->rec_len = actual_rec_len(next->name_len);
            }

            // For any file/link, restore_dir call restore_dir_entry to restore them.
            // For any subdirecotry, call restore_dir instead.
            if (next->inode != 0 && (strncmp(next->name, "..", 2) == 0 )) {
                inode_table[next->inode - 1].i_links_count++;         
            } else if (next->inode != 0 && !IS_S_DIR(next->inode)) {
                strncpy(buf, next->name, next->name_len);
                buf[base_entry->name_len] = '\0';
                restore_dir_entry(next->inode, buf, dir_inode);
            } else if (next->inode != 0 && IS_S_DIR(next->inode)) {
                strncpy(buf, next->name, next->name_len);
                buf[base_entry->name_len] = '\0';
                restore_dir(next->inode, buf,dir_inode);
            }
            rec_len += next->rec_len;
        }
    }

    // Finally, restore itself in the parent_inode dir entry
    // Increment the used dir count for the filesystem.
    restore_dir_entry(dir_inode, name,parent_inode);
    gd->bg_used_dirs_count++;

}

// Function for checker.
// It will check for all the existing inodes in the inode table.
// Recursively checking every file's type to see if they match
// their type in the inode table.
void examine_dir_inode(int dir_inode) {
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;
    int block_num;
    struct ext2_dir_entry *base_entry;
    int data_blocks = inode_table[dir_inode - 1].i_blocks / 2;
    if ( inode_table[dir_inode - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[dir_inode - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }
    for (int i = 0; i < data_blocks; i++) {
        if (i < 12) {
            block_num = inode_table[dir_inode - 1].i_block[i];
        } else {
            indirect_idx = i - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        }

        base_entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * block_num);
        if (IS_S_DIR(base_entry->inode) && !IS_FT_DIR(base_entry->file_type))  {
            fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d]\n",
            base_entry->inode);
            base_entry->file_type = 0;
            base_entry->file_type |= EXT2_FT_DIR;
            total_fixes++;
        } else if (IS_S_FILE(base_entry->inode) && !IS_FT_FILE(base_entry->file_type))  {
            fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d]\n",
            base_entry->inode);
            base_entry->file_type = 0;
            base_entry->file_type |= EXT2_FT_REG_FILE;
            total_fixes++;
        } else if (IS_S_LINK(base_entry->inode) && !IS_FT_LINK(base_entry->file_type))  {
            fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d]\n",
            base_entry->inode);
            base_entry->file_type = 0;
            base_entry->file_type |= EXT2_FT_SYMLINK;
            total_fixes++;
        }

        struct ext2_dir_entry *next; 
        int rec_len = base_entry->rec_len;

        while (rec_len != EXT2_BLOCK_SIZE) {
            next = (struct ext2_dir_entry *)((char *)base_entry + rec_len);
            if (IS_S_DIR(next->inode) && !IS_FT_DIR(next->file_type))  {
                fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d]\n", next->inode);
                next->file_type = 0;
                next->file_type |= EXT2_FT_DIR;
                total_fixes++;
            } else if (IS_S_FILE(next->inode) && !IS_FT_FILE(next->file_type))  {
                fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d]\n",next->inode);
                next->file_type = 0;
                next->file_type |= EXT2_FT_REG_FILE;
                total_fixes++;
            } else if (IS_S_LINK(next->inode) && !IS_FT_LINK(next->file_type))  {
                fprintf(stderr, "Fixed: Entry type vs inode mismatch: inode [%d]\n",
                next->inode);
                next->file_type = 0;
                next->file_type |= EXT2_FT_SYMLINK;
                total_fixes++;
            }
            rec_len += next->rec_len;
        }
    }
}
