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
    if(argc < 4) {
        fprintf(stderr, 
        "Usage: %s <image file name> [OPTIONAL -s] <absolute path to src file> <absolute path to dest file>\n",
         argv[0]);
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

    if (argc == 4) {  // hard link
        char *source = argv[2];
        char *path = argv[3];
        validate_path(source, ABS_PATH);
        validate_path(path, ABS_PATH);
        char* source_filename = basename(source);
        char* target_filename = basename(path);

        // Source
        int s_prev_inode;
        int s_inode = inode_num(source, &s_prev_inode);
        // Target
        int t_prev_inode;
        int t_inode = inode_num(path, &t_prev_inode);
        if (!s_inode) {
            fprintf(stderr, "ERROR: source file %s does not exist.\n", source_filename);
            exit(ENOENT);
        } else if (IS_S_DIR(s_inode)) {
            fprintf(stderr, "ERROR: hard link not allowed for directory\n");
            exit(EISDIR);
        } else if (t_inode) {
            fprintf(stderr, "ERROR: link name %s already exists.\n", target_filename);
            exit(EISDIR);
        }
        /******************************************************************
	     * Link
	     ******************************************************************/

        // Create a link under the target parent inode dir enty
        insert_dir_entry(s_inode, target_filename, t_prev_inode, EXT2_FT_REG_FILE);
        inode_table[s_inode - 1].i_links_count++;
        /******************************************************************
	     * End
	     ******************************************************************/
    } else if (argc == 5 ) {   // soft-link
        if ( strcmp(argv[2], "-s") != 0 ) {
            fprintf(stderr, 
            "Usage: %s <image file name> [OPTIONAL -s] <absolute path to src file> <absolute path to dest file>\n", 
            argv[0]);
            exit(1);
        }
        char *source = argv[3];
        char *path = argv[4];
        validate_path(source, ABS_PATH);
        validate_path(path, ABS_PATH);
        char* source_filename = basename(source);
        char* target_filename = basename(path);

        // Source
        int s_prev_inode;
        int s_inode = inode_num(source, &s_prev_inode);
        // Target
        int t_prev_inode;
        int t_inode = inode_num(path, &t_prev_inode);
        if (!s_inode) {
            fprintf(stderr, "ERROR: source file %s does not exist.\n", source_filename);
            exit(ENOENT);
        } else if (t_inode) {
            fprintf(stderr, "ERROR: link name %s already exists.\n", target_filename);
            exit(EEXIST);
        }
        /******************************************************************
	    * Link
	     ******************************************************************/

        // Get new inode and block num for the soft link
        int new_inode_num = find_next_available(inode_bitmap, INODE_BITMAP_SIZE);
        int new_block_num = find_next_available(block_bitmap, BLOCK_BITMAP_SIZE);
        insert_dir_entry(new_inode_num, target_filename, t_prev_inode, EXT2_FT_SYMLINK);

        int file_size = strlen(source);
        // Create a new entry in the inode table
        inode_table[new_inode_num - 1].i_mode = 0;
        inode_table[new_inode_num - 1].i_mode |= EXT2_S_IFLNK;
        inode_table[new_inode_num - 1].i_uid = 0;
        inode_table[new_inode_num - 1].i_size = file_size;
        inode_table[new_inode_num - 1].i_ctime = 0;
        inode_table[new_inode_num - 1].i_dtime = 0;
        inode_table[new_inode_num - 1].i_gid = 0;
        inode_table[new_inode_num - 1].i_links_count = 1;
        // Path name cannot be longer than EXT2_NAME_LEN
        // 1 block is enough for the soft link
        inode_table[new_inode_num - 1].i_blocks = 2;
        inode_table[new_inode_num - 1].i_block[0] = new_block_num;
        inode_table[new_inode_num - 1].osd1 = 0;
        inode_table[new_inode_num - 1].i_generation = 0;
        inode_table[new_inode_num - 1].i_file_acl = 0;
        inode_table[new_inode_num - 1].i_dir_acl = 0;
        inode_table[new_inode_num - 1].i_faddr = 0;
        
        // Copy the source path name to the soft link's data block
        struct ext2_dir_entry *data = (struct ext2_dir_entry *)(disk + EXT2_BLOCK_SIZE * new_block_num);
        memcpy(data, source, file_size);

        /******************************************************************
	     * End
	     ******************************************************************/
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

    return 0;
}
