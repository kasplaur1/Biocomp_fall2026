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

#define HS3000_ADDR (0x44 << 1) // HS3000 default 7-but address is 0x44
#define SDA_PIN P0_14
#define SCL_PIN P0_15

//event flags
#define HUMIDITY (1 << 0) // pin 0
#define TEMPERATURE (1 << 1) // pin 1

I2C i2c(SDA_PIN, SCL_PIN); // you still need to figure these out
DigitalOut hs_power(P0_22); // sensor power control (HS3003 VDD/VDDIO via P0.22)

USBSerial serial;
EventFlags flag;
Mutex mutex;
Mutex i2c_mutex; // protects i2c transactions

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

//read humidity and temperature from HS3000
// returns true when value measurement recieved
bool read_hs3000(float &humidity, float &temperature){
    char data[4] = {0};

    i2c_mutex.lock();
    int result = i2c.write(HS3000_ADDR, nullptr, 0);
    i2c_mutex.unlock();

    if(result != 0){
        i2c_mutex.unlock();
        return false;
    }

    thread_sleep_for(10); // allow time for measurement to complete
    
    i2c_mutex.lock();
    result = i2c.read(HS3000_ADDR, data, 4); // read 4 byte measurement response
    i2c_mutex.unlock();

    if(result != 0){
        return false;
    }

    unsigned char byte0 = (unsigned char)data[0]; // upper 2 bits are the status bits
    unsigned char byte1 = (unsigned char)data[1];
    unsigned char byte2 = (unsigned char)data[2];
    unsigned char byte3 = (unsigned char)data[3];
    unsigned int status = (byte0 >> 6) & 0x03;

    if(status != 0){
        return false; //status 0 meanas the measurement is valid
    }
    
    unsigned int raw_humidity = (((unsigned int)(byte0 & 0x3F)) << 8) | byte1;

    unsigned int raw_temperature = ((((unsigned int)byte2 << 8) | byte3) >> 2);

    humidity = ((float)raw_humidity / 16383.0f) * 165.0f - 40.0f; // convert raw values using HS3000 datasheet equations
    temperature = ((float)raw_temperature / 16383.0f) * 165.0f - 40.0f;

    return true;

}

//humidity thread
void read_humidity(){
    mutex.lock();
    serial.printf("Humidity thread started\r\n");
    mutex.unlock();

    while(1){
        flag.wait_any(HUMIDITY); //wait until HUMIDITY is set
        humidity_led = 1; // turn LEDR on

        mutex.lock();
        serial.printf("Humidity event recieved\r\n");
        mutex.unlock();

        humidity_led = 1;
        thread_sleep_for(200);
        humidity_led = 0;
/*
        float humidity_value = 0.0f;
        float temperature_value = 0.0f; 

        if(read_hs3000(humidity_value, temperature_value)){
            mutex.lock();
            serial.printf("reading humidity: %.2f %%RH\r\n", humidity_value);
            mutex.unlock();  
        }
        else{
            mutex.lock();
            serial.printf("failed reading humidity\r\n");
            mutex.unlock();
        }
            thread_sleep_for(200); // thread stays on for 1 second
            humidity_led = 0; //turn LEDR off
    }*/}
}

//temperature thread
void read_temperature(){
    mutex.lock();
    serial.printf("Temperature thread started\r\n");
    mutex.unlock();

    while(1){
        flag.wait_any(TEMPERATURE); //wait until TEMPERATURE is set
        
        mutex.lock();
        serial.printf("Temperature event recieved\r\n");
        mutex.unlock();

        temperature_led = 1;
        thread_sleep_for(200);
        temperature_led = 0;
/*
        temperature_led = 1; // turn LEDG on

        float humidity_value = 0.0f;
        float temperature_value = 0.0f;

        mutex.lock();
        serial.printf("starting sensor read\r\n");
        mutex.unlock();

        if(read_hs3000(humidity_value, temperature_value)){
            mutex.lock();
            serial.printf("reading temperature: %.2f C\r\n", temperature_value);
            mutex.unlock(); 
        }
        else{
            mutex.lock();
            serial.printf("failed reading temperature\r\n");
            mutex.unlock();
        }

        serial.printf("sensor read finished\r\n");
        thread_sleep_for(200); // thread stays on for 1 second
        temperature_led = 0; // turn LEDG off
        serial.printf("temperature LED turned off\r\n");
    }*/}
}


// main() runs in its own thread in the OS
int main()
{
    hs_power = 1; // power up HS3003
    i2c.frequency(100000); // i2c for standard mode communication;

    thread_sleep_for(1000); // give the sensor time to power up

    mutex.lock();
    serial.printf("Lab 3 started \r\n");
    mutex.unlock();

    humidity_thread.start(read_humidity);
    temperature_thread.start(read_temperature);
    ticker.attach(&ticker_thread, 1s);

    while (1) {
        thread_sleep_for(1000); //keep main on forever
    }
}

