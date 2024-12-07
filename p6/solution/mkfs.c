#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <time.h>
#include "wfs.h"

#define BLOCK_SIZE (512)
#define MAX_DISKS 10
#define ROUNDUP(num, factor) ((((num) + (factor) - 1) / (factor)) * (factor))

void parse_arguments(int argc, char *argv[], int *raid_mode, char *diskimg[], int *num_disks, int *num_inodes, int *num_data_blocks);
int initialize_disk(char *paths, int inodes, int blocks, int raid, int f_id, int disk_id);
int open_disk_image(char *paths);
int validate_disk_image(int fd, struct stat *st);
int initialize_superblock(struct wfs_sb *sb, int inodes, int blocks, size_t size, int f_id, int raid, int disk_id);
int write_superblock(int fd, struct wfs_sb *sb);
void initialize_root_inode(struct wfs_inode *inode);
int initialize_inode_bitmap(int fd, off_t bitmap_ptr);
int write_root_inode(int fd, off_t inode_ptr, struct wfs_inode *inode);

/**
 * This function creates a RAID-based filesystem by:
 * 1. Parsing command-line arguments to determine the RAID mode, disk paths, number of inodes, and data blocks.
 *    Example usage:
 *      Command: ./mkfs -r 1 -d disk1 -d disk2 -i 32 -b 200
 *      Parsed output:
 *        RAID Mode: RAID1
 *        Disks: [disk1, disk2]
 *        Number of Inodes: 32
 *        Number of Data Blocks: 224
 * 2. Generating a unique filesystem ID based on the current time.
 * 3. Initializing each disk in the file system with the help of `initialize_disk`.
 *    Inside `initialize_disk`:
 *      a. [X]Open the disk image file for writing.
 *      b. [X]Validate the disk image file's size.
 *      c. [X]Initialize the superblock with metadata for inodes, blocks, RAID mode, etc.
 *      d. [X]Write the superblock to the disk.
 *      e. [X]Initialize the root inode (e.g., root directory metadata).
 *      f. [X]Set up the inode bitmap to track allocated inodes.
 *      g. [X]Write the root inode to the disk.
 *      h. [X]Close the disk image file.
 *
 */
int main(int argc, char *argv[])
{
    int raid_mode = -1;                // RAID mode
    char *disk_paths[MAX_DISKS] = {0}; // disk paths
    int num_disks = 0;                 // number of disks
    int num_inodes = -1;               // number of inodes
    int num_data_blocks = -1;          // number of data blocks

    // parse command-line arguments
    parse_arguments(argc, argv, &raid_mode, disk_paths, &num_disks, &num_inodes, &num_data_blocks);
    // Debugging: Print parsed arguments
    // printf("Debugging: Parsed Arguments:\n");
    // printf("  RAID Mode: %d\n", raid_mode);
    // printf("  Number of Disks: %d\n", num_disks);
    // for (int i = 0; i < num_disks; i++) {
    //     printf("    Disk %d: %s\n", i + 1, disk_paths[i]);
    // }
    // printf("  Number of Inodes: %d\n", num_inodes);
    // printf("  Number of Data Blocks: %d\n", num_data_blocks);

    // base on current time
    int f_id = (int)time(NULL);

    // initialize each disk in the fs
    for (int i = 0; i < num_disks; i++)
    {
        // printf("initializing disk %d: %s\n", i + 1, disk_paths[i]);
        if (initialize_disk(disk_paths[i], num_inodes, num_data_blocks, raid_mode, f_id, i) != 0)
        {
            fprintf(stderr, "Error: Failed to initialize disk %s\n", disk_paths[i]);
            return -1;
        }
        // printf("disk %d: %s initialized successfully.\n", i + 1, disk_paths[i]);
    }

    printf("File system created successfully with ID: %d\n", f_id);
    return 0;
}

void parse_arguments(int argc, char *argv[], int *raid_mode, char *diskimg[], int *num_disks, int *num_inodes, int *num_data_blocks)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-r") == 0)
        {
            // parse RAID mode
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Option -r requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            if (strcmp(argv[++i], "0") == 0)
            {
                *raid_mode = RAID0;
            }
            else if (strcmp(argv[i], "1") == 0)
            {
                *raid_mode = RAID1;
            }
            else if (strcmp(argv[i], "1v") == 0)
            {
                *raid_mode = RAID1V;
            }
            else
            {
                fprintf(stderr, "Error: Invalid RAID mode. Use 0, 1, or 1v.\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(argv[i], "-d") == 0)
        {
            // parse disk paths
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Option -d requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            if (*num_disks >= MAX_DISKS)
            {
                fprintf(stderr, "Error: Too many disks specified. Max is %d.\n", MAX_DISKS);
                exit(EXIT_FAILURE);
            }
            diskimg[(*num_disks)++] = argv[++i];
        }
        else if (strcmp(argv[i], "-i") == 0)
        {
            // parse number of inodes
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Option -i requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            *num_inodes = atoi(argv[++i]);
            if (*num_inodes <= 0)
            {
                fprintf(stderr, "Error: Invalid number of inodes.\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(argv[i], "-b") == 0)
        {
            // parse number of data blocks
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Error: Option -b requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            *num_data_blocks = atoi(argv[++i]);
            if (*num_data_blocks <= 0)
            {
                fprintf(stderr, "Error: Invalid number of data blocks.\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            fprintf(stderr, "Error: Invalid argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s -r [0|1|1v] -d disk1 [-d disk2 ...] -i num_inodes -b num_data_blocks\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // validate required arguments
    if (*raid_mode == -1)
    {
        fprintf(stderr, "Error: RAID mode not specified. Use -r [0|1|1v].\n");
        exit(EXIT_FAILURE);
    }
    if (*num_disks == 0)
    {
        fprintf(stderr, "Error: No disks specified. Use -d disk1 [-d disk2 ...].\n");
        exit(EXIT_FAILURE);
    }
    if ((*raid_mode == RAID1 || *raid_mode == RAID1V) && *num_disks < 2)
    {
        fprintf(stderr, "Error: RAID 1 and RAID 1v require at least two disks.\n");
        exit(EXIT_FAILURE);
    }
    if (*num_inodes == -1)
    {
        fprintf(stderr, "Error: Number of inodes not specified. Use -i num_inodes.\n");
        exit(EXIT_FAILURE);
    }
    if (*num_data_blocks == -1)
    {
        fprintf(stderr, "Error: Number of data blocks not specified. Use -b num_data_blocks.\n");
        exit(EXIT_FAILURE);
    }
}

int initialize_disk(char *paths, int inodes, int blocks, int raid, int f_id, int disk_id)
{
    int fd;
    struct stat st;
    struct wfs_sb sb;
    struct wfs_inode inode;

    // a: open the disk image file
    // printf("START to open files.\n");
    fd = open_disk_image(paths);
    if (fd < 0)
        return -1;

    // b: validate the disk image file's size
    // printf("START to validate size.\n");
    if (validate_disk_image(fd, &st) < 0)
    {
        close(fd);
        return -1;
    }

    // c: initialize the superblock
    // printf("START to init sb.\n");
    if (initialize_superblock(&sb, inodes, blocks, st.st_size, f_id, raid, disk_id) < 0)
    {
        close(fd);
        return -1;
    }

    // d: write the superblock to the disk
    // printf("START to write sb.\n");
    if (write_superblock(fd, &sb) < 0)
    {
        close(fd);
        return -1;
    }

    // e: initialize the root inode
    // printf("START to init root inode.\n");
    initialize_root_inode(&inode);

    // f: set up the inode bitmap
    if (initialize_inode_bitmap(fd, sb.i_bitmap_ptr) < 0)
    {
        close(fd);
        return -1;
    }

    // g: write the root inode to the disk
    if (write_root_inode(fd, sb.i_blocks_ptr, &inode) < 0)
    {
        close(fd);
        return -1;
    }

    // h: close the disk image file
    close(fd);
    return 0;
}

int open_disk_image(char *paths)
{
    int fd = open(paths, O_RDWR, S_IRWXU);
    if (fd < 0)
    {
        perror("Error opening disk image file");
    }
    return fd;
}

int validate_disk_image(int fd, struct stat *st)
{
    if (fstat(fd, st) < 0)
    {
        perror("Error validating disk image file");
        return -1;
    }
    return 0;
}

int initialize_superblock(struct wfs_sb *sb, int inodes, int blocks, size_t size, int f_id, int raid, int disk_id)
{
    inodes = ROUNDUP(inodes, 32);
    blocks = ROUNDUP(blocks, 32);

    // Set up the superblock fields
    sb->num_inodes = inodes;
    sb->num_data_blocks = blocks;
    sb->i_bitmap_ptr = sizeof(struct wfs_sb);
    sb->d_bitmap_ptr = sb->i_bitmap_ptr + (inodes / 8);
    sb->i_blocks_ptr = sb->d_bitmap_ptr + (blocks / 8);
    if (sb->i_blocks_ptr % BLOCK_SIZE != 0)
    {
        sb->i_blocks_ptr = (sb->i_blocks_ptr / BLOCK_SIZE + 1) * BLOCK_SIZE;
    }
    sb->d_blocks_ptr = sb->i_blocks_ptr + (inodes * BLOCK_SIZE);
    sb->f_id = f_id;
    sb->raid = raid;
    sb->disk_id = disk_id;

    // ensure the superblock fits within the disk size
    size_t required_size = (inodes * BLOCK_SIZE) + (blocks * BLOCK_SIZE) + sb->i_blocks_ptr;
    if (required_size > size)
    {
        printf("Error: Too many blocks requested, superblock setup failed.\n");
        return -1;
    }

    printf("Superblock initialized: inodes=%d, blocks=%d, size=%zu\n", inodes, blocks, size);
    return 0;
}

int write_superblock(int fd, struct wfs_sb *sb)
{
    if (write(fd, sb, sizeof(struct wfs_sb)) < 0)
    {
        perror("Error writing superblock to disk");
        return -1;
    }
    return 0;
}

void initialize_root_inode(struct wfs_inode *inode)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);

    memset(inode, 0, sizeof(struct wfs_inode));
    inode->mode = S_IFDIR | S_IRUSR | S_IWUSR | S_IXUSR;
    inode->uid = getuid();
    inode->gid = getgid();
    inode->size = 0;
    inode->nlinks = 1;
    inode->atim = t.tv_sec;
    inode->mtim = t.tv_sec;
    inode->ctim = t.tv_sec;
}

int initialize_inode_bitmap(int fd, off_t bitmap_ptr)
{
    uint32_t bit = 0x1; // mark the first inode as used
    lseek(fd, bitmap_ptr, SEEK_SET);
    if (write(fd, &bit, sizeof(uint32_t)) < 0)
    {
        perror("Error setting up inode bitmap");
        return -1;
    }
    return 0;
}

int write_root_inode(int fd, off_t inode_ptr, struct wfs_inode *inode)
{
    lseek(fd, inode_ptr, SEEK_SET);
    if (write(fd, inode, sizeof(struct wfs_inode)) < 0)
    {
        perror("Error writing root inode to disk");
        return -1;
    }
    return 0;
}
