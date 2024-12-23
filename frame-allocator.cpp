#include "vm.h"
#include "constants.h"
#include "simpleOSlibc.h"

void createPageFrameMap(char *pageFrameMap, int numberOfFrames)
{
    // ASSIGNMENT 3 TO DO
    //   This function creates the page map that will show which page frames are available and, if used, which PID owns that page frame.
    //   There will be one byte allocated for each page of the 4 MB of RAM.

    // Loop through all page frames based on numberOfFrames
    for (int i = 0; i < numberOfFrames; i++)
    {
        int frame_i = i * PAGE_SIZE;

        if (frame_i == 0)
        {
            // 1. Frame zero should be KERNEL_OWNED
            pageFrameMap[i] = KERNEL_OWNED;
        }
        else if (frame_i == KEYBOARD_BUFFER)
        {
            // 1. KEYBOARD_BUFFER should be KERNEL_OWNED
            pageFrameMap[i] = KERNEL_OWNED;
        }
        else if (frame_i == USER_PID_INFO)
        {
            // 2. USER_PID_INFO should be KERNEL_OWNED
            pageFrameMap[i] = KERNEL_OWNED;
        }
        else if (frame_i == GDT_LOC)
        {
            // 3. GDT_LOC should be KERNEL_OWNED
            pageFrameMap[i] = KERNEL_OWNED;
        }
        else if (frame_i >= VIDEO_AND_BIOS_RESERVED_START && frame_i <= VIDEO_AND_BIOS_RESERVED_END)
        {
            // 4. VIDEO_AND_BIOS_RESERVED_START to VIDEO_AND_BIOS_RESERVED_END should be KERNEL_OWNED
            pageFrameMap[i] = KERNEL_OWNED;
        }
        else if (frame_i >= KERNEL_BASE && frame_i <= KERNEL_LIMIT)
        {
            // 5. KERNEL_BASE to KERNEL_LIMIT should be KERNEL_OWNED
            pageFrameMap[i] = KERNEL_OWNED;
        }
        else
        {
            // 6. All else for the size of RAM should be PAGEFRAME_AVAILABLE
            pageFrameMap[i] = PAGEFRAME_AVAILABLE;
        }
    }
}

int allocateFrame(int pid, char *pageFrameMap)
{
    // ASSIGNMENT 3 TO DO
    //   This function allocates a frame of memory to a PID.

    // 1. First acquire a lock on PAGEFRAME_MAP_BASE
    while (!acquireLock(KERNEL_OWNED, (char *)PAGEFRAME_MAP_BASE)) {}

    // 2. Iterate through the page frame map and find the first available frame
    int i;
    for (i = 0; i < PAGEFRAME_MAP_SIZE; i++)
    { 
        // Check if the current frame is available
        if (*(char *)(pageFrameMap + i) == PAGEFRAME_AVAILABLE)
        {
            // Allocate the frame to the given PID by writing to the page frame map
            *(char *)(pageFrameMap + i) = (unsigned char)pid;
            break;  // Frame allocated, exit the loop
        }
    }

    // 3. Release the lock
    while (!releaseLock(KERNEL_OWNED, (char *)PAGEFRAME_MAP_BASE)) {}

    // 4. Return the frame number if allocated successfully, otherwise return an error code if no frame was found
    return i; 
}

void freeFrame(int frameNumber)
{
    while (!acquireLock(KERNEL_OWNED, (char *)PAGEFRAME_MAP_BASE)){}

    *(char *)(PAGEFRAME_MAP_BASE + frameNumber) = (unsigned char)0x0;

    while (!releaseLock(KERNEL_OWNED, (char *)PAGEFRAME_MAP_BASE)){}
}

void freeAllFrames(int pid, char *pageFrameMap)
{
    // ASSIGNMENT 3 TO DO
    //   This function releases all page frames for a given PID.

    // 1. First acquire a lock on PAGEFRAME_MAP_BASE
    while (!acquireLock(KERNEL_OWNED, (char *)PAGEFRAME_MAP_BASE)){}

    // 2. Loop through and free all frames (marking them as PAGEFRAME_AVAILABLE) for a given PID
    for (int i = 0; i < PAGEFRAME_MAP_SIZE; i++)
    {
        // Check if the current frame is owned by the given PID
        if (pageFrameMap[i] == (unsigned char)pid)
        {
            pageFrameMap[i] = PAGEFRAME_AVAILABLE;
        }
    }

    // 3. Release lock
    while (!releaseLock(KERNEL_OWNED, (char *)PAGEFRAME_MAP_BASE)){}
}

unsigned int processFramesUsed(int pid, char *pageFrameMap)
{
    unsigned char lastUsedFrame = PAGEFRAME_AVAILABLE;
    int framesUsed = 0;

    for (int frameNumber = 0; frameNumber < (KERNEL_BASE / PAGE_SIZE); frameNumber++)
    {
        lastUsedFrame = *(char *)(pageFrameMap + frameNumber);

        if (lastUsedFrame == pid)
        {
            framesUsed++;
        }
    }
    return (unsigned int)framesUsed;
}

unsigned int totalFramesUsed(char *pageFrameMap)
{
    unsigned char lastUsedFrame = PAGEFRAME_AVAILABLE;
    int framesUsed = 0;

    for (int frameNumber = 0; frameNumber < PAGEFRAME_MAP_SIZE; frameNumber++)
    {
        lastUsedFrame = *(char *)(pageFrameMap + frameNumber);

        if (lastUsedFrame != PAGEFRAME_AVAILABLE)
        {
            framesUsed++;
        }
    }
    return (unsigned int)framesUsed;
}