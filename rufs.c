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

	// Step 1: Read inode bitmap from disk
	
	// Step 2: Traverse inode bitmap to find an available slot

	// Step 3: Update inode bitmap and write to disk 
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
		fprintf(stderr, "Error, Unable to read inode block %d.\n");
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
		fprintf(stderr, "Error, Unable to read inode block %d.\n");
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

  // Step 1: Call readi() to get the inode using ino (inode number of current directory)

  // Step 2: Get data block of current directory from inode

  // Step 3: Read directory's data block and check each directory entry.
  //If the name matches, then copy directory entry to dirent structure

	return 0;
}

int dir_add(struct inode dir_inode, uint16_t f_ino, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and check each directory entry of dir_inode
	
	// Step 2: Check if fname (directory name) is already used in other entries

	// Step 3: Add directory entry in dir_inode's data block and write to disk

	// Allocate a new data block for this directory if it does not exist

	// Update directory inode

	// Write directory entry

	return 0;
}

int dir_remove(struct inode dir_inode, const char *fname, size_t name_len) {

	// Step 1: Read dir_inode's data block and checks each directory entry of dir_inode
	
	// Step 2: Check if fname exist

	// Step 3: If exist, then remove it from dir_inode's data block and write to disk

	return 0;
}

/* 
 * namei operation
 */
int get_node_by_path(const char *path, uint16_t ino, struct inode *inode) {
	
	// Step 1: Resolve the path name, walk through path, and finally, find its inode.
	// Note: You could either implement it in a iterative way or recursive way

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
	set_bitmap(&i_bitmap, 0);
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
	set_bitmap(&d_bitmap, 0);
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
		reutnr -1;
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
		return -1;
	}

	return NULL;
}

static void rufs_destroy(void *userdata) {

	// Step 1: De-allocate in-memory data structures
	// Currently we do not have in-memory data structures so no need to de-allocate right now
	// Step 2: Close diskfile
	dev_close();

}

static int rufs_getattr(const char *path, struct stat *stbuf) {

	// Step 1: call get_node_by_path() to get inode from path

	// Step 2: fill attribute of file into stbuf from inode

		stbuf->st_mode   = S_IFDIR | 0755;
		stbuf->st_nlink  = 2;
		time(&stbuf->st_mtime);

	return 0;
}

static int rufs_opendir(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

    return 0;
}

static int rufs_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: Read directory entries from its data blocks, and copy them to filler

	return 0;
}


static int rufs_mkdir(const char *path, mode_t mode) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target directory to parent directory

	// Step 5: Update inode for target directory

	// Step 6: Call writei() to write inode to disk
	

	return 0;
}

static int rufs_rmdir(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target directory name

	// Step 2: Call get_node_by_path() to get inode of target directory

	// Step 3: Clear data block bitmap of target directory

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target directory in its parent directory

	return 0;
}

static int rufs_releasedir(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_create(const char *path, mode_t mode, struct fuse_file_info *fi) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of parent directory

	// Step 3: Call get_avail_ino() to get an available inode number

	// Step 4: Call dir_add() to add directory entry of target file to parent directory

	// Step 5: Update inode for target file

	// Step 6: Call writei() to write inode to disk

	return 0;
}

static int rufs_open(const char *path, struct fuse_file_info *fi) {

	// Step 1: Call get_node_by_path() to get inode from path

	// Step 2: If not find, return -1

	return 0;
}

static int rufs_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {

	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: copy the correct amount of data from offset to buffer

	// Note: this function should return the amount of bytes you copied to buffer
	return 0;
}

static int rufs_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi) {
	// Step 1: You could call get_node_by_path() to get inode from path

	// Step 2: Based on size and offset, read its data blocks from disk

	// Step 3: Write the correct amount of data from offset to disk

	// Step 4: Update the inode info and write it to disk

	// Note: this function should return the amount of bytes you write to disk
	return size;
}

static int rufs_unlink(const char *path) {

	// Step 1: Use dirname() and basename() to separate parent directory path and target file name

	// Step 2: Call get_node_by_path() to get inode of target file

	// Step 3: Clear data block bitmap of target file

	// Step 4: Clear inode bitmap and its data block

	// Step 5: Call get_node_by_path() to get inode of parent directory

	// Step 6: Call dir_remove() to remove directory entry of target file in its parent directory

	return 0;
}

static int rufs_truncate(const char *path, off_t size) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_release(const char *path, struct fuse_file_info *fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
	return 0;
}

static int rufs_flush(const char * path, struct fuse_file_info * fi) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}

static int rufs_utimens(const char *path, const struct timespec tv[2]) {
	// For this project, you don't need to fill this function
	// But DO NOT DELETE IT!
    return 0;
}


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

