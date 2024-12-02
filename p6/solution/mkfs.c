#include <stdint.h>
#include <sys/types.h>
#include "wfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <inttypes.h>

#define BLOCK_SIZE (512)
#define ROUNDUP(num, factor) ((((num) + (factor) - 1) / (factor)) * (factor))

// Define constants for RAID modes
#define RAID0 0
#define RAID1 1

// Structure to store RAID information separately
struct __attribute__((packed)) raid_info {
    uint64_t raid_mode;
    uint64_t num_disks;
};

void parse_arguments(int argc, char *argv[], int *raid_mode, char ***disk_paths, int *num_disks, int *num_inodes, int *num_data_blocks);

int main(int argc, char *argv[])
{
    char **disk_paths = NULL;
    int num_disks = 0;
    int raid_mode = -1;
    int num_inodes = -1;
    int num_data_blocks = -1;

    // Step 1: Parse command-line arguments
    parse_arguments(argc, argv, &raid_mode, &disk_paths, &num_disks, &num_inodes, &num_data_blocks);

    uint64_t inode_bitmap_size = 0;
    uint64_t data_bitmap_size = 0;

    // Parse command-line arguments
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "-r") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Option -r requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            char *endptr;
            raid_mode = strtol(argv[++i], &endptr, 10);
            if (*endptr != '\0' || (raid_mode != RAID0 && raid_mode != RAID1))
            {
                //fprintf(stderr, "Invalid RAID mode. Use 0 for RAID 0 or 1 for RAID 1.\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(argv[i], "-d") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Option -d requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            num_disks++;
            disk_paths = realloc(disk_paths, num_disks * sizeof(char *));
            if (!disk_paths)
            {
                perror("Memory allocation failed");
                exit(EXIT_FAILURE);
            }
            disk_paths[num_disks - 1] = argv[++i];
        }
        else if (strcmp(argv[i], "-i") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Option -i requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            num_inodes = atoi(argv[++i]);
            if (num_inodes <= 0)
            {
                fprintf(stderr, "Invalid number of inodes.\n");
                exit(EXIT_FAILURE);
            }
        }
        else if (strcmp(argv[i], "-b") == 0)
        {
            if (i + 1 >= argc)
            {
                fprintf(stderr, "Option -b requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            num_data_blocks = atoi(argv[++i]);
            if (num_data_blocks <= 0)
            {
                fprintf(stderr, "Invalid number of data blocks.\n");
                exit(EXIT_FAILURE);
            }
        }
        else
        {
            fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s -r [0|1] -d disk1 [-d disk2 ...] -i num_inodes -b num_data_blocks\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Validate required arguments
    if (raid_mode == -1)
    {
        //fprintf(stderr, "RAID mode not specified. Use -r [0|1].\n");
        exit(EXIT_FAILURE);
    }

    if (num_disks == 0)
    {
        fprintf(stderr, "No disks specified. Use -d disk1 [-d disk2 ...].\n");
        exit(EXIT_FAILURE);
    }

    if (raid_mode == RAID1 && num_disks < 2)
    {
        fprintf(stderr, "RAID 1 requires at least two disks.\n");
        exit(EXIT_FAILURE);
    }

    if (num_inodes == -1)
    {
        fprintf(stderr, "Number of inodes not specified. Use -i num_inodes.\n");
        exit(EXIT_FAILURE);
    }

    if (num_data_blocks == -1)
    {
        fprintf(stderr, "Number of data blocks not specified. Use -b num_data_blocks.\n");
        exit(EXIT_FAILURE);
    }

    // Step 2: Round up inodes and data blocks to nearest multiple of 32
    // Round up to the nearest multiple of 32
    num_inodes = ROUNDUP(num_inodes, 32);
    num_data_blocks = ROUNDUP(num_data_blocks, 32);

    // Calculate bitmap sizes (in bytes)
    inode_bitmap_size = (num_inodes + 7) / 8;
    data_bitmap_size = (num_data_blocks + 7) / 8;

    // Calculate offsets
    uint64_t sb_size = sizeof(struct wfs_sb);
    uint64_t raid_info_size = sizeof(struct raid_info);
    uint64_t metadata_size = sb_size + raid_info_size;

    uint64_t i_bitmap_ptr = metadata_size;
    uint64_t d_bitmap_ptr = i_bitmap_ptr + inode_bitmap_size;

    // Ensure i_blocks_ptr is block-aligned
    uint64_t i_blocks_ptr = ROUNDUP(d_bitmap_ptr + data_bitmap_size, BLOCK_SIZE);
    uint64_t i_blocks_size = num_inodes * BLOCK_SIZE; // Each inode occupies 512 bytes

    // Ensure d_blocks_ptr is block-aligned
    uint64_t d_blocks_ptr = ROUNDUP(i_blocks_ptr + i_blocks_size, BLOCK_SIZE);
    uint64_t d_blocks_size = num_data_blocks * BLOCK_SIZE;

    // Open all disk files
    int *fds = malloc(num_disks * sizeof(int));
    if (!fds)
    {
        perror("Memory allocation failed");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_disks; i++)
    {
        fds[i] = open(disk_paths[i], O_RDWR | O_CREAT, 0644);
        if (fds[i] == -1)
        {
            perror("Failed to open disk image");
            // Close previously opened files
            for (int j = 0; j < i; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
    }

    // Calculate required disk size per disk
    uint64_t required_size;
    if (raid_mode == RAID0)
    {
        // In RAID 0, data is striped across disks
        required_size = d_blocks_ptr + ((d_blocks_size + num_disks - 1) / num_disks);
    }
    else // RAID1
    {
        // In RAID 1, data is mirrored on each disk
        required_size = d_blocks_ptr + d_blocks_size;
    }

    // Check if disk images are large enough
    for (int i = 0; i < num_disks; i++)
    {
        struct stat st;
        if (fstat(fds[i], &st) == -1)
        {
            perror("Failed to get disk image size");
            // Close files
            for (int j = 0; j <= i; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
        if ((uint64_t)st.st_size < required_size)
        {
            //fprintf(stderr, "Disk image %s is too small.\n", disk_paths[i]);
            // Close files
            for (int j = 0; j <= i; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(-1); // Exit with return code -1
        }
    }

    // Initialize the superblock
    struct wfs_sb sb;
    sb.num_inodes = (uint64_t)num_inodes;
    sb.num_data_blocks = (uint64_t)num_data_blocks;
    sb.i_bitmap_ptr = i_bitmap_ptr;
    sb.d_bitmap_ptr = d_bitmap_ptr;
    sb.i_blocks_ptr = i_blocks_ptr;
    sb.d_blocks_ptr = d_blocks_ptr;

    // Store RAID information
    struct raid_info raid_info;
    raid_info.raid_mode = (uint64_t)raid_mode;
    raid_info.num_disks = (uint64_t)num_disks;

    // Write the superblock and RAID info to all disk images
    for (int i = 0; i < num_disks; i++)
    {
        if (lseek(fds[i], 0, SEEK_SET) == -1)
        {
            perror("Failed to seek to superblock position");
            // Close files
            for (int j = 0; j < num_disks; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
        ssize_t written = write(fds[i], &sb, sizeof(struct wfs_sb));
        if (written != (ssize_t)sizeof(struct wfs_sb))
        {
            perror("Failed to write superblock");
            // Close files
            for (int j = 0; j < num_disks; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
        written = write(fds[i], &raid_info, sizeof(struct raid_info));
        if (written != (ssize_t)sizeof(struct raid_info))
        {
            perror("Failed to write RAID info");
            // Close files
            for (int j = 0; j < num_disks; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
    }

    // Allocate zero-filled buffers for bitmaps
    char *inode_bitmap = calloc(1, inode_bitmap_size);
    char *data_bitmap = calloc(1, data_bitmap_size);

    if (!inode_bitmap || !data_bitmap)
    {
        perror("Memory allocation failed");
        free(inode_bitmap);
        free(data_bitmap);
        for (int i = 0; i < num_disks; i++)
        {
            close(fds[i]);
        }
        free(fds);
        free(disk_paths);
        exit(EXIT_FAILURE);
    }

    // Zero out inode bitmap
    for (int i = 0; i < num_disks; i++)
    {
        if (lseek(fds[i], i_bitmap_ptr, SEEK_SET) == -1 ||
            write(fds[i], inode_bitmap, inode_bitmap_size) != (ssize_t)inode_bitmap_size)
        {
            perror("Failed to write inode bitmap");
            free(inode_bitmap);
            free(data_bitmap);
            for (int j = 0; j < num_disks; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
    }

    // Zero out data block bitmap
    for (int i = 0; i < num_disks; i++)
    {
        if (lseek(fds[i], d_bitmap_ptr, SEEK_SET) == -1 ||
            write(fds[i], data_bitmap, data_bitmap_size) != (ssize_t)data_bitmap_size)
        {
            perror("Failed to write data block bitmap");
            free(inode_bitmap);
            free(data_bitmap);
            for (int j = 0; j < num_disks; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
    }

    free(inode_bitmap);
    free(data_bitmap);

    // Initialize root inode
    struct wfs_inode root_inode = {0};
    root_inode.num = 0;
    root_inode.mode = S_IFDIR | 0755; // Directory with rwxr-xr-x permissions
    root_inode.uid = getuid();        // Owner's user ID
    root_inode.gid = getgid();        // Owner's group ID
    root_inode.size = 0;              // Initially empty
    root_inode.nlinks = 2;            // Standard for directories

    time_t current_time = time(NULL);
    root_inode.atim = current_time;
    root_inode.mtim = current_time;
    root_inode.ctim = current_time;

    memset(root_inode.blocks, 0, sizeof(root_inode.blocks));

    // Update inode bitmap to mark root inode as allocated
    inode_bitmap_size = (num_inodes + 7) / 8;
    inode_bitmap = calloc(1, inode_bitmap_size);
    if (!inode_bitmap)
    {
        perror("Memory allocation failed");
        for (int i = 0; i < num_disks; i++)
        {
            close(fds[i]);
        }
        free(fds);
        free(disk_paths);
        exit(EXIT_FAILURE);
    }
    inode_bitmap[0] |= 1 << 0; // Mark inode 0 as allocated

    // Write updated inode bitmap to all disk images
    for (int i = 0; i < num_disks; i++)
    {
        if (lseek(fds[i], i_bitmap_ptr, SEEK_SET) == -1 ||
            write(fds[i], inode_bitmap, inode_bitmap_size) != (ssize_t)inode_bitmap_size)
        {
            perror("Failed to update inode bitmap");
            free(inode_bitmap);
            for (int j = 0; j < num_disks; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
    }

    free(inode_bitmap);

    // Write root inode to all disk images
    uint64_t root_inode_offset = i_blocks_ptr; // Inode 0 is at the start of inode blocks

    for (int i = 0; i < num_disks; i++)
    {
        if (lseek(fds[i], root_inode_offset, SEEK_SET) == -1 ||
            write(fds[i], &root_inode, sizeof(root_inode)) != sizeof(root_inode))
        {
            perror("Failed to write root inode");
            for (int j = 0; j < num_disks; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
    }

    // Close file descriptors
    for (int i = 0; i < num_disks; i++)
    {
        close(fds[i]);
    }
    free(fds);
    free(disk_paths);

    return 0;
}


