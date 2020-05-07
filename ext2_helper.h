#ifndef CSC369_EXT2_FS_HELPER
#define CSC369_EXT2_FS_HELPER

unsigned char *disk;
struct ext2_super_block *sb;
struct ext2_group_desc *gd;
unsigned char *block_bitmap;
unsigned char *inode_bitmap;
struct ext2_inode *inode_table;
int total_fixes;

#define IS_S_DIR(x)   (inode_table[x - 1].i_mode & EXT2_S_IFDIR)
#define IS_S_FILE(x)   (inode_table[x - 1].i_mode & EXT2_S_IFREG)
#define IS_S_LINK(x)   (inode_table[x - 1].i_mode & EXT2_S_IFLNK)
#define IS_FT_DIR(x)   (x == EXT2_FT_DIR)
#define IS_FT_FILE(x)   (x == EXT2_FT_REG_FILE)
#define IS_FT_LINK(x)   (x == EXT2_FT_SYMLINK)

#define ABS_PATH 1
#define REG_PATH 0

#define BLOCK_BITMAP_SIZE 16
#define INODE_BITMAP_SIZE 4

int find_next_available(unsigned char *bitmap, int size);
void set_bit(unsigned char* bitmap, int num, int size);
void unset_bit(unsigned char* bitmap, int num, int size);
int is_set(unsigned char* bitmap, int num);
int actual_rec_len(int name_len);
int check_exist(char* dir_name, int inode);
int inode_num(char* path, int* prev);
void validate_path(char* path, int flag);
void insert_dir_entry(int new_inode, char* name, int inode, int type);
void remove_dir_entry(int inode, char* name, int parent_inode);
void cleanup_inode(int inode);
void remove_dir(int dir_inode, char* name, int parent_inode);
int check_restore(char* name, int parent_inode);
void restore_dir_entry(int inode, char* name, int parent_inode);
void restore_dir(int dir_inode, char* name, int parent_inode);
void examine_dir_inode(int dir_inode);

#endif