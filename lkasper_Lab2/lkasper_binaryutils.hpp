/*
Name: Lauren M Kasper
Assignment: Lab2_4280_2026
Date: 09/22/26
Overview: 
- Make PMW work three ways, simultaneously
- eat some neapolitan ice cream to cope with this lab
*/


// volatile means the compiler from optimizing away reads/writes - varaibels value can change reguardless f how the program flows 

#define LEDR (1 << 24)     //Red LED P0.24 
#define LEDG (1 << 16)     //Green LED P0.16 
#define LEDB (1 << 6)      //Blue LED P0.06 

#define vanilla_o (1 << 27); // output 

#define OUTSET (*(volatile uint32_t*)0x50000508)
#define OUTCLR (*(volatile uint32_t*)0x5000050C)
#define DIRSET (*(volatile uint32_t*)0x50000518)
#define DIRCLR (*(volatile uint32_t*)0x5000051C)

//diagnostic states
enum state{
    NO_ERROR = 0, // this is automatically set to 0
    ATTN_REQ, // 1
    FATAL_ERROR // 2
};

// Mail struct
//typedef = creates new name for a type
typedef struct{
    state diag_state;
} Message;

// holds one cycle: 
//produce fulls/pushes into queue & consumers read and apply PWM
struct Cycle{
    float percent; // cycle between 0 & 1
};

