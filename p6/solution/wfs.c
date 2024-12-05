#define FUSE_USE_VERSION 26 

#include <sys/types.h>
#include "wfs.h"
#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>  
#include <inttypes.h>


#define min(a, b) ((a) < (b) ? (a) : (b))

// Function prototypes
int wfs_init(size_t num_inodes, size_t num_data_blocks, void *memory_start);
static int wfs_getattr(const char *path, struct stat *stbuf);
static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
static int wfs_mknod(const char *path, mode_t mode, dev_t dev);
static int wfs_mkdir(const char *path, mode_t mode);
static int wfs_unlink(const char *path);
static int wfs_rmdir(const char *path);

// Map functions to fuse_operations
static struct fuse_operations wfs_oper = {
    .getattr = wfs_getattr,
    .readdir = wfs_readdir,
    .read = wfs_read,
    .write = wfs_write,
    .mknod = wfs_mknod,
    .mkdir = wfs_mkdir,
    .unlink = wfs_unlink,
    .rmdir = wfs_rmdir
};

// Global variables
char *disk_image_path;
int global_fd;
void *mapped_memory;
struct wfs_sb sb;
struct wfs_inode *inodes;
char *inode_bitmap;
char *data_bitmap;
char *data_blocks; // Pointer to the data blocks section
struct raid_info raid;

// Function prototypes
struct wfs_inode *find_inode_by_path(const char *path);
int allocate_inode();
int allocate_block();
static int add_directory_entry(struct wfs_inode *parent_inode, int new_inode_num, const char *new_entry_name);
static int remove_directory_entry(struct wfs_inode *parent_inode, int inode_num, const char *entry_name);
static void free_block(int block_num);
static void free_inode(int inode_num);

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Usage: %s <disk_path> [FUSE options] <mount_point>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Store the disk image path in a global variable
    disk_image_path = argv[1];
    global_fd = open(disk_image_path, O_RDWR);
    if (global_fd == -1)
    {
        perror("Failed to open disk image");
        exit(EXIT_FAILURE);
    }

    // Get file status to determine file size
    struct stat file_stat;
    if (fstat(global_fd, &file_stat) == -1)
    {
        perror("fstat");
        exit(EXIT_FAILURE);
    }

    // Map the entire file into memory
    mapped_memory = mmap(NULL, file_stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, global_fd, 0);
    if (mapped_memory == MAP_FAILED)
    {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    // Close the file descriptor
    close(global_fd);

    // Set superblock pointer
    memcpy(&sb, mapped_memory, sizeof(struct wfs_sb));  // Use memcpy for safety
    inode_bitmap = (char *)mapped_memory + sb.i_bitmap_ptr;
    data_bitmap = (char *)mapped_memory + sb.d_bitmap_ptr;
    inodes = (struct wfs_inode *)((char *)mapped_memory + sb.i_blocks_ptr);
    data_blocks = (char *)mapped_memory + sb.d_blocks_ptr; // Initialize pointer to data blocks

    // Read RAID info after the superblock
    if (pread(global_fd, &raid, sizeof(struct raid_info), sizeof(struct wfs_sb)) != sizeof(struct raid_info))
    {
        perror("Failed to read RAID information");
        exit(EXIT_FAILURE);
    }

    // Ensure RAID mode is valid
    if ((raid.raid_mode == RAID0 && raid.num_disks < 1) ||
        ((raid.raid_mode == RAID1 || raid.raid_mode == RAID1V) && raid.num_disks < 2))
    {
        fprintf(stderr, "Invalid RAID configuration: RAID Mode %" PRIu64 ", Number of Disks %" PRIu64 "\n",
                raid.raid_mode, raid.num_disks);
        exit(EXIT_FAILURE);
    }

    // Mark root inode as used
    inode_bitmap[0] |= 0x01;

    // Pass the modified argv and argc to fuse_main
    argv++;
    argc--;

    // Call fuse_main with modified arguments
    int fuse_ret = fuse_main(argc, argv, &wfs_oper, NULL);

    // Unmap the memory
    if (munmap(mapped_memory, file_stat.st_size) == -1)
    {
        perror("munmap");
        exit(EXIT_FAILURE);
    }

    return fuse_ret;
}

struct wfs_inode *find_inode_by_path(const char *path)
{
    if (strcmp(path, "/") == 0)
    {
        return inodes; // Return root inode directly
    }

    struct wfs_inode *current_inode = inodes;
    char *path_copy = strdup(path);
    if (!path_copy)
    {
        perror("strdup failed");
        return NULL;
    }

    char *token = strtok(path_copy, "/");
    while (token != NULL)
    {
        if (!S_ISDIR(current_inode->mode))
        {
            fprintf(stderr, "Not a directory\n");
            free(path_copy);
            return NULL;
        }

        bool found = false;
        // Check direct blocks
        for (int i = 0; i < D_BLOCK; i++)
        {
            if (current_inode->blocks[i] != 0)
            {
                struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + current_inode->blocks[i]);
                for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
                {
                    if (strcmp(dentries[j].name, token) == 0)
                    {
                        current_inode = &inodes[dentries[j].num];
                        found = true;
                        break;
                    }
                }
            }
            if (found)
                break;
        }

        // If not found in direct blocks, check indirect block
        if (!found && current_inode->blocks[IND_BLOCK] != 0)
        {
            off_t *indirect_blocks = (off_t *)((char *)mapped_memory + current_inode->blocks[IND_BLOCK]);
            for (int k = 0; k < BLOCK_SIZE / sizeof(off_t) && indirect_blocks[k] != 0; k++)
            {
                struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + indirect_blocks[k]);
                for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
                {
                    if (strcmp(dentries[j].name, token) == 0)
                    {
                        current_inode = &inodes[dentries[j].num];
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
        }

        if (!found)
        {
            free(path_copy);
            return NULL;
        }

        token = strtok(NULL, "/");
    }

    free(path_copy);
    return current_inode; // Return the inode found at the end of the path
}

static int wfs_getattr(const char *path, struct stat *stbuf)
{
    memset(stbuf, 0, sizeof(struct stat));

    // Find the inode for the given path
    struct wfs_inode *inode = find_inode_by_path(path);
    if (!inode)
    {
        return -ENOENT;
    }

    // Set the appropriate fields in stbuf from the inode
    stbuf->st_mode = inode->mode;
    stbuf->st_nlink = inode->nlinks;
    stbuf->st_size = inode->size;
    stbuf->st_uid = inode->uid;
    stbuf->st_gid = inode->gid;
    stbuf->st_atime = inode->atim;
    stbuf->st_mtime = inode->mtim;
    stbuf->st_ctime = inode->ctim;

    stbuf->st_blocks = (inode->size + 511) / 512; // Number of 512-byte blocks

    return 0;
}

static int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct wfs_inode *inode = find_inode_by_path(path);
    if (!inode || !S_ISDIR(inode->mode))
    {
        return -ENOENT; // Inode not found or not a directory
    }

    // Add "." and ".."
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    // Read from direct blocks first
    for (int i = 0; i < D_BLOCK && inode->blocks[i] != 0; i++)
    {
        struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
        {
            if (dentries[j].num != 0) // Valid entry
            {
                if (filler(buf, dentries[j].name, NULL, 0) != 0)
                    return 0; // Buffer full or other filler-related error
            }
        }
    }

    // Handle indirect block
    if (inode->blocks[IND_BLOCK] != 0)
    {
        off_t *indirect_blocks = (off_t *)((char *)mapped_memory + inode->blocks[IND_BLOCK]);
        for (int i = 0; i < BLOCK_SIZE / sizeof(off_t) && indirect_blocks[i] != 0; i++)
        {
            struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + indirect_blocks[i]);
            for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
            {
                if (dentries[j].num != 0) // Valid entry
                {
                    if (filler(buf, dentries[j].name, NULL, 0) != 0)
                        return 0; // Buffer full or other filler-related error
                }
            }
        }
    }

    return 0;
}

static int wfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    struct wfs_inode *inode = find_inode_by_path(path);
    if (!inode) {
        return -ENOENT;
    }

    if (offset >= inode->size) {
        return 0; // Nothing to read
    }

    size_t bytes_to_read = min(size, inode->size - offset);
    size_t bytes_read = 0;
    size_t block_index = offset / BLOCK_SIZE;
    size_t block_offset = offset % BLOCK_SIZE;

    while (bytes_read < bytes_to_read) {
        // RAID 1v: Read all copies
        off_t current_block_ptrs[raid.num_disks];
        char block_data[raid.num_disks][BLOCK_SIZE];
        int valid_copies = 0;

        // Fetch the block pointers
        for (int disk = 0; disk < raid.num_disks; disk++) {
            if (block_index < D_BLOCK) {
                current_block_ptrs[disk] = inode->blocks[block_index];
            } else if (inode->blocks[IND_BLOCK] != 0) {
                off_t *indirect_blocks = (off_t *)((char *)mapped_memory + inode->blocks[IND_BLOCK]);
                current_block_ptrs[disk] = indirect_blocks[block_index - D_BLOCK];
            } else {
                current_block_ptrs[disk] = 0; // Block not initialized
            }

            if (current_block_ptrs[disk] != 0) {
                char *disk_block_data = (char *)mapped_memory + current_block_ptrs[disk];
                memcpy(block_data[disk], disk_block_data, BLOCK_SIZE);
                valid_copies++;
            }
        }

        // Verify and decide the majority block for RAID 1v
        int majority_index = 0; // Default to disk 0 if tie
        if (raid.raid_mode == RAID1V) {
            // Initialize match_count to zero
            int match_count[raid.num_disks];
            memset(match_count, 0, sizeof(match_count));

            for (int i = 0; i < valid_copies; i++) {
                for (int j = 0; j < valid_copies; j++) {
                    if (memcmp(block_data[i], block_data[j], BLOCK_SIZE) == 0) {
                        match_count[i]++;
                    }
                }
            }

            // Find the majority block or tie-break using index
            int max_matches = 0;
            for (int i = 0; i < valid_copies; i++) {
                if (match_count[i] > max_matches) {
                    max_matches = match_count[i];
                    majority_index = i;
                } else if (match_count[i] == max_matches && i < majority_index) {
                    majority_index = i;
                }
            }
        }

        // Copy data from the selected block
        size_t bytes_from_block = min(BLOCK_SIZE - block_offset, bytes_to_read - bytes_read);
        memcpy(buf + bytes_read, block_data[majority_index] + block_offset, bytes_from_block);

        bytes_read += bytes_from_block;
        block_index++;
        block_offset = 0;
    }

    return bytes_read;
}



int allocate_block()
{
    // Access the data bitmap directly from the global variable
    char *bitmap = data_bitmap;

    // Iterate over the bitmap to find a free block
    for (size_t i = 0; i < sb.num_data_blocks; i++)
    {
        size_t byte_index = i / 8;
        size_t bit_index = i % 8;

        // Check if the current block is free
        if (!(bitmap[byte_index] & (1 << bit_index)))
        {
            // Mark the block as used
            bitmap[byte_index] |= (1 << bit_index);

            // Return the offset from the beginning of the data blocks section
            return sb.d_blocks_ptr + i * BLOCK_SIZE;
        }
    }

    // Return -1 if no free blocks are available
    return -1;
}

int initialize_indirect_block(struct wfs_inode *inode)
{
    int indirect_block_ptr = allocate_block();
    if (indirect_block_ptr == -1)
        return -ENOSPC;

    inode->blocks[IND_BLOCK] = indirect_block_ptr;
    memset((char *)mapped_memory + indirect_block_ptr, 0, BLOCK_SIZE); // Initialize block

    return 0;
}

int allocate_inode()
{
    char *bitmap = inode_bitmap;
    for (size_t i = 1; i < sb.num_inodes; i++) // Start from 1 to skip root inode
    {
        size_t byte_index = i / 8;
        size_t bit_index = i % 8;

        if (!(bitmap[byte_index] & (1 << bit_index)))
        {
            bitmap[byte_index] |= (1 << bit_index);

            struct wfs_inode *new_inode = &inodes[i];
            memset(new_inode, 0, sizeof(struct wfs_inode)); // Zero out the new inode
            new_inode->num = i;
            new_inode->nlinks = 1;
            new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);

            return i;
        }
    }
    return -1; // No free inodes available
}

static int wfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    struct wfs_inode *inode = find_inode_by_path(path);
    if (!inode)
        return -ENOENT;

    if (!S_ISREG(inode->mode))
        return -EISDIR;

    off_t end_offset = offset + size;
    if (end_offset > inode->size)
    {
        inode->size = end_offset;
        inode->mtim = time(NULL); // Update modification time
    }

    size_t bytes_written = 0;
    while (bytes_written < size)
    {
        size_t block_index = (offset + bytes_written) / BLOCK_SIZE;
        size_t block_offset = (offset + bytes_written) % BLOCK_SIZE;
        size_t bytes_to_write = min(BLOCK_SIZE - block_offset, size - bytes_written);

        for (int disk = 0; disk < raid.num_disks; disk++) {
            if (block_index < D_BLOCK) {
                if (inode->blocks[block_index] == 0) {
                    inode->blocks[block_index] = allocate_block();
                    if (inode->blocks[block_index] == -1) {
                        return -ENOSPC;
                    }
                }
                char *block_data = (char *)mapped_memory + inode->blocks[block_index];
                memcpy(block_data + block_offset, buf + bytes_written, bytes_to_write);
            } else {
                if (inode->blocks[IND_BLOCK] == 0) {
                    if (initialize_indirect_block(inode) != 0) {
                        return -ENOSPC;
                    }
                }
                off_t *indirect_blocks = (off_t *)((char *)mapped_memory + inode->blocks[IND_BLOCK]);
                int indirect_index = block_index - D_BLOCK;
                if (indirect_blocks[indirect_index] == 0) {
                    indirect_blocks[indirect_index] = allocate_block();
                    if (indirect_blocks[indirect_index] == -1) {
                        return -ENOSPC;
                    }
                }
                char *block_data = (char *)mapped_memory + indirect_blocks[indirect_index];
                memcpy(block_data + block_offset, buf + bytes_written, bytes_to_write);
            }
        }
        bytes_written += bytes_to_write;
    }

    return bytes_written;
}

static int add_directory_entry(struct wfs_inode *parent_inode, int new_inode_num, const char *new_entry_name)
{
    // Handle direct blocks first
    for (int i = 0; i < D_BLOCK; i++)
    {
        if (parent_inode->blocks[i] == 0)
        {
            parent_inode->blocks[i] = allocate_block();
            if (parent_inode->blocks[i] == -1)
            {
                return -ENOSPC;
            }
            memset((char *)mapped_memory + parent_inode->blocks[i], 0, BLOCK_SIZE);
        }
        struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + parent_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
        {
            if (dentries[j].num == 0)
            {
                strncpy(dentries[j].name, new_entry_name, MAX_NAME - 1);
                dentries[j].name[MAX_NAME - 1] = '\0'; // Ensure null termination
                dentries[j].num = new_inode_num;
                return 0;
            }
        }
    }

    // Handle indirect blocks
    if (parent_inode->blocks[IND_BLOCK] == 0)
    {
        parent_inode->blocks[IND_BLOCK] = allocate_block();
        if (parent_inode->blocks[IND_BLOCK] == -1)
        {
            return -ENOSPC;
        }
        memset((char *)mapped_memory + parent_inode->blocks[IND_BLOCK], 0, BLOCK_SIZE);
    }

    off_t *indirect_blocks = (off_t *)((char *)mapped_memory + parent_inode->blocks[IND_BLOCK]);
    for (int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++)
    {
        if (indirect_blocks[i] == 0)
        {
            indirect_blocks[i] = allocate_block();
            if (indirect_blocks[i] == -1)
            {
                return -ENOSPC;
            }
            memset((char *)mapped_memory + indirect_blocks[i], 0, BLOCK_SIZE);
        }
        struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + indirect_blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
        {
            if (dentries[j].num == 0)
            {
                strncpy(dentries[j].name, new_entry_name, MAX_NAME - 1);
                dentries[j].name[MAX_NAME - 1] = '\0'; // Ensure null termination
                dentries[j].num = new_inode_num;
                return 0;
            }
        }
    }

    return -ENOSPC; // No space left
}

static int wfs_mknod(const char *path, mode_t mode, dev_t dev)
{
    if (find_inode_by_path(path))
    {
        return -EEXIST; // File already exists
    }

    // Find parent directory
    char *parent_path = strdup(path);
    if (!parent_path)
    {
        return -ENOMEM;
    }

    char *base_name = strrchr(parent_path, '/');
    if (!base_name)
    {
        free(parent_path);
        return -ENOENT;
    }

    if (base_name == parent_path)
    {
        strcpy(parent_path, "/");
    }
    else
    {
        *base_name = '\0'; // Null-terminate parent path
    }
    base_name++;

    struct wfs_inode *parent_inode = find_inode_by_path(parent_path);
    free(parent_path);

    if (!parent_inode)
        return -ENOENT;

    if (!S_ISDIR(parent_inode->mode))
        return -ENOTDIR;

    // Allocate new inode
    int new_inode_num = allocate_inode();
    if (new_inode_num == -1)
        return -ENOSPC;

    struct wfs_inode *new_inode = &inodes[new_inode_num];

    new_inode->num = new_inode_num;
    new_inode->mode = mode;
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    new_inode->size = 0;
    new_inode->nlinks = 1;
    new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
    memset(new_inode->blocks, 0, sizeof(new_inode->blocks));

    if (add_directory_entry(parent_inode, new_inode_num, base_name) != 0)
    {
        free_inode(new_inode_num);
        return -EIO;
    }

    return 0;
}

static int wfs_mkdir(const char *path, mode_t mode)
{
    if (find_inode_by_path(path))
        return -EEXIST;

    // Find parent directory
    char *parent_path = strdup(path);
    if (!parent_path)
    {
        return -ENOMEM;
    }

    char *base_name = strrchr(parent_path, '/');
    if (!base_name)
    {
        free(parent_path);
        return -ENOENT;
    }

    if (base_name == parent_path)
    {
        strcpy(parent_path, "/");
    }
    else
    {
        *base_name = '\0';
    }
    base_name++;

    struct wfs_inode *parent_inode = find_inode_by_path(parent_path);
    free(parent_path);

    if (!parent_inode)
        return -ENOENT;

    if (!S_ISDIR(parent_inode->mode))
        return -ENOTDIR;

    // Allocate new inode
    int new_inode_num = allocate_inode();
    if (new_inode_num == -1)
        return -ENOSPC;

    struct wfs_inode *new_inode = &inodes[new_inode_num];

    new_inode->num = new_inode_num;
    new_inode->mode = S_IFDIR | mode;
    new_inode->uid = getuid();
    new_inode->gid = getgid();
    new_inode->size = 0;
    new_inode->nlinks = 2; // For '.' and '..'
    new_inode->atim = new_inode->mtim = new_inode->ctim = time(NULL);
    memset(new_inode->blocks, 0, sizeof(new_inode->blocks));

    // Initialize directory entries '.' and '..'
    int block_ptr = allocate_block();
    if (block_ptr == -1)
    {
        free_inode(new_inode_num);
        return -ENOSPC;
    }
    new_inode->blocks[0] = block_ptr;
    struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + block_ptr);
    memset(dentries, 0, BLOCK_SIZE);

    // '.' entry
    strncpy(dentries[0].name, ".", MAX_NAME - 1);
    dentries[0].num = new_inode_num;

    // '..' entry
    strncpy(dentries[1].name, "..", MAX_NAME - 1);
    dentries[1].num = parent_inode->num;

    if (add_directory_entry(parent_inode, new_inode_num, base_name) != 0)
    {
        free_block(block_ptr);
        free_inode(new_inode_num);
        return -EIO;
    }

    parent_inode->nlinks++; // Increment link count

    return 0;
}

static int remove_directory_entry(struct wfs_inode *parent_inode, int inode_num, const char *entry_name)
{
    // Check direct blocks first
    for (int i = 0; i < D_BLOCK; i++)
    {
        if (parent_inode->blocks[i] == 0)
            continue;

        struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + parent_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
        {
            if (dentries[j].num == inode_num && strcmp(dentries[j].name, entry_name) == 0)
            {
                dentries[j].num = 0;
                memset(dentries[j].name, 0, MAX_NAME);
                return 0;
            }
        }
    }

    // Handle indirect block
    if (parent_inode->blocks[IND_BLOCK] != 0)
    {
        off_t *indirect_blocks = (off_t *)((char *)mapped_memory + parent_inode->blocks[IND_BLOCK]);
        for (int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++)
        {
            if (indirect_blocks[i] == 0)
                continue;

            struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + indirect_blocks[i]);
            for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
            {
                if (dentries[j].num == inode_num && strcmp(dentries[j].name, entry_name) == 0)
                {
                    dentries[j].num = 0;
                    memset(dentries[j].name, 0, MAX_NAME);
                    return 0;
                }
            }
        }
    }

    return -ENOENT; // Entry not found
}

static void free_inode(int inode_num)
{
    if (inode_num < 0 || inode_num >= sb.num_inodes)
    {
        return; // Out of bounds
    }
    size_t byte_index = inode_num / 8;
    size_t bit_index = inode_num % 8;
    inode_bitmap[byte_index] &= ~(1 << bit_index); // Clear the bit
}

static void free_block(int block_ptr)
{
    int block_num = (block_ptr - sb.d_blocks_ptr) / BLOCK_SIZE;
    if (block_num < 0 || block_num >= sb.num_data_blocks)
    {
        return; // Out of bounds
    }
    size_t byte_index = block_num / 8;
    size_t bit_index = block_num % 8;
    data_bitmap[byte_index] &= ~(1 << bit_index); // Clear the bit

    memset((char *)mapped_memory + block_ptr, 0, BLOCK_SIZE);
}

static int wfs_unlink(const char *path)
{
    struct wfs_inode *inode = find_inode_by_path(path);
    if (!inode)
    {
        return -ENOENT;
    }

    if (S_ISDIR(inode->mode))
    {
        return -EISDIR;
    }

    // Find parent directory
    char *parent_path = strdup(path);
    if (!parent_path)
        return -ENOMEM;

    char *base_name = strrchr(parent_path, '/');
    if (!base_name)
    {
        free(parent_path);
        return -ENOENT;
    }

    if (base_name == parent_path)
    {
        strcpy(parent_path, "/");
    }
    else
    {
        *base_name = '\0';
    }
    base_name++;

    struct wfs_inode *parent_inode = find_inode_by_path(parent_path);
    free(parent_path);

    if (!parent_inode)
        return -ENOENT;

    if (remove_directory_entry(parent_inode, inode->num, base_name) != 0)
    {
        return -EIO;
    }

    // Decrease link count and free inode if necessary
    inode->nlinks--;
    if (inode->nlinks == 0)
    {
        for (int i = 0; i < N_BLOCKS; i++)
        {
            if (inode->blocks[i] != 0)
            {
                free_block(inode->blocks[i]);
            }
        }
        free_inode(inode->num);
    }

    return 0;
}

static int wfs_rmdir(const char *path)
{
    struct wfs_inode *dir_inode = find_inode_by_path(path);
    if (!dir_inode)
    {
        return -ENOENT;
    }

    if (!S_ISDIR(dir_inode->mode))
    {
        return -ENOTDIR;
    }

    // Check if directory is empty
    bool is_empty = true;
    for (int i = 0; i < N_BLOCKS && dir_inode->blocks[i] != 0; i++)
    {
        struct wfs_dentry *dentries = (struct wfs_dentry *)((char *)mapped_memory + dir_inode->blocks[i]);
        for (int j = 0; j < BLOCK_SIZE / sizeof(struct wfs_dentry); j++)
        {
            if (dentries[j].num != 0 && strcmp(dentries[j].name, ".") != 0 && strcmp(dentries[j].name, "..") != 0)
            {
                is_empty = false;
                break;
            }
        }
        if (!is_empty)
            break;
    }
    if (!is_empty)
    {
        return -ENOTEMPTY;
    }

    // Find parent directory
    char *parent_path = strdup(path);
    if (!parent_path)
        return -ENOMEM;

    char *base_name = strrchr(parent_path, '/');
    if (!base_name)
    {
        free(parent_path);
        return -ENOENT;
    }

    if (base_name == parent_path)
    {
        strcpy(parent_path, "/");
    }
    else
    {
        *base_name = '\0';
    }
    base_name++;

    struct wfs_inode *parent_inode = find_inode_by_path(parent_path);
    free(parent_path);

    if (!parent_inode)
        return -ENOENT;

    if (remove_directory_entry(parent_inode, dir_inode->num, base_name) != 0)
    {
        return -EIO;
    }

    // Decrease link counts and free inode
    parent_inode->nlinks--;
    dir_inode->nlinks = 0;

    for (int i = 0; i < N_BLOCKS; i++)
    {
        if (dir_inode->blocks[i] != 0)
        {
            free_block(dir_inode->blocks[i]);
            dir_inode->blocks[i] = 0;
        }
    }
    free_inode(dir_inode->num);

    return 0;
}
