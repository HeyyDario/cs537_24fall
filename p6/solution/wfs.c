#define FUSE_USE_VERSION 30

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

#define MMAP_PTR(offset, disk) ((char *)mapped_memory[disk] + offset)
#define MAX_DISKS 10

// global variables
void *mapped_memory[MAX_DISKS];
int disk_mapping[MAX_DISKS];
int wfs_error;
int raid;
int num_disks;

//  ============= functions to set up main =============
void setup_mmap_and_check_superblocks(int *fds, struct stat *file_stat);
void reorder_disks();
int check_root_inodes();
void cleanup(int *fds, struct stat *file_stat, char **fuse_argv);
//  ============= functions to set up sys calls =============
int dentry_to_num(char *name, struct wfs_inode *inode, int disk);
int get_inode_rec(struct wfs_inode *enclosing, char *path, struct wfs_inode **inode, int disk);
int find_inode_by_path(char *path, struct wfs_inode **inode, int disk);
int wfs_mknod(const char *path, mode_t mode, dev_t dev, int disk);
int wfs_mknod_by_mode(const char *path, mode_t mode, dev_t dev);
int add_directory_entry(struct wfs_inode *parent, int num, char *name, int disk);
void initialize_inode(struct wfs_inode *inode, mode_t mode);
int wfs_mkdir(const char *path, mode_t mode, int disk);
int wfs_mkdir_raid(const char *path, mode_t mode);
int wfs_getattr(const char *path, struct stat *statbuf);
int remove_directory_entry(struct wfs_inode *inode, int inum, int disk);
char *calculate_block_offset(struct wfs_inode *inode, off_t offset, int alloc, int disk);
int wfs_read(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk);
int chsum(char *buf, size_t length);
int wfs_read_r1v(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi);
int wfs_read_raid(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi);
int wfs_write(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk);
int wfs_write_raid(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi);
int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi);
int wfs_unlink(const char *path, int disk);
int wfs_unlink_raid(const char *path);
int wfs_rmdir(const char *path);
struct wfs_inode* allocate_inode(int disk);
off_t allocate_data_block(int disk);
struct wfs_inode* retrieve_inode(int num, int disk);
void free_bitmap(uint32_t position, uint32_t *bitmap);
void free_inode(struct wfs_inode* inode, int disk);
void free_block(off_t blk, int disk);
int dentry_to_num(char* name, struct wfs_inode* inode, int disk);

static struct fuse_operations wfs_oper = {
    .getattr = wfs_getattr,
    .mknod = wfs_mknod_by_mode,
    .mkdir = wfs_mkdir_raid,
    .unlink = wfs_unlink_raid,
    .rmdir = wfs_rmdir,
    .read = wfs_read_raid,
    .write = wfs_write_raid,
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
    disk_mapping[sb->disk_id] = 0;

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

        disk_mapping[other->disk_id] = i;
    }

    // set global RAID mode
    raid = sb->raid;
}

void reorder_disks() {
    void *buf[MAX_DISKS]; // tmp use

    // reorder based on disk_mapping
    for (int i = 0; i < num_disks; i++) {
        buf[disk_mapping[i]] = mapped_memory[i];
    }

    // Copy back into mapped_memory
    for (int i = 0; i < num_disks; i++) {
        mapped_memory[i] = buf[i];
    }
}

int check_root_inodes() {
    for (int i = 0; i < num_disks; i++) {
        struct wfs_inode *inode = retrieve_inode(0, i);
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

int dentry_to_num(char *name, struct wfs_inode *inode, int disk)
{
    size_t sz = inode->size;
    struct wfs_dentry *dentries;

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry))
    {
        dentries = (struct wfs_dentry *)calculate_block_offset(inode, off, 0, disk);

        if (dentries->num != 0 && !strcmp(dentries->name, name))
        { // match
            return dentries->num;
        }
    }
    return -1;
}

int get_inode_rec(struct wfs_inode *enclosing, char *path, struct wfs_inode **inode, int disk)
{
    if (!strcmp(path, ""))
    {
        *inode = enclosing;
        return 0;
    }

    char *next = path;
    while (*path != '/' && *path != '\0')
    {
        path++;
    }
    if (*path != '\0')
    {
        *path++ = '\0';
    }

    int inum = dentry_to_num(next, enclosing, disk);
    if (inum < 0)
    {
        wfs_error = -ENOENT;
        return -1;
    }
    return get_inode_rec(retrieve_inode(inum, disk), path, inode, disk);
}

int find_inode_by_path(char *path, struct wfs_inode **inode, int disk)
{
    // all paths must start at root, thus path+1 is safe
    return get_inode_rec(retrieve_inode(0, disk), path + 1, inode, disk);
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

int wfs_mknod_by_mode(const char *path, mode_t mode, dev_t dev) {
    //printf("START: wfs_mknod_by_mode);

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
                    struct wfs_inode *w = retrieve_inode(parent_inode->num, i);
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
            struct wfs_inode *w = retrieve_inode(parent_inode->num, i);
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

int wfs_mkdir(const char *path, mode_t mode, int disk)
{
    printf("wfs_mkdir: %s\n", path);

    struct wfs_inode *parent_inode = NULL;

    char *base = strdup(path);
    char *name = strdup(path);

    if (find_inode_by_path(dirname(base), &parent_inode, disk) < 0)
    {
        // printf("Cannot get inode from path!\n");
        return wfs_error;
    }

    struct wfs_inode *inode = NULL;
    if (raid == RAID0)
    {
        for (int i = 0; i < num_disks; i++)
        {
            inode = allocate_inode(i);
            if (inode == NULL)
            {
                printf("Cannot allocate inode!\n");
                return wfs_error;
            }
            initialize_inode(inode, S_IFDIR | mode);
        }
    }
    else
    {
        inode = allocate_inode(disk);
        if (inode == NULL)
        {
            printf("Cannot allocate inode!\n");
            return wfs_error;
        }
        initialize_inode(inode, S_IFDIR | mode);
    }

    // add dentry to parent
    if (add_directory_entry(parent_inode, inode->num, basename(name), disk) < 0)
    {
        printf("Cannot add dentry!\n");
        return wfs_error;
    }

    free(name);
    free(base);

    return 0;
}

int wfs_mkdir_raid(const char *path, mode_t mode)
{
    if (raid == RAID0)
    {
        if (wfs_mkdir(path, mode, 0) != 0)
        {
            printf("mkdir!\n");
            return wfs_error;
        }
    }
    else
    {
        for (int i = 0; i < num_disks; i++)
            if (wfs_mkdir(path, mode, i) != 0)
            {
                printf("mkdir2!\n");
                return wfs_error;
            }
    }
    return 0;
}

int wfs_getattr(const char *path, struct stat *statbuf)
{
    printf("wfs_getattr: %s\n", path);
    struct wfs_inode *inode;
    char *searchpath = strdup(path);
    if (find_inode_by_path(searchpath, &inode, 0) < 0)
    {
        printf("Cannot get inode from path!\n");
        return wfs_error;
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

    free(searchpath);
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
    struct wfs_dentry *dent;

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry))
    {
        dent = (struct wfs_dentry *)calculate_block_offset(inode, off, 0, disk);

        if (dent->num == inum)
        { // match
            dent->num = 0;
            return 0;
        }
    }
    return -1; // not found
}

char *calculate_block_offset(struct wfs_inode *inode, off_t offset, int alloc, int disk) {
    int blocknum = offset / BLOCK_SIZE; // Calculate block number within the inode
    off_t *blks_arr; // Pointer to block array

    // Step 1: Ensure the block number is within range
    if (blocknum > D_BLOCK + (BLOCK_SIZE / sizeof(off_t))) {
        fprintf(stderr, "resolve_block_offset: Block number %d out of range!\n", blocknum);
        return NULL;
    }

    // Step 2: RAID0 Handling
    if (raid == RAID0) {
        int d = blocknum % num_disks; // Determine the disk where the block resides
        if (blocknum > D_BLOCK) { // Indirect block
            blocknum -= IND_BLOCK;
            if (inode->blocks[IND_BLOCK] == 0) {
                // Allocate indirect block for all disks
                for (int i = 0; i < num_disks; i++) {
                    struct wfs_inode *w = retrieve_inode(inode->num, i);
                    w->blocks[IND_BLOCK] = allocate_data_block(i);
                }
            }
            blks_arr = (off_t *)MMAP_PTR(inode->blocks[IND_BLOCK], disk);
        } else { // Direct block
            blks_arr = inode->blocks;
        }

        // Allocate block if requested and necessary
        if (alloc && *(blks_arr + blocknum) == 0) {
            off_t new_block_offset = allocate_data_block(d);
            for (int i = 0; i < num_disks; i++) {
                struct wfs_inode *w = retrieve_inode(inode->num, i);
                if (blks_arr != inode->blocks) { // Indirect block
                    off_t *indirect = (off_t *)MMAP_PTR(w->blocks[IND_BLOCK], i);
                    *(indirect + blocknum) = new_block_offset;
                } else { // Direct block
                    w->blocks[blocknum] = new_block_offset;
                }
            }
        }

        return MMAP_PTR(blks_arr[blocknum], d) + (offset % BLOCK_SIZE);
    }

    // Step 3: Mirrored RAID Handling
    else {
        if (blocknum > D_BLOCK) { // Indirect block
            blocknum -= IND_BLOCK;
            if (inode->blocks[IND_BLOCK] == 0) {
                inode->blocks[IND_BLOCK] = allocate_data_block(disk);
            }
            blks_arr = (off_t *)MMAP_PTR(inode->blocks[IND_BLOCK], disk);
        } else { // Direct block
            blks_arr = inode->blocks;
        }

        // Allocate block if requested and necessary
        if (alloc && *(blks_arr + blocknum) == 0) {
            *(blks_arr + blocknum) = allocate_data_block(disk);
        }

        // Return NULL if block is not allocated and allocation is not requested
        if (*(blks_arr + blocknum) == 0) {
            if (alloc) {
                fprintf(stderr, "resolve_block_offset: Insufficient space to allocate block.\n");
                return NULL;
            }
        }

        return MMAP_PTR(blks_arr[blocknum], disk) + (offset % BLOCK_SIZE);
    }
}

int wfs_read(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk)
{
    (void)fi;
    printf("wfs_read: %s\n", path);
    struct wfs_inode *inode;
    char *searchpath = strdup(path);
    if (find_inode_by_path(searchpath, &inode, disk) < 0)
    {
        printf("Cannot get inode from path!\n");
        return wfs_error;
    }
    size_t have_read = 0;
    size_t pos = offset;

    while (have_read < length && pos < inode->size)
    {
        size_t to_read = BLOCK_SIZE - (pos % BLOCK_SIZE);
        // length might be larger than file or block size
        size_t eof = inode->size - pos;
        if (to_read > eof)
        {
            to_read = eof;
        }

        char *addr = calculate_block_offset(inode, pos, 0, disk);
        memcpy(buf + have_read, addr, to_read);
        pos += to_read;
        have_read += to_read;
    }

    free(searchpath);
    return have_read;
}

int chsum(char *buf, size_t length)
{
    int ret = 0;
    for (size_t s = 0; s < length; s++)
        ret += buf[s];

    return ret;
}

int wfs_read_r1v(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi)
{
    int chsums[MAX_DISKS];

    // compute checksums for all disks
    for (int i = 0; i < num_disks; i++)
    {
        wfs_read(path, buf, length, offset, fi, i);
        chsums[i] = chsum(buf, length);
    }

    // replace the first occurence of the checksum with number of occurences
    // replace other occurences of the checksum with -1
    // 1,2,3,1,2 -> 2,2,1,-1,-1
    for (int i = 0; i < num_disks; i++)
    {
        if (chsums[i] == -1)
            continue;

        int count = 1;

        for (int j = i + 1; j < num_disks; j++)
        {
            if (chsums[i] == chsums[j])
            {
                chsums[j] = -1;
                count++;
            }
        }

        chsums[i] = count;
    }

    // select the the first disk with highest occurence
    int max = -1;
    int argmax = -1;
    for (int i = 0; i < num_disks; i++)
    {
        printf("disk: %d count: %d\n", i, chsums[i]);
        if (chsums[i] > max)
        {
            max = chsums[i];
            argmax = i;
        }
    }

    // perform another read from the right disk
    printf("Reading from disk %d\n", argmax);
    return wfs_read(path, buf, length, offset, fi, argmax);
}

int wfs_read_raid(const char *path, char *buf, size_t length, off_t offset, struct fuse_file_info *fi)
{
    switch (raid)
    {
    case RAID0:
        return wfs_read(path, buf, length, offset, fi, 0);
    case RAID1:
        return wfs_read(path, buf, length, offset, fi, 0);
    case RAID1V:
        return wfs_read_r1v(path, buf, length, offset, fi);
    default:
        printf("Unknown raid mode!\n");
    }
    return 0;
}

int wfs_write(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi, int disk)
{
    (void)fi;
    printf("wfs_write: %s\n", path);
    struct wfs_inode *inode;
    char *searchpath = strdup(path);
    if (find_inode_by_path(searchpath, &inode, disk) < 0)
    {
        printf("Cannot get inode from path!\n");
        return wfs_error;
    }

    // size will be increased by the amount of data we add to the file
    ssize_t newdatalen = length - (inode->size - offset);

    size_t have_written = 0;
    size_t pos = offset;

    while (have_written < length)
    {
        size_t to_write = BLOCK_SIZE - (pos % BLOCK_SIZE);
        if (to_write + have_written > length)
        {
            to_write = length - have_written;
        }

        char *addr = calculate_block_offset(inode, pos, 1, disk);
        if (addr == NULL)
        {
            printf("data offset!\n");
            return wfs_error;
        }
        memcpy(addr, buf + have_written, to_write);
        pos += to_write;
        have_written += to_write;
    }

    inode->size += newdatalen > 0 ? newdatalen : 0;

    if (raid == RAID0)
        for (int i = 0; i < num_disks; i++)
        {
            struct wfs_inode *w = retrieve_inode(inode->num, i);
            w->size = inode->size;
        }

    free(searchpath);
    return have_written;
}

int wfs_write_raid(const char *path, const char *buf, size_t length, off_t offset, struct fuse_file_info *fi)
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

int wfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    (void)fi;
    (void)offset;

    printf("wfs_readdir: %s\n", path);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);

    struct wfs_inode *inode;
    char *searchpath = strdup(path);
    if (find_inode_by_path(searchpath, &inode, 0) < 0)
    {
        printf("Cannot get inode from path!\n");
        return wfs_error;
    }

    size_t sz = inode->size;
    struct wfs_dentry *dent;

    for (off_t off = 0; off < sz; off += sizeof(struct wfs_dentry))
    {
        dent = (struct wfs_dentry *)calculate_block_offset(inode, off, 0, 0);

        if (dent->num != 0)
        {
            filler(buf, dent->name, NULL, 0);
        }
    }

    free(searchpath);
    return 0;
}

int wfs_unlink(const char *path, int disk)
{
    printf("wfs_unlink: %s\n", path);
    struct wfs_inode *parent_inode;
    struct wfs_inode *inode;
    char *base = strdup(path);
    char *searchpath = strdup(path);

    // parent inode
    if (find_inode_by_path(dirname(base), &parent_inode, disk) < 0)
    {
        printf("Cannot get inode from path!\n");
        return wfs_error;
    }

    // inode
    if (find_inode_by_path(searchpath, &inode, disk) < 0)
    {
        printf("Cannot get inode from path!\n");
        return wfs_error;
    }

    // free all the data blocks
    off_t *blks_arr;
    if (inode->blocks[IND_BLOCK] != 0)
    { // free indirect blocks
        blks_arr = (off_t *)MMAP_PTR(inode->blocks[IND_BLOCK], disk);
        for (int i = 0; i < BLOCK_SIZE / sizeof(off_t); i++)
        {
            if (blks_arr[i] != 0)
            { // TODO raid0
                free_block(blks_arr[i], disk);
            }
        }
    }
    blks_arr = inode->blocks;
    for (int i = 0; i < N_BLOCKS; i++)
    { // free all the direct blocks
        if (blks_arr[i] != 0)
        {
            if (raid == RAID0)
                free_block(blks_arr[i], i % num_disks);
            else
                free_block(blks_arr[i], disk);
        }
    }

    // remove dentry from parent
    // raid0 is handled inside the function
    remove_directory_entry(parent_inode, inode->num, disk);

    // free inode
    if (raid == RAID0)
    {
        int inum = inode->num;
        for (int i = 0; i < num_disks; i++)
        {
            struct wfs_inode *w = retrieve_inode(inum, i);
            free_inode(w, i);
        }
    }
    else
    {
        free_inode(inode, disk);
    }

    free(base);
    free(searchpath);
    return 0;
}

int wfs_unlink_raid(const char *path)
{
    if (raid == RAID0)
    {
        if (wfs_unlink(path, 0) != 0)
        {
            printf("Cannot unlink!\n");
            return wfs_error;
        }
    }
    else
    {
        for (int i = 0; i < num_disks; i++)
            if (wfs_unlink(path, i) != 0)
            {
                printf("Cannot unlink!\n");
                return wfs_error;
            }
    }

    return 0;
}

int wfs_rmdir(const char *path)
{
    printf("wfs_rmdir: %s\n", path);
    wfs_unlink_raid(path);
    return 0;
}

void free_bitmap(uint32_t position, uint32_t *bitmap)
{
    int b = position / 32;
    int p = position % 32;
    bitmap[b] = bitmap[b] ^ (0x1 << p);
}

// we choose to zero blocks and inodes as they are freed because some
// functions depend on fields being zero-initialized (e.g. finding an
// empty directory entry slot.) If blocks are freed and reused, garbage
// data could affect correctness.

void free_block(off_t blk, int disk)
{
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    memset(MMAP_PTR(blk, disk), 0, BLOCK_SIZE); // zero

    free_bitmap(/*position*/ (blk - sb->d_blocks_ptr) / BLOCK_SIZE,
                /*bitmap*/ (uint32_t *)MMAP_PTR(sb->d_bitmap_ptr, disk));
}

void free_inode(struct wfs_inode *inode, int disk)
{
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    memset((char *)inode, 0, BLOCK_SIZE); // zero

    free_bitmap(/*position*/ ((char *)inode - MMAP_PTR(sb->i_blocks_ptr, disk)) / BLOCK_SIZE,
                /*bitmap*/ (uint32_t *)MMAP_PTR(sb->i_bitmap_ptr, disk));
}

struct wfs_inode *retrieve_inode(int num, int disk)
{
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    uint32_t *bitmap = (uint32_t *)MMAP_PTR(sb->i_bitmap_ptr, disk);

    int b = num / 32;
    int p = num % 32;
    // check if it is allocated first
    if (bitmap[b] & (0x1 << p))
    {
        return (struct wfs_inode *)(MMAP_PTR(sb->i_blocks_ptr, disk) + num * BLOCK_SIZE);
    }
    return NULL;
}

// careful - block allocations are always stored by their offsets
// to use a block, we add the block address (i.e. offset) to mapped_memory
// we don't store pointers in the inode as they are not persistent across fs reboot.
ssize_t allocate_block(uint32_t *bitmap, size_t len)
{
    for (uint32_t i = 0; i < len; i++)
    {
        uint32_t bm_region = bitmap[i];
        if (bm_region == 0xFFFFFFFF)
        {
            continue;
        }
        for (uint32_t k = 0; k < 32; k++)
        {
            if (!((bm_region >> k) & 0x1))
            { // it is free
              // allocate
                bitmap[i] = bitmap[i] | (0x1 << k);
                return 32 * i + k;
                //                return block_region + (BLOCK_SIZE * (32*i + k));
            }
        }
    }
    return -1; // no free blocks found
}

off_t allocate_data_block(int disk)
{
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    off_t num_block = allocate_block((uint32_t *)MMAP_PTR(sb->d_bitmap_ptr, disk),
                                  sb->num_data_blocks / 32);
    if (num_block < 0)
    {
        printf("Cannot allocate block!\n");
        return 0;
    }
    return sb->d_blocks_ptr + BLOCK_SIZE * num_block;
}

struct wfs_inode *allocate_inode(int disk)
{
    struct wfs_sb *sb = (struct wfs_sb *)mapped_memory[disk];
    off_t num_block = allocate_block((uint32_t *)MMAP_PTR(sb->i_bitmap_ptr, disk),
                                  sb->num_inodes / 32);
    if (num_block < 0)
    {
        wfs_error = -ENOSPC;
        return NULL;
    }
    struct wfs_inode *inode = (struct wfs_inode *)(MMAP_PTR(sb->i_blocks_ptr, disk) + BLOCK_SIZE * num_block);
    inode->num = num_block;
    return inode;
}