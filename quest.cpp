#include "screen.h"
#include "keyboard.h"
#include "vm.h"
#include "simpleOSlibc.h"
#include "constants.h"
#include "x86.h"
#include "file.h"
#include "sound.h"

void printQuestLogo(int lineNumber)
{
    printString(12, lineNumber++, 0, (char *)"               /\\            ____                 _            /\\");
    printString(12, lineNumber++, 0, (char *)"  ____________ )(     _     /___ \\_   _  ___  ___| |_    _     )( ____________ ");
    printString(12, lineNumber++, 0, (char *)" <____________(**)\\\\\\(_)   //  / / | | |/ _ \\/ __| __|  (_)///(**)____________>");
    printString(12, lineNumber++, 0, (char *)"               )(         / \\_/ /| |_| |  __/\\__ \\ |_          )(");
    printString(12, lineNumber, 0, (char *)"               \\/         \\___,_\\ \\__,_|\\___||___/\\__|         \\/");
}

void printPrompt(unsigned char myPid)
{
    printQuestLogo(20);

    char *shellHorizontalLine = malloc(myPid, sizeof(char));
    *shellHorizontalLine = ASCII_HORIZONTAL_LINE;
    for (int columnPos = 0; columnPos < 80; columnPos++)
    {
        printString(COLOR_RED, 18, columnPos, shellHorizontalLine);
    }

    free(shellHorizontalLine);

    printString(COLOR_RED, 18, 3, (char *)" Adventurer's Choice ");
    printString(COLOR_RED, 19, 1, (char *)">");
}

void printBufferToScreen(char *userSpaceBuffer)
{
    int linearPosition = 0;

    if (userSpaceBuffer == 0x0)
    {
        printString(COLOR_RED, 21, 3, (char *)"No such buffer!");
        return; // null pointer
    }

    int row = 0;
    int column = 0;

    while (row <= 15)
    {
        if (column > 79)
        {
            column = 0;
            row++;
        }
        if (*(char *)((int)userSpaceBuffer + linearPosition) == (unsigned char)0x0a)
        {
            row++;
            column = 0;
        }
        if (*(char *)((int)userSpaceBuffer + linearPosition) == (unsigned char)0x0d)
        {
            column++;
        }
        if (*(char *)((int)userSpaceBuffer + linearPosition) == (unsigned char)0x09)
        {
            column = column + 4;
        }

        if (row <= 15)
        {
            printCharacter(COLOR_WHITE, row, column, (char *)((int)userSpaceBuffer + linearPosition));
        }

        column++;
        linearPosition++;
    }
}

void main()
{
    int myPid;

    disableCursor();
    clearScreen();

    myPid = readValueFromMemLoc(RUNNING_PID_LOC);

    // clearing shared buffer area
    fillMemory((char *)KEYBOARD_BUFFER, (unsigned char)0x0, (KEYBOARD_BUFFER_SIZE * 2));

    systemShowOpenFiles();
    myPid = readValueFromMemLoc(RUNNING_PID_LOC);
    char *commandArgument1 = malloc(myPid, 10);

    // Show starting file
    disableCursor();
    clearScreen();
    printPrompt(myPid);
    myPid = readValueFromMemLoc(RUNNING_PID_LOC);
    clearScreen();

    systemOpenFile((char *)"startup", RDONLY);

    myPid = readValueFromMemLoc(RUNNING_PID_LOC);
    int currentFileDescriptor;
    currentFileDescriptor = readValueFromMemLoc(CURRENT_FILE_DESCRIPTOR);

    struct openBufferTable *openBufferTable = (struct openBufferTable *)OPEN_BUFFER_TABLE;
    printBufferToScreen((char *)openBufferTable->buffers[currentFileDescriptor]);

    while (true)
    {

        char *bufferMem = (char *)KEYBOARD_BUFFER;
        char *cursorMemory = (char *)SHELL_CURSOR_POS;

        myPid = readValueFromMemLoc(RUNNING_PID_LOC);

        fillMemory((char *)KEYBOARD_BUFFER, (unsigned char)0x0, (KEYBOARD_BUFFER_SIZE * 2));

        printPrompt(myPid);
        readCommand(bufferMem, cursorMemory);

        char *command = (char *)COMMAND_BUFFER;
        commandArgument1 = (char *)(COMMAND_BUFFER + strlen(COMMAND_BUFFER) + 1);

        // Any commands that don't take an argument, add "\n" to the end
        char *goCommand = (char *)"go";
        char *exitCommand = (char *)"exit\n";
        char *runCommand = (char *)"new";

        if (strcmp(command, goCommand) == 0)
        {
            disableCursor();
            clearScreen();

            printPrompt(myPid);
            myPid = readValueFromMemLoc(RUNNING_PID_LOC);
            clearScreen();

            systemOpenFile(commandArgument1, RDONLY);

            myPid = readValueFromMemLoc(RUNNING_PID_LOC);
            int currentFileDescriptor;
            currentFileDescriptor = readValueFromMemLoc(CURRENT_FILE_DESCRIPTOR);

            struct openBufferTable *openBufferTable = (struct openBufferTable *)OPEN_BUFFER_TABLE;
            printBufferToScreen((char *)openBufferTable->buffers[currentFileDescriptor]);
        }
        else if (strcmp(command, exitCommand) == 0)
        {
            disableCursor();
            clearScreen();
            printPrompt(myPid);

            systemExit();
        }
        else if (strcmp(command, runCommand) == 0)
        {
            clearScreen();
            printPrompt(myPid);

            systemForkExec(commandArgument1, 60);

            myPid = readValueFromMemLoc(RUNNING_PID_LOC);

            freeAll(myPid);
            main(); // Seems to page fault when returning back from launched process without this
        }
        else
        {
            printPrompt(myPid);
            printString(COLOR_RED, 16, 3, (char *)"Command not recognized!");
            systemBeep();
            myPid = readValueFromMemLoc(RUNNING_PID_LOC);

            fillMemory((char *)KEYBOARD_BUFFER, (unsigned char)0x0, (KEYBOARD_BUFFER_SIZE * 2));
            fillMemory((char *)SHELL_CURSOR_POS, (unsigned char)0x0, 40);
            wait(1);
            printString(COLOR_WHITE, 21, 3, (char *)"                                ");

            myPid = readValueFromMemLoc(RUNNING_PID_LOC);
            freeAll(myPid);
        }
    }
}