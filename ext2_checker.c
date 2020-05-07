#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <libgen.h>
#include <sys/stat.h>
#include <time.h>
#include "ext2.h"
#include "ext2_helper.h"

int main(int argc, char **argv) {
    if(argc != 2) {
        fprintf(stderr, "Usage: %s <image file name>\n", argv[0]);
        exit(1);
    }
    int fd = open(argv[1], O_RDWR);
    if (fd == -1) {
        perror("open");
        exit(1);
    }

    disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(disk == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    // Initalize the global variables
    sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    block_bitmap = disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE;
    inode_bitmap = disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE;
    inode_table = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);
    total_fixes = 0;

    /******************************************************************
	 * Checker
	 ******************************************************************/
    int free_inodes_count = 0;
    int free_blocks_count = 0;
    int i;
    for (i = 0; i < sb->s_inodes_count; i++) {
        if (!is_set(inode_bitmap, i + 1)) {
            free_inodes_count++;
        }
    }
    for (i = 0; i < sb->s_blocks_count; i++) {
        if (!is_set(block_bitmap, i + 1)) {
            free_blocks_count++;
        }
    }

    int offset;
    // Check the free inode/block counts for superblock and block group.
    if (sb->s_free_inodes_count != free_inodes_count) {
        offset = abs(sb->s_free_inodes_count - free_inodes_count);
        fprintf(stderr, "Fixed: superblock's free inodes counter was off by %d compared to the bitmap\n", offset);
        sb->s_free_inodes_count = free_inodes_count;
        total_fixes += offset;
    }
    if (sb->s_free_blocks_count != free_blocks_count) {
        offset = abs(sb->s_free_blocks_count - free_blocks_count);
        fprintf(stderr, "Fixed: superblock's free blocks counter was off by %d compared to the bitmap\n", offset);
        sb->s_free_blocks_count = free_blocks_count;
        total_fixes += offset;
    }
    if (gd->bg_free_inodes_count != free_inodes_count) {
        offset = abs(gd->bg_free_inodes_count - free_inodes_count);
        fprintf(stderr, "Fixed: block group's free inodes counter was off by %d compared to the bitmap\n", offset);
        gd->bg_free_inodes_count = free_inodes_count;
        total_fixes += offset;
    }
    if (gd->bg_free_blocks_count != free_blocks_count) {
        offset = abs(gd->bg_free_blocks_count  - free_blocks_count);
        fprintf(stderr, "Fixed: block group's free blocks counter was off by %d compared to the bitmap\n", offset);
        gd->bg_free_blocks_count = free_blocks_count;
        total_fixes += offset;
    }

     // Check if each file, directory or symlink is allocated in the inode bitmap
    if (!is_set(inode_bitmap, EXT2_ROOT_INO)) {
        fprintf(stderr, "Fixed: inode [%d] not marked as in-use\n", EXT2_ROOT_INO);
        set_bit(inode_bitmap, EXT2_ROOT_INO, 4);
        total_fixes++;
    }

    for (i = 10 ; i < sb->s_inodes_count; i++) {
        if (inode_table[i].i_links_count > 0 && !is_set(inode_bitmap, i + 1)) {
            fprintf(stderr, "Fixed: inode [%d] not marked as in-use\n", i + 1);
            set_bit(inode_bitmap, i + 1, 4);
            total_fixes++;
        }  
    }

    // Check for data block allocation for each file, directory and symlink
    int block_num;
    int indirect_block_num;
    unsigned char* indirect_block;
    int indirect_idx;
    int data_blocks = inode_table[EXT2_ROOT_INO - 1].i_blocks / 2;
    if ( inode_table[EXT2_ROOT_INO - 1].i_blocks / 2 > 12 ) {
        indirect_block_num = inode_table[EXT2_ROOT_INO - 1].i_block[12];
        indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
        data_blocks--;
    }

    int D = 0;
    int j;
    for (j = 0; j < data_blocks ; j++) {
        if (j < 12) {
            block_num = inode_table[EXT2_ROOT_INO - 1].i_block[j];
        } else {
            indirect_idx = j - 12;
            memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
        }
        if (!is_set(block_bitmap, block_num)) {
            set_bit(block_bitmap, block_num, 16);
            total_fixes++;
            D++;
        }
    }
    if (D != 0) {
        fprintf(stderr, "Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", D, EXT2_ROOT_INO);
    }
    
    for (i = 10; i < sb->s_inodes_count; i++) {
        data_blocks = inode_table[i].i_blocks / 2;
        if ( inode_table[i].i_blocks / 2 > 12 ) {
            indirect_block_num = inode_table[i].i_block[12];
            indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
            data_blocks--;
        }

        D = 0;
        for (j = 0; j < data_blocks ; j++) {
            if (j < 12) {
                block_num = inode_table[i].i_block[j];
            } else {
                indirect_idx = j - 12;
                memcpy(&block_num, indirect_block + (4 * indirect_idx), sizeof(int));
            }
            if (!is_set(block_bitmap, block_num)) {
                set_bit(block_bitmap, block_num, 16);
                total_fixes++;
                D++;
            }
        }
        if (D != 0) {
        fprintf(stderr, "Fixed: %d in-use data blocks not marked in data bitmap for inode: [%d]\n", D, i + 1);
        }
    }

    // Check file_type for each file, directory or symlink
    examine_dir_inode(EXT2_ROOT_INO);

    for (i = EXT2_GOOD_OLD_FIRST_INO ; i < sb->s_inodes_count; i++) {
        if (inode_table[i].i_links_count > 0 && IS_S_DIR(i + 1)) {
            examine_dir_inode(i + 1);
        }
    } 

    // Check inode's i_dtime for each file, directory or symlink
    if (inode_table[EXT2_ROOT_INO - 1].i_dtime != 0) {
        fprintf(stderr, "Fixed: valid inode marked for deletion: [%d]\n", EXT2_ROOT_INO);
        inode_table[EXT2_ROOT_INO - 1].i_dtime = 0;
        total_fixes++;
    }

    for (i = 10; i < sb->s_inodes_count; i++) {
        if (inode_table[i].i_links_count > 0 && inode_table[i].i_dtime != 0) {
            fprintf(stderr, "Fixed: valid inode marked for deletion: [%d]\n", i + 1);
            inode_table[i].i_dtime = 0;
            total_fixes++;
        }
    }

    if (total_fixes == 0) {
        printf("No file system inconsistencies detected!\n");
    } else if (total_fixes > 0) {
        printf("%d file system inconsistencies repaired!\n", total_fixes);
    }

    if (close(fd) == -1) {
        perror("close");
        exit(1);
    }

    int ret = munmap(disk, 128 * 1024);
    if (ret == -1) {
        perror("munmap");
        exit(1);
    }
    /******************************************************************
	 * End
	 ******************************************************************/

    return 0;
}
