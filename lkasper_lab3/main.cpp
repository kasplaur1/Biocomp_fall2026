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

//HS3000 sensor I2C address (7-bit address is 0x44, shifted for Mbeds API)
#define HS3000_ADDR (0x44 << 1) // HS3000 default 7-but address is 0x44
#define SDA_PIN P0_14 // pin definitions
#define SCL_PIN P0_15 // pin definitions

//event flags -> used to signal humidity or temperature tasks
#define HUMIDITY (1 << 0) // pin 0
#define TEMPERATURE (1 << 1) // pin 1

I2C i2c(SDA_PIN, SCL_PIN); // I2C interface object
DigitalOut hs_power(P0_22); // sensor power control (HS3003 VDD/VDDIO via P0.22)
DigitalOut i2c_pullup(P1_0);  // I2C pull-up control for bus

USBSerial serial;
EventFlags flag;
Mutex mutex; // protects serial printing
Mutex i2c_mutex; // protects i2c transactions

PwmOut  humidity_led(P0_24); //red LED  
PwmOut  temperature_led(P0_16); // green LED

// Ticker
Ticker ticker;
Thread humidity_thread; // worker thread
Thread temperature_thread; // worker thread

// Global Variable
volatile uint32_t ticker_count = 0;
volatile bool humidity_trigger = true; // alternate between humidity & temperature

//ticker thread ISR triggered by the ticker every 1 second
// Alternates between humidity and temperature events
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
bool read_hs3000(int humidity, int temperature)
{
    char data[4] = {0, 0, 0, 0};
    int result;

    i2c_mutex.lock(); // protects I2C bus from concurrent access 
    result = i2c.write(HS3000_ADDR, nullptr, 0); // request a measurement from the i2c bus (write with not data)

    if (result != 0) {
        i2c_mutex.unlock();
        mutex.lock();
        serial.printf("ERROR: I2C write failed: %d\r\n", result); // the i2c did not write a message back
        mutex.unlock();
        return false;
    }

    thread_sleep_for(35); // wait for the sensor to capture the measuring
    result = i2c.read(HS3000_ADDR, data, 4); // read the 4 bits for response
    i2c_mutex.unlock();

    if (result != 0) {
        mutex.lock();
        serial.printf("ERROR: I2C read failed: %d\r\n", result); // i2c did not read the message
        mutex.unlock();
        return false;
    }

    // Convert bytes to unsigned values/ extract raw bytes
    unsigned char b0 = data[0];
    unsigned char b1 = data[1];
    unsigned char b2 = data[2];
    unsigned char b3 = data[3];

    // Print raw data for debugging
    mutex.lock();
    serial.printf("Raw data: %02X %02X %02X %02X\r\n", b0, b1, b2, b3);
    mutex.unlock();

    unsigned int status = (b0 >> 6) & 0x03; // Check measurement status to see if valid

    if (status != 0) {
        mutex.lock();
        serial.printf("ERROR: Invalid sensor status: %u\r\n", status); // status was empty, error
        mutex.unlock();
        return false;
    }

    unsigned int raw_humidity = ((b0 & 0x3F) << 8) | b1; // extract 14-bit humidity value
    unsigned int raw_temperature = ((b2 << 8) | b3) >> 2; // extract 1-bit temperature value

    humidity = (raw_humidity * 100) / 16383;  // Convert raw measurements to physical units 
    temperature = (raw_temperature  * 297) / 16383 - 40; // Convert the 14-bit raw temperature to Fahrenheiet

    mutex.lock();
    serial.printf("Humidity: %d%%\r\n", humidity);
    serial.printf("Temperature: %d F\r\n", temperature);
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
        flag.wait_any(HUMIDITY); // Wait for the humidity event from ISR ticker
        humidity_led = 1;  // Turn on the red humidity LED

        int humidity_value = 0;
        int temperature_value = 0;

        // Read the HS3000 sensor
        if (!read_hs3000(humidity_value, temperature_value)) {
            mutex.lock();
            serial.printf("Humidity measurement failed\r\n");
            mutex.unlock();
        }

        thread_sleep_for(200); // Keep the LED on briefly
        humidity_led = 0; // Turn off the red humidity LED
    }
}



//temperature thread
void read_temperature(){
    mutex.lock();
    serial.printf("Temperature thread started\r\n");
    mutex.unlock();

    while(1){
        flag.wait_any(TEMPERATURE); //wait until TEMPERATURE is set

        temperature_led = 1;
        thread_sleep_for(200);
        temperature_led = 0;
    }
}


// main() runs in its own thread in the OS
int main()
{
    i2c_pullup = 1; // Enable the I2C bus pull-ups
    hs_power = 1; // Power up the onboard sensors
    i2c.frequency(100000); // Configure I2C speed
    thread_sleep_for(1000); // Allow sensors to power up

    mutex.lock();
    serial.printf("Lab 3 started\r\n");
    mutex.unlock();

    humidity_thread.start(read_humidity);
    temperature_thread.start(read_temperature);
    ticker.attach(&ticker_thread, 1s);

    while (1) {
        thread_sleep_for(1000); // idle loop, keeps programming running
    }
}


