#include "syscalls.h"
#include "kernel.h"
#include "screen.h"
#include "fs.h"
#include "x86.h"
#include "vm.h"
#include "simpleOSlibc.h"
#include "interrupts.h"
#include "constants.h"
#include "frame-allocator.h"
#include "exceptions.h"
#include "keyboard.h"
#include "file.h"
#include "schedule.h"
#include "sound.h"

int returnedArgument = 0;
int totalInterruptCount = 0;
int systemTimerInterruptCount = 0;
int keyboardInterruptCount = 0;
int otherInterruptCount = 0;
int currentFileDescriptor = 0;
int uptimeSeconds = 0;
int uptimeMinutes = 0;
struct task *SysHandlerTask;
struct task *newSysHandlerTask;
int sysHandlertaskStructLocation;
int newSysHandlertaskStructLocation;
int returnedPid;

void sysSound(struct soundParameter *SoundParameter)
{
    generateTone(SoundParameter->frequency);

    for (int iterations = 0; iterations <= SoundParameter->duration; iterations++)
    {
        sysWaitOneInterrupt();
    }

    stopTone();
}

void sysBeep()
{
    generateTone(1000);
    sysWaitOneInterrupt();
    stopTone();
}

void sysOpen(struct fileParameter *FileParameter, int currentPid)
{
    char *newBinaryFilenameLoc = kMalloc(currentPid, FileParameter->fileNameLength);
    strcpyRemoveNewline(newBinaryFilenameLoc, FileParameter->fileName);

    if (FileParameter->requestedPermissions == RDWRITE)
    {
        if (!fileAvailableToBeLocked((char *)OPEN_FILE_TABLE, returnInodeofFileName(newBinaryFilenameLoc)))
        {
            printString(COLOR_RED, 3, 2, (char *)"File locked by another process!");
            sysBeep();
            sysWait();
        }
    }

    int taskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *Task = (struct task *)taskStructLocation;

    if (Task->nextAvailableFileDescriptor >= MAX_FILE_DESCRIPTORS)
    {
        return; // reached the max open files
    }

    char *inodePage = requestAvailablePage(currentPid, PG_USER_PRESENT_RW);

    if (inodePage == 0)
    {
        panic((char *)"Syscalls.cpp:sysOpen() -> open request available page is null");
    }

    if (!fsFindFile(newBinaryFilenameLoc, (char *)inodePage))
    {
        printString(COLOR_RED, 2, 5, (char *)"File not found!");
        sysBeep();
        return;
    }

    struct inode *Inode = (struct inode *)inodePage;
    int pagesNeedForTmpBinary = ceiling(Inode->i_size, PAGE_SIZE);

    char *requestedBuffer = findBuffer(currentPid, pagesNeedForTmpBinary, PG_USER_PRESENT_RW);

    // request block of pages for temporary file storage to load file based on first available page above
    for (int pageCount = 0; pageCount < pagesNeedForTmpBinary; pageCount++)
    {
        if (!requestSpecificPage(currentPid, (char *)((int)requestedBuffer + (pageCount * PAGE_SIZE)), PG_USER_PRESENT_RW))
        {
            clearScreen();
            printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
            panic((char *)"syscalls.cpp -> USER_TEMP_FILE_LOC page request");
        }

        // Zero each page in case it has been used previously
        fillMemory((char *)((int)requestedBuffer + (pageCount * PAGE_SIZE)), 0x0, PAGE_SIZE);
    }

    loadFileFromInodeStruct((char *)inodePage, requestedBuffer);

    Task->fileDescriptor[Task->nextAvailableFileDescriptor] = (openFileTableEntry *)insertOpenFileTableEntry((char *)OPEN_FILE_TABLE, (int)(returnInodeofFileName(newBinaryFilenameLoc)), currentPid, requestedBuffer, pagesNeedForTmpBinary, newBinaryFilenameLoc, 0, 0);

    if (FileParameter->requestedPermissions == RDWRITE)
    {
        if (!lockFile((char *)OPEN_FILE_TABLE, currentPid, returnInodeofFileName(newBinaryFilenameLoc)))
        {
            printString(COLOR_RED, 4, 2, (char *)"Unable to acquire file lock!");
            sysBeep();
        }
    }

    struct openBufferTable *openBufferTable = (struct openBufferTable *)OPEN_BUFFER_TABLE;

    freePage(currentPid, inodePage);
    storeValueAtMemLoc(CURRENT_FILE_DESCRIPTOR, ((int)Task->nextAvailableFileDescriptor));
    storeValueAtMemLoc((char *)&openBufferTable->buffers[Task->nextAvailableFileDescriptor], (int)requestedBuffer);

    Task->nextAvailableFileDescriptor++;
}

void sysWrite(int fileDescriptorToPrint, int currentPid)
{
    int taskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *Task = (struct task *)taskStructLocation;

    if ((int)Task->fileDescriptor[fileDescriptorToPrint] == 0x0)
    {
        printString(COLOR_RED, 3, 5, (char *)"No such file descriptor!");
        sysBeep();
        sysWait();
        sysWait();
        clearScreen();
    }
    else
    {
        struct openFileTableEntry *OpenFileTableEntry = (openFileTableEntry *)(int)Task->fileDescriptor[fileDescriptorToPrint];
        printString(COLOR_WHITE, 0, 0, (char *)(OpenFileTableEntry->userspaceBuffer));
    }
}

void sysClose(int fileDescriptor, int currentPid)
{

    // ASSIGNMENT 4 TO DO
    // Locate the task structure for the current process
    int curr_task_struct_loc = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *current_task = (struct task *)curr_task_struct_loc;

    // Validate file descriptor
    //   File descriptors below 3 are reserved for inputs/outputs (e.g., console)
    if (current_task->nextAvailableFileDescriptor < MAX_FILE_DESCRIPTORS && current_task->nextAvailableFileDescriptor >= 3)
    {
        // Locate the open file table entry for the file descriptor
        int open_file_addr = (int)current_task->fileDescriptor[fileDescriptor];
        if (open_file_addr != 0)
        {
            struct openFileTableEntry *open_file_table_entry = (struct openFileTableEntry *)open_file_addr;

            // Clearing the open file table entry for that file
            open_file_table_entry->openedByPid = 0x0;            // Reset PID ownership
            open_file_table_entry->numberOfPagesForBuffer = 0x0; // Reset page count
            open_file_table_entry->lockedForWriting = 0x0;       // Mark not in use

            // Free each page in the userspace buffer associated with this file
            char *buffer_p = open_file_table_entry->userspaceBuffer;
            for (int i = 0; i < open_file_table_entry->numberOfPagesForBuffer; i++)
            {
                freePage(currentPid, buffer_p);
                buffer_p += PAGE_SIZE; // Move to the next page in the buffer
            }
            open_file_table_entry->userspaceBuffer = nullptr; // Clear buffer pointer

            // Clear the file descriptor entry in the task struct
            current_task->fileDescriptor[fileDescriptor] = 0x0;
        }
    }
}

void sysPs(int currentPid)
{
    int currentTaskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *currentTask = (struct task *)currentTaskStructLocation;

    int lastUsedPid = 0;
    int nextAvailPid = 0;
    int cursor = 1;
    int taskStructNumber = 0;

    int taskStructLocation = PROCESS_TABLE_LOC;

    if (char *totalFramesCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor++, 2, (char *)"System Frames Used: ");
        itoa(totalFramesUsed((char *)PAGEFRAME_MAP_BASE), totalFramesCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 30, totalFramesCount);
        kFree(totalFramesCount);
    }

    if (char *totalMemCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor - 1, 38, (char *)"System Memory Used (Bytes): ");
        itoa((totalFramesUsed((char *)PAGEFRAME_MAP_BASE) * 4096), totalMemCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 70, totalMemCount);
        kFree(totalMemCount);
    }

    if (char *processFramesCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor++, 2, (char *)"Current PID Frames Used: ");
        itoa((processFramesUsed(currentPid, (char *)PAGEFRAME_MAP_BASE)), processFramesCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 30, processFramesCount);
        kFree(processFramesCount);
    }

    if (char *processMemCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor - 1, 38, (char *)"Current PID Mem Used (Bytes): ");
        itoa((processFramesUsed(currentPid, (char *)PAGEFRAME_MAP_BASE) * 4096), processMemCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 70, processMemCount);
        kFree(processMemCount);
    }

    if (char *userHeapObjectCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor++, 2, (char *)"Current PID Heap Objects: ");
        itoa(countHeapObjects((char *)USER_HEAP), userHeapObjectCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 30, userHeapObjectCount);
        kFree(userHeapObjectCount);
    }

    if (char *interruptCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor - 1, 38, (char *)"Total System Interrupts: ");
        itoa(totalInterruptCount, interruptCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 70, interruptCount);
        kFree(interruptCount);
    }

    if (char *timerCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor++, 38, (char *)"Total Timer Interrupts: ");
        itoa(systemTimerInterruptCount, timerCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 70, timerCount);
        kFree(timerCount);
    }

    if (char *keyboardCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor++, 38, (char *)"Total Keyboard Interrupts: ");
        itoa(keyboardInterruptCount, keyboardCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 70, keyboardCount);
        kFree(keyboardCount);
    }

    if (char *otherCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor++, 38, (char *)"All Other Interrupts: ");
        itoa(otherInterruptCount, otherCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 70, otherCount);
        kFree(otherCount);
    }

    if (char *openFilesCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor - 1, 2, (char *)"System-wide Open Files: ");
        itoa(totalOpenFiles((char *)OPEN_FILE_TABLE), openFilesCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 30, openFilesCount);
        kFree(openFilesCount);
    }

    if (char *kernelHeapObjectCount = kMalloc(currentPid, sizeof(int)))
    {
        printString(COLOR_GREEN, cursor++, 38, (char *)"Kernel Heap Objects: ");
        itoa(countHeapObjects((char *)KERNEL_HEAP), kernelHeapObjectCount);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 70, kernelHeapObjectCount);
        kFree(kernelHeapObjectCount);
    }

    char *psUpperLeftCorner = kMalloc(currentPid, sizeof(char));
    *psUpperLeftCorner = ASCII_UPPERLEFT_CORNER;

    printString(COLOR_WHITE, cursor, 1, psUpperLeftCorner);

    char *psHorizontalLine = kMalloc(currentPid, sizeof(char));
    *psHorizontalLine = ASCII_HORIZONTAL_LINE;
    for (int columnPos = 2; columnPos < 78; columnPos++)
    {
        printString(COLOR_WHITE, cursor, columnPos, psHorizontalLine);
    }

    char *psVerticalLine = kMalloc(currentPid, sizeof(char));
    *psVerticalLine = ASCII_VERTICAL_LINE;

    char *psUpperRightCorner = kMalloc(currentPid, sizeof(char));
    *psUpperRightCorner = ASCII_UPPERRIGHT_CORNER;

    char *psLowerLeftCorner = kMalloc(currentPid, sizeof(char));
    *psLowerLeftCorner = ASCII_LOWERLEFT_CORNER;

    char *psLowerRightCorner = kMalloc(currentPid, sizeof(char));
    *psLowerRightCorner = ASCII_LOWERRIGHT_CORNER;

    printString(COLOR_WHITE, (cursor), 78, psUpperRightCorner);

    printString(COLOR_RED, 8, 3, (char *)"Processes");

    cursor++;

    while (taskStructNumber < MAX_PROCESSES)
    {
        taskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * taskStructNumber);
        ;

        struct task *Task = (struct task *)taskStructLocation;

        lastUsedPid = *(int *)(PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * taskStructNumber));

        if ((unsigned int)lastUsedPid == 0)
        {
            nextAvailPid = (taskStructNumber + 1);
            taskStructNumber++;
        }
        else
        {
            printString(COLOR_WHITE, (cursor++), 1, psVerticalLine);
            printString(COLOR_WHITE, (cursor - 1), 2, (char *)"PID:");
            printHexNumber(COLOR_WHITE, (cursor - 1), 7, (unsigned char)Task->pid);
            printString(COLOR_WHITE, (cursor - 1), 10, psVerticalLine);
            printString(COLOR_WHITE, (cursor - 1), 12, (char *)"Name:");
            printString(COLOR_WHITE, (cursor - 1), 18, (char *)Task->binaryName);
            printString(COLOR_WHITE, (cursor - 1), 31, psVerticalLine);
            printString(COLOR_WHITE, (cursor - 1), 33, (char *)"State:");
            if ((unsigned char)Task->state == PROC_SLEEPING)
            {
                printString(COLOR_WHITE, (cursor - 1), 40, (char *)"SLEEPING");
            }
            if ((unsigned char)Task->state == PROC_RUNNING)
            {
                printString(COLOR_WHITE, (cursor - 1), 40, (char *)"RUNNING");
            }
            if ((unsigned char)Task->state == PROC_ZOMBIE)
            {
                printString(COLOR_WHITE, (cursor - 1), 40, (char *)"ZOMBIE");
            }
            if ((unsigned char)Task->state == PROC_KILLED)
            {
                printString(COLOR_WHITE, (cursor - 1), 40, (char *)"KILLED");
            }
            printString(COLOR_WHITE, (cursor - 1), 49, psVerticalLine);
            printString(COLOR_WHITE, (cursor - 1), 51, (char *)"PPID:");
            printHexNumber(COLOR_WHITE, (cursor - 1), 57, (unsigned char)Task->ppid);
            printString(COLOR_WHITE, (cursor - 1), 60, psVerticalLine);
            printString(COLOR_WHITE, (cursor - 1), 62, (char *)"P:");
            printHexNumber(COLOR_WHITE, (cursor - 1), 65, (unsigned char)Task->priority);

            if (char *runtimeMemLoc = kMalloc(currentPid, sizeof(int)))
            {
                printString(COLOR_WHITE, (cursor - 1), 68, psVerticalLine);
                printString(COLOR_WHITE, (cursor - 1), 70, (char *)"R:");
                itoa(((Task->runtime) / SYSTEM_INTERRUPTS_PER_SECOND), runtimeMemLoc);
                printString(COLOR_WHITE, (cursor - 1), 73, runtimeMemLoc);
                printString(COLOR_WHITE, (cursor - 1), 78, psVerticalLine);
                kFree(runtimeMemLoc);
            }

            taskStructNumber++;
        }
        if ((unsigned int)(currentTask->nextAvailableFileDescriptor - 1) <= 2)
        {
            returnedArgument = 0x0;
        }
        else
        {
            returnedArgument = (unsigned int)(currentTask->nextAvailableFileDescriptor - 1);
        }
    }
    printSystemUptime(uptimeSeconds, uptimeMinutes);

    printString(COLOR_WHITE, cursor, 1, psLowerLeftCorner);
    printString(COLOR_WHITE, cursor, 78, psLowerRightCorner);

    for (int columnPos = 2; columnPos < 78; columnPos++)
    {
        printString(COLOR_WHITE, cursor, columnPos, psHorizontalLine);
    }

    kFree(psHorizontalLine);
    kFree(psVerticalLine);
    kFree(psUpperLeftCorner);
    kFree(psUpperRightCorner);
    kFree(psLowerLeftCorner);
    kFree(psLowerRightCorner);
}

void sysForkExec(struct fileParameter *FileParameter, int currentPid)
{
    char *newBinaryFilenameLoc = kMalloc(currentPid, strlen(FileParameter->fileName));
    strcpyRemoveNewline(newBinaryFilenameLoc, FileParameter->fileName);

    if (!fsFindFile(newBinaryFilenameLoc, USER_TEMP_INODE_LOC))
    {
        printString(COLOR_RED, 2, 5, (char *)"File not found!");
        sysBeep();
        sysWait();
        sysWait();
        return;
    }

    int newPid = 0;

    newPid = initializeTask(currentPid, PROC_SLEEPING, STACK_START_LOC, newBinaryFilenameLoc, FileParameter->requestedRunPriority);
    initializePageTables(newPid);

    contextSwitch(newPid);

    if (!requestSpecificPage(newPid, (char *)(STACK_PAGE - PAGE_SIZE), PG_USER_PRESENT_RW))
    {
        clearScreen();
        printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
        panic((char *)"syscalls.cpp -> STACK_PAGE - PAGE_SIZE page request");
    }

    if (!requestSpecificPage(newPid, (char *)(STACK_PAGE), PG_USER_PRESENT_RW))
    {
        clearScreen();
        printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
        panic((char *)"syscalls.cpp -> STACK_PAGE page request");
    }

    if (!requestSpecificPage(newPid, (char *)(USER_HEAP), PG_USER_PRESENT_RW))
    {
        clearScreen();
        printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
        panic((char *)"syscalls.cpp -> USER_HEAP page request");
    }

    if (!requestSpecificPage(newPid, USER_TEMP_INODE_LOC, PG_USER_PRESENT_RW))
    {
        clearScreen();
        printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
        panic((char *)"syscalls.cpp -> USER_TEMP_INODE_LOC page request");
    }

    // we do this function twice. First one (above) to make sure the file exists.
    // this second one (below) is because there was a context switch and a different
    // userspace.
    fsFindFile(newBinaryFilenameLoc, USER_TEMP_INODE_LOC);

    struct inode *Inode = (struct inode *)USER_TEMP_INODE_LOC;

    int pagesNeedForTmpBinary = ceiling(Inode->i_size, PAGE_SIZE);

    // request pages for temporary file storage to load raw ELF file
    for (int tempFileLoc = 0; tempFileLoc < (pagesNeedForTmpBinary * PAGE_SIZE); tempFileLoc = tempFileLoc + PAGE_SIZE)
    {
        if (!requestSpecificPage(newPid, (USER_TEMP_FILE_LOC + tempFileLoc), PG_USER_PRESENT_RW))
        {
            clearScreen();
            printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
            panic((char *)"syscalls.cpp -> USER_TEMP_FILE_LOC page request");
        }
    }

    loadFileFromInodeStruct(USER_TEMP_INODE_LOC, USER_TEMP_FILE_LOC);

    if (*(int *)USER_TEMP_FILE_LOC != MAGIC_ELF)
    {
        printString(COLOR_RED, 2, 5, (char *)"File is not an ELF file!");
        sysBeep();
        sysWait();
        sysWait();
        sysKill(newPid);
        contextSwitch(currentPid);
        return;
    }

    struct elfHeader *ELFHeader = (struct elfHeader *)USER_TEMP_FILE_LOC;
    struct pHeader *ProgHeaderTextSegment, *ProgHeaderDataSegment;
    ProgHeaderTextSegment = (struct pHeader *)((char *)(int)ELFHeader + ELFHeader->e_phoff + ELF_PROGRAM_HEADER_SIZE);
    ProgHeaderDataSegment = (struct pHeader *)((char *)(int)ELFHeader + (ELFHeader->e_phoff + (ELF_PROGRAM_HEADER_SIZE * 2)));

    int totalTextSegmentPagesNeeded = ceiling(ProgHeaderTextSegment->p_memsz, PAGE_SIZE);
    int totalDataSegmentPagesNeeded = ceiling(ProgHeaderDataSegment->p_memsz, PAGE_SIZE);

    // request enough pages at the required virtual address to load and run binary
    for (int tempFileLoc = 0; tempFileLoc < ((totalTextSegmentPagesNeeded + totalDataSegmentPagesNeeded) * PAGE_SIZE); tempFileLoc = tempFileLoc + PAGE_SIZE)
    {
        if (!requestSpecificPage(newPid, (char *)(ProgHeaderTextSegment->p_vaddr + tempFileLoc), PG_USER_PRESENT_RW))
        {
            clearScreen();
            printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
            panic((char *)"syscalls.cpp -> USERPROG VADDR page requests");
        }
    }

    loadElfFile(USER_TEMP_FILE_LOC);

    struct elfHeader *ELFHeaderLaunch = (struct elfHeader *)USER_TEMP_FILE_LOC;

    updateTaskState(currentPid, PROC_SLEEPING);
    updateTaskState(newPid, PROC_RUNNING);

    // Letting the new process know its pid
    storeValueAtMemLoc(RUNNING_PID_LOC, newPid);

    enableInterrupts();

    switchToRing3LaunchBinary((char *)ELFHeaderLaunch->e_entry);
}

void sysExit(int currentPid)
{
    if (currentPid == 1)
    {
        return; // PID 1 cannot exit
    }

    int currentTaskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));

    struct task *currentTask = (struct task *)currentTaskStructLocation;

    updateTaskState(currentPid, PROC_ZOMBIE);
    updateTaskState(currentTask->ppid, PROC_RUNNING);

    closeAllFiles((char *)OPEN_FILE_TABLE, currentPid);

    // Letting the new process know its pid
    storeValueAtMemLoc(RUNNING_PID_LOC, (currentTask->ppid));

    // switch CR3 back to parent's page directory
    contextSwitch(currentTask->ppid);
}

void sysFree(int currentPid)
{
    // ASSIGNMENT 4 TO DO
    // Iterate through all processes in the process table
    for (int i = 0; i < MAX_PROCESSES; i++)
    {
        int task_struct_location = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * i);
        struct task *current_task = (struct task *)task_struct_location;

        // If the process is in state PROC_ZOMBIE or PROC_KILLED
        if (current_task->state == PROC_ZOMBIE || current_task->state == PROC_KILLED)
        {
            // Calculate page directory address & table address
            int p_dir_addr = PAGE_DIR_BASE + (MAX_PGTABLES_SIZE * i);
            int *p_table = (int *)(p_dir_addr + PAGE_SIZE);

            // Free all the frames associated with that process
            for (int x = 0; x < MAX_PROCESSES; x++)
            {
                if (p_table[x] != 0)
                {
                    freePage(i + 1, (char *)(x * PAGE_SIZE));
                }
            }

            // Zero out the task struct for this process
            fillMemory((char *)task_struct_location, 0x0, TASK_STRUCT_SIZE);

            // Zero the page directory and page table for this process
            fillMemory((char *)p_table, 0x0, MAX_PGTABLES_SIZE);
        }
    }
}

void sysMmap(int currentPid)
{
    int returnedPage = (unsigned int)requestAvailablePage(currentPid, PG_USER_PRESENT_RW);

    if (returnedPage == 0)
    {
        panic((char *)"syscalls.cpp -> MMAP - when requesting available page.");
    }

    storeValueAtMemLoc(RETURNED_MMAP_PAGE_LOC, returnedPage);
}

void sysKill(int pidToKill)
{

    // ASSIGNMENT 4 TO DO

    // Kills a process (as long as the pid to kill is > 1)
    if (pidToKill > 1 && pidToKill != readValueFromMemLoc(RUNNING_PID_LOC) && pidToKill < MAX_PROCESSES)
    {
        // Mark the task state as PROC_KILLED
        updateTaskState(pidToKill, PROC_KILLED);

        // Close all files associated with this pid
        for (int i = 3; i < MAX_FILE_DESCRIPTORS; i++)
        {
            sysClose(i, pidToKill);
        }

        // Traverse all processes running and change the ppid for those from the killed pid to pid 1
        //   (as someone needs to be the parent of processes whose parents have been killed)
        for (int i = 1; i < MAX_PROCESSES; i++)
        {
            int task_struct_loc = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * i);
            struct task *Task = (struct task *)task_struct_loc;

            if (Task->ppid == pidToKill)
            {
                Task->ppid = 1;
            }
        }
    }
}

void sysSwitch(int pidToSwitchTo, int currentPid)
{

    // ASSIGNMENT 4 TO DO
    int switch_task_struct_loc = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (pidToSwitchTo - 1));
    struct task *switch_task = (struct task *)switch_task_struct_loc;

    if (switch_task->state == PROC_SLEEPING && pidToSwitchTo < MAX_PROCESSES && switch_task != 0)
    {
        int curr_task_struct_loc = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
        struct task *current_task = (struct task *)curr_task_struct_loc;

        // Make the currently running process go to sleep (change the state to PROC_SLEEPING)
        current_task->state = PROC_SLEEPING;

        // Change the state of the arg1 pid to PROC_RUNNING
        switch_task->state = PROC_RUNNING;

        // Update the RUNNING_PID_LOC value as this address in memory is used to allow a running process to know its own pid.
        //   (Consider using readValueFromMemLoc() and storeValueAtMemLoc() for these actions.)
        storeValueAtMemLoc(RUNNING_PID_LOC, pidToSwitchTo);

        // Make a context switch to the arg1 pid
        contextSwitch(pidToSwitchTo);
    }
}

void sysMemDump(int memAddressToDump)
{
    clearScreen();

    for (int row = 1; row < 17; row++)
    {
        for (int column = 1; column < 48; column++)
        {
            printHexNumber(COLOR_WHITE, row, column, *(char *)memAddressToDump);
            memAddressToDump++;
            column++;
            column++;
        }
    }
}

void sysUptime()
{
    printSystemUptime(uptimeSeconds, uptimeMinutes);
}

void sysSwitchToParent(int currentPid)
{
    currentPid = readValueFromMemLoc(RUNNING_PID_LOC);

    int currentTaskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));

    struct task *currentTask = (struct task *)currentTaskStructLocation;

    updateTaskState(currentTask->pid, PROC_SLEEPING);
    updateTaskState(currentTask->ppid, PROC_RUNNING);

    // Letting the new process know its pid
    storeValueAtMemLoc(RUNNING_PID_LOC, currentTask->ppid);

    contextSwitch(currentTask->ppid);
}

void sysWait()
{
    // Waits one second and returns

    enableInterrupts();

    int futureSystemTimerInterruptCount = (systemTimerInterruptCount + SYSTEM_INTERRUPTS_PER_SECOND);

    while (systemTimerInterruptCount <= futureSystemTimerInterruptCount)
    {
    }
    disableInterrupts();
}

void sysWaitOneInterrupt()
{
    // Waits one second and returns

    enableInterrupts();

    int futureSystemTimerInterruptCount = (systemTimerInterruptCount + 1);

    while (systemTimerInterruptCount <= futureSystemTimerInterruptCount)
    {
    }
    disableInterrupts();
}

void sysDirectory(int currentPid)
{
    int cursor = 0;

    fillMemory((char *)KERNEL_TEMP_INODE_LOC, 0x0, (PAGE_SIZE * 2));
    readBlock(ROOTDIR_BLOCK, (char *)KERNEL_TEMP_INODE_LOC);
    readBlock(ROOTDIR_BLOCK + 1, (char *)(KERNEL_TEMP_INODE_LOC + BLOCK_SIZE));
    readBlock(ROOTDIR_BLOCK + 2, (char *)(KERNEL_TEMP_INODE_LOC + (BLOCK_SIZE * 2)));
    readBlock(ROOTDIR_BLOCK + 3, (char *)(KERNEL_TEMP_INODE_LOC + (BLOCK_SIZE * 3)));

    struct directoryEntry *DirectoryEntry = (directoryEntry *)(KERNEL_TEMP_INODE_LOC);
    struct ext2SuperBlock *Ext2SuperBlock = (ext2SuperBlock *)SUPERBLOCK_LOC;

    printString(COLOR_WHITE, cursor++, 2, (char *)"Root Dir");

    char *psUpperLeftCorner = kMalloc(currentPid, sizeof(char));
    *psUpperLeftCorner = ASCII_UPPERLEFT_CORNER;

    char *psHorizontalLine = kMalloc(currentPid, sizeof(char));
    *psHorizontalLine = ASCII_HORIZONTAL_LINE;
    cursor++;
    for (int columnPos = 3; columnPos < 77; columnPos++)
    {
        printString(COLOR_WHITE, cursor, columnPos, psHorizontalLine);
    }

    char *psVerticalLine = kMalloc(currentPid, sizeof(char));
    *psVerticalLine = ASCII_VERTICAL_LINE;

    char *psUpperRightCorner = kMalloc(currentPid, sizeof(char));
    *psUpperRightCorner = ASCII_UPPERRIGHT_CORNER;

    char *psLowerLeftCorner = kMalloc(currentPid, sizeof(char));
    *psLowerLeftCorner = ASCII_LOWERLEFT_CORNER;

    char *psLowerRightCorner = kMalloc(currentPid, sizeof(char));
    *psLowerRightCorner = ASCII_LOWERRIGHT_CORNER;

    cursor++;

    printString(COLOR_WHITE, 2, 2, psUpperLeftCorner);
    printString(COLOR_RED, 2, 4, (char *)"In");
    printString(COLOR_RED, 2, 8, (char *)"Filename");
    printString(COLOR_RED, 2, 20, (char *)"Bytes");
    printString(COLOR_RED, 2, 27, (char *)"Unix Create");
    printString(COLOR_RED, 2, 40, (char *)"Unix Modify");
    printString(COLOR_RED, 2, 53, (char *)"Type");
    printString(COLOR_RED, 2, 59, (char *)"Other");
    printString(COLOR_RED, 2, 65, (char *)"Group");
    printString(COLOR_RED, 2, 71, (char *)"User");
    printString(COLOR_WHITE, 2, 77, psUpperRightCorner);

    while ((int)DirectoryEntry->directoryInode != 0)
    {
        char *directoryFilename = kMalloc(currentPid, strlen((char *)&DirectoryEntry->fileName));
        char *directoryFileSize = kMalloc(currentPid, sizeof(int));
        char *fileCreateTimeUnix = kMalloc(currentPid, sizeof(int));
        // char *fileCreateTimeUnixYear = kMalloc(currentPid, sizeof(int));
        // char *fileCreateTimeUnixDay = kMalloc(currentPid, sizeof(int));
        char *fileModifyTimeUnix = kMalloc(currentPid, sizeof(int));
        struct time *Time = (struct time *)kMalloc(currentPid, sizeof(struct time));

        printString(COLOR_WHITE, (cursor++), 2, psVerticalLine);
        printHexNumber(COLOR_LIGHT_BLUE, (cursor - 1), 4, (unsigned char)DirectoryEntry->directoryInode);

        memoryCopy((char *)&DirectoryEntry->fileName, directoryFilename, 6);
        printString(COLOR_WHITE, (cursor - 1), 8, directoryFilename);

        fsFindFile(directoryFilename, EXT2_TEMP_INODE_STRUCTS);
        struct inode *Inode = (struct inode *)EXT2_TEMP_INODE_STRUCTS;

        itoa(Inode->i_size, directoryFileSize);
        printString(COLOR_LIGHT_BLUE, cursor - 1, 20, directoryFileSize);

        itoa(Inode->i_mtime, fileCreateTimeUnix);
        printString(COLOR_GREEN, cursor - 1, 27, fileCreateTimeUnix);

        // Time = convertFromUnixTime(Inode->i_ctime);
        // itoa(Time->year, fileCreateTimeUnixYear);
        // printString(COLOR_GREEN, cursor-1, 27, fileCreateTimeUnixYear);
        // printString(COLOR_GREEN, cursor-1, 31, (char*)"-");
        // itoa(Time->dayOfYear, fileCreateTimeUnixDay);
        // printString(COLOR_GREEN, cursor-1, 32, fileCreateTimeUnixDay);

        itoa(Inode->i_mtime, fileModifyTimeUnix);
        printString(COLOR_GREEN, cursor - 1, 40, fileModifyTimeUnix);

        printString(COLOR_GREEN, cursor - 1, 53, directoryEntryTypeTranslation((Inode->i_mode >> 12) & 0x000F));

        // Other Permissions
        printString(COLOR_GREEN, cursor - 1, 60, octalTranslation(((Inode->i_mode >> 6) & 0b0000000000000111)));

        // Group Permissions
        printString(COLOR_GREEN, cursor - 1, 66, octalTranslation(((Inode->i_mode >> 3) & 0b0000000000000111)));

        // User Permissions
        printString(COLOR_GREEN, cursor - 1, 72, octalTranslation((Inode->i_mode & 0b0000000000000111)));

        printString(COLOR_WHITE, (cursor - 1), 77, psVerticalLine);

        kFree(directoryFilename);
        kFree(directoryFileSize);
        kFree(fileCreateTimeUnix);
        // kFree(fileCreateTimeUnixYear);
        // kFree(fileCreateTimeUnixDay);
        kFree(fileModifyTimeUnix);
        kFree((char *)Time);

        DirectoryEntry = (directoryEntry *)((int)DirectoryEntry + DirectoryEntry->recLength);
    }

    printString(COLOR_WHITE, cursor, 2, psLowerLeftCorner);
    printString(COLOR_WHITE, cursor, 77, psLowerRightCorner);

    for (int columnPos = 3; columnPos < 77; columnPos++)
    {
        printString(COLOR_WHITE, cursor, columnPos, psHorizontalLine);
    }

    cursor++;

    char *volumeSizeBytes = kMalloc(currentPid, sizeof(int));
    itoa((Ext2SuperBlock->sb_total_blocks) * BLOCK_SIZE, volumeSizeBytes);
    printString(COLOR_GREEN, 0, 13, (char *)"FS Size:");
    printString(COLOR_LIGHT_BLUE, 0, 22, volumeSizeBytes);
    kFree(volumeSizeBytes);

    char *totalBlocksUsed = kMalloc(currentPid, sizeof(int));
    itoa(readTotalBlocksUsed(), totalBlocksUsed);
    printString(COLOR_GREEN, 1, 13, (char *)"Total Blocks Used:");
    printString(COLOR_LIGHT_BLUE, 1, 32, totalBlocksUsed);
    kFree(totalBlocksUsed);

    char *volumeRemainingBytes = kMalloc(currentPid, sizeof(int));
    itoa((Ext2SuperBlock->sb_total_blocks - readTotalBlocksUsed()) * BLOCK_SIZE, volumeRemainingBytes);
    printString(COLOR_GREEN, 0, 30, (char *)"Free:");
    printString(COLOR_LIGHT_BLUE, 0, 36, volumeRemainingBytes);
    kFree(volumeRemainingBytes);

    char *volumeTotalInodes = kMalloc(currentPid, sizeof(int));
    itoa((Ext2SuperBlock->sb_total_inodes) * BLOCK_SIZE, volumeTotalInodes);
    printString(COLOR_GREEN, 0, 44, (char *)"Total Inodes:");
    printString(COLOR_LIGHT_BLUE, 0, 58, volumeTotalInodes);
    kFree(volumeTotalInodes);

    char *nextAvailableInode = kMalloc(currentPid, sizeof(int));
    itoa(readNextAvailableInode(), nextAvailableInode);
    printString(COLOR_GREEN, 1, 44, (char *)"Next Avail Inode:");
    printString(COLOR_LIGHT_BLUE, 1, 62, nextAvailableInode);
    kFree(nextAvailableInode);

    char *volumeRemainingInodes = kMalloc(currentPid, sizeof(int));
    itoa((Ext2SuperBlock->sb_total_unallocated_inodes) * BLOCK_SIZE, volumeRemainingInodes);
    printString(COLOR_GREEN, 0, 65, (char *)"Free:");
    printString(COLOR_LIGHT_BLUE, 0, 71, volumeRemainingInodes);
    kFree(volumeRemainingInodes);

    kFree(psHorizontalLine);
    kFree(psVerticalLine);
    kFree(psUpperLeftCorner);
    kFree(psUpperRightCorner);
    kFree(psLowerLeftCorner);
    kFree(psLowerRightCorner);
}

void sysToggleScheduler()
{
    struct kernelConfiguration *KernelConfiguration = (struct kernelConfiguration *)KERNEL_CONFIGURATION;

    if (KernelConfiguration->runScheduler == 0)
    {
        KernelConfiguration->runScheduler = 1;
    }
    else if (KernelConfiguration->runScheduler == 1)
    {
        KernelConfiguration->runScheduler = 0;
    }
}
void sysShowOpenFiles(int currentPid)
{
    int currentTaskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *currentTask = (struct task *)currentTaskStructLocation;

    struct openFileTableEntry *OpenFileTableEntry = (struct openFileTableEntry *)OPEN_FILE_TABLE;
    int column = 0;
    int startingFileDescriptor = 3;

    while (startingFileDescriptor < MAX_FILE_DESCRIPTORS)
    {
        if (currentTask->fileDescriptor[startingFileDescriptor] != 0x0)
        {
            int addressOfOpenFile = (int)currentTask->fileDescriptor[startingFileDescriptor];

            OpenFileTableEntry = (struct openFileTableEntry *)addressOfOpenFile;

            printHexNumber(COLOR_LIGHT_BLUE, 23, column, startingFileDescriptor);
            if (OpenFileTableEntry->openedByPid == currentPid && OpenFileTableEntry->lockedForWriting == FILE_LOCKED)
            {
                printCharacter(COLOR_WHITE, 23, (column + 2), (char *)":");
                printString(COLOR_LIGHT_BLUE, 23, (column + 3), (char *)"RW");
            }
            else if (OpenFileTableEntry->openedByPid == currentPid && OpenFileTableEntry->lockedForWriting != FILE_LOCKED)
            {
                printCharacter(COLOR_WHITE, 23, (column + 2), (char *)":");
                printString(COLOR_LIGHT_BLUE, 23, (column + 3), (char *)"RO");
            }
            column = column + 6;
            printString(COLOR_WHITE, 23, column, (char *)(int)OpenFileTableEntry->fileName);
            column = column + 12;
        }

        startingFileDescriptor++;
    }
}

void sysCreate(struct fileParameter *FileParameter, int currentPid)
{
    createFile(FileParameter->fileName, currentPid, FileParameter->fileDescriptor);
}

void sysDelete(struct fileParameter *FileParameter, int currentPid)
{
    deleteFile(FileParameter->fileName, currentPid);
}

void sysOpenEmpty(struct fileParameter *FileParameter, int currentPid)
{
    char *newBinaryFilenameLoc = kMalloc(currentPid, FileParameter->fileNameLength);
    strcpyRemoveNewline(newBinaryFilenameLoc, FileParameter->fileName);

    int taskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *Task = (struct task *)taskStructLocation;

    if (Task->nextAvailableFileDescriptor >= MAX_FILE_DESCRIPTORS)
    {
        return; // reached the max open files
    }

    int pagesNeedForTmpBinary = FileParameter->requestedSizeInPages;

    char *requestedBuffer = findBuffer(currentPid, pagesNeedForTmpBinary, PG_USER_PRESENT_RW);

    // request block of pages for temporary file storage to load file based on first available page above
    for (int pageCount = 0; pageCount < pagesNeedForTmpBinary; pageCount++)
    {
        if (!requestSpecificPage(currentPid, (char *)((int)requestedBuffer + (pageCount * PAGE_SIZE)), PG_USER_PRESENT_RW))
        {
            clearScreen();
            printString(COLOR_RED, 2, 2, (char *)"Requested page is not available");
            panic((char *)"syscalls.cpp -> USER_TEMP_FILE_LOC page request");
        }

        // Zero each page in case it has been used previously
        fillMemory((char *)((int)requestedBuffer + (pageCount * PAGE_SIZE)), 0x0, PAGE_SIZE);
    }

    Task->fileDescriptor[Task->nextAvailableFileDescriptor] = (openFileTableEntry *)insertOpenFileTableEntry((char *)OPEN_FILE_TABLE, (int)(0xFFFF), currentPid, requestedBuffer, pagesNeedForTmpBinary, newBinaryFilenameLoc, 0, 0);

    struct openBufferTable *openBufferTable = (struct openBufferTable *)OPEN_BUFFER_TABLE;

    storeValueAtMemLoc(CURRENT_FILE_DESCRIPTOR, ((int)Task->nextAvailableFileDescriptor));
    storeValueAtMemLoc((char *)&openBufferTable->buffers[Task->nextAvailableFileDescriptor], (int)requestedBuffer);

    createFile(newBinaryFilenameLoc, currentPid, Task->nextAvailableFileDescriptor);

    sysClose(Task->nextAvailableFileDescriptor, currentPid);
}

void syscallHandler()
{
    // This code is very sensitive.
    // Don't Touch!
    asm volatile("pusha\n\t");

    int syscallNumber;
    int arg1;
    int currentPid;
    asm volatile("movl %%eax, %0\n\t" : "=r"(syscallNumber) :);
    asm volatile("movl %%ebx, %0\n\t" : "=r"(arg1) :);
    asm volatile("movl %%ecx, %0\n\t" : "=r"(currentPid) :);

    sysHandlertaskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    SysHandlerTask = (struct task *)sysHandlertaskStructLocation;

    // These manually grab from the stack are very sensitive to local variables.
    // The stack prior to iret should be:
    // (top to bottom) -> EIP -> CS -> UKNOWN (EAX) -> ESP

    asm volatile("movl %esp, %edx\n\t");
    asm volatile("add $8, %edx\n\t");
    asm volatile("movl (%edx), %edx\n\t");
    asm volatile("movl %%edx, %0\n\t" : "=r"((unsigned int)(SysHandlerTask->ebp)) :);

    asm volatile("movl %esp, %edx\n\t");
    asm volatile("add $60, %edx\n\t");
    asm volatile("movl (%edx), %edx\n\t");
    asm volatile("movl %%edx, %0\n\t" : "=r"((unsigned int)(SysHandlerTask->eip)) :);

    asm volatile("movl %esp, %edx\n\t");
    asm volatile("add $64, %edx\n\t");
    asm volatile("movl (%edx), %edx\n\t");
    asm volatile("mov %%dx, %0\n\t" : "=r"((unsigned short)(SysHandlerTask->cs)) :);

    asm volatile("movl %esp, %edx\n\t");
    asm volatile("add $68, %edx\n\t");
    asm volatile("movl (%edx), %edx\n\t");
    asm volatile("movl %%edx, %0\n\t" : "=r"((unsigned int)(SysHandlerTask->eax)) :);

    asm volatile("movl %esp, %edx\n\t");
    asm volatile("add $72, %edx\n\t");
    asm volatile("movl (%edx), %edx\n\t");
    asm volatile("movl %%edx, %0\n\t" : "=r"((unsigned int)(SysHandlerTask->esp)) :);

    if ((unsigned int)syscallNumber == SYS_SOUND)
    {
        sysSound((struct soundParameter *)arg1);
    }
    else if ((unsigned int)syscallNumber == SYS_OPEN)
    {
        sysOpen((struct fileParameter *)arg1, currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_WRITE)
    {
        sysWrite(arg1, currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_CLOSE)
    {
        sysClose(arg1, currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_PS)
    {
        sysPs(currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_FORK_EXEC)
    {
        sysForkExec((struct fileParameter *)arg1, currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_EXIT)
    {
        sysExit(currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_FREE)
    {
        sysFree(currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_MMAP)
    {
        sysMmap(currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_KILL)
    {
        sysKill(arg1);
    }
    else if ((unsigned int)syscallNumber == SYS_SWITCH_TASK)
    {
        sysSwitch(arg1, currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_MEM_DUMP)
    {
        sysMemDump(arg1);
    }
    else if ((unsigned int)syscallNumber == SYS_UPTIME)
    {
        sysUptime();
    }
    else if ((unsigned int)syscallNumber == SYS_SWITCH_TASK_TO_PARENT)
    {
        sysSwitchToParent(currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_WAIT)
    {
        sysWait();
    }
    else if ((unsigned int)syscallNumber == SYS_DIR)
    {
        sysDirectory(currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_TOGGLE_SCHEDULER)
    {
        sysToggleScheduler();
    }
    else if ((unsigned int)syscallNumber == SYS_SHOW_OPEN_FILES)
    {
        sysShowOpenFiles(currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_BEEP)
    {
        sysBeep();
    }
    else if ((unsigned int)syscallNumber == SYS_CREATE)
    {
        sysCreate((struct fileParameter *)arg1, currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_DELETE)
    {
        sysDelete((struct fileParameter *)arg1, currentPid);
    }
    else if ((unsigned int)syscallNumber == SYS_OPEN_EMPTY)
    {
        sysOpenEmpty((struct fileParameter *)arg1, currentPid);
    }

    scheduler(currentPid);

    returnedPid = readValueFromMemLoc(RUNNING_PID_LOC);
    newSysHandlertaskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (returnedPid - 1));
    newSysHandlerTask = (struct task *)newSysHandlertaskStructLocation;

    asm volatile("popa\n\t");
    asm volatile("leave\n\t");

    // pop off the important registers from the last task and save

    asm volatile("pop %edx\n\t"); // burn
    asm volatile("pop %edx\n\t"); // burn
    asm volatile("pop %edx\n\t"); // burn
    asm volatile("pop %edx\n\t"); // burn

    // Now recreate the stack for the IRET after saving the registers
    // Pushing these in reverse order so they are ready for the iret

    asm volatile("movl %0, %%edx\n\t" : : "r"((unsigned int)newSysHandlerTask->esp));
    asm volatile("pushl %edx\n\t");

    asm volatile("movl %0, %%edx\n\t" : : "r"((unsigned int)newSysHandlerTask->eax));
    asm volatile("pushl %edx\n\t");

    asm volatile("mov %0, %%dx\n\t" : : "r"((unsigned short)newSysHandlerTask->cs));
    asm volatile("pushl %eax\n\t");

    asm volatile("movl %0, %%edx\n\t" : : "r"((unsigned int)newSysHandlerTask->eip));
    asm volatile("pushl %edx\n\t");

    asm volatile("movl %0, %%ebp\n\t" : : "r"((unsigned int)newSysHandlerTask->ebp));

    asm volatile("iret\n\t");
}

void systemInterruptHandler()
{
    asm volatile("pusha\n\t");

    unsigned char currentInterrupt = 0;
    int currentPid = readValueFromMemLoc(RUNNING_PID_LOC);

    disableInterrupts();

    totalInterruptCount++;

    int currentTaskStructLocation = PROCESS_TABLE_LOC + (TASK_STRUCT_SIZE * (currentPid - 1));
    struct task *currentTask;
    currentTask = (struct task *)currentTaskStructLocation;

    currentTask->runtime++;
    currentTask->recentRuntime++;

    int taskStructNumber = 0;
    struct task *Task = (struct task *)PROCESS_TABLE_LOC;

    while (taskStructNumber < MAX_PROCESSES)
    {
        if (Task->state == PROC_SLEEPING)
        {
            Task->sleepTime++;
        }
        taskStructNumber++;
        Task++;
    }

    outputIOPort(MASTER_PIC_COMMAND_PORT, 0xA);
    outputIOPort(MASTER_PIC_COMMAND_PORT, 0xB);
    currentInterrupt = inputIOPort(MASTER_PIC_COMMAND_PORT);

    if ((currentInterrupt & 0b0000001) == 0x1) // system timer IRQ 0
    {
        systemTimerInterruptCount++;

        if (totalInterruptCount % SYSTEM_INTERRUPTS_PER_SECOND)
        {
            uptimeSeconds = totalInterruptCount / SYSTEM_INTERRUPTS_PER_SECOND;
            if (uptimeSeconds == 60)
            {
                uptimeSeconds = 0;
            }
            uptimeMinutes = ((totalInterruptCount / SYSTEM_INTERRUPTS_PER_SECOND) / 60);
        }
    }
    else if ((currentInterrupt & 0b0000010) == 0x2) // keyboard interrupt IRQ 1
    {
        keyboardInterruptCount++;
    }
    else
    {
        otherInterruptCount++; // capture any other interrupts
    }

    outputIOPort(MASTER_PIC_COMMAND_PORT, INTERRUPT_END_OF_INTERRUPT);
    outputIOPort(SLAVE_PIC_COMMAND_PORT, INTERRUPT_END_OF_INTERRUPT);

    enableInterrupts();

    asm volatile("popa\n\t");
    asm volatile("leave\n\t");
    asm volatile("iret\n\t");
}

void printSystemUptime(int uptimeSeconds, int uptimeMinutes)
{
    char *uptimeSecondsLoc = kMalloc(KERNEL_OWNED, sizeof(int));
    char *uptimeMinutesLoc = kMalloc(KERNEL_OWNED, sizeof(int));

    itoa((unsigned int)(uptimeSeconds), uptimeSecondsLoc);
    itoa((unsigned int)(uptimeMinutes), uptimeMinutesLoc);

    printString(COLOR_GREEN, 4, 2, (char *)"Uptime (mins):");
    printString(COLOR_LIGHT_BLUE, 4, 30, uptimeMinutesLoc);
    printString(COLOR_GREEN, 5, 2, (char *)"Uptime (secs):");
    printString(COLOR_LIGHT_BLUE, 5, 30, uptimeSecondsLoc);

    kFree(uptimeSecondsLoc);
    kFree(uptimeMinutesLoc);
}
