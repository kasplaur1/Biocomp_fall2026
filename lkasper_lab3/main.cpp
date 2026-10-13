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
DigitalOut i2c_pullup(P1_0);  // I2C pull-up control

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

bool read_hs3000(float &humidity, float &temperature)
{
    char data[4] = {0, 0, 0, 0};
    int result;

    // Keep the complete measurement sequence together.
    // Both threads share the same sensor.
    i2c_mutex.lock();

    // STEP 1: Request a measurement
    result = i2c.write(HS3000_ADDR, nullptr, 0);

    if (result != 0) {
        i2c_mutex.unlock();

        mutex.lock();
        serial.printf("ERROR: I2C write failed: %d\r\n", result);
        mutex.unlock();

        return false;
    }

    // STEP 2: Wait for the sensor to finish measuring
    thread_sleep_for(35);

    // STEP 3: Read the four response bytes
    result = i2c.read(HS3000_ADDR, data, 4);

    i2c_mutex.unlock();

    if (result != 0) {
        mutex.lock();
        serial.printf("ERROR: I2C read failed: %d\r\n", result);
        mutex.unlock();

        return false;
    }

    // STEP 4: Convert bytes to unsigned values
    unsigned char b0 = (unsigned char)data[0];
    unsigned char b1 = (unsigned char)data[1];
    unsigned char b2 = (unsigned char)data[2];
    unsigned char b3 = (unsigned char)data[3];

    // Print raw data for debugging
    mutex.lock();
    serial.printf("Raw data: %02X %02X %02X %02X\r\n", b0, b1, b2, b3);
    mutex.unlock();

    // STEP 5: Check measurement status
    unsigned int status = (b0 >> 6) & 0x03;

    if (status != 0) {
        mutex.lock();
        serial.printf("ERROR: Invalid sensor status: %u\r\n",
                      status);
        mutex.unlock();

        return false;
    }

    // STEP 6: Extract 14-bit humidity value
    unsigned int raw_humidity =
        (((unsigned int)(b0 & 0x3F)) << 8) |
        (unsigned int)b1;

    // STEP 7: Extract 14-bit temperature value
    unsigned int raw_temperature =
        ((((unsigned int)b2 << 8) |
          (unsigned int)b3) >> 2);

    
    // STEP 8: Convert raw measurements to physical units
    humidity =
        ((float)raw_humidity / 16383.0f) * 100.0f;

    // Convert the 14-bit raw temperature to Celsius
    temperature =
        ((float)raw_temperature / 16383.0f) * 165.0f - 40.0f;

    // Convert Celsius to Fahrenheit
    temperature = (temperature * 9.0f / 5.0f) + 32.0f;


    // STEP 9: Confirm successful measurement
   
    mutex.lock();
    int humidity_x100 = (int)(humidity * 100.0f);
    int temperature_x100 = (int)(temperature * 100.0f);

    serial.printf("Humidity: %d.%02d %%\r\n", humidity_x100 / 100, abs(humidity_x100 % 100));
    serial.printf("Temperature: %d.%02d F\r\n", temperature_x100 / 100, abs(temperature_x100 % 100));
    mutex.unlock();

    return true;
}


//humidity thread


void read_humidity()
{
    mutex.lock();
    serial.printf("Humidity thread started\r\n");
    mutex.unlock();

    while (1) {
        // Wait for the humidity event
        flag.wait_any(HUMIDITY);

        // Turn on the red humidity LED
        humidity_led = 1;

        float humidity_value = 0.0f;
        float temperature_value = 0.0f;

        mutex.lock();
        serial.printf("Reading humidity...\r\n");
        mutex.unlock();

        // Read the HS3000 sensor
        if (!read_hs3000(humidity_value, temperature_value)) {
            mutex.lock();
            serial.printf("Humidity measurement failed\r\n");
            mutex.unlock();
        }

        // Keep the LED on briefly
        thread_sleep_for(200);

        // Turn off the red humidity LED
        humidity_led = 0;
    }
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
    }
}


// main() runs in its own thread in the OS
int main()
{
    // Enable the I2C bus pull-ups
    i2c_pullup = 1;

    // Power up the onboard sensors
    hs_power = 1;

    // Configure I2C speed
    i2c.frequency(100000);

    // Allow sensors to power up
    thread_sleep_for(1000);

    mutex.lock();
    serial.printf("Lab 3 started\r\n");
    mutex.unlock();

    humidity_thread.start(read_humidity);
    temperature_thread.start(read_temperature);

    ticker.attach(&ticker_thread, 1s);

    while (1) {
        thread_sleep_for(1000);
    }
}


