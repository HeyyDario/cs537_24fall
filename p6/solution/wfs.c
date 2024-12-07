#define FUSE_USE_VERSION 26

#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <libgen.h>
#include <stdlib.h>
#include <fuse.h>
#include <assert.h>
#include <string.h>
#include "wfs.h"

#define MAX_DISKS 10

// global variables
void *mapped_memory[MAX_DISKS]; // memory-mapped regions for each disk image.
int disk_order[MAX_DISKS]; // order of disk id
int raid; // RAID mode
int num_disks; // number of disks in fs
int err_rc; // rc of error

//  ============= functions to set up main =============
void setup_mmap_and_check_superblocks(int *fds, struct stat *file_stat);
void reorder_disks();
int check_root_inodes();
void cleanup(int *fds, struct stat *file_stat, char **fuse_argv);
//  ============= functions to set up sys calls =============
int wfs_mknod(const char *path, mode_t mode, dev_t dev, int disk);
int WFS_MKNOD(const char *path, mode_t mode, dev_t dev); // WRAPPER FUNCTION 
int wfs_mkdir(const char *path, mode_t mode, int disk);
int WFS_MKDIR(const char *path, mode_t mode); // WRAPPER FUNCTION
int wfs_getattr(const char *path, struct stat *statbuf);
int wfs_read(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk);
int wfs_read_r1v(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi);
int WFS_READ(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi);
int wfs_write(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk);
int WFS_WRITE(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi);
int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int wfs_unlink(const char *path, int disk);
int WFS_UNLINK(const char *path);
int wfs_rmdir(const char *path);
int find_inode(struct wfs_inode *enclosing, char *path, struct wfs_inode **inode, int disk);
int find_inode_by_path(char *path, struct wfs_inode **inode, int disk);
int add_directory_entry(struct wfs_inode *parent, int num, char *name, int disk);
void initialize_inode(struct wfs_inode *inode, mode_t mode);
int remove_directory_entry(struct wfs_inode *inode, int inum, int disk);
char *calculate_block_offset(struct wfs_inode *inode, off_t offset, int alloc, int disk);
struct wfs_inode* allocate_inode(int disk);
off_t allocate_data_block(int disk);
struct wfs_inode* get_inode_by_number(int num, int disk);
void free_bitmap(uint32_t position, uint32_t *bitmap);
void free_inode(struct wfs_inode* inode, int disk);
void free_block(off_t blk, int disk);

static struct fuse_operations wfs_oper = {
    .getattr = wfs_getattr,
    .mknod = WFS_MKNOD,
    .mkdir = WFS_MKDIR,
    .unlink = WFS_UNLINK,
    .rmdir = wfs_rmdir,
    .read = WFS_READ,
    .write = WFS_WRITE,
    .readdir = wfs_readdir,
};

int main(int argc, char *argv[])
{
    //printf("STARTING main() in wfs.c.\n");
    // Step 1: identify the number of disk images
    num_disks = 0;
    while (num_disks < argc - 1 && argv[num_disks + 1][0] != '-') {
        num_disks++;
    }
    if (num_disks == 0) {
        fprintf(stderr, "Error: No disk images specified.\n");
        exit(EXIT_FAILURE);
    }
    if (num_disks < 2)
    {
        fprintf(stderr, "Error: Not enough disks.\n");
        exit(EXIT_FAILURE);
    }

    // Step 2: open and validate all disk images
    int fds[MAX_DISKS];
    struct stat file_stat;
    for (int i = 0; i < num_disks; i++)
    {
        char *disk_image_path = argv[i + 1];
        // open the file
        fds[i] = open(disk_image_path, O_RDWR, 0666);
        if (fds[i] == -1)
        {
            perror("Failed to open disk image");
            exit(EXIT_FAILURE);
        }

        // stat so we know how large the mmap needs to be
        if (fstat(fds[i], &file_stat) == -1)
        {
            perror("stat");
            exit(EXIT_FAILURE);
        }
    }

    // Step 3: setup mmap and check superblocks
    setup_mmap_and_check_superblocks(fds, &file_stat);

    // Step 4: reorder disks
    reorder_disks();

    // Step 5: check for the root inode
    if (check_root_inodes() != 0) {
        cleanup(fds, &file_stat, NULL);
        return EXIT_FAILURE;
    }

    // Step 6: parse argv and argc as required
    char **fuse_argv = malloc((argc - num_disks) * sizeof(char *));
    if (!fuse_argv) {
        perror("Failed to allocate memory for FUSE arguments");
        exit(EXIT_FAILURE);
    }

    fuse_argv[0] = argv[0]; // Preserve the program name
    
    for (int i = num_disks + 1; i < argc; i++) {
        fuse_argv[i - num_disks] = argv[i];
    }
    int fuse_argc = argc - num_disks;

    // Debugging: Print FUSE arguments
    // printf("FUSE arguments:\n");
    // for (int i = 0; i < fuse_argc; i++) {
    //     printf("fuse_argv[%d] = %s\n", i, fuse_argv[i]);
    // }
    // printf("Number of args passed into fuse_main: %d. \n", fuse_argc);

    // Step 7: Call FUSE
    //printf("Start to init FUSE: \n");
    int fuse_ret = fuse_main(fuse_argc, fuse_argv, &wfs_oper, NULL);
    //printf("Middle.\n");
    if (fuse_ret != 0) {
        printf("FUSE mount failed with return code %d\n", fuse_ret);
    }
    //printf("End init FUSE: \n");

    cleanup(fds, &file_stat, fuse_argv);

    return fuse_ret;
}

void setup_mmap_and_check_superblocks(int *fds, struct stat *file_stat) {
    // map each disk image into memory
    for (int i = 0; i < num_disks; i++) {
        mapped_memory[i] = mmap(NULL, file_stat->st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fds[i], 0);
        if (mapped_memory[i] == MAP_FAILED) {
            perror("mmap");
            exit(EXIT_FAILURE);
        }
    }

    // retrieve the first superblock directly from mapped_memory
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[0];
    disk_order[sb->disk_id] = 0;

    // check consistency of superblocks across all disks
    for (int i = 1; i < num_disks; i++) {
        struct wfs_sb *other = (struct wfs_sb *)mapped_memory[i];
        size_t sb_common_size = 48; // size of the superblock fields to compare

        // compare the superblocks to ensure consistency
        if (sb->f_id != other->f_id || 
            sb->raid != other->raid || 
            memcmp(sb, other, sb_common_size)) {
            fprintf(stderr, "Inconsistent superblocks detected!\n");
            exit(EXIT_FAILURE);
        }

        disk_order[other->disk_id] = i;
    }

    // set global RAID mode
    raid = sb->raid;
}

void reorder_disks() {
    void *buf[MAX_DISKS]; // tmp use

    // reorder based on disk_order
    for (int i = 0; i < num_disks; i++) {
        buf[disk_order[i]] = mapped_memory[i];
    }

    // Copy back into mapped_memory
    for (int i = 0; i < num_disks; i++) {
        mapped_memory[i] = buf[i];
    }
}

int check_root_inodes() {
    for (int i = 0; i < num_disks; i++) {
        struct wfs_inode *inode = get_inode_by_number(0, i);
        if (!inode) {
            fprintf(stderr, "Cannot retrieve root inode on disk %d!\n", i);
            return -1;
        }
    }
    return 0;
}

void cleanup(int *fds, struct stat *file_stat, char **fuse_argv) {
    for (int i = 0; i < num_disks; i++) {
        munmap(mapped_memory[i], file_stat->st_size);
        close(fds[i]);
    }
    free(fuse_argv);
}

int find_inode(struct wfs_inode *enclosing, char *path, struct wfs_inode **inode, int disk)
{
    // base case: If the path is empty, return the current inode
    if (!strcmp(path, "")) {
        *inode = enclosing;
        return 0;
    }

    // extract the next component of the path
    char *next = path;
    while (*path != '/' && *path != '\0') {
        path++;
    }
    if (*path != '\0') {
        *path++ = '\0'; // null-terminate the current component
    }

    // search for the directory entry matching 'next' within 'enclosing'
    size_t sz = enclosing->size;
    struct wfs_dentry *dentries;
    int inum = -1; // inode number of the matching directory entry

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry)) {
        // retrieve the directory entry for the current offset
        dentries = (struct wfs_dentry *)calculate_block_offset(enclosing, off, 0, disk);

        // check for a matching directory entry
        if (dentries->num != 0 && !strcmp(dentries->name, next)) {
            inum = dentries->num;
            break;
        }
    }
    if (inum < 0)
    {
        fprintf(stderr, "find_inode: Component '%s' not found in path.\n", next);
        err_rc = -ENOENT;
        return -1;
    }
    return find_inode(get_inode_by_number(inum, disk), path, inode, disk);
}

int find_inode_by_path(char *path, struct wfs_inode **inode, int disk)
{
    // all paths must start at root, thus path+1 is safe
    return find_inode(get_inode_by_number(0, disk), path + 1, inode, disk);
}

int wfs_mknod(const char* path, mode_t mode, dev_t dev, int disk) {
    printf("START: wfs_mknod \n");

    struct wfs_inode* parent_inode = NULL;
    char *base = strdup(path);
    char *name = strdup(path);

    // Retrieve parent inode
    if (find_inode_by_path(dirname(base), &parent_inode, disk) < 0) {
        fprintf(stderr, "Error: Parent inode for path %s not found\n", dirname(base));
        free(base);
        free(name);
        return -ENOENT; // No such file or directory
    }

    // Allocate inode
    struct wfs_inode* inode = NULL;
    if (raid == RAID0) {
        for (int i = 0; i < num_disks; i++) {
            inode = allocate_inode(i);
            if (!inode) {
                fprintf(stderr, "Error: Insufficient space to allocate inode on disk %d\n", i);
                free(base);
                free(name);
                return -ENOSPC; // No space left on device
            }
            initialize_inode(inode, S_IFREG | mode);
        }
    } else {
        inode = allocate_inode(disk);
        if (!inode) {
            fprintf(stderr, "Error: Insufficient space to allocate inode on disk %d\n", disk);
            free(base);
            free(name);
            return -ENOSPC; // No space left on device
        }
        initialize_inode(inode, S_IFREG | mode);
    }

    // Add directory entry
    if (add_directory_entry(parent_inode, inode->num, basename(name), disk) < 0) {
        fprintf(stderr, "Error: Could not add directory entry for %s\n", basename(name));
        free(base);
        free(name);
        return -EEXIST; // File or directory already exists
    }

    free(base);
    free(name);
    return 0; // Success
}

int WFS_MKNOD(const char *path, mode_t mode, dev_t dev) {
    //printf("START: WFS_MKNOD);

    // handle RAID0 case (one disk only)
    if (raid == RAID0) {
        int result = wfs_mknod(path, mode, dev, 0);
        if (result != 0) {
            fprintf(stderr, "Fail to create node in RAID0 mode.\n");
            return result;
        }
        return 0;
    }

    // handle other RAID modes (all disks)
    for (int i = 0; i < num_disks; i++) {
        int result = wfs_mknod(path, mode, dev, i);
        if (result != 0) {
            fprintf(stderr, "Failed to create node on disk %d in RAID1 mode.\n", i);
            return result;
        }
    }
    return 0;
}

int add_directory_entry(struct wfs_inode *parent_inode, int num, char *name, int disk)
{
    // insert dentry if there is an empty slot
    struct wfs_dentry *dentries;
    off_t offset = 0;

    while (offset < parent_inode->size)
    {
        dentries = (struct wfs_dentry *)calculate_block_offset(parent_inode, offset, 0, disk);

        if (dentries->num == 0)
        {
            dentries->num = num;
            strncpy(dentries->name, name, MAX_NAME);
            if (raid == RAID0)
            {
                for (int i = 0; i < num_disks; i++)
                {
                    struct wfs_inode *w = get_inode_by_number(parent_inode->num, i);
                    w->nlinks++;
                }
            }
            else
            {
                parent_inode->nlinks++;
            }
            return 0;
        }
        offset += sizeof(struct wfs_dentry);
    }

    // careful this will not work with indirect blocks for now
    // We will not do indirect blocks with directories
    dentries = (struct wfs_dentry *)calculate_block_offset(parent_inode, parent_inode->size, 1, disk);
    if (!dentries)
    {
        printf("mknod error\n");
        return -1;
    }
    dentries->num = num;
    strncpy(dentries->name, name, MAX_NAME);
    if (raid == RAID0)
    {
        for (int i = 0; i < num_disks; i++)
        {
            struct wfs_inode *w = get_inode_by_number(parent_inode->num, i);
            w->nlinks++;
            w->size += BLOCK_SIZE;
        }
    }
    else
    {
        parent_inode->nlinks++;
        parent_inode->size += BLOCK_SIZE;
    }

    return 0;
}

void initialize_inode(struct wfs_inode *inode, mode_t mode)
{
    struct timespec time;
    clock_gettime(CLOCK_REALTIME, &time);
    inode->mode = mode;
    inode->uid = getuid();
    inode->gid = getgid();
    inode->size = 0;
    inode->nlinks = 1;
    inode->atim = time.tv_sec;
    inode->mtim = time.tv_sec;
    inode->ctim = time.tv_sec;
}

int wfs_mkdir(const char *path, mode_t mode, int disk) {
    printf("START: wfs_mkdir: \n");

    struct wfs_inode *parent_inode = NULL; // inode of the parent directory

    char *base = strdup(path);  // used to extract the parent path
    char *name = strdup(path);  // used to extract the base name of the directory

    // Step 1: find the parent directory inode by its path
    if (find_inode_by_path(dirname(base), &parent_inode, disk) < 0) {
        fprintf(stderr, "Error: Cannot find parent inode for path '%s'.\n", path);
        free(base);
        free(name);
        return err_rc;
    }

    // Step 2: allocate and initialize the new directory inode
    struct wfs_inode *inode = NULL; // inode for the new directory
    if (raid == RAID0) {
        for (int i = 0; i < num_disks; i++) {
            inode = allocate_inode(i);
            if (inode == NULL) {
                fprintf(stderr, "Error: Cannot allocate inode on disk %d.\n", i);
                free(base);
                free(name);
                return err_rc;
            }
            initialize_inode(inode, S_IFDIR | mode); // initialize as a directory
        }
    } else {
        // RAID1 or RAID1v: allocate inode on the specific disk
        inode = allocate_inode(disk);
        if (inode == NULL) {
            fprintf(stderr, "Error: Cannot allocate inode on disk %d.\n", disk);
            free(base);
            free(name);
            return err_rc;
        }
        initialize_inode(inode, S_IFDIR | mode);
    }

    // Step 3: add a directory entry to the parent directory
    if (add_directory_entry(parent_inode, inode->num, basename(name), disk) < 0) {
        fprintf(stderr, "Error: Cannot add directory entry for '%s' in parent inode.\n", basename(name));
        free(base);
        free(name);
        return err_rc;
    }

    free(name);
    free(base);
    return 0;
}

int WFS_MKDIR(const char *path, mode_t mode) {
    // RAID0: create the directory only on disk 0
    if (raid == RAID0) {
        if (wfs_mkdir(path, mode, 0) != 0) {
            printf("Error: Failed to create directory '%s' on disk 0.\n", path);
            return err_rc; 
        }
    } 
    else { // RAID1 or RAID1v: create the directory on all disks
        for (int i = 0; i < num_disks; i++) {
            if (wfs_mkdir(path, mode, i) != 0) {
                printf("Error: Failed to create directory '%s' on disk %d.\n", path, i);
                return err_rc;
            }
        }
    }
    
    return 0;
}

int wfs_getattr(const char *path, struct stat *statbuf)
{
    printf("wfs_getattr: %s\n", path);
    struct wfs_inode *inode;
    char *path_copy = strdup(path);
    if (find_inode_by_path(path_copy, &inode, 0) < 0)
    {
        printf("Cannot get inode from path!\n");
        return err_rc;
    }

    // printf("Debugging: inode info: \n");
    // printf("Printing inode %d\n", inode->num);
    // printf("    mode: %d", inode->mode);
    // printf("    uid: %d", inode->uid);
    // printf("    gid: %d", inode->gid);
    // printf("    size: %lu", inode->size);
    // printf("    nlinks: %d\n", inode->nlinks);

    statbuf->st_mode = inode->mode;
    statbuf->st_uid = inode->uid;
    statbuf->st_gid = inode->gid;
    statbuf->st_size = inode->size;
    statbuf->st_atime = inode->atim;
    statbuf->st_mtime = inode->mtim;
    statbuf->st_ctime = inode->ctim;
    statbuf->st_nlink = inode->nlinks;

    free(path_copy);
    return 0;
}

// removes a dentry from the directory inode
// if this results in an empty data block, we will not deallocate it.
// removed dentries can result in "holes" in the dentry list, thus it
// is important to use the first available slot in add_directory_entry()
// TODO: update parent's inode
int remove_directory_entry(struct wfs_inode *inode, int inum, int disk)
{
    size_t sz = inode->size;
    struct wfs_dentry *dentries;

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry))
    {
        dentries = (struct wfs_dentry *)calculate_block_offset(inode, off, 0, disk);

        if (dentries->num == inum)
        { // match
            dentries->num = 0;
            return 0;
        }
    }
    return -1; // not found
}

char *calculate_block_offset(struct wfs_inode *inode, off_t offset, int alloc, int disk) {
    int block_num = offset / BLOCK_SIZE; 
    off_t *blocks;

    // Step 1: block number within valid range
    if (block_num > D_BLOCK + (BLOCK_SIZE / sizeof(off_t))) {
        fprintf(stderr, "Error: Block number %d is out of range!\n", block_num);
        return NULL;
    }

    // Step 2: handle RAID0
    if (raid == RAID0) {
        int d = block_num % num_disks; // target disk for this block
        if (block_num > D_BLOCK) { // indirect block
            block_num -= IND_BLOCK;
            if (inode->blocks[IND_BLOCK] == 0) {
                // allocate indirect block for all disks
                for (int i = 0; i < num_disks; i++) {
                    struct wfs_inode *w = get_inode_by_number(inode->num, i);
                    w->blocks[IND_BLOCK] = allocate_data_block(i);
                }
            }
            // calculate block array for indirect blocks
            blocks = (off_t *)((char *)mapped_memory[disk] + inode->blocks[IND_BLOCK]);
        } else { // direct block
            blocks = inode->blocks;
        }

        // Step 3: allocate block
        if (alloc && *(blocks + block_num) == 0) {
            off_t new_block_offset = allocate_data_block(d);
            for (int i = 0; i < num_disks; i++) {
                struct wfs_inode *w = get_inode_by_number(inode->num, i);
                if (blocks != inode->blocks) { // indirect block
                    off_t *indirect = (off_t *)((char *)mapped_memory[i] + w->blocks[IND_BLOCK]);
                    *(indirect + block_num) = new_block_offset;
                } else { // direct block
                    w->blocks[block_num] = new_block_offset;
                }
            }
        }

        return (char *)mapped_memory[d] + blocks[block_num] + (offset % BLOCK_SIZE);
    } else {
        // Step 4: handle RAID1/RAID1v (direct and indirect blocks)
        if (block_num > D_BLOCK) { // indirect block
            block_num -= IND_BLOCK;
            if (inode->blocks[IND_BLOCK] == 0) {
                inode->blocks[IND_BLOCK] = allocate_data_block(disk);
            }

            blocks = (off_t *)((char *)mapped_memory[disk] + inode->blocks[IND_BLOCK]);
        } else { // direct block
            blocks = inode->blocks;
        }

        // Step 5: allocate block 
        if (alloc && *(blocks + block_num) == 0) {
            *(blocks + block_num) = allocate_data_block(disk);
        }
        if (*(blocks + block_num) == 0) {
            fprintf(stderr, "Error: Block allocation failed!\n");
            return NULL;
        }

        return (char *)mapped_memory[disk] + blocks[block_num] + (offset % BLOCK_SIZE);
    }
}

int wfs_read(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk) {
    printf("START: wfs_read: \n");

    struct wfs_inode *inode; // the inode of the file
    char *path_copy = strdup(path);

    // Step 1: locate the inode of the file
    if (find_inode_by_path(path_copy, &inode, disk) < 0) {
        fprintf(stderr, "Error: Cannot locate inode for path '%s'.\n", path);
        free(path_copy);
        return err_rc; 
    }

    size_t num_bytes = 0; // total number of bytes read
    size_t position = offset;  // current position in the file

    // Step 2: read data 
    while (num_bytes < length && position < inode->size) {
        // calculate the number of bytes to read
        size_t to_read = BLOCK_SIZE - (position % BLOCK_SIZE); // read up to the end of the current block

        // ensure we don't read beyond the file size
        size_t eof = inode->size - position; // bytes remaining until the end of the file
        if (to_read > eof) {
            to_read = eof;
        }

        // Step 3: calculate the memory address for the current block
        char *addr = calculate_block_offset(inode, position, 0, disk);

        // Step 4: copy the data from the file to the buffer
        memcpy(buf + num_bytes, addr, to_read);

        // Step 5: update counters for the next iteration
        position += to_read;       // move to the next position
        num_bytes += to_read; // increment the total bytes read
    }

    free(path_copy);
    return num_bytes;
}

int wfs_read_r1v(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi) {
    int chsums[MAX_DISKS]; // store checksums for all disks

    // Step 1: compute checksums for all disks
    for (int i = 0; i < num_disks; i++) {
        // read data from the current disk
        wfs_read(path, buf, length, offset, fi, i);

        // Sum of all bytes in the buffer
        chsums[i] = 0;
        for (size_t s = 0; s < length; s++) {
            chsums[i] += buf[s];
        }
    }

    // Step 2: implement checksum occurrences
    // replace the first occurrence of each checksum with the count of its occurrences
    for (int i = 0; i < num_disks; i++) {
        if (chsums[i] == -1) // skip already processed checksums
            continue;

        int count = 1; // count for the current checksum
        for (int j = i + 1; j < num_disks; j++) {
            if (chsums[i] == chsums[j]) {
                chsums[j] = -1; // mark duplicate checksums as -1
                count++;        // increment the count for the checksum
            }
        }
        chsums[i] = count; // Store the count in the first occurrence
    }

    // Step 3: select the disk with the highest checksum occurrence
    int max = -1;     // maximum count of occurrences
    int argmax = -1;  // index of max
    for (int i = 0; i < num_disks; i++) {
        if (chsums[i] > max) {
            max = chsums[i]; 
            argmax = i;     
        }
    }

    return wfs_read(path, buf, length, offset, fi, argmax);
}


int WFS_READ(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi) {
    if (raid == RAID0) {
        return wfs_read(path, buf, length, offset, fi, 0);
    } 
    else if (raid == RAID1) {
        return wfs_read(path, buf, length, offset, fi, 0);
    } 
    else if (raid == RAID1V) {
        // RAID1v: select the most "reliable" disk for reading
        return wfs_read_r1v(path, buf, length, offset, fi);
    } 
    else {
        printf("Usage: Please use RAID mode RAID0, RAID1, or RAID1V.\n");
    }

    return 0;
}

int wfs_write(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk) {
    printf("START: wfs_write \n");

    struct wfs_inode *inode; // inode of the file
    char *path_copy = strdup(path); 

    // Step 1: locate the inode of the file
    if (find_inode_by_path(path_copy, &inode, disk) < 0) {
        fprintf(stderr, "Error: Cannot locate inode for path '%s'.\n", path);
        free(path_copy);
        return err_rc; 
    }

    // Step 2: calculate the additional data length required
    // if the write goes beyond the current file size, increase the file size accordingly
    ssize_t new_data_len = length - (inode->size - offset);

    size_t written_bytes = 0; // total number of bytes written
    size_t position = offset; // current position 

    // Step 3: write data
    while (written_bytes < length) {
        size_t to_write = BLOCK_SIZE - (position % BLOCK_SIZE);

        // don't write beyond the buffer size
        if (to_write + written_bytes > length) {
            to_write = length - written_bytes;
        }

        // Step 4: calculate the memory address for the current block
        char *addr = calculate_block_offset(inode, position, 1, disk);
        if (addr == NULL) {
            fprintf(stderr, "Error: Failed to calculate data block offset.\n");
            free(path_copy);
            return err_rc; 
        }

        // Step 5: copy the data from the buffer to the file
        memcpy(addr, buf + written_bytes, to_write);

        // Step 6: update counters for the next iteration
        position += to_write;       // move to the next position
        written_bytes += to_write;   // increment the total bytes written
    }

    // Step 7: update the file size if new data extends beyond the current size
    if (new_data_len > 0) {
        inode->size += new_data_len;
    }

    // Step 8: handle RAID0-specific logic
    // apply the updated size to all replicas of the inode across disks
    if (raid == RAID0) {
        for (int i = 0; i < num_disks; i++) {
            struct wfs_inode *wfs_inode = get_inode_by_number(inode->num, i);
            wfs_inode->size = inode->size;
        }
    }
    free(path_copy);
    return written_bytes;
}

int WFS_WRITE(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi)
{
    int ret;
    if (raid == RAID0)
    {
        ret = wfs_write(path, buf, length, offset, fi, 0);
    }
    else
    {
        for (int i = 0; i < num_disks; i++)
            ret = wfs_write(path, buf, length, offset, fi, i);
    }
    return ret;
}

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {
    printf("START wfs_readdir \n");

    // Step 1: add default entries "." and ".." to the buffer
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    struct wfs_inode *inode; // inode of the directory
    char *path_copy = strdup(path); 

    // Step 2: locate the inode of the directory
    if (find_inode_by_path(path_copy, &inode, 0) < 0) {
        fprintf(stderr, "Error: Cannot locate inode for path '%s'.\n", path);
        free(path_copy);
        return err_rc;
    }

    // Step 3: get the size of the directory and iterate through its entries
    size_t sz = inode->size; // size of the directory in bytes
    struct wfs_dentry *dentries; 

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry)) {
        // Step 4: calculate the memory address of the current directory entry
        dentries = (struct wfs_dentry *)calculate_block_offset(inode, off, 0, 0);

        // Step 5: add valid entries to the buffer
        // if the entry is allocated 
        if (dentries->num != 0) {
            filler(buf, dentries->name, NULL, 0); // add the entry name to the buffer
        }
    }

    free(path_copy);
    return 0;
}

int wfs_unlink(const char *path, int disk) {
    printf("START: wfs_unlink \n");

    struct wfs_inode *parent_inode; // inode of the parent directory
    struct wfs_inode *inode;       // inode of the target
    char *base = strdup(path);     // used to extract the parent directory
    char *path_copy = strdup(path);// used to extract the target name

    // Step 1: find the parent directory's inode
    if (find_inode_by_path(dirname(base), &parent_inode, disk) < 0) {
        fprintf(stderr, "Error: Cannot find parent inode for path '%s'.\n", path);
        free(base);
        free(path_copy);
        return err_rc;
    }

    // Step 2: find the inode for the target
    if (find_inode_by_path(path_copy, &inode, disk) < 0) {
        fprintf(stderr, "Error: Cannot find inode for path '%s'.\n", path);
        free(base);
        free(path_copy);
        return err_rc;
    }

    // Step 3: free all associated data blocks
    off_t *blocks;
    if (inode->blocks[IND_BLOCK] != 0) { // free indirect blocks
        blocks = (off_t *)(((char *)mapped_memory[disk] + inode->blocks[IND_BLOCK]));
        for (int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++) {
            if (blocks[i] != 0) { // free the block on the appropriate disk
                free_block(blocks[i], disk);
            }
        }
    }

    // free direct blocks
    blocks = inode->blocks;
    for (int i = 0; i < N_BLOCKS; i++) {
        if (blocks[i] != 0) {
            if (raid == RAID0) {
                free_block(blocks[i], i % num_disks); // disk determined by block number for RAID0
            } else {
                free_block(blocks[i], disk); // same disk for RAID1 or RAID1v
            }
        }
    }

    // Step 4: remove the directory entry from the parent directory
    if (remove_directory_entry(parent_inode, inode->num, disk) < 0) {
        fprintf(stderr, "Error: Cannot remove directory entry for '%s'.\n", path);
        free(base);
        free(path_copy);
        return err_rc;
    }

    // Step 5: free the inode
    if (raid == RAID0) { // RAID0: free the inode on all disks
        int inum = inode->num;
        for (int i = 0; i < num_disks; i++) {
            struct wfs_inode *w = get_inode_by_number(inum, i);
            free_inode(w, i);
        }
    } else { // RAID1 or RAID1v: free the inode on the specific disk
        free_inode(inode, disk);
    }

    free(base);
    free(path_copy);
    return 0;
}

int WFS_UNLINK(const char *path) {
    // RAID0: remove the file/directory only from disk 0
    if (raid == RAID0) {
        if (wfs_unlink(path, 0) != 0) {
            printf("Error: Cannot unlink '%s' from disk 0.\n", path);
            return err_rc;
        }
    } 
    else { // RAID1 or RAID1v: remove the file/directory from all disks
        for (int i = 0; i < num_disks; i++) {
            if (wfs_unlink(path, i) != 0) {
                printf("Error: Cannot unlink '%s' from disk %d.\n", path, i);
                return err_rc;
            }
        }
    }
    return 0;
}

int wfs_rmdir(const char *path)
{
    printf("START: wfs_rmdir \n");
    WFS_UNLINK(path);
    return 0;
}

void free_bitmap(uint32_t position, uint32_t *bitmap)
{
    int b = position / 32;
    int p = position % 32; // bit position within the 32-bit word
    bitmap[b] ^= (0x1 << p); // mark as free
}

void free_block(off_t blk, int disk)
{
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    // the address of the block to free
    void *block_address = (char *)mapped_memory[disk] + blk;
    memset(block_address, 0, BLOCK_SIZE); // zero out

    // position of the block in the data block bitmap
    uint32_t position = (blk - sb->d_blocks_ptr) / BLOCK_SIZE; 
    uint32_t *bitmap = (uint32_t *)((char *)mapped_memory[disk] + sb->d_bitmap_ptr);

    free_bitmap(position, bitmap);
}

void free_inode(struct wfs_inode *inode, int disk)
{
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    memset((char *)inode, 0, BLOCK_SIZE); // zero out 
    // position of the inode in the inode bitmap
    uint32_t position = ((char *)inode - (char *)mapped_memory[disk] + sb->i_blocks_ptr) / BLOCK_SIZE;
    uint32_t *bitmap = (uint32_t *)((char *)mapped_memory[disk] + sb->i_bitmap_ptr);
    free_bitmap(position, bitmap);
}

struct wfs_inode *get_inode_by_number(int num, int disk) {
    // access the superblock directly from the mapped memory for the given disk
    struct wfs_sb *sb = (struct wfs_sb *)((char *)mapped_memory[disk]);
    // calculate the address of the inode bitmap
    uint32_t *bitmap = (uint32_t *)((char *)mapped_memory[disk] + sb->i_bitmap_ptr);

    int b = num / 32; // block in the bitmap
    int p = num % 32; // bosition within the block

    // if the inode is allocated in the bitmap
    if (bitmap[b] & (0x1 << p)) {
        return (struct wfs_inode *)((char *)mapped_memory[disk] + sb->i_blocks_ptr + num * BLOCK_SIZE);
    }

    // if not allocated, return NULL
    return NULL;
}

ssize_t allocate_block(uint32_t *bitmap, size_t len) {
    // scan through all bits in the bitmap
    for (size_t bit_index = 0; bit_index < len * 32; bit_index++) {
        size_t idx = bit_index / 32; //  32-bit region index
        size_t position = bit_index % 32; // bit position within the region

        // check if the bit is free (0)
        if (!((bitmap[idx] >> position) & 0x1)) {
            // allocate the block by setting the bit to 1
            bitmap[idx] |= (0x1 << position);
            return bit_index;
        }
    }

    // no free block is found
    return -1;
}

off_t allocate_data_block(int disk) {
    // get the superblock for the specified disk
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    // locate the bitmap for data blocks
    uint32_t *bitmap = (uint32_t *)((char *)mapped_memory[disk] + sb->d_bitmap_ptr);

    off_t num_block = allocate_block(bitmap, sb->num_data_blocks / 32);
    if (num_block < 0) {
        fprintf(stderr, "Error: Unable to allocate a data block on disk %d (no space available).\n", disk);
        return 0; 
    }
    off_t block_offset = sb->d_blocks_ptr + BLOCK_SIZE * num_block;

    return block_offset;
}

struct wfs_inode *allocate_inode(int disk) {
    // get the superblock for the specified disk
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    // locate the bitmap for inodes
    uint32_t *bitmap = (uint32_t *)((char *)mapped_memory[disk] + sb->i_bitmap_ptr);
    off_t num_block = allocate_block(bitmap, sb->num_inodes / 32);
    if (num_block < 0) {
        fprintf(stderr, "Error: Unable to allocate an inode on disk %d (no inodes available).\n", disk);
        err_rc = -ENOSPC; 
        return NULL;
    }
    struct wfs_inode *inode = (struct wfs_inode *)((char *)mapped_memory[disk] + sb->i_blocks_ptr + BLOCK_SIZE * num_block);
    inode->num = num_block;

    return inode;
}