/*
In Class Assignment 2
9/23/26
*/

#include "mbed.h"
#include "USBSerial.h"

//defines from my labs
#define LEDR (1 << 24)     //Red LED P0.24 
#define LEDG (1 << 16)     //Green LED P0.16 
#define LEDB (1 << 6)      //Blue LED P0.06

#define OUTSET (*(volatile uint32_t*)0x50000508)
#define OUTCLR (*(volatile uint32_t*)0x5000050C)
#define DIRSET (*(volatile uint32_t*)0x50000518)
#define DIRCLR (*(volatile uint32_t*)0x5000051C)
//defines from my labs

USBSerial serial;
Ticker tick; 
Thread thread;

volatile int tick_count = 0; // global integer variable for program

void ticker_ticker(){
    tick_count++; // tick the ticker by one each time
}

void thread_thread(){
    DIRSET = LEDR;

    while(1){
        if(tick_count >= 3000){
            OUTSET = LEDR;
            ThisThread::sleep_for(200ms);
            OUTCLR = LEDR;

            tick_count = 0;
        }
        ThisThread::sleep_for(1ms);
    }
}

// main() runs in its own thread in the OS
int main()
{
    thread.start(callback(thread_thread)); // start the thread
    tick.attach(ticker_ticker, 1ms); // start the ticker
    
    while(1){
        ThisThread::sleep_for(10s); //sleep for 10 seconds
    }
}

