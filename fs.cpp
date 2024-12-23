#include "screen.h"
#include "fs.h"
#include "simpleOSlibc.h"
#include "constants.h"
#include "x86.h"
#include "vm.h"
#include "file.h"

void diskStatusCheck()
{
    // checks disk status and loops if not ready
    while (((inputIOPort(PRIMARY_ATA_COMMAND_STATUS_REGISTER) & 0xC0) != 0x40))
    {
    }
}

void diskReadSector(int sectorNumber, char *destinationMemory)
{
    // ASSIGNMENT 2 TO DO

    // 1. Perform a diskStatusCheck() call
    //   Make sure the disk is ready for I/O operations
    diskStatusCheck();

    // 2. Set the primary ATA sector count to 1 sector
    //   We want to read exactly 1 sector
    outputIOPort(PRIMARY_ATA_SECTOR_COUNT_REGISTER, 0x01);

    // 3. Set the low-byte and mid-byte sector bytes (using masks and shifts) from sectorNumber
    //   Break down the sectorNumber into bytes for transmission
    //   Load these bytes into their respective ATA registers
    outputIOPort(PRIMARY_ATA_SECTOR_LOWBYTE_NUMBER, sectorNumber & 0x000000FF);
    outputIOPort(PRIMARY_ATA_SECTOR_MIDBYTE_NUMBER, (sectorNumber >> 8) & 0x000000FF);

    // 4. Set the high-byte sector number to zero
    outputIOPort(PRIMARY_ATA_SECTOR_HIGHBYTE_NUMBER, 0x00);

    // 5. Set the primary ATA driver header register to 0xE0
    outputIOPort(PRIMARY_ATA_DRIVE_HEADER_REGISTER, 0xE0);

    // 6. Issue the read command (ATA_READ) to initiate the sector read operation
    outputIOPort(PRIMARY_ATA_COMMAND_STATUS_REGISTER, ATA_READ);

    // 7. Perform another diskStatusCheck() call
    diskStatusCheck();

    // 8. Perform an ioPortWordToMem() call to the correct destination memory and for the number of words
    //   appropriate for the sector size
    ioPortWordToMem(PRIMARY_ATA_DATA_REGISTER, destinationMemory, SECTOR_SIZE / 2);
}

void diskWriteSector(int sectorNumber, char *sourceMemory)
{
    // ASSIGNMENT 2 TO DO

    // 1. Perform a diskStatusCheck() call
    //   Make sure the disk is ready for I/O operations
    diskStatusCheck();

    // 2. Set the primary ATA sector count to 1 sector
    //   We want to read exactly 1 sector
    outputIOPort(PRIMARY_ATA_SECTOR_COUNT_REGISTER, 0x01);

    // 3. Set the low-byte and mid-byte sector bytes (using masks and shifts) from sectorNumber
    //   Break down the sectorNumber into bytes for transmission
    //   Load these bytes into their respective ATA registers
    outputIOPort(PRIMARY_ATA_SECTOR_LOWBYTE_NUMBER, sectorNumber & 0x000000FF);
    outputIOPort(PRIMARY_ATA_SECTOR_MIDBYTE_NUMBER, (sectorNumber >> 8) & 0x000000FF);

    // 4. Set the high-byte sector number to zero
    outputIOPort(PRIMARY_ATA_SECTOR_HIGHBYTE_NUMBER, 0x00);

    // 5. Set the primary ATA driver header register to 0xE0
    outputIOPort(PRIMARY_ATA_DRIVE_HEADER_REGISTER, 0xE0);

    // 6. Issue the write command (ATA_WRITE) to initiate the sector write operation
    outputIOPort(PRIMARY_ATA_COMMAND_STATUS_REGISTER, ATA_WRITE);

    // 7. Perform another diskStatusCheck() call
    diskStatusCheck();

    // 8. Perform an ioPortWordToMem() call to the correct source memory and for the number of words
    //   appropriate for the sector size
    ioPortWordToMem(PRIMARY_ATA_DATA_REGISTER, sourceMemory, SECTOR_SIZE / 2);
}

void readBlock(int blockNumber, char *destinationMemory)
{
    for (int firstSector = 0; firstSector < 2; firstSector++)
    {
        diskReadSector(((blockNumber * 2) + EXT2_SECTOR_START), destinationMemory);
        diskReadSector(((blockNumber * 2) + EXT2_SECTOR_START + 1), (char *)((int)destinationMemory + SECTOR_SIZE));
    }
}

void writeBlock(int blockNumber, char *sourceMemory)
{
    for (int firstSector = 0; firstSector < 2; firstSector++)
    {
        diskWriteSector(((blockNumber * 2) + EXT2_SECTOR_START), sourceMemory);
        diskWriteSector(((blockNumber * 2) + EXT2_SECTOR_START + 1), (char *)((int)sourceMemory + SECTOR_SIZE));
    }
}

unsigned int allocateFreeBlock()
{
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);
    readBlock(BlockGroupDescriptor->bgd_block_address_of_block_usage, (char *)EXT2_BLOCK_USAGE_MAP);

    unsigned int lastUsedBlock = 0;
    unsigned int blockNumber = 0;
    unsigned int valueToWrite = 0;

    while (blockNumber < 0xF0) // Max byte of block bit flag for this 2 MB file system
    {
        if (*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)1)
        {
            lastUsedBlock++;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)3))
        {
            lastUsedBlock = lastUsedBlock + 2;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)7))
        {
            lastUsedBlock = lastUsedBlock + 3;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)15))
        {
            lastUsedBlock = lastUsedBlock + 4;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)31))
        {
            lastUsedBlock = lastUsedBlock + 5;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)63))
        {
            lastUsedBlock = lastUsedBlock + 6;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)127))
        {
            lastUsedBlock = lastUsedBlock + 7;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)255))
        {
            lastUsedBlock = lastUsedBlock + 8;
        }
        else if (*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)0)
        {
            break;
        }
        blockNumber++;
    }

    lastUsedBlock++;

    if ((lastUsedBlock % 8) == (unsigned char)1)
    {
        valueToWrite = (unsigned char)1;
    }
    else if ((lastUsedBlock % 8) == (unsigned char)2)
    {
        valueToWrite = (unsigned char)3;
    }
    else if ((lastUsedBlock % 8) == (unsigned char)3)
    {
        valueToWrite = (unsigned char)7;
    }
    else if ((lastUsedBlock % 8) == (unsigned char)4)
    {
        valueToWrite = (unsigned char)15;
    }
    else if ((lastUsedBlock % 8) == (unsigned char)5)
    {
        valueToWrite = (unsigned char)31;
    }
    else if ((lastUsedBlock % 8) == (unsigned char)6)
    {
        valueToWrite = (unsigned char)63;
    }
    else if ((lastUsedBlock % 8) == (unsigned char)7)
    {
        valueToWrite = (unsigned char)127;
    }
    else if ((lastUsedBlock % 8) == (unsigned char)0)
    {
        valueToWrite = (unsigned char)255;
    }

    *(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) = (unsigned char)valueToWrite;

    writeBlock(BlockGroupDescriptor->bgd_block_address_of_block_usage, (char *)EXT2_BLOCK_USAGE_MAP);

    return (unsigned int)lastUsedBlock;
}

unsigned int readNextAvailableBlock()
{
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);
    readBlock(BlockGroupDescriptor->bgd_block_address_of_block_usage, (char *)EXT2_BLOCK_USAGE_MAP);

    unsigned int lastUsedBlock = 0;
    unsigned int blockNumber = 0;

    while (blockNumber < 0xF0) // Max byte of block bit flag for this 2 MB file system
    {
        if (*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)1)
        {
            lastUsedBlock++;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)3))
        {
            lastUsedBlock = lastUsedBlock + 2;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)7))
        {
            lastUsedBlock = lastUsedBlock + 3;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)15))
        {
            lastUsedBlock = lastUsedBlock + 4;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)31))
        {
            lastUsedBlock = lastUsedBlock + 5;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)63))
        {
            lastUsedBlock = lastUsedBlock + 6;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)127))
        {
            lastUsedBlock = lastUsedBlock + 7;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)255))
        {
            lastUsedBlock = lastUsedBlock + 8;
        }
        else if (*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)0)
        {
            break;
        }
        blockNumber++;
    }

    lastUsedBlock++;

    return (unsigned int)lastUsedBlock;
}

unsigned int readTotalBlocksUsed()
{
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);
    readBlock(BlockGroupDescriptor->bgd_block_address_of_block_usage, (char *)EXT2_BLOCK_USAGE_MAP);

    unsigned int blocksInUse = 0;
    unsigned int blockNumber = 0;

    while (blockNumber < 0xF0) // Max byte of block bit flag for this 2 MB file system
    {
        if (*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)1)
        {
            blocksInUse++;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)3))
        {
            blocksInUse = blocksInUse + 2;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)7))
        {
            blocksInUse = blocksInUse + 3;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)15))
        {
            blocksInUse = blocksInUse + 4;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)31))
        {
            blocksInUse = blocksInUse + 5;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)63))
        {
            blocksInUse = blocksInUse + 6;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)127))
        {
            blocksInUse = blocksInUse + 7;
        }
        else if (((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockNumber) == (unsigned char)255))
        {
            blocksInUse = blocksInUse + 8;
        }
        blockNumber++;
    }

    return (unsigned int)blocksInUse;
}

void freeBlock(unsigned int blockNumber)
{
    unsigned int blockGroupByte = blockNumber / 8;
    unsigned int blockGroupBit = blockNumber % 8;
    unsigned int valueToWrite = 0;

    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);
    readBlock(BlockGroupDescriptor->bgd_block_address_of_block_usage, (char *)EXT2_BLOCK_USAGE_MAP);

    if (blockGroupBit == (unsigned char)1)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 1;
    }
    else if (blockGroupBit == (unsigned char)2)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 2;
    }
    else if (blockGroupBit == (unsigned char)3)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 4;
    }
    else if (blockGroupBit == (unsigned char)4)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 8;
    }
    else if (blockGroupBit == (unsigned char)5)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 16;
    }
    else if (blockGroupBit == (unsigned char)6)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 32;
    }
    else if (blockGroupBit == (unsigned char)7)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 64;
    }
    else if (blockGroupBit == (unsigned char)0)
    {
        valueToWrite = ((unsigned char)*(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte)) - 128;
    }

    *(char *)(EXT2_BLOCK_USAGE_MAP + blockGroupByte) = (unsigned char)valueToWrite;

    writeBlock(BlockGroupDescriptor->bgd_block_address_of_block_usage, (char *)EXT2_BLOCK_USAGE_MAP);
}

void freeAllBlocks(char *inodeStructMemory)
{
    struct inode *Inode = (struct inode *)inodeStructMemory;

    for (int x = 0; x < EXT2_NUMBER_OF_DIRECT_BLOCKS; x++)
    {
        if (x < EXT2_NUMBER_OF_DIRECT_BLOCKS && Inode->i_block[x] != 0)
        {
            freeBlock(Inode->i_block[x]);
        }
    }
    if (Inode->i_block[EXT2_FIRST_INDIRECT_BLOCK] && Inode->i_block[EXT2_FIRST_INDIRECT_BLOCK] != 0)
    {
        readBlock(Inode->i_block[EXT2_FIRST_INDIRECT_BLOCK], EXT2_INDIRECT_BLOCK);
        int *indirectBlock = (int *)EXT2_INDIRECT_BLOCK;

        for (int y = 0; y < 256; y++)
        {
            if (indirectBlock[y] != 0)
            {
                freeBlock(indirectBlock[y]);
            }
        }
    }
}

void deleteFile(char *fileName, int currentPid)
{
    char *inodePage = requestAvailablePage(currentPid, PG_USER_PRESENT_RW);
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);

    fsFindFile(fileName, inodePage);

    if (!fsFindFile(fileName, inodePage))
    {
        // File not found
        return;
    }
    freeAllBlocks(inodePage);

    // Load all inodes of a directory up to max number of files per directory. This requires 16KB of memory.
    for (int blocksOfInodes = 0; blocksOfInodes < (MAX_FILES_PER_DIRECTORY / INODES_PER_BLOCK); blocksOfInodes++)
    {
        readBlock(BlockGroupDescriptor->bgd_starting_block_of_inode_table + blocksOfInodes, (char *)(int)EXT2_TEMP_INODE_STRUCTS + (BLOCK_SIZE * blocksOfInodes));
    }

    // Zero out the inode
    fillMemory((EXT2_TEMP_INODE_STRUCTS + ((returnInodeofFileName(fileName) - 1) * INODE_SIZE)), 0x0, INODE_SIZE);

    for (int blocksOfInodes = 0; blocksOfInodes < (MAX_FILES_PER_DIRECTORY / INODES_PER_BLOCK); blocksOfInodes++)
    {
        writeBlock(BlockGroupDescriptor->bgd_starting_block_of_inode_table + blocksOfInodes, (char *)(int)EXT2_TEMP_INODE_STRUCTS + (BLOCK_SIZE * blocksOfInodes));
    }

    deleteDirectoryEntry(fileName);
}

unsigned int allocateInode()
{
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);
    readBlock(BlockGroupDescriptor->bgd_block_address_of_inode_usage, (char *)EXT2_INODE_USAGE_MAP);

    unsigned int lastUsedInode = 0;
    unsigned int inodeNumber = 0;
    unsigned int valueToWrite = 0;

    while (inodeNumber < 0x1E) // Max byte of inode bit flag for this 2 MB file system
    {
        if (*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)1)
        {
            lastUsedInode++;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)3))
        {
            lastUsedInode = lastUsedInode + 2;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)7))
        {
            lastUsedInode = lastUsedInode + 3;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)15))
        {
            lastUsedInode = lastUsedInode + 4;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)31))
        {
            lastUsedInode = lastUsedInode + 5;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)63))
        {
            lastUsedInode = lastUsedInode + 6;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)127))
        {
            lastUsedInode = lastUsedInode + 7;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)255))
        {
            lastUsedInode = lastUsedInode + 8;
        }
        else if (*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)0)
        {
            break;
        }
        inodeNumber++;
    }

    lastUsedInode++;

    if ((lastUsedInode % 8) == (unsigned char)1)
    {
        valueToWrite = (unsigned char)1;
    }
    else if ((lastUsedInode % 8) == (unsigned char)2)
    {
        valueToWrite = (unsigned char)3;
    }
    else if ((lastUsedInode % 8) == (unsigned char)3)
    {
        valueToWrite = (unsigned char)7;
    }
    else if ((lastUsedInode % 8) == (unsigned char)4)
    {
        valueToWrite = (unsigned char)15;
    }
    else if ((lastUsedInode % 8) == (unsigned char)5)
    {
        valueToWrite = (unsigned char)31;
    }
    else if ((lastUsedInode % 8) == (unsigned char)6)
    {
        valueToWrite = (unsigned char)63;
    }
    else if ((lastUsedInode % 8) == (unsigned char)7)
    {
        valueToWrite = (unsigned char)127;
    }
    else if ((lastUsedInode % 8) == (unsigned char)0)
    {
        valueToWrite = (unsigned char)255;
    }

    *(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) = (unsigned char)valueToWrite;

    writeBlock(BlockGroupDescriptor->bgd_block_address_of_inode_usage, (char *)EXT2_INODE_USAGE_MAP);

    return (unsigned int)lastUsedInode;
}

unsigned int readNextAvailableInode()
{
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);
    readBlock(BlockGroupDescriptor->bgd_block_address_of_inode_usage, (char *)EXT2_INODE_USAGE_MAP);

    unsigned int lastUsedInode = 0;
    unsigned int inodeNumber = 0;

    while (inodeNumber < 0x1E) // Max byte of inode bit flag for this 2 MB file system
    {
        if (*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)1)
        {
            lastUsedInode++;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)3))
        {
            lastUsedInode = lastUsedInode + 2;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)7))
        {
            lastUsedInode = lastUsedInode + 3;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)15))
        {
            lastUsedInode = lastUsedInode + 4;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)31))
        {
            lastUsedInode = lastUsedInode + 5;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)63))
        {
            lastUsedInode = lastUsedInode + 6;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)127))
        {
            lastUsedInode = lastUsedInode + 7;
            break;
        }
        else if (((unsigned char)*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)255))
        {
            lastUsedInode = lastUsedInode + 8;
        }
        else if (*(char *)(EXT2_INODE_USAGE_MAP + inodeNumber) == (unsigned char)0)
        {
            break;
        }
        inodeNumber++;
    }

    lastUsedInode++;

    return (unsigned int)lastUsedInode;
}

void deleteDirectoryEntry(char *fileName)
{
    unsigned int inodeToRemove = returnInodeofFileName(fileName);

    fillMemory((char *)KERNEL_TEMP_INODE_LOC, 0x0, PAGE_SIZE);
    readBlock(ROOTDIR_BLOCK, (char *)KERNEL_TEMP_INODE_LOC);

    struct directoryEntry *DirectoryEntry = (directoryEntry *)(KERNEL_TEMP_INODE_LOC);
    char *savedDirectoryEntry;
    char *previousDirectoryEntry;
    int savedDirectoryEntryRecLength;
    int previousDirectoryEntryRecLength;

    while ((int)DirectoryEntry->directoryInode != 0)
    {
        // This is due to the last entry in a directory having a very long record length
        // It is the way to identify the last entry in a directory so we can add a new entry
        if (DirectoryEntry->directoryInode == inodeToRemove)
        {
            savedDirectoryEntry = (char *)DirectoryEntry;
            savedDirectoryEntryRecLength = DirectoryEntry->recLength;

            if (DirectoryEntry->recLength > 255)
            {
                fillMemory(savedDirectoryEntry, 0x0, savedDirectoryEntryRecLength);
                memoryCopy((char *)(savedDirectoryEntry + savedDirectoryEntryRecLength), savedDirectoryEntry, (BLOCK_SIZE * 3) / 2);
                *(short *)(previousDirectoryEntry + 4) = (unsigned short)0x100; // make it the last entry in the directory
            }
            else
            {
                fillMemory(savedDirectoryEntry, 0x0, savedDirectoryEntryRecLength);
                memoryCopy((char *)(savedDirectoryEntry + savedDirectoryEntryRecLength), savedDirectoryEntry, (BLOCK_SIZE * 3) / 2);
            }

            break;
        }
        else
        {
            previousDirectoryEntry = (char *)DirectoryEntry;
            previousDirectoryEntryRecLength = DirectoryEntry->recLength;

            DirectoryEntry = (directoryEntry *)((int)DirectoryEntry + DirectoryEntry->recLength);
        }
    }

    writeBlock(ROOTDIR_BLOCK, (char *)KERNEL_TEMP_INODE_LOC);
}

void createFile(char *fileName, int currentPid, int fileDescriptor)
{
    int taskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *Task = (struct task *)taskStructLocation;

    fillMemory((char *)KERNEL_TEMP_INODE_LOC, 0x0, PAGE_SIZE);
    readBlock(ROOTDIR_BLOCK, (char *)KERNEL_TEMP_INODE_LOC);

    struct directoryEntry *DirectoryEntry = (directoryEntry *)(KERNEL_TEMP_INODE_LOC);

    while ((int)DirectoryEntry->directoryInode != 0)
    {
        // This is due to the last entry in a directory having a very long record length
        // It is the way to identify the last entry in a directory so we can add a new entry
        if (DirectoryEntry->recLength > 255)
        {
            break;
        }
        else
        {
            DirectoryEntry = (directoryEntry *)((int)DirectoryEntry + DirectoryEntry->recLength);
        }
    }

    // Have to update the record length of the last directory entry before adding another file to the directory
    DirectoryEntry->recLength = (ceiling(DirectoryEntry->nameLength + 1, 4) * 4) + 8;

    DirectoryEntry = (directoryEntry *)((int)DirectoryEntry + DirectoryEntry->recLength);

    DirectoryEntry->directoryInode = allocateInode();
    strcpy((char *)&DirectoryEntry->fileName, fileName);
    DirectoryEntry->fileType = (unsigned char)1;
    DirectoryEntry->nameLength = (unsigned char)(strlen(fileName));
    DirectoryEntry->recLength = (unsigned short)0x100; // make it the last entry in the directory

    writeInodeEntry((int)DirectoryEntry->directoryInode, 0x81b6, (char *)Task->fileDescriptor[fileDescriptor]);

    writeBlock(ROOTDIR_BLOCK, (char *)KERNEL_TEMP_INODE_LOC);
}

void writeInodeEntry(unsigned int inodeEntry, unsigned short mode, char *openFile)
{
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);
    struct openFileTableEntry *OpenFileTableEntry = (struct openFileTableEntry *)openFile;
    int blocksOfInodes = 0;

    while (blocksOfInodes < (MAX_FILES_PER_DIRECTORY / INODES_PER_BLOCK))
    {
        readBlock(BlockGroupDescriptor->bgd_starting_block_of_inode_table + blocksOfInodes, (char *)(int)EXT2_TEMP_INODE_STRUCTS + (BLOCK_SIZE * blocksOfInodes));
        blocksOfInodes++;
    }

    struct inode *Inode = (struct inode *)(EXT2_TEMP_INODE_STRUCTS + (INODE_SIZE * (inodeEntry - 1)));

    Inode->i_mode = mode;

    writeBufferToDisk((char *)OpenFileTableEntry, inodeEntry);

    blocksOfInodes = 0;

    while (blocksOfInodes < (MAX_FILES_PER_DIRECTORY / INODES_PER_BLOCK))
    {
        writeBlock(BlockGroupDescriptor->bgd_starting_block_of_inode_table + blocksOfInodes, (char *)(int)EXT2_TEMP_INODE_STRUCTS + (BLOCK_SIZE * blocksOfInodes));
        blocksOfInodes++;
    }
}

void writeBufferToDisk(char *openFile, unsigned int inodeEntry)
{
    struct inode *Inode = (struct inode *)(EXT2_TEMP_INODE_STRUCTS + (INODE_SIZE * (inodeEntry - 1)));
    struct openFileTableEntry *OpenFileTableEntry = (struct openFileTableEntry *)openFile;

    fillMemory((char *)EXT2_INDIRECT_BLOCK_TMP_LOC, 0x0, BLOCK_SIZE);

    int blockArrayDirect[13];
    int *blockArraySinglyIndirect = (int *)EXT2_INDIRECT_BLOCK_TMP_LOC;
    int totalBlocksNeeded = ceiling((OpenFileTableEntry->numberOfPagesForBuffer * PAGE_SIZE), BLOCK_SIZE);
    int currentDirectBlock = 0;
    int currentIndirectBlock = 0;

    while (currentDirectBlock <= 12 && (currentDirectBlock < totalBlocksNeeded))
    {
        if (currentDirectBlock < 12)
        {
            blockArrayDirect[currentDirectBlock] = allocateFreeBlock();
            writeBlock(blockArrayDirect[currentDirectBlock], (char *)(OpenFileTableEntry->userspaceBuffer + (currentDirectBlock * BLOCK_SIZE)));
            Inode->i_block[currentDirectBlock] = blockArrayDirect[currentDirectBlock];
            currentDirectBlock++;
        }
        else if (currentDirectBlock = 12)
        {
            blockArrayDirect[12] = allocateFreeBlock(); // write the indirect block
            Inode->i_block[12] = blockArrayDirect[12];
            currentDirectBlock++;
        }
    }

    if (totalBlocksNeeded > 12)
    {

        for (currentIndirectBlock = 0; (currentIndirectBlock + currentDirectBlock) < totalBlocksNeeded; currentIndirectBlock++)
        {
            blockArraySinglyIndirect[currentIndirectBlock] = allocateFreeBlock();
            writeBlock(blockArraySinglyIndirect[currentIndirectBlock], (char *)(OpenFileTableEntry->userspaceBuffer + ((currentIndirectBlock + currentDirectBlock) * BLOCK_SIZE)));
        }

        // Write the indirect block to disk
        writeBlock(Inode->i_block[12], (char *)EXT2_INDIRECT_BLOCK_TMP_LOC);
    }

    Inode->i_size = (currentDirectBlock + currentIndirectBlock) * BLOCK_SIZE;
}

void loadElfFile(char *elfHeaderLocation)
{
    // ASSIGNMENT 2 TO DO
    // Cast elfHeaderLocation to an elf_header pointer
    struct elfHeader *elf_header = (struct elfHeader *)elfHeaderLocation;

    // Text header
    struct pHeader *p_text_header = (struct pHeader *)(elfHeaderLocation + elf_header->e_phoff + ELF_PROGRAM_HEADER_SIZE);
    memoryCopy(elfHeaderLocation + p_text_header->p_offset, (char *)p_text_header->p_vaddr, ceiling(p_text_header->p_memsz, 2));

    // Data header
    struct pHeader *p_data_header = (struct pHeader *)(elfHeaderLocation + elf_header->e_phoff + (ELF_PROGRAM_HEADER_SIZE * 2));
    memoryCopy(elfHeaderLocation + p_data_header->p_offset, (char *)p_data_header->p_vaddr, ceiling(p_data_header->p_memsz, 2));
}

void loadFileFromInodeStruct(char *inodeStructMemory, char *fileBuffer)
{
    // ASSIGNMENT 2 TO DO
    // Cast the raw memory to an inode structure
    struct inode *Inode = (struct inode *)inodeStructMemory;

    // Process Direct Blocks (i_block[0] to i_block[11])
    for (int i = 0; i < EXT2_NUMBER_OF_DIRECT_BLOCKS; i++)
    {
        // Get the block number from the inode's direct blocks
        if (Inode->i_block[i] != 0)
        {
            // Read the block into buffer_ptr
            readBlock(Inode->i_block[i], fileBuffer);
        }

        // Advance buffer_ptr by BLOCK_SIZE (1024 bytes)
        fileBuffer += BLOCK_SIZE;
    }

    // Process Single Indirect Block (i_block[12])
    if (Inode->i_block[EXT2_FIRST_INDIRECT_BLOCK] != 0)
    {
        // Read the indirect block into EXT2_INDIRECT_BLOCK
        readBlock(Inode->i_block[EXT2_FIRST_INDIRECT_BLOCK], EXT2_INDIRECT_BLOCK);
        int *indirect_block = (int *)EXT2_INDIRECT_BLOCK;

        // Loop through entries in the indirect block
        for (int i = 0; i < 256; i++)
        {
            // Get the block number from EXT2_INDIRECT_BLOCK
            if (indirect_block[i] != 0)
            {
                // Read the block into buffer_ptr
                readBlock(indirect_block[i], fileBuffer);
            }

            // Advance buffer_ptr by BLOCK_SIZE
            fileBuffer += BLOCK_SIZE;
        }
    }
}

bool fsFindFile(char *fileName, char *destinationMemory)
{
    fillMemory((char *)KERNEL_TEMP_INODE_LOC, 0x0, PAGE_SIZE);
    readBlock(ROOTDIR_BLOCK, (char *)KERNEL_TEMP_INODE_LOC);

    struct directoryEntry *DirectoryEntry = (directoryEntry *)(KERNEL_TEMP_INODE_LOC);
    struct blockGroupDescriptor *BlockGroupDescriptor = (blockGroupDescriptor *)(BLOCK_GROUP_DESCRIPTOR_TABLE);

    while ((int)DirectoryEntry->directoryInode != 0)
    {
        if (strcmp((char *)(&DirectoryEntry->fileName), fileName) == '\0')
        {
            // Load all inodes of a directory up to max number of files per directory. This requires 16KB of memory.
            for (int blocksOfInodes = 0; blocksOfInodes < (MAX_FILES_PER_DIRECTORY / INODES_PER_BLOCK); blocksOfInodes++)
            {
                readBlock(BlockGroupDescriptor->bgd_starting_block_of_inode_table + blocksOfInodes, (char *)(int)EXT2_TEMP_INODE_STRUCTS + (BLOCK_SIZE * blocksOfInodes));
            }
            memoryCopy((char *)((int)EXT2_TEMP_INODE_STRUCTS + ((DirectoryEntry->directoryInode - 1) * INODE_SIZE)), destinationMemory, INODE_SIZE / 2);

            return true;
        }

        DirectoryEntry = (directoryEntry *)((int)DirectoryEntry + DirectoryEntry->recLength);
    }

    return false;
}

int returnInodeofFileName(char *fileName)
{
    fillMemory((char *)KERNEL_TEMP_INODE_LOC, 0x0, PAGE_SIZE);
    readBlock(ROOTDIR_BLOCK, (char *)KERNEL_TEMP_INODE_LOC);

    struct directoryEntry *DirectoryEntry = (directoryEntry *)(KERNEL_TEMP_INODE_LOC);

    while ((int)DirectoryEntry->directoryInode != 0)
    {
        if (strcmp((char *)(&DirectoryEntry->fileName), fileName) == '\0')
        {
            return DirectoryEntry->directoryInode;
        }

        DirectoryEntry = (directoryEntry *)((int)DirectoryEntry + DirectoryEntry->recLength);
    }

    return 0;
}