/*
Name: Lauren M Kasper
Assignment: Lab0_4280_2026
Date: 09/01/26
Overview: 
- set up the development enviornment for the semeseter
- get practice using C/C++
- learn/remember Bitwise operations
- set up declarations, definitions, and binanry solo
*/

#include "mbed.h"
#include "USBSerial.h" // need this for the connection
#include "lkasper_binaryutils.hpp"

//USBSerial virtual serial port over USB, needs a name
USBSerial serial(9600); // USB Serial
//serial = serial connection (chat helped me understand this)
//(9600) = 9600 bits per second (chat helped me understand this)


// main() runs in its own thread in the OS
int main()
{
    uint32_t solo = 0;

    // set 24th bit
    setbit(&solo, 24);

    // set 16th and 17th bit
    setbit(&solo, 16);
    setbit(&solo, 17);

    // set 0-11 bits
    setbits(&solo, 0xFFF);

    // clear 11th bit
    clearbit(&solo, 11);

    // clear 4th-7th bit
    clearbits(&solo, 0xF0);

    serial.write("Binary Solo:\n\r", 14);
    serial.write(display_binary(solo), 32);
    serial.write("\n\r", 2);

    while (true) {
        // keeps the program alive
    }
}

