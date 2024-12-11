/*
 *  Copyright (C) 2023 CS416 Rutgers CS
 *	Tiny File System
 *	File:	rufs.c
 * Use -d at the end of mounting to see all print statements
 */

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/time.h>
#include <libgen.h>
#include <limits.h>

#include "block.h"
#include "rufs.h"
extern int diskfile; //so that it compiles
char diskfile_path[PATH_MAX];

// Declare your in-memory data structures here

/* 
 * Get available inode number from bitmap
 */
int get_avail_ino() {
	struct superblock sb;
	uint8_t buffer[BLOCK_SIZE];

	//read superblock from disk, we set it at block 0
	if (bio_read(0, buffer) < 0){
		fprintf(stderr, "Error, Unable to read superblock.\n");
		return -1;
	}
	//copy superblock information to sb
	memcpy(&sb, buffer, sizeof(struct superblock));

	uint8_t buf[BLOCK_SIZE]; // Buffer to hold the bitmap block
    int max_inodes = sb.max_inum;

    // Read the inode bitmap block from disk
    if (bio_read(sb.i_bitmap_blk, buf) < 0) {
        fprintf(stderr, "Error: Unable to read inode bitmap block.\n");
        return -1;
    }

    // Find the first free inode
    for (int i = 0; i < max_inodes; i++) {
        if (get_bitmap(buf, i) == 0) {
            // Found a free inode, set it as allocated
            set_bitmap(buf, i);

            // Write updated bitmap back to disk
            if (bio_write(sb.i_bitmap_blk, buf) < 0) {
                fprintf(stderr, "Error: Unable to write inode bitmap block.\n");
                return -1;
            }

            // Return the index of the free inode found
            return i;
        }
    }

    // If no free inode found, return error
    return -1;

}

/* 
 * Get available data block number from bitmap
 */
int get_avail_blkno() {
	uint8_t buffer[BLOCK_SIZE];
	
	struct superblock sb;
	//read superblock from disk, we set it at block 0
	if (bio_read(0, buffer) < 0){
		fprintf(stderr, "Error, Unable to read superblock.\n");
		return -1;
	}
	//copy superblock information to sb
	memcpy(&sb, buffer, sizeof(struct superblock));

	int max_blocks = sb.max_dnum;
	// Step 1: Read data block bitmap from disk
	if(bio_read(sb.d_bitmap_blk, buffer) < 0){
		fprintf(stderr, "Error, Unable to read data block bitmap.\n");
		return -1;
	}
	// Step 2: Traverse data block bitmap to find an available slot
	for (int i = 0; i < max_blocks; i++){
		//found free block
		if (get_bitmap(buffer, i) == 0){
			//set allocated
			set_bitmap(buffer, i);

			// Step 3: Update data block bitmap and write to disk 
			if(bio_write(sb.d_bitmap_blk, buffer) < 0){
				fprintf(stderr, "Error: Unable to write data block to disk. \n");
				return -1;
			}
			//because data blocks start at sb.d_start_blk, we add this to i
			return sb.d_start_blk + i;
		}
	}


	//if no block is found return -1
	return -1;
}

/* 
 * inode operations
 */
int readi(uint16_t ino, struct inode *inode) {
	struct superblock sb;
	uint8_t buffer[BLOCK_SIZE];
	//read superblock from disk, we set it at block 0
	if (bio_read(0, buffer) < 0){
		fprintf(stderr, "Error, Unable to read superblock.\n");
		return -1;
	}
	memcpy(&sb, buffer, sizeof(struct superblock));

	// Step 1: Get the inode's on-disk block number
	//calculate how many inodes fit per block
	int inodes_per_block = BLOCK_SIZE/sizeof(struct inode);
	int block_num = sb.i_start_blk + (ino/inodes_per_block);

	// Step 2: Get offset of the inode in the inode on-disk block
	int offset = (ino % inodes_per_block) * sizeof(struct inode);

	// Step 3: Read the block containing the inode
	if (bio_read(block_num, buffer) < 0){
		fprintf(stderr, "Error, Unable to read inode block %d.\n", block_num);
		return -1;
	}
	//copy inode from buffer to given pararmeter
	memcpy(inode, buffer + offset, sizeof(struct inode));

	return 0;
  
}

int writei(uint16_t ino, struct inode *inode) {
	struct superblock sb;
	uint8_t buffer[BLOCK_SIZE];

	//read superblock from disk, we set it at block 0
	if (bio_read(0, buffer) < 0){
		fprintf(stderr, "Error, Unable to read superblock.\n");
		return -1;
	}
	//copy superblock information to sb
	memcpy(&sb, buffer, sizeof(struct superblock));

	// Step 1: Get the block number where this inode resides on disk
	int inodes_per_block = BLOCK_SIZE/sizeof(struct inode);
	int block_num = sb.i_start_blk + (ino/inodes_per_block);

	// Step 2: Get the offset in the block where this inode resides on disk
	int offset = (ino % inodes_per_block) * sizeof(struct inode);

	// Step 3: Write inode to disk 
	//Read existing block so that we don't overwrite other inodes in the same block
	if (bio_read(block_num, buffer) < 0){
		fprintf(stderr, "Error, Unable to read inode block %d.\n", block_num);
		return -1;
	}
	//overwrite specific inode
	memcpy(buffer + offset, inode, sizeof(struct inode));

	//write to disk
	if (bio_write(block_num, buffer) < 0){
		fprintf(stderr, "Error, Unable to write inode to block %d.\n", block_num);
		return -1;
	}
	return 0;
}


/* 
 * directory operations
 */
int dir_find(uint16_t ino, const char *fname, size_t name_len, struct dirent *dirent) {
	struct inode dir_inode;

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)
	if (readi(ino, &dir_inode) < 0){
		fprintf(stderr, "Error: Unable to read inode %u. \n", ino);
		return -1;
	}

	//Inode represents a directory, another check for debugging purposes
	if((dir_inode.type & S_IFDIR) == 0){
		fprintf(stderr, "Error: Inode %u is not a directory. \n", ino);
		return -1;
	}

  // Step 2: Get data block of current directory from inode
	//Assume entries fit into direct pointers. Check each direct pointer until
	//we find an entry or run out of blocks
	for (int i = 0; i < 16 && dir_inode.direct_ptr[i] != -1; i++){
		uint32_t dir_block = dir_inode.direct_ptr[i];
		//skip if it is not assigned
		if(dir_block == -1) {
			continue;
		}
		// Step 3: Read directory's data block and check each directory entry.
  		//If the name matches, then copy directory entry to dirent structure
		uint8_t block_buffer[BLOCK_SIZE];
		if (bio_read(dir_block,block_buffer) < 0){
			fprintf(stderr, "Error: Unable to read directory block %u.\n", dir_block);
			return -1;
		}

		//Each data block may contain multiple directory entries
		//Calculate how many entries can fit in one block
		int entries_per_block = BLOCK_SIZE/ sizeof(struct dirent);
		struct dirent *entries = (struct dirent *)block_buffer;
		for (int j = 0; j < entries_per_block; j++){
			if (entries[j].valid == 1 && entries[j].len == name_len){
				//compare the names
				if (strncmp(entries[j].name, fname, name_len) == 0){
					//found a match so now we copy the directory entry
					if (dirent != NULL){
						memcpy(dirent, &entries[j], sizeof(struct dirent));
					}
					return entries[j].ino; //Return the inode number of the found entry
				}
			}
		}
	}

	
	//if we are here then the file name was never found in the directory blocks
	return -1;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) 
{
	if (dir_find(dir_inode.ino, fname, name_len, NULL) == 0) 
	{
		fprintf(stderr, "Error: File name already used\n");
        return -1;
    }

    int dirents_per_block = BLOCK_SIZE / sizeof(struct dirent);
    struct dirent dir_block[dirents_per_block];

    // Try to find a free slot in existing directory blocks
    for (int i = 0; i < 16; i++) 
	{
        // If no block allocated, consider allocating a new one
        if (dir_inode.direct_ptr[i] == -1) 
		{
            // Allocate a new data block
            int new_block = get_avail_blkno();
            if (new_block < 0) 
			{
				fprintf(stderr, "Error: get_avail_blkno() failed\n");
                return -1;
            }

            dir_inode.direct_ptr[i] = new_block;

            // Initialize the new block with empty dirents
            memset(dir_block, 0, sizeof(dir_block));
            if (bio_write(new_block, dir_block) < 0) 
			{
				fprintf(stderr, "Error: bio_write() failed\n");
                return -1;
            }
        }

        // Read the block
        if (bio_read(dir_inode.direct_ptr[i], dir_block) < 0) 
		{
			fprintf(stderr, "Error: bio_read() failed\n");
            return -1;
        }

        // Search for a free slot
        for (int j = 0; j < dirents_per_block; j++) 
		{
            if (dir_block[j].valid == 0) 
			{
                // Found a free slot, fill it
                dir_block[j].ino = f_ino;
                dir_block[j].valid = 1;
                dir_block[j].len = (uint16_t)name_len;
                memset(dir_block[j].name, 0, sizeof(dir_block[j].name));
                strncpy(dir_block[j].name, fname, name_len);

                // Write updated block back
                if (bio_write(dir_inode.direct_ptr[i], dir_block) < 0) 
				{
					fprintf(stderr, "Error: bio_write() failed\n");
                    return -1;
                }

                // Update directory inode size
                dir_inode.size += sizeof(struct dirent);
                if (writei(dir_inode.ino, &dir_inode) < 0) 
				{
					fprintf(stderr, "Error: writei() failed\n");
                    return -1;
                }

                return 0;
            }
        }
    }

	fprintf(stderr, "Error: No space found in existing or newly allocated blocks\n");
    return -1;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) 
{
	int dirents_per_block = BLOCK_SIZE / sizeof(struct dirent);
    struct dirent dir_block[dirents_per_block];

    for (int i = 0; i < 16; i++) {
        if (dir_inode.direct_ptr[i] == -1) continue;

        if (bio_read(dir_inode.direct_ptr[i], dir_block) < 0) 
		{
			fprintf(stderr, "Error: bio_read() failed\n");
            return -1;
        }

        for (int j = 0; j < dirents_per_block; j++) 
		{
            if (dir_block[j].valid == 1 && dir_block[j].len == name_len && strncmp(dir_block[j].name, fname, name_len) == 0) 
            {
                dir_block[j].valid = 0; // Invalidate this entry

                if (bio_write(dir_inode.direct_ptr[i], dir_block) < 0) 
				{
					fprintf(stderr, "Error: bio_write() failed\n");
                    return -1;
                }

                dir_inode.size -= sizeof(struct dirent);
                if (writei(dir_inode.ino, &dir_inode) < 0) 
				{
					fprintf(stderr, "Error: writei() failed\n");
                    return -1;
                }

                return 0;
            }
        }
    }

    fprintf(stderr, "Error: Directory entry not found\n");
    return -1;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	//if either parameter is NULL, return error
	if (!path || !inode){
		return -1;
	}

	//if path is empty or root, return the inode of ino
	if (strcmp(path, "") == 0 || strcmp(path, "/") == 0){
		if (readi(ino,inode) < 0){
			return -1; //can't read starting inode
		}
		return 0;
	}

	//make a copy of the path
	char *path_copy = strdup(path);
	if (!path_copy){
		return -1; //strdup failed
	}

	//If the path starts with '/', start from root inode 
	// Else, use provided ino as start
	if (path_copy[0] == '/') {
		ino = 0;
	}

	//Load starting inode
	struct inode current_inode;
	if (readi(ino, &current_inode) < 0){
		free(path_copy);
		return -1;
	}
	//Tokenize the path by '/'
	char *token;
	//start tokenizing after skipping leading slashes
	//can't free path_copy until much later because we use it to iterate
	char *rest = path_copy;
	while(*rest == '/'){
		rest++;
	}

	while((token = strsep(&rest, "/")) != NULL){
		if(strcmp(token, "") == 0 || strcmp(token,".") == 0){
			//if it is empty or '.' just ignore it
			continue;
		}
		//right now token is a directory component we must look up
		//Need to make sure current inode is a dir first though.
		if((current_inode.type & S_IFDIR) == 0){
			//Current inode not a dir, can't proceed
			free(path_copy);
			return -1;
		}

		//If we have a dir, want to find next component
		struct dirent entry;
		int next_ino = dir_find(current_inode.ino, token, strlen(token), &entry);
		if(next_ino < 0){
			//not found
			free(path_copy);
			return -1;
		}
		//Read next inode, will copy to the location of current_inode and then we continue
		//this while loop
		if(readi((uint16_t)next_ino, &current_inode) < 0) {
			free(path_copy);
			return -1;
		}
	}
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way


	//after walking through path, current_inode is our target
	memcpy(inode, &current_inode, sizeof(struct inode));
	free(path_copy);
	return 0;
}

/* 
 * Make file system
 */
int rufs_mkfs() {
	// Call dev_init() to initialize (Create) Diskfile
	dev_init(diskfile_path);
	if(diskfile == -1){
		fprintf(stderr, "Error: Unable to make RU File System.\n");
		return -1;
	}
	// write superblock information
	struct superblock sb;
	sb.magic_num = MAGIC_NUM;
    sb.max_inum = MAX_INUM;
    sb.max_dnum = MAX_DNUM;
	sb.i_bitmap_blk = 1; //the integer associated to which block is the inode bitmap shouldn't matter so just assign it to some arbitrary value here, i.e. 1
	sb.d_bitmap_blk = 2;
	sb.i_start_blk = 3; //We will need to offset the data block start for after i_start which will require some calculation. Will do later
	int i_perblock = BLOCK_SIZE/sizeof(struct inode);
	//calculate total number of inode designated blocks needed
	//we add i_perblock in the numerator so that the int division doesn't round down
	//equivalent to ceiling function
	int num_iblocks = (sb.max_inum + i_perblock - 1) / i_perblock;
	//offset start of data by num_iblocks
	sb.d_start_blk = sb.i_start_blk + num_iblocks;
	//write superblock information to disk
	bio_write(0,&sb);
	// initialize inode bitmap
	//max_inum/8 is the total number of bytes required to allocate
	// (This is because 1 byte is 8 bits)
	bitmap_t i_bitmap;
	//use memset here because initializing at 0 is actually relevant
	memset(&i_bitmap, 0, MAX_INUM/8);
	//reserve root inode
	set_bitmap(i_bitmap, 0);
	uint8_t i_bitmapblock[BLOCK_SIZE];
	memset(i_bitmapblock, 0, BLOCK_SIZE);
	memcpy(i_bitmapblock, &i_bitmap, MAX_INUM/8);
	if (bio_write(sb.i_bitmap_blk, i_bitmapblock) <0){
		fprintf(stderr, "Error: Unable to write inode bitmap.\n");
		return -1;
	}

	// initialize data block bitmap
	bitmap_t d_bitmap;
	memset(&d_bitmap, 0, MAX_DNUM/8);

	//Reserve one block for root directory
	set_bitmap(d_bitmap, 0);
	//write data bitmap to disk
	uint8_t d_bitmapblock[BLOCK_SIZE];
	memset(d_bitmapblock, 0, BLOCK_SIZE);
	memcpy(d_bitmapblock, &d_bitmap, MAX_DNUM/8);
	if (bio_write(sb.d_bitmap_blk, d_bitmapblock) < 0){
		fprintf(stderr, "Error, Unable to write data bitmap.\n");
		return -1;
	}
	// update bitmap information for root directory
	struct inode root_inode;
	memset(&root_inode, 0, sizeof(struct inode));
	root_inode.ino = 0;
	root_inode.valid = 1;
	root_inode.type = S_IFDIR; //dir type
	root_inode.link = 2; //. and .. at least
	root_inode.size = 0; //nothing in it currently

	//first data block for root is at the d_start_blk (kind of obvious)
	root_inode.direct_ptr[0] = sb.d_start_blk;
	//write root to inode region on disk
	int root_inode_block = sb.i_start_blk;
	//no offset yet so we don't care about that
	// update inode for root directory
	uint8_t inode_block_buffer[BLOCK_SIZE];
	if (bio_read(root_inode_block, inode_block_buffer) < 0){
		fprintf(stderr, "Error, Unable to read inode block.\n");
		return -1;
	}
	memcpy(inode_block_buffer, &root_inode, sizeof(struct inode));
	if(bio_write(root_inode_block, inode_block_buffer) < 0){
		fprintf(stderr, "Error: Unable to write root inode. \n");
		return -1;
	}
	return 0;
}


/* 
 * FUSE file operations
 */
static void *rufs_init(struct fuse_conn_info *conn) {

	// Step 1a: If disk file is not found, call mkfs
	if (diskfile == -1){
		rufs_mkfs();
	}
	// Step 1b: If disk file is found, just initialize in-memory data structures
  	// and read superblock from disk
	
	//Currently no in_mem data structures so just read superblock from disk I suppose
	//I don't believe that rufs_mkfs inherently opens the diskfile, so open it just in case.
	if (diskfile == -1){
		if (dev_open(diskfile_path) < 0){
			fprintf(stderr, "Error: Unable to open disk file %s. \n", diskfile_path);
			return NULL;
		}
	}
	//read superblock
	struct superblock sb;
	if (bio_read(0, &sb) < 0) {
		fprintf(stderr, "Error: Unable to read superblock.\n");
		return NULL;
	}

	return NULL;
}

static void rufs_destroy(void *userdata) 
{
	dev_close();
}

static int rufs_getattr(const char *path, struct stat *stbuf) 
{
	struct inode inode;

    // Initialize stbuf to zeros
    memset(stbuf, 0, sizeof(struct stat));

    int ret = get_node_by_path(path, 0, &inode);
    if (ret < 0) 
    {
        fprintf(stderr, "Error: failed to get node\n");
        return -1;
    }

    // Fill in the stat structure based on inode information
    stbuf->st_mode  = inode.vstat.st_mode;   // File mode
    stbuf->st_nlink = inode.vstat.st_nlink;  // Link count
    stbuf->st_uid   = inode.vstat.st_uid;    // Owner UID
    stbuf->st_gid   = inode.vstat.st_gid;    // Group ID
    stbuf->st_size  = inode.vstat.st_size;   // Size in bytes
    stbuf->st_atime = inode.vstat.st_atime;  // Access time
    stbuf->st_mtime = inode.vstat.st_mtime;  // Modification time
    stbuf->st_ctime = inode.vstat.st_ctime;  // Change time

    return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) 
{
	struct inode inode;
    if (get_node_by_path(path, 0, &inode) < 0) 
    {
        fprintf(stderr, "Error: File does not exist\n");
        return -1;
    }

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct inode dir_inode;
    int ret = get_node_by_path(path, 0, &dir_inode);
    if (ret < 0)
    {
        fprintf(stderr, "Error: Directory not found\n");
        return -1;
    }

    if (!S_ISDIR(dir_inode.type)) 
    {
        fprintf(stderr, "Error: Not a directory\n");
        return -1;
    }

    int dirents_per_block = BLOCK_SIZE / sizeof(struct dirent);
    struct dirent dir_block[dirents_per_block];

    filler(buffer, ".", NULL, 0);
    filler(buffer, "..", NULL, 0);

    // Iterate over data blocks of the directory
    for (int i = 0; i < 16; i++) 
    {
        if (dir_inode.direct_ptr[i] == -1) continue;

        if (bio_read(dir_inode.direct_ptr[i], dir_block) < 0) 
        {
            fprintf(stderr, "Error: bio_read() failed\n");
            return -1;
        }

        for (int j = 0; j < dirents_per_block; j++) 
        {
            if (dir_block[j].valid == 1) 
            {
                filler(buffer, dir_block[j].name, NULL, 0);
            }
        }
    }

    return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) 
{
	char *path_copy = strdup(path);
    if (!path_copy) 
    {
        fprintf(stderr, "Error: Out of memory\n");
        return -1;
    }

    char *parent_path = strdup(path_copy);
    char *base_name = strdup(path_copy);
    if (!parent_path || !base_name) 
    {
        fprintf(stderr, "Error: Out of memory\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return -1;
    }

    dirname(parent_path);
    basename(base_name); 

    struct inode parent_inode;
    int ret = get_node_by_path(parent_path, 0, &parent_inode);
    if (ret < 0) 
    {
        fprintf(stderr, "Error: Parent directory does not exist\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return -1;
    }

    // Allocate a new inode for the new directory
    int new_ino = get_avail_ino();
    if (new_ino < 0) 
    {
        fprintf(stderr, "Error: No available inode\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return new_ino; 
    }

    // Allocate a data block for this directory
    int new_blk = get_avail_blkno();
    if (new_blk < 0) 
    {
        fprintf(stderr, "Error: No available block\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return new_blk;
    }

    // Initialize the new directory inode
    struct inode dir_inode;
    memset(&dir_inode, 0, sizeof(struct inode));
    dir_inode.ino   = new_ino;
    dir_inode.valid = 1;
    dir_inode.size  = 0; 
    dir_inode.type  = S_IFDIR | mode;
    dir_inode.link  = 2;

    for (int i = 0; i < 16; i++) 
    {
        dir_inode.direct_ptr[i] = -1;
        if (i < 8) dir_inode.indirect_ptr[i] = -1;
    }

    dir_inode.direct_ptr[0]  = new_blk;
    dir_inode.vstat.st_mode  = dir_inode.type;
    dir_inode.vstat.st_uid   = getuid();
    dir_inode.vstat.st_gid   = getgid();
    dir_inode.vstat.st_size  = dir_inode.size;
    time_t current_time      = time(NULL);
    dir_inode.vstat.st_atime = current_time;
    dir_inode.vstat.st_mtime = current_time;
    dir_inode.vstat.st_ctime = current_time;
    dir_inode.vstat.st_nlink = dir_inode.link;

    // Add the directory entry to the parent directory
    ret = dir_add(parent_inode, new_ino, base_name, strlen(base_name));
    if (ret < 0) 
    {
        fprintf(stderr, "Error: dir_add() failed\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return ret;
    }

    // Write the new directory inode to disk
    if (writei(new_ino, &dir_inode) < 0) 
    {
        fprintf(stderr, "Error: writei() failed\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return -1;
    }

    free(path_copy);
    free(parent_path);
    free(base_name);

    return 0;
}

static int rufs_rmdir(const char *path) { return 0; }

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) { return 0; }

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) 
{
    char *path_copy = strdup(path);
    if (!path_copy) 
    {
        fprintf(stderr, "Error: Out of memory\n");
        return -1;
    }

    char *parent_path = strdup(path_copy);
    char *base_name = strdup(path_copy);
    if (!parent_path || !base_name) 
    {
        fprintf(stderr, "Error: Out of memory\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return -1;
    }

    dirname(parent_path); 
    basename(base_name);

    struct inode parent_inode;
    int ret = get_node_by_path(parent_path, 0, &parent_inode);
    if (ret < 0) 
    {
        fprintf(stderr, "Error: Parent directory does not exist\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return -1;
    }

    // Allocate a new inode for the file
    int new_ino = get_avail_ino();
    if (new_ino < 0) 
    {
        fprintf(stderr, "Error: No available inode\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return new_ino;
    }

    struct inode file_inode;
    memset(&file_inode, 0, sizeof(struct inode));
    file_inode.ino   = new_ino;
    file_inode.valid = 1;
    file_inode.size  = 0;
    file_inode.type  = S_IFREG | mode;
    file_inode.link  = 1;

    for (int i = 0; i < 16; i++) 
    {
        file_inode.direct_ptr[i] = -1;
        if (i < 8) file_inode.indirect_ptr[i] = -1;
    }

    time_t current_time       = time(NULL);
    file_inode.vstat.st_mode  = file_inode.type;
    file_inode.vstat.st_nlink = file_inode.link;
    file_inode.vstat.st_uid   = getuid();
    file_inode.vstat.st_gid   = getgid();
    file_inode.vstat.st_size  = file_inode.size;
    file_inode.vstat.st_atime = current_time;
    file_inode.vstat.st_mtime = current_time;
    file_inode.vstat.st_ctime = current_time;

    // Add file entry to the parent directory
    if (dir_add(parent_inode, new_ino, base_name, strlen(base_name)) < 0) 
    {
        fprintf(stderr, "Error: dir_add() failed\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return -1;
    }

    // Write the file inode to disk
    if (writei(new_ino, &file_inode) < 0) 
    {
        fprintf(stderr, "Error: writei() failed\n");
        free(path_copy);
        free(parent_path);
        free(base_name);
        return -1;
    }

    free(path_copy);
    free(parent_path);
    free(base_name);
    return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) 
{
	struct inode inode;
    if (get_node_by_path(path, 0, &inode) < 0) 
    {
        fprintf(stderr, "Error: File does not exist\n");
        return -1;
    }

    return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) 
{
	struct inode inode;
    if (get_node_by_path(path, 0, &inode) < 0) 
    {
        fprintf(stderr, "Error: File does not exist\n");
        return -1;
    }

    // Make sure we do not read beyond the end of the file
    if ((size_t)offset >= inode.size) return 0;

    if (offset + size > inode.size) 
    {
        size = inode.size - offset; // Adjust size to read until EOF
    }

    int bytes_read = 0;
    int block_size = BLOCK_SIZE;
    char block_buf[BLOCK_SIZE];

    // Calculate which block to start from and the offset within it
    int start_block = offset / block_size;
    int start_offset = offset % block_size;

    // How many blocks to read fully or partially
    int end_block = (offset + size - 1) / block_size;

    for (int b = start_block; b <= end_block; b++) 
    {
        int block_num = inode.direct_ptr[b];
        if (block_num == -1) 
        {
            // Sparse or missing block - just fill with zeros
            memset(block_buf, 0, block_size);
        } 
        else 
        {
            if (bio_read(block_num, block_buf) < 0) 
            {
                fprintf(stderr, "Error: bio_read() failed\n");
                return -1;
            }
        }

        int copy_start = 0;
        int copy_len = block_size;

        if (b == start_block) 
        {
            copy_start = start_offset;
        }

        if (b == end_block) 
        {
            // The last block may end partway
            int end_offset = (offset + size) % block_size;
            if (end_offset == 0) 
            {
                end_offset = block_size; 
            }
            copy_len = end_offset - copy_start;
        } 
        else 
        {
            copy_len -= copy_start;
        }

        memcpy(buffer + bytes_read, block_buf + copy_start, copy_len);
        bytes_read += copy_len;
    }

    return bytes_read;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) 
{
	struct inode file_inode;
    if (get_node_by_path(path, 0, &file_inode) < 0) 
    {
        fprintf(stderr, "Error: File does not exist\n");
        return -1;
    }

    if (!S_ISREG(file_inode.type))
    {
        fprintf(stderr, "Error: Not a file\n");
        return -1;
    }

    int block_size = BLOCK_SIZE;
    int bytes_written = 0;

    int start_block = offset / block_size;
    int start_offset = offset % block_size;
    int end_block = (offset + size - 1) / block_size;

    // Check if we need to allocate additional blocks
    for (int b = start_block; b <= end_block; b++) 
    {
        if (file_inode.direct_ptr[b] == -1) 
        {
            int new_blk = get_avail_blkno();
            if (new_blk < 0) 
            {
                return new_blk; 
            }
            file_inode.direct_ptr[b] = new_blk;
        }
    }

    char block_buf[BLOCK_SIZE];
    size_t remaining = size;
    size_t current_offset = offset;

    for (int b = start_block; b <= end_block; b++) 
    {
        int block_num = file_inode.direct_ptr[b];
        if (block_num == -1) 
        {
            fprintf(stderr, "Error: Block not allocated\n");
            return -1;
        }

        if (bio_read(block_num, block_buf) < 0) 
        {
            fprintf(stderr, "Error: bio_read() failed\n");
            return -1;
        }

        int copy_start = (b == start_block) ? start_offset : 0;
        int copy_len = block_size - copy_start;
        if (copy_len > (int)remaining)  copy_len = (int)remaining;

        memcpy(block_buf + copy_start, buffer + bytes_written, copy_len);

        if (bio_write(block_num, block_buf) < 0) 
        {
            fprintf(stderr, "Error: bio_write() failed\n");
            return -1;
        }

        bytes_written += copy_len;
        remaining -= copy_len;
        current_offset += copy_len;
    }

    // Update file size if we wrote beyond the current size
    off_t new_end = offset + size;
    if ((size_t)new_end > file_inode.size) 
    {
        file_inode.size = new_end;
    }

    // Update timestamps
    time_t current_time = time(NULL);
    file_inode.vstat.st_mtime = current_time;
    file_inode.vstat.st_ctime = current_time;
    file_inode.vstat.st_size = file_inode.size;

    if (writei(file_inode.ino, &file_inode) < 0) 
    {
        fprintf(stderr, "Error: writei() failed\n");
        return -1;
    }

    return bytes_written;
}

static int rufs_unlink(const char *path) { return 0; }
static int rufs_truncate(const char *path, off_t size) { return 0; }
static int rufs_release(const char *path, struct fuse_file_info *fi) { return 0; }
static int rufs_flush(const char * path, struct fuse_file_info * fi) { return 0; }
static int rufs_utimens(const char *path, const struct timespec tv[2]) { return 0; }


static struct fuse_operations rufs_ope = {
	.init		= rufs_init,
	.destroy	= rufs_destroy,

	.getattr	= rufs_getattr,
	.readdir	= rufs_readdir,
	.opendir	= rufs_opendir,
	.releasedir	= rufs_releasedir,
	.mkdir		= rufs_mkdir,
	.rmdir		= rufs_rmdir,

	.create		= rufs_create,
	.open		= rufs_open,
	.read 		= rufs_read,
	.write		= rufs_write,
	.unlink		= rufs_unlink,

	.truncate   = rufs_truncate,
	.flush      = rufs_flush,
	.utimens    = rufs_utimens,
	.release	= rufs_release
};


int main(int argc, char *argv[]) {
	int fuse_stat;

	getcwd(diskfile_path, PATH_MAX);
	strcat(diskfile_path, "/DISKFILE");

	fuse_stat = fuse_main(argc, argv, &rufs_ope, NULL);

	return fuse_stat;
}
