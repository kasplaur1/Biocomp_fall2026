/*
Name: Lauren M Kasper
Assignment: midterm-4280-2026
Date: 10/20/26
Overview: 
- 
*/

#include "mbed.h"
#include "USBSerial.h"
//#include "nrf_pwm.h"

#define BMM150_ADDR (0x10 << 1) // HS3000 default 7-but address shifted
#define SDA_PIN P0_14 // pin definitions
#define SCL_PIN P0_15 // pin definitions
#define CHIP_ID_REG 0x40
#define POWER_REG 0x4B
#define EXPECTED_CHIP_ID 0x32

I2C i2c(SDA_PIN, SCL_PIN); // I2C interface object
DigitalOut hs_power(P0_22); // sensor power control (HS3003 VDD/VDDIO via P0.22)
DigitalOut i2c_pullup(P1_0);  // I2C pull-up control for bus

USBSerial serial;
Mutex serial_mutex; // protects serial printing
Mutex i2c_mutex; // i2c serial 

// global variable
//volatile uint32_t ticker_count = 0;
//const uint32_t pwm_period = 10; // how many times ticker fires to complete one PWM cycle
//volatile uint32_t on_counts = 8; // how many of the pwm_period that the LED should be ON

Thread magnometer;

// determine how long to keep the ticker on/off
 /*void ticker_function(){
    ticker_count++;

    // once ticker_count reaches 20, reset to 0 to creeate repeating cycle
    if(ticker_count >= pwm_period){
        ticker_count = 0;
    }

    // LED ON VS LED OFF
    if(ticker_count < on_counts){
        OUTCLR = LEDG;
    }
    else{
        OUTSET = LEDG;
    }
}*/

void magnometer_thread()
{
    int reg = CHIP_ID_REG;
    int chip_id = 0;
    int result;
    char data = 0;

    // Enable the BMM150 power control bit
    char power_data[2] = {POWER_REG, 0x01};

    i2c_mutex.lock();
    result = i2c.write(BMM150_ADDR, power_data, 2);
    i2c_mutex.unlock();

    if (result != 0) {
        serial_mutex.lock();
        serial.printf("ERROR: I2C power write failed: %d\r\n",
                      result);
        serial_mutex.unlock();
        return;
    }

    thread_sleep_for(5);

    // Select the chip ID register
    char register_address = static_cast<char>(reg);

    i2c_mutex.lock();

    result = i2c.write(BMM150_ADDR, &register_address, 1, true);

    // Read one byte from the selected register
    if (result == 0) {
        result = i2c.read(BMM150_ADDR, &data, 1);
    }

    i2c_mutex.unlock();
    if (result != 0) {
        serial_mutex.lock();
        serial.printf("ERROR: I2C chip ID read failed: %d\r\n", result);
        serial_mutex.unlock();
        return;
    }

    chip_id = static_cast<unsigned char>(data); // Convert the received byte to an integer
    serial_mutex.lock(); // Print and verify the chip ID

    serial.printf("Chip ID: 0x%02X (%d)\r\n", chip_id, chip_id);

    if (chip_id == EXPECTED_CHIP_ID) {
        serial.printf("SUCCESS: BMM150 chip ID verified!\r\n");
    } else {
        serial.printf("ERROR: Unexpected chip ID.\r\n");
        serial.printf("Expected: 0x%02X (%d)\r\n", EXPECTED_CHIP_ID,EXPECTED_CHIP_ID);
    }

    serial_mutex.unlock();
}

// main() runs in its own thread in the OS
int main()
{
    i2c_pullup = 1; // Enable the I2C bus pull-ups
    hs_power = 1; // Power up the onboard sensors
    i2c.frequency(100000); // Configure I2C speed
    thread_sleep_for(1000); // Allow sensors to power up

    serial_mutex.lock();
    serial.printf("\r\nBMM150 Chip ID Test \r\n");
    serial_mutex.unlock();

    magnometer.start(magnometer_thread);
    //pwm_ticker.attach(&ticker_function, 2ms);

    while(1){
        thread_sleep_for(1000); //keep main alive forever  else stop all threads
    }
}
