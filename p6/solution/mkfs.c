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

// Function prototypes
void parse_arguments(int argc, char *argv[], int *raid_mode, char ***disk_paths, int *num_disks, int *num_inodes, int *num_data_blocks);
void calculate_sizes_and_offsets(int *num_inodes, int num_data_blocks, uint64_t *inode_bitmap_size, uint64_t *data_bitmap_size, uint64_t *i_bitmap_ptr, uint64_t *d_bitmap_ptr, uint64_t *i_blocks_ptr, uint64_t *d_blocks_ptr);
int *open_disk_files(char **disk_paths, int num_disks);
void validate_disks(int *fds, char **disk_paths, int num_disks, uint64_t required_size);
void initialize_superblock_and_raid_info(struct wfs_sb *sb, struct raid_info *raid_info, int raid_mode, int num_disks, int num_inodes, int num_data_blocks, uint64_t i_bitmap_ptr, uint64_t d_bitmap_ptr, uint64_t i_blocks_ptr, uint64_t d_blocks_ptr);
void write_superblock_and_raid_info(int *fds, int num_disks, struct wfs_sb *sb, struct raid_info *raid_info);
void initialize_bitmaps(int *fds, int num_disks, uint64_t i_bitmap_ptr, uint64_t d_bitmap_ptr, uint64_t inode_bitmap_size, uint64_t data_bitmap_size);
void initialize_root_inode(struct wfs_inode *root_inode);
void update_inode_bitmap(int *fds, int num_disks, uint64_t i_bitmap_ptr, uint64_t inode_bitmap_size);
void write_root_inode(int *fds, int num_disks, uint64_t i_blocks_ptr, struct wfs_inode *root_inode);
void cleanup(int *fds, int num_disks, char **disk_paths);

int main(int argc, char *argv[]) {
    char **disk_paths = NULL;
    int num_disks = 0;
    int raid_mode = -1;
    int num_inodes = -1;
    int num_data_blocks = -1;

    // Step 1: Parse command-line arguments
    parse_arguments(argc, argv, &raid_mode, &disk_paths, &num_disks, &num_inodes, &num_data_blocks);

    // Step 2: Round up inodes and data blocks to nearest multiple of 32
    uint64_t inode_bitmap_size = 0;
    uint64_t data_bitmap_size = 0;

    // Round up to the nearest multiple of 32
    num_inodes = ROUNDUP(num_inodes, 32);
    num_data_blocks = ROUNDUP(num_data_blocks, 32);

    // Step 3: Calculate sizes and offsets
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

    // Step 4: Open disk files
    int *fds = malloc(num_disks * sizeof(int));
    if (!fds)
    {
        perror("Memory allocation failed");
        free(disk_paths);
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

    // Step 5: Validate disk sizes
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

    validate_disks(fds, disk_paths, num_disks, required_size);

    // Step 5: Initialize superblock and RAID info
    struct wfs_sb sb;
    struct raid_info raid_info;
    initialize_superblock_and_raid_info(&sb, &raid_info, raid_mode, num_disks, num_inodes, num_data_blocks, i_bitmap_ptr, d_bitmap_ptr, i_blocks_ptr, d_blocks_ptr);

    // Step 6: Write superblock and RAID info
    write_superblock_and_raid_info(fds, num_disks, &sb, &raid_info);

    // Step 7: Initialize bitmaps
    initialize_bitmaps(fds, num_disks, i_bitmap_ptr, d_bitmap_ptr, inode_bitmap_size, data_bitmap_size);

    // Step 8: Initialize root inode
    struct wfs_inode root_inode;
    initialize_root_inode(&root_inode);

    // Step 9: Update inode bitmap
    update_inode_bitmap(fds, num_disks, i_bitmap_ptr, inode_bitmap_size);

    // Step 10: Write root inode
    write_root_inode(fds, num_disks, i_blocks_ptr, &root_inode);

    // Step 11: Cleanup
    cleanup(fds, num_disks, disk_paths);

    printf("Filesystem successfully created.\n");
    return 0;
}

void parse_arguments(int argc, char *argv[], int *raid_mode, char ***disk_paths, int *num_disks, int *num_inodes, int *num_data_blocks) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-r") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option -r requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            if (strcmp(argv[++i], "0") == 0) {
                *raid_mode = RAID0;
            } else if (strcmp(argv[i], "1") == 0) {
                *raid_mode = RAID1;
            } else if (strcmp(argv[i], "1v") == 0) {
                *raid_mode = RAID1V; // support for RAID 1v
            } else {
                fprintf(stderr, "Invalid RAID mode. Use 0, 1, or 1v.\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-d") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option -d requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            (*num_disks)++;
            *disk_paths = realloc(*disk_paths, (*num_disks) * sizeof(char *));
            if (!*disk_paths) {
                perror("Memory allocation failed");
                exit(EXIT_FAILURE);
            }
            (*disk_paths)[(*num_disks) - 1] = argv[++i];
        } else if (strcmp(argv[i], "-i") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option -i requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            *num_inodes = atoi(argv[++i]);
            if (*num_inodes <= 0) {
                fprintf(stderr, "Invalid number of inodes.\n");
                exit(EXIT_FAILURE);
            }
        } else if (strcmp(argv[i], "-b") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Option -b requires an argument.\n");
                exit(EXIT_FAILURE);
            }
            *num_data_blocks = atoi(argv[++i]);
            if (*num_data_blocks <= 0) {
                fprintf(stderr, "Invalid number of data blocks.\n");
                exit(EXIT_FAILURE);
            }
        } else {
            fprintf(stderr, "Invalid argument: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s -r [0|1|1v] -d disk1 [-d disk2 ...] -i num_inodes -b num_data_blocks\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Validate required arguments
    if (*raid_mode == -1) {
        fprintf(stderr, "RAID mode not specified. Use -r [0|1|1v].\n");
        exit(EXIT_FAILURE);
    }

    if (*num_disks == 0) {
        fprintf(stderr, "No disks specified. Use -d disk1 [-d disk2 ...].\n");
        exit(EXIT_FAILURE);
    }

    if ((*raid_mode == RAID1 || *raid_mode == RAID1V) && *num_disks < 2) {
        fprintf(stderr, "RAID 1 and RAID 1v require at least two disks.\n");
        exit(EXIT_FAILURE);
    }

    if (*num_inodes == -1) {
        fprintf(stderr, "Number of inodes not specified. Use -i num_inodes.\n");
        exit(EXIT_FAILURE);
    }

    if (*num_data_blocks == -1) {
        fprintf(stderr, "Number of data blocks not specified. Use -b num_data_blocks.\n");
        exit(EXIT_FAILURE);
    }

    // printf("Debug: Parsed arguments - num_inodes=%d, num_data_blocks=%d, raid_mode=%d, num_disks=%d\n",
    //        *num_inodes, *num_data_blocks, *raid_mode, *num_disks);
}

void calculate_sizes_and_offsets(int *num_inodes, int num_data_blocks, uint64_t *inode_bitmap_size, uint64_t *data_bitmap_size, uint64_t *i_bitmap_ptr, uint64_t *d_bitmap_ptr, uint64_t *i_blocks_ptr, uint64_t *d_blocks_ptr) {
    // Round up to the nearest multiple of 32
    *num_inodes = ROUNDUP(*num_inodes, 32);
    num_data_blocks = ROUNDUP(num_data_blocks, 32);

    // Calculate bitmap sizes (in bytes)
    *inode_bitmap_size = (*num_inodes + 7) / 8;
    *data_bitmap_size = (num_data_blocks + 7) / 8;

    // Calculate offsets
    uint64_t sb_size = sizeof(struct wfs_sb);
    uint64_t raid_info_size = sizeof(struct raid_info);
    uint64_t metadata_size = sb_size + raid_info_size;

    *i_bitmap_ptr = metadata_size;
    *d_bitmap_ptr = *i_bitmap_ptr + *inode_bitmap_size;

    // Ensure i_blocks_ptr is block-aligned
    *i_blocks_ptr = ROUNDUP(*d_bitmap_ptr + *data_bitmap_size, BLOCK_SIZE);
    uint64_t i_blocks_size = (*num_inodes) * BLOCK_SIZE;

    // Ensure d_blocks_ptr is block-aligned
    *d_blocks_ptr = *i_blocks_ptr + i_blocks_size; // Avoid extra ROUNDUP here

    // printf("Debug: Calculated sizes and offsets:\n");
    // printf("  num_inodes=%d, num_data_blocks=%d\n", *num_inodes, num_data_blocks);
    // printf("  inode_bitmap_size=%lu, data_bitmap_size=%lu\n", *inode_bitmap_size, *data_bitmap_size);
    // printf("  i_bitmap_ptr=%lu, d_bitmap_ptr=%lu\n", *i_bitmap_ptr, *d_bitmap_ptr);
    // printf("  i_blocks_ptr=%lu, d_blocks_ptr=%lu\n", *i_blocks_ptr, *d_blocks_ptr);
}

int *open_disk_files(char **disk_paths, int num_disks) {
    int *fds = malloc(num_disks * sizeof(int));
    if (!fds) {
        perror("Memory allocation failed");
        free(disk_paths);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_disks; i++) {
        fds[i] = open(disk_paths[i], O_RDWR | O_CREAT, 0644);
        if (fds[i] == -1) {
            perror("Failed to open disk image");
            // Close previously opened files
            for (int j = 0; j < i; j++) {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(EXIT_FAILURE);
        }
    }
    return fds;
}

void validate_disks(int *fds, char **disk_paths, int num_disks, uint64_t required_size)
{
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

        // printf("Debug: Disk %s size=%lu, required_size=%lu\n", disk_paths[i], (uint64_t)st.st_size, required_size);
        
        if ((uint64_t)st.st_size < required_size)
        {
            // fprintf(stderr, "Disk image %s is too small.\n", disk_paths[i]);
            //  Close files
            for (int j = 0; j <= i; j++)
            {
                close(fds[j]);
            }
            free(fds);
            free(disk_paths);
            exit(-1); // Exit with return code -1
        }
    }
}

void initialize_superblock_and_raid_info(struct wfs_sb *sb, struct raid_info *raid_info, int raid_mode, int num_disks, int num_inodes, int num_data_blocks, uint64_t i_bitmap_ptr, uint64_t d_bitmap_ptr, uint64_t i_blocks_ptr, uint64_t d_blocks_ptr) {
    sb->num_inodes = (uint64_t)num_inodes;
    sb->num_data_blocks = (uint64_t)num_data_blocks;
    sb->i_bitmap_ptr = i_bitmap_ptr;
    sb->d_bitmap_ptr = d_bitmap_ptr;
    sb->i_blocks_ptr = i_blocks_ptr;
    sb->d_blocks_ptr = d_blocks_ptr;
    sb->raid_mode = (uint64_t)raid_mode;
    sb->num_disks = (uint64_t)num_disks;

    raid_info->raid_mode = (uint64_t)raid_mode;
    raid_info->num_disks = (uint64_t)num_disks;

    // printf("Debug: Superblock initialization:\n");
    // printf("  num_inodes=%lu, num_data_blocks=%lu\n", sb->num_inodes, sb->num_data_blocks);
    // printf("  i_bitmap_ptr=%lu, d_bitmap_ptr=%lu, i_blocks_ptr=%lu, d_blocks_ptr=%lu\n", sb->i_bitmap_ptr, sb->d_bitmap_ptr, sb->i_blocks_ptr, sb->d_blocks_ptr);
    // printf("  num_disks=%lu\n", sb->num_disks);
}

void write_superblock_and_raid_info(int *fds, int num_disks, struct wfs_sb *sb, struct raid_info *raid_info) {
    for (int i = 0; i < num_disks; i++) {
        if (lseek(fds[i], 0, SEEK_SET) == -1) {
            perror("Failed to seek to superblock position");
            cleanup(fds, num_disks, NULL);
            exit(EXIT_FAILURE);
        }
        ssize_t written = write(fds[i], sb, sizeof(struct wfs_sb));
        if (written != (ssize_t)sizeof(struct wfs_sb)) {
            perror("Failed to write superblock");
            cleanup(fds, num_disks, NULL);
            exit(EXIT_FAILURE);
        }
        written = write(fds[i], raid_info, sizeof(struct raid_info));
        if (written != (ssize_t)sizeof(struct raid_info)) {
            perror("Failed to write RAID info");
            cleanup(fds, num_disks, NULL);
            exit(EXIT_FAILURE);
        }
        // printf("Debug: Writing superblock to disk %d\n", i);
        // printf("  num_inodes=%lu, num_data_blocks=%lu\n", sb->num_inodes, sb->num_data_blocks);
    }
}

void initialize_bitmaps(int *fds, int num_disks, uint64_t i_bitmap_ptr, uint64_t d_bitmap_ptr, uint64_t inode_bitmap_size, uint64_t data_bitmap_size) {
    char *inode_bitmap = calloc(1, inode_bitmap_size);
    char *data_bitmap = calloc(1, data_bitmap_size);

    if (!inode_bitmap || !data_bitmap) {
        perror("Memory allocation failed");
        free(inode_bitmap);
        free(data_bitmap);
        cleanup(fds, num_disks, NULL);
        exit(EXIT_FAILURE);
    }

    // Write zeroed inode bitmap
    for (int i = 0; i < num_disks; i++) {
        if (lseek(fds[i], i_bitmap_ptr, SEEK_SET) == -1 ||
            write(fds[i], inode_bitmap, inode_bitmap_size) != (ssize_t)inode_bitmap_size) {
            perror("Failed to write inode bitmap");
            free(inode_bitmap);
            free(data_bitmap);
            cleanup(fds, num_disks, NULL);
            exit(EXIT_FAILURE);
        }
    }

    // Write zeroed data bitmap
    for (int i = 0; i < num_disks; i++) {
        if (lseek(fds[i], d_bitmap_ptr, SEEK_SET) == -1 ||
            write(fds[i], data_bitmap, data_bitmap_size) != (ssize_t)data_bitmap_size) {
            perror("Failed to write data block bitmap");
            free(inode_bitmap);
            free(data_bitmap);
            cleanup(fds, num_disks, NULL);
            exit(EXIT_FAILURE);
        }
    }
    //printf("Debug: Initializing bitmaps - inode_bitmap_size=%lu, data_bitmap_size=%lu\n", inode_bitmap_size, data_bitmap_size);

    free(inode_bitmap);
    free(data_bitmap);
}

void initialize_root_inode(struct wfs_inode *root_inode) {
    memset(root_inode, 0, sizeof(struct wfs_inode));
    root_inode->num = 0;
    root_inode->mode = S_IFDIR | 0755; // Directory with rwxr-xr-x permissions
    root_inode->uid = getuid();        // Owner's user ID
    root_inode->gid = getgid();        // Owner's group ID
    root_inode->size = 0;              // Initially empty
    root_inode->nlinks = 2;            // Standard for directories

    time_t current_time = time(NULL);
    root_inode->atim = current_time;
    root_inode->mtim = current_time;
    root_inode->ctim = current_time;

    memset(root_inode->blocks, 0, sizeof(root_inode->blocks));

    // printf("Debug: Root inode initialization:\n");
    // printf("  num=%d, mode=%o, size=%lu, nlinks=%d\n", root_inode->num, root_inode->mode, root_inode->size, root_inode->nlinks);
}

void update_inode_bitmap(int *fds, int num_disks, uint64_t i_bitmap_ptr, uint64_t inode_bitmap_size) {
    char *inode_bitmap = calloc(1, inode_bitmap_size);
    if (!inode_bitmap) {
        perror("Memory allocation failed");
        cleanup(fds, num_disks, NULL);
        exit(EXIT_FAILURE);
    }
    inode_bitmap[0] |= 1 << 0; // Mark inode 0 as allocated

    // Write updated inode bitmap to all disk images
    for (int i = 0; i < num_disks; i++) {
        if (lseek(fds[i], i_bitmap_ptr, SEEK_SET) == -1 ||
            write(fds[i], inode_bitmap, inode_bitmap_size) != (ssize_t)inode_bitmap_size) {
            perror("Failed to update inode bitmap");
            free(inode_bitmap);
            cleanup(fds, num_disks, NULL);
            exit(EXIT_FAILURE);
        }
    }

    // printf("Debug: Updating inode bitmap - inode_bitmap_size=%lu\n", inode_bitmap_size);

    free(inode_bitmap);
}

void write_root_inode(int *fds, int num_disks, uint64_t i_blocks_ptr, struct wfs_inode *root_inode) {
    uint64_t root_inode_offset = i_blocks_ptr; // Inode 0 is at the start of inode blocks

    for (int i = 0; i < num_disks; i++) {
        if (lseek(fds[i], root_inode_offset, SEEK_SET) == -1 ||
            write(fds[i], root_inode, sizeof(struct wfs_inode)) != sizeof(struct wfs_inode)) {
            perror("Failed to write root inode");
            cleanup(fds, num_disks, NULL);
            exit(EXIT_FAILURE);
        }
    }
}

void cleanup(int *fds, int num_disks, char **disk_paths) {
    // Close file descriptors
    for (int i = 0; i < num_disks; i++) {
        if (fds[i] != -1) {
            close(fds[i]);
        }
    }
    free(fds);
    if (disk_paths) {
        free(disk_paths);
    }
}
