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

#include "ext2.h"
#include "ext2_helper.h"

int main(int argc, char **argv) {
    
    if(argc <= 2) {
        fprintf(stderr, "Usage: %s <image file name> <absolute path to directory>\n", argv[0]);
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

    // Path validation
    char *path = argv[2];
    validate_path(path, ABS_PATH);
    // Target directory name
    char* dirname = basename(path);

    // inode_num returns the inode number of the last entry in the path if exists,
    // Otherwise, it should return 0.
    int prev_inode;
    int inode = inode_num(path, &prev_inode);

    if (inode != 0) {
        fprintf(stderr, "ERROR: file or directory %s already exists.\n", dirname);
        exit(EEXIST);
    }

    /******************************************************************
	 * Create inode, block, dir_entry
	******************************************************************/
    // Allocate a new block and inode num to the new directory
    int new_inode_num = find_next_available(inode_bitmap, INODE_BITMAP_SIZE);
    int new_block_num = find_next_available(block_bitmap, BLOCK_BITMAP_SIZE);

    inode_table[prev_inode - 1].i_links_count++;
    inode_table[new_inode_num - 1].i_mode = 0;
    inode_table[new_inode_num - 1].i_mode |= EXT2_S_IFDIR;
    inode_table[new_inode_num - 1].i_uid = 0;
    inode_table[new_inode_num - 1].i_size = EXT2_BLOCK_SIZE;
    inode_table[new_inode_num - 1].i_ctime = 0;
    inode_table[new_inode_num - 1].i_dtime = 0;
    inode_table[new_inode_num - 1].i_gid = 0;
    inode_table[new_inode_num - 1].i_links_count = 2;
    inode_table[new_inode_num - 1].i_blocks = 2;
    inode_table[new_inode_num - 1].osd1 = 0;
    inode_table[new_inode_num - 1].i_block[0] = new_block_num;
    inode_table[new_inode_num - 1].i_generation = 0;
    inode_table[new_inode_num - 1].i_file_acl = 0;
    inode_table[new_inode_num - 1].i_dir_acl = 0;
    inode_table[new_inode_num - 1].i_faddr = 0;

    // Insert it in the parent inode (prev_inode) and set the type to EXT2_FT_DIR
    insert_dir_entry(new_inode_num, dirname, prev_inode, EXT2_FT_DIR);

    // Create an "empty" directory enty for this direcotry 
    struct ext2_dir_entry *entry = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * new_block_num);
    entry->inode = new_inode_num;
    entry->rec_len = 12;
    entry->name_len = 1;
    entry->file_type = 0;
    entry->file_type |= EXT2_FT_DIR;
    memcpy(entry->name, ".", 1);
    struct ext2_dir_entry *next = (struct ext2_dir_entry *)((char *)entry + entry->rec_len);
    next->inode = prev_inode;
    next->rec_len = EXT2_BLOCK_SIZE - entry->rec_len;
    next->name_len = 2;
    next->file_type = 0;
    next->file_type |= EXT2_FT_DIR;
    memcpy(next->name, "..", 2);

    // Increment used dir count
    gd->bg_used_dirs_count++;

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