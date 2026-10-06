/*
Name: Lauren M Kasper
Assignment: Lab3_4280_2026
Date: 10/06/26
Overview: 
- Read temperature and humidity from the HS3000 sensor
- understand ISR and I2C
*/

#include "mbed.h"
#include "header.hpp"
#include "USBSerial.h"

//event flags
#define HUMIDITY (1 << 0) // pin 0
#define TEMPERATURE (1 << 1) // pin 1

USBSerial serial;
EventFlags flag;
Mutex mutex;

PwmOut  humidity_led(P0_24); //red LED  
PwmOut  temperature_led(P0_16); // green LED

// Ticker
Ticker ticker;
Thread humidity_thread;
Thread temperature_thread;

// Global Variable
volatile uint32_t ticker_count = 0;
volatile bool humidity_trigger = true; // alternate between humidity & temperature

//ticker thread
void ticker_thread(){
    ticker_count++;

    if(humidity_trigger){
        flag.set(HUMIDITY);
    }
    else{
        flag.set(TEMPERATURE);
    }

    humidity_trigger = !humidity_trigger; //flip for next flag interrupt
}

//humidity thread
void read_humidity(){
    while(1){
        flag.wait_any(HUMIDITY); //wait until HUMIDITY is set
        humidity_led = 1; // turn LEDR on
    
        mutex.lock();
        serial.printf("reading humidity\r\n");
        mutex.unlock();
        thread_sleep_for(1000); // thread stays on for 1 second
        humidity_led = 0; //turn LEDR off
    }
}

//temperature thread
void read_temperature(){
    while(1){
        flag.wait_any(TEMPERATURE); //wait until TEMPERATURE is set
        temperature_led = 1; // turn LEDG on

        mutex.lock();
        serial.printf("reading temperature\r\n");
        mutex.unlock();
        thread_sleep_for(1000); // thread stays on for 1 second
        temperature_led = 0; // turn LEDG off
    }
}


// main() runs in its own thread in the OS
int main()
{
    ticker.attach(&ticker_thread, 1s);
    humidity_thread.start(read_humidity);
    temperature_thread.start(read_temperature);
    while (1) {
        thread_sleep_for(1000); //keep main on forever
    }
}

