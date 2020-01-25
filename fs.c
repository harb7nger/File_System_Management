#include "fs.h"
#include "disk.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#define FS_MAGIC           0xf0f03410
#define INODES_PER_BLOCK   128
#define POINTERS_PER_INODE 5
#define POINTERS_PER_BLOCK 1024

struct fs_superblock {
    int magic;
    int nblocks;
    int ninodeblocks;
    int ninodes;
};

struct fs_inode {
    int isvalid;
    int size;
    int direct[POINTERS_PER_INODE];
    int indirect;
};

union fs_block {
    struct fs_superblock super;
    struct fs_inode inode[INODES_PER_BLOCK];
    int pointers[POINTERS_PER_BLOCK];
    char data[DISK_BLOCK_SIZE];
};

//Global variables
int is_mounted = 0;
int* free_block_bitmap;

int fs_format()
{

    if(is_mounted == 1) { 
        printf("disk already mounted\n");
        return 0;
    }

    //1. get disk size and compute inodeblocks, inodes
    //2. create super block and write to disk
    //3. create Inode blocks and write to disk
    //4. Clear all data blocks and write to disk
  
    //get disk size and compute inodeblocks, inodes
    union fs_block block;
    int nblocks, ninodeblocks, ninodes;
    nblocks = disk_size();
    ninodeblocks = nblocks/10 + 1;
    ninodes = ninodeblocks * INODES_PER_BLOCK;

    //Create super block
    //1. set magic number
    //2. set nblocks
    //3. set inodeblocks
    //4. set inodes
    block.super.magic = FS_MAGIC;
    block.super.nblocks = nblocks;
    block.super.ninodeblocks = ninodeblocks;
    block.super.ninodes = ninodes;
    disk_write(0,block.data);

    //create Inode blocks and write to disk
    for(int i=1;i<=ninodeblocks;i++) {
        memset(block.data,0,DISK_BLOCK_SIZE);
        disk_write(i,block.data);
    } 

    //Clear all data blocks and write to disk
    for(int i=ninodeblocks+1;i<nblocks;i++) {
        memset(block.data,0,DISK_BLOCK_SIZE);
        disk_write(i,block.data);
    } 

    return 1;
}

//Function to traverse disk
//Argument: int 
//          0 : print debug statement
//          1 : set free_block_bitmap
//          2 : find free inode 
int traverse_disk(int option) {

    union fs_block block;
    int ninodeblocks;

    disk_read(0,block.data);

    //1. Check if magic number is valid
    if (block.super.magic != FS_MAGIC) {

        printf("ERROR: Magic number not set. Call format\n");
        abort();
    }

    if (option == 0) {
        printf("superblock:\n");
        printf("    %d blocks\n",block.super.nblocks);
        printf("    %d inode blocks\n",block.super.ninodeblocks);
        printf("    %d inodes\n",block.super.ninodes);
    }

    ninodeblocks = block.super.ninodeblocks;

    if (option == 1) {
        free_block_bitmap[0] = 1;
    }

    for(int i=1;i<=ninodeblocks;i++) {
        disk_read(i,block.data);
        if(option == 1) {
            free_block_bitmap[i] = 1;
        }
        for(int j=0;j<INODES_PER_BLOCK;j++) {
            if(block.inode[j].isvalid == 1) {
                if(option == 0) {
                    printf("INode : %d\n",((i-1)*INODES_PER_BLOCK + j));
                    printf("    Size = %d bytes\n",block.inode[j].size);
                    printf("    Direct Blocks = ");
                }
                for(int k=0;k<POINTERS_PER_INODE;k++) {
                    if(block.inode[j].direct[k] != 0) {
                        if(option == 0) {
                            printf("%d ",block.inode[j].direct[k]);
                        }
                        else if (option == 1) {
                            free_block_bitmap[block.inode[j].direct[k]] = 1;
                        }
                    }
                }
                if(option == 0) {
                    printf("\n");
                }
                if (block.inode[j].indirect != 0) {
                    if(option == 0) {
                        printf("    Indirect Block = %d\n",block.inode[j].indirect);
                        printf("    Indirect data blocks = ");
                    }
                    else if (option == 1) {
                        free_block_bitmap[block.inode[j].indirect] = 1;
                    }
                    union fs_block indirect_block;
                    disk_read(block.inode[j].indirect,indirect_block.data);
                    for(int k=0;k<POINTERS_PER_BLOCK;k++) {
                        if(indirect_block.pointers[k] != 0) { 
                            if(option == 0) {
                                printf("%d ",indirect_block.pointers[k]);
                            }
                            else if(option == 1) {
                                free_block_bitmap[indirect_block.pointers[k]] = 1;
                            }
                        }
                    }
                    if(option == 0) {
                        printf("\n");
                    }
                }
            }
            else {
                if (option == 2) {
                    return (i-1)*INODES_PER_BLOCK + j; 
                }
            }
        }
    }

    return -1;

}

void inode_print(int inumber, struct fs_inode *inode) {

    printf("INode : %d\n",inumber);
    printf("    Size = %d bytes\n",inode->size);
    printf("    Direct Blocks = ");
    for(int k=0;k<POINTERS_PER_INODE;k++) {
        if(inode->direct[k] != 0) {
            printf("%d ",inode->direct[k]);
        }
    }
    printf("\n");
    if (inode->indirect != 0) {
        printf("    Indirect Block = %d\n",inode->indirect);
        printf("    Indirect data blocks = ");
        union fs_block indirect_block;
        disk_read(inode->indirect,indirect_block.data);
        for(int k=0;k<POINTERS_PER_BLOCK;k++) {
            if(indirect_block.pointers[k] != 0) { 
                printf("%d ",indirect_block.pointers[k]);
            }
        }
        printf("\n");
   }
 
}

void inode_copy(struct fs_inode *src, struct fs_inode *dest) {

    dest->isvalid = src->isvalid;
    dest->size = src->size;
    for(int i=0;i<POINTERS_PER_INODE;i++) {
        dest->direct[i] = src->direct[i];
    }
    dest->indirect = src->indirect;
}

void inode_load( int inumber, struct fs_inode *inode ) { 

    //1. Check if inumber is a valid number
    //2. Compute block number and offset
    //3. read block
    //3. copy inode

    union fs_block block;
    disk_read(0,block.data);

    if ((inumber < 0) || (inumber >= block.super.ninodes)) {
        printf("Invalid Inumber : %d\n",inumber);
        inode = NULL;
        return;
    }

    int block_num = (inumber / INODES_PER_BLOCK) + 1;
    int offset = inumber % INODES_PER_BLOCK;

    disk_read(block_num,block.data);
    inode_copy(&block.inode[offset],inode);

}

void inode_save( int inumber, struct fs_inode *inode ) { 
    //1. Check if inumber is a valid number
    //2. Compute block number and offset
    //3. read block
    //4. copy inode
    //5. save block

    union fs_block block;
    disk_read(0,block.data);

    if ((inumber < 0) || (inumber >= block.super.ninodes)) {
        printf("Invalid Inumber : %d\n",inumber);
        inode = NULL;
        return;
    }

    int block_num = (inumber / INODES_PER_BLOCK) + 1;
    int offset = inumber % INODES_PER_BLOCK;

    disk_read(block_num,block.data);
    inode_copy(inode,&block.inode[offset]);
    disk_write(block_num,block.data);


}

void update_inode(struct fs_inode* inode) {

    //Get the num of blocks till which inode was written
    int bn = inode->size/DISK_BLOCK_SIZE;
    printf("update_inode: bn = %d\n",bn);

    //Increment num blocks if inode->size is not a multiple of block size length
    if (inode->size%DISK_BLOCK_SIZE > 0) {
        bn = bn+1;
    }

    if(bn < POINTERS_PER_INODE) {
        for (int i=bn;i<POINTERS_PER_INODE;i++) { 
            if(inode->direct[i] > 0) {

                union fs_block block;
                memset(block.data,0,DISK_BLOCK_SIZE);
                disk_write(inode->direct[i],block.data);

                free_block_bitmap[inode->direct[i]] = 0;
                inode->direct[i] = 0;
            }
        }

        if(inode->indirect > 0) {
            union fs_block indirect_block;
            disk_read(inode->indirect,indirect_block.data);

            for (int i=0;i<POINTERS_PER_BLOCK;i++) { 
                if (indirect_block.pointers[i] > 0) {

                   union fs_block block;
                   memset(block.data,0,DISK_BLOCK_SIZE);
                   disk_write(indirect_block.pointers[i],block.data);

                   free_block_bitmap[indirect_block.pointers[i]] = 0;
                   indirect_block.pointers[i] = 0;
                }
            }
            memset(indirect_block.data,0,DISK_BLOCK_SIZE);
            disk_write(inode->indirect,indirect_block.data);
            free_block_bitmap[inode->indirect] = 0;
            inode->indirect = 0;
        }
    }
    else {
        if(inode->indirect > 0) {

            union fs_block indirect_block;
            disk_read(inode->indirect,indirect_block.data);

            for (int i=bn-POINTERS_PER_INODE;i<POINTERS_PER_BLOCK;i++) { 
                if (indirect_block.pointers[i] > 0) {

                   union fs_block block;
                   memset(block.data,0,DISK_BLOCK_SIZE);
                   disk_write(indirect_block.pointers[i],block.data);

                   free_block_bitmap[indirect_block.pointers[i]] = 0;
                   indirect_block.pointers[i] = 0;
                }
            }
            disk_write(inode->indirect,indirect_block.data);
        }
    }
    
}


void fs_debug()
{

    if(is_mounted == 0) { 
        printf("disk not mounted\n");
        printf("superblock:\n");
        printf("    0 blocks\n");
        printf("    0 inode blocks\n");
        printf("    0 inodes\n");
        return;
    }

    traverse_disk(0);

    //Print free_block_bitmap
    union fs_block block;
    disk_read(0,block.data);
    int nblocks = block.super.nblocks;
    printf("\nFree Block BitMap\n");
    for(int i=0;i<nblocks;i++) {
        printf("Block %d : %d\n",i,free_block_bitmap[i]);
    }

}

int fs_mount()
{
    //1. Check disk for magic number
    //2. Traverse disk and malloc and populate free block bitmap
    //3. Set is_mounted = 1
    
    //Check disk for magic number
    union fs_block block;
    int nblocks;

    disk_read(0,block.data);

    if (block.super.magic != FS_MAGIC) {
        printf("File System doesn't exist\n");
        return 0;
    }

    //Get disk size
    nblocks = block.super.nblocks;

    //Malloc free block bitmap
    free_block_bitmap = (int*)malloc(nblocks*sizeof(int));
    memset(free_block_bitmap,0,nblocks*sizeof(int));

    //Traverse disk and malloc and populate free block bitmap
    traverse_disk(1);

    //Set is_mounted
    is_mounted = 1;

    return 1; 

}

int fs_create()
{
    //1. Check which inode is free
    //2. Set isvalid = 1 and size = 0
    //3. Return inode number
    
    //Get free inode number
    int inode_number = traverse_disk(2);
    if(inode_number == -1) {
        printf("No free Inode\n");
        return -1;
    }

    //Load the inode
    struct fs_inode* inode = (struct fs_inode*)malloc(sizeof(struct fs_inode)); 
    inode_load(inode_number,inode);

    //Set isvalid and size
    inode->isvalid = 1;
    inode->size = 0;

    //Save inode to disk
    inode_save(inode_number,inode);

    return inode_number;
}

int fs_delete( int inumber )
{

    struct fs_inode* inode = (struct fs_inode*)malloc(sizeof(struct fs_inode)); 
    inode_load(inumber,inode);

    if (inode->isvalid == 0)
        return 0;

    inode->size = 0;
    update_inode(inode);

    inode->isvalid = 0;
    inode_save(inumber,inode);

    return 1;
}

int fs_getsize( int inumber )
{

    struct fs_inode* inode = (struct fs_inode*)malloc(sizeof(struct fs_inode)); 
    inode_load(inumber,inode);

    if (inode->isvalid == 0)
        return -1;

    return inode->size;
}

int getFreeBlock() {

    union fs_block block;
    disk_read(0,block.data);
    int nblocks = block.super.nblocks;

    for(int i=0;i<nblocks;i++) {
        if (free_block_bitmap[i] == 0) {
            free_block_bitmap[i] = 1;
            return i;
        }
    }

    return -1;

}

int getBlockNumber(struct fs_inode* inode, int offset){
    
   if (inode->size <= offset) 
       return -1;

   int bn = offset/DISK_BLOCK_SIZE;
   if(bn > POINTERS_PER_INODE + POINTERS_PER_BLOCK)
        return -1;
    if(bn < POINTERS_PER_INODE) {
        if (inode->direct[bn] > 0)
            return inode->direct[bn];
        else
            return -1;
    }
    else {
        union fs_block indirect_block;
        disk_read(inode->indirect,indirect_block.data);
        if (indirect_block.pointers[bn-POINTERS_PER_INODE] > 0)
            return indirect_block.pointers[bn-POINTERS_PER_INODE];
        else 
            return -1;
    }
}

int getBlockIndex(int offset){
    return offset % DISK_BLOCK_SIZE;
}

int insertBlockinInode(struct fs_inode* inode, int block_n, int offset) {

    int index = offset/DISK_BLOCK_SIZE;
    if(index > POINTERS_PER_INODE + POINTERS_PER_BLOCK)
        return -1;
    if(index < POINTERS_PER_INODE) {
        inode->direct[index] = block_n;
    }
    else {
        if(inode->indirect > 0) {
            union fs_block indirect_block;
            disk_read(inode->indirect,indirect_block.data);
            indirect_block.pointers[index-POINTERS_PER_INODE] = block_n;
            disk_write(inode->indirect,indirect_block.data);
        }
        else {
            if(index != POINTERS_PER_INODE) {
                printf("Block index should be %d",POINTERS_PER_INODE);
                exit(0);
            }
            int indirect_block_n = getFreeBlock(); 
        if (indirect_block_n == -1) {
        printf("Disk Full. Cannot allocate indirect_block\n");
                return -1;
            }
            union fs_block indirect_block;
            memset(indirect_block.data,0,DISK_BLOCK_SIZE);
            indirect_block.pointers[0] = block_n;
            disk_write(indirect_block_n,indirect_block.data);
            inode->indirect = indirect_block_n;
        }
    }
    return 1;
}


int fs_read( int inumber, char *data, int length, int offset )
{
    //TODO:
    //1. Get inode from inumber
    //2. if inode is invalid return 0
    //3. Compute block number from offset
    //4. Check if multiple blocks needs to be read (calculate from length)
    //      - Length can be more than inode->size. Should account for this
    //5. Read blocks
    //6. Store in data
    //7. Return number of bytes read
    
    //Inode from inumber
    struct fs_inode* inode = (struct fs_inode*)malloc(sizeof(struct fs_inode)); 
    inode_load(inumber,inode);
    if(inode->isvalid==0)
        return 0;
    
    
    //Reading blocks
    int bytes_to_read;
    int index = 0;
    int bytes_read=0;

    if(inode->size < offset + length) {
        bytes_to_read = inode->size - offset;
    }
    else {
        bytes_to_read = length;
    }

    while(bytes_to_read>0){
        
        int block_n = getBlockNumber(inode, offset);
        if(block_n == -1){
           return bytes_read;
        }
        union fs_block block;
        disk_read(block_n,block.data);
        int bstart = getBlockIndex(offset);
        if(bytes_to_read > DISK_BLOCK_SIZE-bstart){
            strncpy(data+index,block.data + bstart,DISK_BLOCK_SIZE-bstart);
            bytes_read += DISK_BLOCK_SIZE-bstart;
            index += DISK_BLOCK_SIZE-bstart;
        }
        else {
            strncpy(data+index,block.data + bstart,bytes_to_read);
            bytes_read += bytes_to_read;
            index += bytes_to_read;
        }
        offset+=DISK_BLOCK_SIZE-bstart;
        bytes_to_read-=DISK_BLOCK_SIZE-bstart;
    }

    return bytes_read;
}

int fs_write( int inumber, const char *data, int length, int offset )
{
    //TODO:
    //1. Get inode from inumber
    //2. if inode is invalid return 0
    //3. Compute block number from offset
    //4. Check if multiple blocks needs to be written (calculate from length)
    //5. Write blocks
    //6. Store in disk
    //7. Return number of bytes written
    
    //Inode from inumber
    struct fs_inode* inode = (struct fs_inode*)malloc(sizeof(struct fs_inode)); 
    inode_load(inumber,inode);
    if(inode->isvalid==0)
        return 0;
    
    int reset_inode_size = 0;
    if (offset == 0)
        reset_inode_size = 1;

    //Writing blocks
    int bytes_to_write = length;
    int index = 0;
    int bytes_written=0;
    while(bytes_to_write>0){
        
        int block_n = getBlockNumber(inode, offset);
        if(block_n == -1) {
            block_n = getFreeBlock(); 
            if (block_n == -1) {
                printf("Disk Full. Cannot allocate data block\n");
                break;
            }
            int inserted = insertBlockinInode(inode,block_n,offset); //Error checking?
            if (inserted == -1) {
                free_block_bitmap[block_n] = 0;
                break;
            } 
        }
	printf("block = %d\n",block_n);
        union fs_block block;
        disk_read(block_n,block.data);
        int bstart = getBlockIndex(offset);
        if(bytes_to_write > DISK_BLOCK_SIZE-bstart){
            strncpy(block.data + bstart,data+index,DISK_BLOCK_SIZE-bstart);
            bytes_written += DISK_BLOCK_SIZE-bstart;
            index += DISK_BLOCK_SIZE-bstart;
        }
        else {
            strncpy(block.data + bstart,data+index,bytes_to_write);
            bytes_written += bytes_to_write;
            index += bytes_to_write;
        }
        disk_write(block_n,block.data);

        //For the last block, bytes_to_write will become -ve/0 and while will be exited
        offset+=DISK_BLOCK_SIZE-bstart;
        bytes_to_write-=DISK_BLOCK_SIZE-bstart;
    }

    if(bytes_written == 0) {
        printf("Bytes written = 0\n");
    }
    if (reset_inode_size == 1)
        inode->size = 0;
    inode->size += bytes_written;
    update_inode(inode);
    inode_save(inumber,inode);

    return bytes_written;

}
