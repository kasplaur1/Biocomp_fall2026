/*
Name: Lauren M Kasper
Assignment: Lab3_4280_2026
Date: 10/06/26
Overview: 
- Read temperature and humidity from the HS3000 sensor
- understand ISR and I2C
*/


// volatile means the compiler from optimizing away reads/writes - varaibels value can change reguardless f how the program flows 

#define LEDR (1 << 24)     //Red LED P0.24 
#define LEDG (1 << 16)     //Green LED P0.16 
#define LEDB (1 << 6)      //Blue LED P0.06 

#define OUTSET (*(volatile uint32_t*)0x50000508)
#define OUTCLR (*(volatile uint32_t*)0x5000050C)
#define DIRSET (*(volatile uint32_t*)0x50000518)
#define DIRCLR (*(volatile uint32_t*)0x5000051C)


