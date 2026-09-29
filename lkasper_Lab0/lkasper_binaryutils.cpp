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

#include "lkasper_binaryutils.hpp"

// sets only the bit at position whichbit to 1
void setbit(uint32_t* addr, uint8_t whichbit){
    // create mask with a 1 at position whichbit
    uint32_t mask = (static_cast<uint32_t>(1) << whichbit);
    *addr |= mask; //OR sets bit to 1, leaves all the rest the same
}

// sets only the bit at position whichbit to 0
void clearbit(uint32_t* addr, uint8_t whichbit){
    // creates mask with 1 at whichbit
    uint32_t mask = (static_cast<uint32_t>(1) << whichbit);
    *addr &= ~mask; //NOT mask = 0 at whichbit, 1 everywhere else the AND clearns that desired bit
}

// set only bits defined in the mask
void setbits(uint32_t* addr, uint32_t bitmask){
    // Any 1 becomes 1 in bitmask
    *addr |= bitmask;
}

// clear only the bits defined in the mask
void clearbits(uint32_t* addr, uint32_t bitmask){
    // Any 1 becomes 0 in bitmask
    *addr &= ~bitmask;
}

// convert 32-bit number into string of characters and return it
char* display_binary(uint32_t num){
    static char characters[33]; //32 bits + null terminator
    int firstBit = 31; // starts at the first bit
    int element = 0; // element within the string

    while(firstBit >= 0){
        uint32_t mask = (1 << firstBit);

        if(num & mask){
            characters[element] = '1';
        }
        else{
            characters[element] = '0';
        }
        firstBit--;
        element++;
    }

    characters[32] = '\0';
    return characters;
}