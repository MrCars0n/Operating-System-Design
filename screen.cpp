 // https://wiki.osdev.org/Printing_To_Screen

#include "screen.h"
#include "x86.h"
#include "constants.h"

// Assignment 1
void clearScreen()
{       
    char* video_memory = (char*)VIDEO_RAM;
    for (int i = 0; i < 80 * 25 * 2; i++) 
    {
        video_memory[i] = 0;   // 0 = null
    }
}

// Assignment 1
void printCharacter(int color, int row, int column, char *message)
{
    // Handle special characters
    if (*message == 0x0A)      // Line Feed (0x0A)
    {
        row++;                 // Move cursor down to next row
        column = 0;            // Reset column to start of the line
        return;                // Print nothing for Line Feed
    } 
    else if (*message == 0x0D) // Carriage Return (0x0D)
    {
        column = 0;            // Move to start of line
        return;
    }
    else if (*message == 0x09) // Tab (0x09)
    {
        column += 4;
        return;
    }

    // Calculate the offset in video memory based on row and column
    int offset = (row * 80 + column) * 2;

    // Write character and color to video memory
    char* video_memory = (char*)VIDEO_RAM;  // Pointer to video memory starting at 0xB8000
    video_memory[offset] = *message;        // Write character at calculated position
    video_memory[offset + 1] = color;       // Set attribute byte to specified color
}

// Assignment 1
void printString(int color, int row, int column, char *message)
{
    while (*message) 
    {
        printCharacter(color, row, column, (char*)message);
        
        if (*message == 0x09)       // Tab (0x09)
        {
            column += 4;
        } 
        else if (column >= 80 || *message == (char)'\n')      // If end of a line reached or new line
        {
            column = 0;
            row++;
        }
        else 
        {
            column++;
        }
        message++;
    }
}

// Assignment 1
void printHexNumber(int color, int row, int column, unsigned char number)
{
    const char* table_hex = "0123456789abcdef";
    char string_hex[3];                 // 2 digits for hex number + null terminator
    string_hex[2] = '\0';               // Null-terminate the string

    // Prepare mask values
    unsigned char high_nibble_mask = 0xF0;   // Mask to extract high nibble
    unsigned char low_nibble_mask = 0x0F;    // Mask to extract low nibble

    // Extract high nibble (bits 4-7) and store as first hex digit
    unsigned char high_nibble = (number & high_nibble_mask) >> 4;  // Apply high nibble mask and shift right
    string_hex[0] = table_hex[high_nibble];

    // Extract low nibble (bits 0-3) and store as second hex digit
    unsigned char low_nibble = number & low_nibble_mask;            // Apply low nibble mask
    string_hex[1] = table_hex[low_nibble];

    // Print string_hex to screen
    printString(color, row, column, string_hex);
}


void printLogo(int lineNumber)
{
        printString(12, lineNumber++, 0,(char *)"                                                __                      _   __");
        printString(12, lineNumber++, 0,(char *)"                                               (_  o ._ _  ._  |  _    / \\ (_ ");
        printString(12, lineNumber++, 0,(char *)"                                               __) | | | | |_) | (/_   \\_/ __)");
        printString(12, lineNumber, 0,(char *)"                                                           |                  ");
        printString(9, lineNumber, 65, (char *)"version 3.0");
}

void enableCursor()
{
    outputIOPort(0x3D4, 0x0A);
    outputIOPort(0x3D5, (inputIOPort(0x3D5) & 0xC0) | 0xD); //scan line start

    outputIOPort(0x3D4, 0x0B);
    outputIOPort(0x3D5, (inputIOPort(0x3D5) & 0xE0) | 0xF); //scan line stop
}

void disableCursor()
{
    outputIOPort(0x3D4, 0x0A);
    outputIOPort(0x3D5, 0x20);
}

void moveCursor(int row, int column)
{
    unsigned short cursorPosition = row * 80 + column;

    outputIOPort(0x3D4, 0x0F);
	outputIOPort(0x3D5, (unsigned char)((unsigned short)cursorPosition & 0xFF));
    outputIOPort(0x3D4, 0x0E);
	outputIOPort(0x3D5, (unsigned char)(((unsigned short)cursorPosition >> 8) & 0xFF));

}