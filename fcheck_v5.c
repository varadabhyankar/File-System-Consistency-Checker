#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <limits.h>
#include "types.h"
#include "fs.h"

#define T_DIR  1   // Directory
#define T_FILE 2   // File
#define T_DEV  3   // Device
#define BLOCK_SIZE (BSIZE)
#define DPB (BSIZE / sizeof(struct dirent))

// Bitset implementation to represent bitmap
#define BITSPERBLOCK (BSIZE * CHAR_BIT)
#define BITSPERBLOCK2 (sizeof(char) * 8)
typedef uint bitmap_t;

// Functions to check if specific bit is set in the bitmap
char bitarr[8] = { 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80 };
int is_bit_set(char *bitmapblocks, int blockaddr) {
    return (bitmapblocks[blockaddr / 8] & bitarr[blockaddr % 8]) != 0;
}
int is_bit_set2(char *bitmapblocks, int block_number) {
    return (bitmapblocks[block_number / BITSPERBLOCK2] & (1 << (block_number % BITSPERBLOCK2))) != 0;
}

//Superblock
struct superblock *sb;

//Stack for directory traversal
struct Stack {
    struct dinode* inode;
    uint block_address;
};

void check_rule1(struct dinode *inode, int num_inodes);
void check_rule2(struct dinode *inode, int num_inodes, char *mmap_image);
void check_rule3(struct dinode *inode, int num_inodes, struct dirent *dir);
void check_rule4(struct dinode *inode, int num_inodes, char *mmap_image);
void check_rule5(struct dinode *inode, int num_inodes, char *bitmap_blocks, char *mmap_image);
void check_rule6(struct dinode *inode, int num_inodes, char *bitmap_blocks, char *mmap_image, char *inode_blocks, int first_data_block);
void check_rule7(struct dinode *inode);
void check_rule8(struct dinode *inode, void *mmap_image);
void check_rule9(struct dinode *inode, int *inodemap, int i);
void check_rule10(struct dinode *inode, int *inodemap, int i);
void check_rule11(struct dinode *inode, int *inodemap, int i);
void check_rule12(struct dinode *inode, int *inodemap, int i);
void directory_traverse(char *inode_blocks, struct dinode *rootinode, int *inodemap, void *mmap_image, int max_inodes);

int main(int argc, char *argv[]) {

    // Check if correct arguments are provided
    if(argc < 2){
      fprintf(stderr, "Usage: fcheck <file_system_image>\n");
      exit(1);
    }

    struct stat fStat;
    int fs_image = open(argv[1], O_RDONLY);

    // Check if the file system image exists
    if(fs_image < 0 || fstat(fs_image, &fStat) < 0){
        fprintf(stderr,"image not found\n");
        exit(1);
    }

    // Memory map the file system image
    char *mmap_image = mmap(NULL, fStat.st_size, PROT_READ, MAP_PRIVATE, fs_image, 0);
    if (mmap_image == MAP_FAILED){
        exit(1);
    }

    // Set up pointers for different blocks in the file system
    sb = (struct superblock*)(mmap_image + 1*BLOCK_SIZE);
    uint num_inode_blocks = (sb->ninodes/(IPB))+1;
    uint num_bitmap_blocks = (sb->size/(BPB))+1;
    char *inode_blocks = (char *)(mmap_image + 2*BLOCK_SIZE);
    char *bitmap_blocks = (char *)(inode_blocks + num_inode_blocks*BLOCK_SIZE);
    uint first_data_block = num_inode_blocks + num_bitmap_blocks + 2;
    struct dinode *inode1, *inode_9_12, *rootinode;
    struct dirent *dirs;
    int i;
    
    // Create an array to keep track of inode references
    int inodemap[sb->ninodes];
    memset(inodemap, 0, sizeof(int)* sb->ninodes);
    
    // Allocate memory for directory entries
    dirs = (struct dirent *)malloc(BLOCK_SIZE / sizeof(struct dirent) * sizeof(struct dirent));
    // Set up pointers for inode
    inode1 = (struct dinode*)(inode_blocks);

    //Do the consistency checks
    check_rule1(inode1, sb->ninodes);
    check_rule2(inode1, sb->ninodes, mmap_image);
    check_rule3(inode1, sb->ninodes, dirs);
    check_rule4(inode1, sb->ninodes, mmap_image);
    check_rule5(inode1, sb->ninodes, bitmap_blocks, mmap_image);
    check_rule6(inode1, sb->ninodes, bitmap_blocks, mmap_image, inode_blocks, first_data_block);
    check_rule7(inode1);
    check_rule8(inode1, mmap_image);
    inode_9_12 = (struct dinode*)(inode_blocks);
    rootinode=++inode_9_12;
    inodemap[0]++;
    inodemap[1]++;
    directory_traverse(inode_blocks, rootinode, inodemap, mmap_image, sb->ninodes);
    inode_9_12++;
    for (i = 2; i < sb->ninodes; i++, inode_9_12++) {
        check_rule9(inode_9_12, inodemap, i);
        check_rule10(inode_9_12, inodemap, i);
        check_rule11(inode_9_12, inodemap, i);
        check_rule12(inode_9_12, inodemap, i);
    }
}

//Function for checking rule 1
void check_rule1(struct dinode *inode, int num_inodes) {
    int i;
    for (i = 0; i < num_inodes; i++) {
        if (inode->type != 0 &&
            inode->type != T_FILE &&
            inode->type != T_DIR &&
            inode->type != T_DEV) {
            fprintf(stderr, "ERROR: bad inode\n");
            exit(1);
        }
        inode++;
    }
}

//Helper function to check bad direct address in inode.
void check_bad_direct_address(struct dinode *inode, int num_inodes){
    int i;
    int j;
    for (i = 0; i < num_inodes; i++) {
        if (inode->type != 0) {
            for (j = 0; j < NDIRECT; j++) {
                if (inode->addrs[j] < 0 || inode->addrs[j] >= sb->size) {
                    fprintf(stderr, "ERROR: bad direct address in inode.\n");
                    exit(1);
                }
            }
        }
        inode++;
    }
}

//Helper function to bad indirect address in inode.
void check_bad_indirect_address(struct dinode *inode, int num_inodes, char* mmap_image){
    int i;
    int j;
    uint *indirect_block;
    for (i = 0; i < num_inodes; i++) {
        if (inode->type != 0) {
            indirect_block = (uint *)(mmap_image + inode->addrs[NDIRECT]*BLOCK_SIZE);
            for(j=0; j<NINDIRECT; j++){
                if(*indirect_block < 0 || *indirect_block >= sb->size){
                    fprintf(stderr,"ERROR: bad indirect address in inode.\n");
                    exit(1);
                }
                indirect_block++;
            }
        }
        inode++;
    }
    
}

// Function to check rule 2.
void check_rule2(struct dinode *inode, int num_inodes, char *mmap_image){
    
    check_bad_direct_address(inode, num_inodes);
    check_bad_indirect_address(inode, num_inodes, mmap_image);
}

//Function to check rule 3.
void check_rule3(struct dinode *inode, int num_inodes, struct dirent *dir){
    int i;
    //Checking root directory existence and properties
    for (i = 0; i < num_inodes; i++) {
        if(i == 1){
            if(inode->type != T_DIR){
                fprintf(stderr,"ERROR: root directory does not exist.\n");
                exit(1);
            }
            if(dir->inum == 1){
                if(strcmp(dir->name, ".") == 0 && inode->addrs[i] != 1){
                    fprintf(stderr,"ERROR: root directory does not exist.\n");
                    exit(1);
                }
            }
            
            break;
        }
        inode++;
    }
}

//Function to check rule 4.
void check_rule4(struct dinode *inode, int num_inodes, char *mmap_image) {
     int i;
     int j;
     struct dirent *dir;
     int found_dot, found_dotdot;
     
     //Implementation details for checking directory format
     for (i = 0; i < num_inodes; i++) {
        if (inode->type == T_DIR) {
            found_dotdot = found_dot = 0;
            for(j=0; j<NDIRECT; j++){
                dir = (struct dirent *)(mmap_image + inode->addrs[j]*BLOCK_SIZE);
                for(j=0; j<DPB; j++){
                    
                    if(!found_dot && strcmp(".", dir->name)==0){
                        found_dot=1;
                        if(dir->inum!=i){
                            fprintf(stderr,"ERROR: directory not properly formatted.\n");
                            exit(1);
                        }
                    }
                     //Check for . and ..
                    if(!found_dotdot && strcmp("..",dir->name)==0){
                        found_dotdot=1;
                        if((i!=1 && dir->inum==i) || (i==1 && dir->inum!=i)){
                            fprintf(stderr,"ERROR: root directory does not exist.\n");
                            exit(1);
                        }
                    }   
                    if(found_dot && found_dotdot) 
                        break;
                    dir++;
                }
                if(found_dot && found_dotdot) break;
            }
            if(!found_dot || !found_dotdot){
                fprintf(stderr,"ERROR: directory not properly formatted.\n");
                exit(1);
            }
        }
        inode++;
    }
}

////Function to check rule 5.
void check_rule5(struct dinode *inode, int num_inodes, char *bitmap_blocks, char *mmap_image){
    int i;
    int j;
    int k;
    int block_number;
    uint *indirect_block;
    
    for (i = 0; i < num_inodes; i++) {
        if(inode->type != 0){
            //Check each direct address
            for (j = 0; j < NDIRECT; j++) {
                block_number = inode->addrs[j];
                if (block_number > 0 && !is_bit_set(bitmap_blocks, block_number)) {
                    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                    exit(1);
                }
                
                if(j == NDIRECT){
                    
                }
            }
            //Check each indirect address
            indirect_block = (uint *)(mmap_image + inode->addrs[NDIRECT]*BLOCK_SIZE);
            for(k = 0; k < NINDIRECT; k++){
                if (*indirect_block > 0 && !is_bit_set(bitmap_blocks, *indirect_block)) {
                    fprintf(stderr, "ERROR: address used by inode but marked free in bitmap.\n");
                    exit(1);
                }
                indirect_block++;
            }
        }
        inode++;
    }
}

//Function to check rule 6.
void check_rule6(struct dinode *inode, int num_inodes, char *bitmap_blocks, char *mmap_image, char *inode_blocks, int first_data_block){
    int i, j;
    uint blockaddr;
    uint *indirect;
    int used_dbs[sb->nblocks];
    memset(used_dbs, 0, sb->nblocks * sizeof(int));

    //Checking block usage consistency
    for (i = 0; i < num_inodes; i++, inode++) {
        if (inode->type == 0) {
            continue;
        }
        for (j = 0; j < (NDIRECT + 1); j++) {
            blockaddr = inode->addrs[j];
            if (blockaddr == 0) {
                continue;
            }
            used_dbs[blockaddr - first_data_block] = 1;
            if (j == NDIRECT) {
                indirect = (uint *)(mmap_image + blockaddr * BLOCK_SIZE);
                for (int k = 0; k < NINDIRECT; k++, indirect++) {
                    blockaddr = *indirect;
                    if (blockaddr == 0) {
                        continue;
                    }
                    used_dbs[blockaddr - first_data_block] = 1;
                }
            }
        }
    }
    for (i = 0; i < sb->nblocks; i++) {
        blockaddr = (uint)(i + first_data_block);
        if (used_dbs[i] == 0 && is_bit_set(bitmap_blocks, blockaddr)) {
            fprintf(stderr, "ERROR: bitmap marks block in use but it is not in use.\n");
            exit(1);
        }
    }
}

//Function to check rule 7.
void check_rule7(struct dinode *inode){
    int i;
    int j;
    uint block_address;
    int direct_usage[sb->nblocks];
    memset(direct_usage, 0, sb->nblocks * sizeof(int));
    
    //Checking direct address usage
    for (i = 0; i < sb->ninodes; i++, inode++) {
        if (inode->type == 0) {
            continue;
        }
        for (j = 0; j < NDIRECT; j++) {
            block_address = inode->addrs[j];
            if (block_address > 0 && direct_usage[block_address] == 1) {
                fprintf(stderr, "ERROR: direct address used more than once.\n");
                exit(1);
            }
            direct_usage[block_address] = 1;
        }
    }
}

////Function to check rule 8.
void check_rule8(struct dinode *inode, void *mmap_image) {
    int i;
    int j;
    uint block_address;
    uint *indirect_usage;
    int indirect_blocks[sb->nblocks];
    memset(indirect_blocks, 0, sb->nblocks * sizeof(int));

    //Checking indirect address usage
    for (i = 0; i < sb->ninodes; i++, inode++) {
        if (inode->type == 0) {
            continue;
        }
 
        block_address = inode->addrs[NDIRECT];
 
        indirect_usage = (uint *)(mmap_image + block_address * BLOCK_SIZE);
        for (j = 0; j < NINDIRECT; j++) {
            block_address = *indirect_usage;
            if (block_address > 0 && indirect_blocks[block_address] == 1) {
                fprintf(stderr, "ERROR: indirect address used more than once.\n");
                exit(1);
            }
 
            indirect_blocks[block_address] = 1;
            indirect_usage++;
        }
    }
}

//Function to check rule 9.
void check_rule9(struct dinode *inode, int *inodemap, int i) {
    //Check if inodes marked in use are referred to in directories
    if (inode->type != 0 && inodemap[i] == 0) {
        fprintf(stderr, "ERROR: inode marked use but not found in a directory.\n");
        exit(1);
    }
}

//Function to check rule 10.
void check_rule10(struct dinode *inode, int *inodemap, int i) {
    //Check if inode numbers referred to in directories are marked in use
    if (inodemap[i] > 0 && inode->type == 0) {
        fprintf(stderr, "ERROR: inode referred to in directory but marked free.\n");
        exit(1);
    }
}

//Function to check rule 11.
void check_rule11(struct dinode *inode, int *inodemap, int i) {
    //Check reference counts for regular files
    if (inode->type == T_FILE && inode->nlink != inodemap[i]) {
        fprintf(stderr, "ERROR: bad reference count for file.\n");
        exit(1);
    }
}

//Function to check rule 12.
void check_rule12(struct dinode *inode, int *inodemap, int i) {
    //Check for extra links for directories
   if (inode->type == T_DIR && inodemap[i] > 1) {
        fprintf(stderr, "ERROR: directory appears more than once in file system.\n");
        exit(1);  
    }
}

//Helper function to traverse the directory and update inode references.
void directory_traverse(char* inode_blocks, struct dinode* rootinode, int* inodemap, void* mmap_image, int max_inodes) { 
    struct Stack* dir_stack = (struct Stack*)malloc(sizeof(struct Stack) * max_inodes);
    int top = -1;

    // Push the root directory on directory stack
    stack[++top].inode = rootinode;
    stack[top].block_address = 0;

    while (top >= 0) {
        struct dinode* curr_inode = dir_stack[top].inode;
        uint curr_block_add = dir_stack[top].block_address;
        top--;
        if (curr_inode->type == T_DIR) {
            uint baddr = curr_inode->addrs[curr_block_add];
            if (baddr != 0) {
                struct dirent* direc = (struct dirent*)(mmap_image + baddr * BLOCK_SIZE);
                for (int j = 0; j < DPB; j++, direc++) {
                    if (direc->inum != 0 && strcmp(direc->name, ".") != 0 && strcmp(direc->name, "..") != 0) {
                        struct dinode* inode = ((struct dinode*)(inode_blocks)) + direc->inum;
                        inodemap[direc->inum]++;
                        // Push the directory onto the stack
                        dir_stack[++top].inode = inode;
                        dir_stack[top].block_address = 0;
                    }
                }
            }
            curr_block_add++;

            if (curr_block_add < NDIRECT) {
                // Push the current directory back onto the stack with the updated block address
                dir_stack[++top].inode = curr_inode;
                dir_stack[top].block_address = curr_block_add;
            } 
            else {
                uint baddr = curr_inode->addrs[NDIRECT];
                if (baddr != 0) {
                    uint* indir = (uint*)(mmap_image + baddr * BLOCK_SIZE);
                    for (int i = 0; i < NINDIRECT; i++) {
                        baddr = *(indir + i);
                        if (baddr != 0) {
                            struct dirent* direc = (struct dirent*)(mmap_image + baddr * BLOCK_SIZE);
                            for (int j = 0; j < DPB; j++, direc++) {
                                if (direc->inum != 0 && strcmp(direc->name, ".") != 0 && strcmp(direc->name, "..") != 0) {
                                    struct dinode* inode = ((struct dinode*)(inode_blocks)) + direc->inum;
                                    inodemap[direc->inum]++;
                                    // Push the directory onto the stack
                                    dir_stack[++top].inode = inode;
                                    dir_stack[top].block_address = 0;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    free(dir_stack);
}