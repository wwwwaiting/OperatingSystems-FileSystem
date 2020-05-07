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
#include "ext2.h"
#include "ext2_helper.h"

int main(int argc, char **argv) {
    
    if(argc <= 3) {
        fprintf(stderr, "Usage: %s <image file name> <native path to source file> <absolute path to directory>\n", argv[0]);
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

    char *source = argv[2];
    if( access( source, F_OK ) == -1 ) {
        fprintf(stderr, "ERROR: source file %s does not exist.\n", source);
        exit(ENOENT);
    }
    char *source_filename = basename(source);
    char *path = argv[3];
    char *target_filename = basename(path);
    validate_path(source, REG_PATH);
    validate_path(path, ABS_PATH);
    int prev_inode;
    int last = inode_num(path, &prev_inode);
    char* curr;
    int location;

    if (last && IS_S_DIR(last)) {
        int already_exist = check_exist(source_filename, last);
        if (already_exist) {
            fprintf(stderr, "ERROR: file or directory %s already exists.\n", source_filename);
            exit(EEXIST);
        }
        curr = source_filename;
        location = last;
    } else if (last && IS_S_FILE(last)) {
        fprintf(stderr, "ERROR: file or directory %s already exists.\n", source_filename);
        exit(EEXIST);
    } else if (!last && IS_S_DIR(prev_inode)) {
        curr = target_filename;
        location = prev_inode;
    }
        
    /******************************************************************
	 * Copy the source file to dest
	 ******************************************************************/
            
    FILE* fp = fopen(argv[2], "r");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: fopen.\n");
        exit(EEXIST);
    }

    // Get the file size
    struct stat st;
    stat(argv[2], &st);
    unsigned int file_size = st.st_size;
    
    // See if the filesystem has enough space for the source file
    int block_required;
    if (file_size % EXT2_BLOCK_SIZE == 0) {
        block_required = file_size / EXT2_BLOCK_SIZE;
    } else {
        block_required = (file_size + EXT2_BLOCK_SIZE - (file_size % EXT2_BLOCK_SIZE)) / EXT2_BLOCK_SIZE;
    }
            
    if (block_required > gd->bg_free_blocks_count) {
        fprintf(stderr, "ERROR: Not enough space in the file system\n");
        exit(ENOENT);
    }
    
    // Find a new inode num for this file
    int new_inode_num = find_next_available(inode_bitmap, INODE_BITMAP_SIZE);
    inode_table[new_inode_num - 1].i_mode = 0;
    inode_table[new_inode_num - 1].i_mode |= EXT2_S_IFREG;
    inode_table[new_inode_num - 1].i_uid = 0;
    inode_table[new_inode_num - 1].i_size = file_size;
    inode_table[new_inode_num - 1].i_ctime = 0;
    inode_table[new_inode_num - 1].i_dtime = 0;
    inode_table[new_inode_num - 1].i_gid = 0;
    inode_table[new_inode_num - 1].i_links_count = 1;
    // If block_required is greater than 11, it means that indirect block is needed.
    if (block_required >= 12) {
        inode_table[new_inode_num - 1].i_blocks = (block_required + 1) * 2;
    } else {
        inode_table[new_inode_num - 1].i_blocks = block_required * 2;
    }
    inode_table[new_inode_num - 1].osd1 = 0;
    inode_table[new_inode_num - 1].i_generation = 0;
    inode_table[new_inode_num - 1].i_file_acl = 0;
    inode_table[new_inode_num - 1].i_dir_acl = 0;
    inode_table[new_inode_num - 1].i_faddr = 0;

    insert_dir_entry(new_inode_num, curr, location, EXT2_FT_REG_FILE);
    int new_block_num;
    int indirect_block_num;
    int indirect_idx;
    struct ext2_dir_entry *data;
    unsigned char* indirect_block;

    // Copy the file data to the filesystem block by block.
    for (int i = 0; i < block_required; i++) {
        if ( i < 12) {
            new_block_num = find_next_available(block_bitmap, BLOCK_BITMAP_SIZE);
            inode_table[new_inode_num - 1].i_block[i] = new_block_num;
        } else if ( i == 12) {
            indirect_block_num = find_next_available(block_bitmap, BLOCK_BITMAP_SIZE);
            inode_table[new_inode_num - 1].i_block[i] = indirect_block_num;
            new_block_num = find_next_available(block_bitmap, BLOCK_BITMAP_SIZE);
            indirect_block = disk + EXT2_BLOCK_SIZE * indirect_block_num;
            memcpy(indirect_block, &new_block_num, sizeof(int));
        } else {
            indirect_idx = i - 12;
            new_block_num = find_next_available(block_bitmap, BLOCK_BITMAP_SIZE);
            memcpy(indirect_block + (4 * indirect_idx), &new_block_num, sizeof(int));
        }

        data = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * new_block_num);
        if ((file_size % EXT2_BLOCK_SIZE) != 0 && i == block_required - 1) {
            fread(data, file_size % EXT2_BLOCK_SIZE, 1, fp);
        } else {
            fread(data, EXT2_BLOCK_SIZE, 1, fp);
        }
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
