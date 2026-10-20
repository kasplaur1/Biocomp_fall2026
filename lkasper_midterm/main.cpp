
/*
Name: Lauren M Kasper
Assignment: midterm-4280-2026
Date: 10/20/26
Overview:
- Read X, Y, and Z magnetic field data from the BMM150 sensor
- Verify the sensor chip ID using I2C
- Use a ticker and event flag to read measurements every 100 ms
- Use blue and green LEDs to show magnetic direction with PWM
*/

#include "mbed.h"
#include "USBSerial.h"

// BMM150 I2C address (7-bit address shifted for Mbed)
#define BMM150_ADDR (0x10 << 1)
// I2C pin definitions
#define SDA_PIN P0_14 // I2C data pin
#define SCL_PIN P0_15 // I2C clock pin

// BMM150 register addresses
#define CHIP_ID_REG 0x40 // sensor chip ID register
#define POWER_REG 0x4B // sensor power control register
#define MODE_REG 0x4C // sensor operating mode register
#define DATA_REG 0x42 // starting register for X, Y, and Z data
#define XY_REG 0x51 // X and Y measurement settings register
#define Z_REG 0x52 // Z measurement settings register
#define EXPECTED_CHIP_ID 0x32 // expected BMM150 chip ID

// LED pin definitions
#define LEDG (1 << 16) // green LED on P0.16
#define LEDB (1 << 6) // blue LED on P0.06

// GPIO registers
#define OUTSET (*(volatile uint32_t*)0x50000508)
#define OUTCLR (*(volatile uint32_t*)0x5000050C)
#define DIRSET (*(volatile uint32_t*)0x50000518)

// Event flag used to signal a new measurement
#define MEASURE (1 << 0)

// PWM settings
const uint32_t pwm_period = 10;
volatile uint32_t ticker_count = 0;
volatile uint32_t blue_on_counts = 0;
volatile uint32_t green_on_counts = 0;

// Adjust this value to change LED sensitivity
const float Y_FULL_BRIGHTNESS = 200.0f;

// Store the latest valid Y measurement
volatile int latest_y = 0;
volatile bool valid_reading = false;

I2C i2c(SDA_PIN, SCL_PIN);
DigitalOut hs_power(P0_22);
DigitalOut i2c_pullup(P1_0);

USBSerial serial;
EventFlags flag;
Ticker ticker;
Ticker pwm_ticker;

// Threads
Thread magnometer;
Thread ledThread;


// Ticker function called every 100 milliseconds
void ticker_function()
{
    flag.set(MEASURE); // signal the measurement thread
}


// Ticker function used to control LED brightness
void pwm_function()
{
    ticker_count++;

    // Reset the PWM cycle
    if (ticker_count >= pwm_period) {
        ticker_count = 0;
    }

    // Turn the blue LED on or off
    if (ticker_count < blue_on_counts) {
        OUTCLR = LEDB;
    } else {
        OUTSET = LEDB;
    }

    // Turn the green LED on or off
    if (ticker_count < green_on_counts) {
        OUTCLR = LEDG;
    } else {
        OUTSET = LEDG;
    }
}


// Initialize the BMM150 sensor
// wasn't able to fully implement the x y and z magnometer directions
// I was getting oerflow issues for X and Y vlaues. still need to troubleshoot
bool initialize_BMM150()
{
    int result;
    char data = 0;
    char write_data[2];
    char reg;

    // Turn on the sensor
    write_data[0] = POWER_REG;
    write_data[1] = 0x01;
    result = i2c.write(BMM150_ADDR, write_data, 2);

    if (result != 0) {
        serial.printf("ERROR: Power write failed\r\n");
        return false;
    }

    thread_sleep_for(5);

    // Read the sensor chip ID
    reg = CHIP_ID_REG;
    result = i2c.write(BMM150_ADDR, &reg, 1, true);

    if (result == 0) {
        result = i2c.read(BMM150_ADDR, &data, 1);
    }

    // Check that the correct sensor is connected
    if (result != 0 ||
        static_cast<unsigned char>(data) != EXPECTED_CHIP_ID) {
        serial.printf("ERROR: Chip ID check failed\r\n");
        return false;
    }

    serial.printf("SUCCESS: BMM150 chip ID verified!\r\n");

    // Configure X and Y measurements
    write_data[0] = XY_REG;
    write_data[1] = 0x04;
    result = i2c.write(BMM150_ADDR, write_data, 2);

    if (result != 0) {
        serial.printf("ERROR: XY configuration failed\r\n");
        return false;
    }

    // Configure Z measurements
    write_data[0] = Z_REG;
    write_data[1] = 0x07;
    result = i2c.write(BMM150_ADDR, write_data, 2);

    if (result != 0) {
        serial.printf("ERROR: Z configuration failed\r\n");
        return false;
    }

    // Set the sensor to normal operation mode
    write_data[0] = MODE_REG;
    write_data[1] = 0x00;
    result = i2c.write(BMM150_ADDR, write_data, 2);

    if (result != 0) {
        serial.printf("ERROR: Mode configuration failed\r\n");
        return false;
    }

    serial.printf("BMM150 initialization complete\r\n");
    return true;
}

// still wrong. using for troubleshooting
// Read X, Y, and Z measurements from the BMM150
bool read_BMM150(int &x, int &y, int &z)
{
    int result;
    char raw[8] = {};
    char reg = DATA_REG;

    // Select the first data register
    result = i2c.write(BMM150_ADDR, &reg, 1, true);

    // Read the measurement data
    if (result == 0) {
        result = i2c.read(BMM150_ADDR, raw, 8);
    }

    if (result != 0) {
        serial.printf("ERROR: Sensor read failed\r\n");
        return false;
    }

    // Combine the bytes for each axis
    x = (static_cast<unsigned char>(raw[1]) << 5) |
        (static_cast<unsigned char>(raw[0]) >> 3);

    y = (static_cast<unsigned char>(raw[3]) << 5) |
        (static_cast<unsigned char>(raw[2]) >> 3);

    z = (static_cast<unsigned char>(raw[5]) << 7) |
        (static_cast<unsigned char>(raw[4]) >> 1);

    // Convert X and Y to signed 13-bit values
    if (x & 0x1000) x -= 0x2000;
    if (y & 0x1000) y -= 0x2000;

    // Convert Z to a signed 15-bit value
    if (z & 0x4000) z -= 0x8000;

    // Check for invalid sensor readings
    if (x == -4096 || y == -4096 || z == -16384) {
        serial.printf("WARNING: Sensor measurement overflow\r\n");
        return false;
    }

    // Print the valid measurements
    serial.printf("X: %d  Y: %d  Z: %d\r\n", x, y, z);

    return true;
}


// Magnetometer thread
void magnometer_thread_function()
{
    serial.printf("Magnetometer thread started\r\n");

    while (true) {
        flag.wait_any(MEASURE); // wait for the ticker event

        int x;
        int y;
        int z;

        // Read the BMM150 sensor
        if (read_BMM150(x, y, z)) {
            latest_y = y;
            valid_reading = true;
        } else {
            serial.printf("Keeping previous LED reading\r\n");
        }
    }
}

// did not implement pwm yet. just trying to get some points back for the lab practical
// ;(
// Separate thread for LED brightness
void led_thread_function()
{
    serial.printf("LED thread started\r\n");

    while (true) {
        if (valid_reading) {
            int y = latest_y;

            float blue_duty = 0.0f;
            float green_duty = 0.0f;

            // Increase blue brightness when Y is positive
            if (y > 0) {
                blue_duty = static_cast<float>(y) /
                            Y_FULL_BRIGHTNESS;
            }

            // Increase green brightness when Y is negative
            if (y < 0) {
                green_duty = static_cast<float>(-y) /
                             Y_FULL_BRIGHTNESS;
            }

            // Limit brightness to 100 percent
            if (blue_duty > 1.0f) blue_duty = 1.0f;
            if (green_duty > 1.0f) green_duty = 1.0f;

            // Convert duty cycle to PWM counts
            blue_on_counts =
                static_cast<uint32_t>(pwm_period * blue_duty);

            green_on_counts =
                static_cast<uint32_t>(pwm_period * green_duty);
        }

        thread_sleep_for(10);
    }
}


// Main program
int main()
{
    // Power up the sensor and enable I2C
    i2c_pullup = 1;
    hs_power = 1;
    i2c.frequency(100000);
    thread_sleep_for(1000);

    // Set the LEDs as outputs
    DIRSET = LEDB | LEDG;

    // Turn off both LEDs
    OUTSET = LEDB | LEDG;

    serial.printf("\r\nBMM150 X Y Z Test\r\n");

    // Initialize the sensor once
    if (!initialize_BMM150()) {
        serial.printf("Sensor initialization failed\r\n");

        while (true) {
            thread_sleep_for(1000);
        }
    }

    // Start the threads
    magnometer.start(magnometer_thread_function);
    ledThread.start(led_thread_function);

    // Start the tickers
    ticker.attach(&ticker_function, 100ms);
    pwm_ticker.attach(&pwm_function, 1ms);

    // Keep the main thread running
    while (true) {
        thread_sleep_for(1000);
    }
}
