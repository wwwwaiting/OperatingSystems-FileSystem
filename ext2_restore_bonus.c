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
    if(argc < 3) {
        fprintf(stderr, "Usage: %s <image file name> [OPTIONAL -r] <absolute path to file>\n", argv[0]);
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

    sb = (struct ext2_super_block *)(disk + EXT2_BLOCK_SIZE);
    gd = (struct ext2_group_desc *)(disk + 2 * EXT2_BLOCK_SIZE);
    block_bitmap = disk + gd->bg_block_bitmap * EXT2_BLOCK_SIZE;
    inode_bitmap = disk + gd->bg_inode_bitmap * EXT2_BLOCK_SIZE;
    inode_table = (struct ext2_inode *)(disk + gd->bg_inode_table * EXT2_BLOCK_SIZE);

    if (gd->bg_free_blocks_count == 0 || gd->bg_free_inodes_count == 0) {
        fprintf(stderr, "ERROR: Not enough space in the file system\n");
        exit(ENOSPC);
    }
    /******************************************************************
	 * Restore
	 ******************************************************************/
    if ( argc == 3 ) {
        char *path = argv[2];
        validate_path(path, 1);
        char* name = basename(path);

        int prev_inode;
        int inode = inode_num(path, &prev_inode);
        if (inode) {
            fprintf(stderr, "ERROR: file or directory %s already exists.\n", name);
            exit(EEXIST);
        }
        inode = check_restore(name, prev_inode);
        if (!inode) {
            fprintf(stderr, "ERROR: cannot restore file %s\n", name);
            exit(ENOENT);
        } else if (IS_S_DIR(inode)) {
            fprintf(stderr, "ERROR: cannot restore %s: Is a directory\n", name);
            exit(ENOENT);
        }
        restore_dir_entry(inode, name, prev_inode);
    } else if ( argc == 4 ) {
        if ( strcmp(argv[2], "-r") != 0 ) {
            fprintf(stderr, "Usage: %s <image file name> [OPTIONAL -r] <absolute path to file>\n", argv[0]);
            exit(1);
        }
        char *path = argv[3];
        validate_path(path, 1);
        char* name = basename(path);

        int prev_inode;
        int inode = inode_num(path, &prev_inode);
        if (inode) {
            fprintf(stderr, "ERROR: file or directory %s already exists.\n", name);
            exit(EEXIST);
        } else if (inode == EXT2_ROOT_INO || inode == 11) {
            fprintf(stderr, "ERROR: Cannot restore root direcotry\n");
            exit(ENOENT);
        }
        inode = check_restore(name, prev_inode);
        if (!inode) {
            printf("There\n");
            fprintf(stderr, "ERROR: cannot restore file %s\n", name);
            exit(ENOENT);
        }

        if (!IS_S_DIR(inode)) {
            restore_dir_entry(inode, name, prev_inode);
        } else {
            restore_dir(inode, name, prev_inode);
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
