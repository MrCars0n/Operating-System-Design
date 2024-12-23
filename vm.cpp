#include "vm.h"
#include "fs.h"
#include "constants.h"
#include "simpleOSlibc.h"
#include "frame-allocator.h"
#include "exceptions.h"
#include "file.h"
#include "screen.h"


void initializePageTables(int pid)
{     
    // ASSIGNMENT 3 TO DO
    //   This function creates the page directory and single page table for a process based on the passed in PID value.
    //   Calculate the base address for the page directory for this PID
    // 1. Compute the position of tables based on PID. PID 1 = PAGE_DIR_BASE. PID 2 = PAGE_DIR_BASE + MAX_PG_TABLES_SIZE.
    int p_dir_addr = PAGE_DIR_BASE + (MAX_PGTABLES_SIZE * (pid - 1));

    // Get pointers to page directory and page table
    int *p_dir = (int *)p_dir_addr;
    int *p_table = (int *)(p_dir_addr + PAGE_SIZE);

    for (int i = 0; i < (KERNEL_LIMIT / PAGE_SIZE); i++)
    {
        // First entry in the page table should be PG_USER_PRESENT_RW
        if (i == 0)
        {
            *p_table = 0 | PG_USER_PRESENT_RW;
        }
        // 2. KEYBOARD_BUFFER buffer should be PG_USER_PRESENT_RW
        else if (i == KEYBOARD_BUFFER / PAGE_SIZE)
        {
            *p_table = KEYBOARD_BUFFER | PG_USER_PRESENT_RW;
        }
        // 3. USER_PID_INFO should be PG_USER_PRESENT_RO
        else if (i == USER_PID_INFO / PAGE_SIZE)
        {
            *p_table = USER_PID_INFO | PG_USER_PRESENT_RO;
        }
        // 4. GDT_LOC should be PG_USER_PRESENT_RO
        else if (i == GDT_LOC / PAGE_SIZE)
        {
            *p_table = GDT_LOC | PG_USER_PRESENT_RO;
        }
        // 5. VIDEO_RAM should be PG_USER_PRESENT_RW
        else if (i == (int)VIDEO_RAM / PAGE_SIZE)
        {
            *p_table = (int)VIDEO_RAM | PG_USER_PRESENT_RW;
        }
        // 6. All of kernel space should be PG_KERNEL_PRESENT_RW
        else if (i >= KERNEL_BASE / PAGE_SIZE && i < KERNEL_LIMIT / PAGE_SIZE)
        {
            *p_table = (i * PAGE_SIZE) | PG_KERNEL_PRESENT_RW;
        }

        p_table++;
    }

    // 7. The first (and only entry) in the page directory should be PG_USER_PRESENT_RW
    *p_dir = (PAGE_TABLE_BASE + (MAX_PGTABLES_SIZE * (pid - 1))) | PG_USER_PRESENT_RW;
} 


void fillMemory(char *memLocation, unsigned char byteToFill, int numberOfBytes)
{
    for (int currentByte = 0; currentByte < numberOfBytes; currentByte++)
    {
        *memLocation = byteToFill;
        memLocation++;
    }
}


void contextSwitch(int pid)
{
    int pgdLocation = ((pid - 1) * MAX_PGTABLES_SIZE) + PAGE_DIR_BASE;
    
    asm volatile ("movl %0, %%eax\n\t" : : "r" (pgdLocation));
    asm volatile ("movl %eax, %cr3\n\t");
    asm volatile ("movl %cr0, %ebx\n\t");
    asm volatile ("or $0x80000000, %ebx\n\t");
    asm volatile ("movl %ebx, %cr0\n\t");
}

int initializeTask(int ppid, short state, int stack, char *binaryName, int priority)
{
    while (!acquireLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}
    
    int lastUsedPid = 0;
    int nextAvailPid = 0;
    int taskStructNumber = 0;

    if (ppid == 0) { ppid = KERNEL_OWNED; }

    while (nextAvailPid == 0 && taskStructNumber < MAX_PROCESSES)
    {
        lastUsedPid = *(int *)(PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * taskStructNumber));

        if ((unsigned int)lastUsedPid == 0)
        {
            nextAvailPid = (taskStructNumber + 1);
        }

        if (taskStructNumber == (MAX_PROCESSES - 1))
        {
            while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}
            panic((char*)"vm.cpp:initializeTask() -> reached max process number");
        }

        taskStructNumber++;
    }
    
    int physMemStart = (MAX_PROCESS_SIZE * (nextAvailPid - 1));
    int taskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (nextAvailPid - 1));
    int pgd = ((nextAvailPid - 1) * MAX_PGTABLES_SIZE) + PAGE_DIR_BASE;
    
    struct task *Task = (struct task*)taskStructLocation;

    Task->pid = nextAvailPid;
    Task->ppid = ppid;
    Task->state = state;
    Task->pgd = pgd;
    Task->stack = stack;
    Task->physMemStart = physMemStart;
    Task->fileDescriptor[1] = (openFileTableEntry *)(OPEN_FILE_TABLE + (sizeof(openFileTableEntry)));
    Task->nextAvailableFileDescriptor = 3;
    Task->priority = priority;
    Task->runtime = 0;
    Task->binaryName = binaryName;

    while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}

    return nextAvailPid;
}

void updateTaskState(int pid, short state)
{
    while (!acquireLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}
    
    int taskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (pid - 1));
    
    struct task *Task = (struct task*)taskStructLocation;
    Task->state = state;

    while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}
}

int requestSpecificPage(int pid, char *pageMemoryLocation, char perms)
{

    // ASSIGNMENT 3 TO DO
    //   This function attempts to allocate a page of memory at a specific virtual address for a process.
    // 1. Acquire lock on PROCESS_TABLE_LOC
    while (!acquireLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}

    //    Calculate page directory address & table address
    int p_dir_addr = PAGE_DIR_BASE + (MAX_PGTABLES_SIZE * (pid - 1));
    int *p_table = (int *)(p_dir_addr + PAGE_SIZE);

    // 2. Allocate a page frame
    int p_frame_num = allocateFrame(pid, (char *)PAGEFRAME_MAP_BASE);
    
    if (p_frame_num != PAGEFRAME_AVAILABLE)
    {
        // 3. Insert the page frame address into the process’s page table at the provided virtual address using the provided permissions
        p_table[(int)pageMemoryLocation / PAGE_SIZE] = ((p_frame_num * PAGE_SIZE) + perms);

        // 4. Release the lock
        while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}

        // 5. Return 1 if successful
        return 1;
    }
    else
    {
        // 4. Release the lock
        while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}

        // 5. Return 0 if unable to do so.
        return 0;
    }
}

char* findBuffer(int pid, int numberOfPages, char perms)
{

    // ASSIGNMENT 3 TO DO
    //   Using a first-fit algorithm, this function attempts to find a contiguous chunk (buffer) of virtual memory within a process’s virtual 
    //   memory based on the number of pages needed. A pointer to the first buffer large enough should be returned. 
    //   This function does not actually allocate the memory--it just finds a spot large enough. The range must start from TEMP_FILE_START_LOC

    // Calculate page directory address & table address
    int p_dir_addr = PAGE_DIR_BASE + (MAX_PGTABLES_SIZE * (pid - 1));
    int *p_table = (int *)(p_dir_addr + PAGE_SIZE);

    // Search through the process's virtual memory space using a first-fit algorithm
    int free_pages_count = 0;
    char* first_free = 0; // NULL

    // Iterate over the entire virtual address space (assuming 4MB is the limit for now)
    for (int i = TEMP_FILE_START_LOC / PAGE_SIZE; i < KERNEL_BASE / PAGE_SIZE; i++)
    {
        // Check if the current page is free
        if (p_table[i] == 0)
        {
            // If this is the start of a new block, note the first free page
            if (free_pages_count == 0)
            {
                first_free = (char *)(i * PAGE_SIZE);
            }

            // Count this page as free
            free_pages_count++;

            // Check if we've found a block large enough
            if (free_pages_count >= numberOfPages)
            {
                // Return the pointer to the first page of the block
                return first_free;
            }
        }
        else
        {
            // Reset the counter if the block is interrupted by an allocated page
            free_pages_count = 0;
            first_free = 0;
        }
    }

    // No suitable block found
    return 0;
}


char* requestAvailablePage(int pid, char perms)
{
    
    // ASSIGNMENT 3 TO DO
    //   This function allocates the first available page of virtual memory in the process’s address 
    //   space that is not used and returns a pointer to it. The range must start from TEMP_FILE_START_LOC.

    // 1. Acquire lock on PROCESS_TABLE_LOC
    while (!acquireLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}

    // 2. Allocate an available frame
    int p_frame_num = allocateFrame(pid, (char *)PAGEFRAME_MAP_BASE);

    // Calculate page directory address
    int p_dir_addr = PAGE_DIR_BASE + (MAX_PGTABLES_SIZE * (pid - 1));
    int *p_table = (int *)(p_dir_addr + PAGE_SIZE);

    if (p_frame_num != PAGEFRAME_AVAILABLE)
    {
        // 3. Find first available (unallocated page) in the page table for that PID that is >= TEMP_FILE_START_LOC. (Meaning, a virtual address beginning at 0x210000 or larger in that address space.)
        // MAX_PROCESS_SIZE = (4 * 1024 * 1024) = 4MB
        for (int i = TEMP_FILE_START_LOC; i < MAX_PROCESS_SIZE; i += PAGE_SIZE)
        {
            // Check if the current page is unallocated (e.g., by checking if it's not present)
            if (p_table[i / PAGE_SIZE] == 0)  // Page not present (not allocated)
            {
                // 4. Insert page frame address into the page table at that location. 
                //    Calculating the page frame address based on the frame number.
                p_table[i / PAGE_SIZE] = ((p_frame_num * PAGE_SIZE) + perms);

                // 5. Release lock
                while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}

                // 6. Return pointer to the page allocated
                return (char *)i;
            }
        }

        // 5. Release the lock and return NULL (0)
        while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}
        return 0;
    }
    else
    {
        // If no frame could be allocated, release the lock and return NULL (0)
        while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}
        return 0;
    }
}

void freePage(int pid, char *pageToFree)
{
    while (!acquireLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}

    int ptLocation = ((pid - 1) * MAX_PGTABLES_SIZE) + PAGE_TABLE_BASE;
    int pageNumberToFree = (int)pageToFree / PAGE_SIZE;
    int physicalAddressToFree = *(int *)((int)ptLocation + (pageNumberToFree * 4));

    freeFrame((physicalAddressToFree / PAGE_SIZE));
    *(int *)((int)ptLocation + (pageNumberToFree * 4)) = 0x0;

    while (!releaseLock(KERNEL_OWNED, (char *)PROCESS_TABLE_LOC)) {}
}

bool acquireLock(int currentPid, char *memoryLocation)
{
    int semaphoreNumber = 0;
    struct semaphore *Semaphore = (struct semaphore*)KERNEL_SEMAPHORE_TABLE;

    while (semaphoreNumber < MAX_SEMAPHORE_OBJECTS)
    {
        if (Semaphore->pid == currentPid && Semaphore->memoryLocationToLock == memoryLocation)
        {
            if (Semaphore->currentValue == 0)
            {
                return false; // Cannot acquire lock
            }
            
            asm volatile ("movl %0, %%ecx\n\t" : : "r" (Semaphore->currentValue - 1));
            asm volatile ("movl %0, %%edx\n\t" : : "r" (&Semaphore->currentValue));
            asm volatile ("xchg %ecx, (%edx)\n\t");
            return true;
        }

        semaphoreNumber++;
        Semaphore++;
    }

    return false;
}

bool releaseLock(int currentPid, char *memoryLocation)
{
    int semaphoreNumber = 0;
    struct semaphore *Semaphore = (struct semaphore*)KERNEL_SEMAPHORE_TABLE;

    while (semaphoreNumber < MAX_SEMAPHORE_OBJECTS)
    {
        if (Semaphore->pid == currentPid && Semaphore->memoryLocationToLock == memoryLocation)
        {
            if (Semaphore->currentValue == Semaphore->maxValue)
            {
                return false; // At max amount
            }
            
            asm volatile ("movl %0, %%ecx\n\t" : : "r" (Semaphore->currentValue + 1));
            asm volatile ("movl %0, %%edx\n\t" : : "r" (&Semaphore->currentValue));
            asm volatile ("xchg %ecx, (%edx)\n\t");
            return true;
        }

        semaphoreNumber++;
        Semaphore++;
    }

    return false;
}

bool createSemaphore(int currentPid, char *memoryLocation, int currentValue, int maxValue)
{
    int semaphoreNumber = 0;
    struct semaphore *Semaphore = (struct semaphore*)KERNEL_SEMAPHORE_TABLE;

    while (semaphoreNumber < MAX_SEMAPHORE_OBJECTS)
    {
        if (Semaphore->pid == 0)
        {
            Semaphore->pid = currentPid;
            Semaphore->memoryLocationToLock = memoryLocation;
            Semaphore->currentValue = currentValue;
            Semaphore->maxValue = maxValue;
            return true;  
        }

        semaphoreNumber++;
        Semaphore++;
    }
    // return false if unable to create
    return false;
}