
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
#define BMM150_ADDR (0x10 << 1) // BMM150 I2C address

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
#define STATUS_REG 0x4A // sensor status register
#define EXPECTED_CHIP_ID 0x32 // expected BMM150 chip ID

// LED pin definitions
#define LEDG (1 << 16) // green LED on P0.16
#define LEDB (1 << 6) // blue LED on P0.06

// GPIO register definitions
#define OUTSET (*(volatile uint32_t*)0x50000508) // set GPIO pins HIGH
#define OUTCLR (*(volatile uint32_t*)0x5000050C) // set GPIO pins LOW
#define DIRSET (*(volatile uint32_t*)0x50000518) // set GPIO pins as outputs

// Event flag definition
#define MEASURE (1 << 0) // measurement event flag

// PWM settings
const uint32_t pwm_period = 10; // number of counts in one PWM cycle
volatile uint32_t ticker_count = 0; // current PWM count
volatile uint32_t blue_on_counts = 0; // blue LED on-time counts
volatile uint32_t green_on_counts = 0; // green LED on-time counts

// Adjust this value to change LED sensitivity
const float Y_FULL_BRIGHTNESS = 1200.0f; // Y value for full LED brightness

// Store the latest sensor measurements
struct MagnetometerData {
    int x; // latest X-axis reading
    int y; // latest Y-axis reading
    int z; // latest Z-axis reading
    bool valid; // indicates whether a valid reading exists
};

MagnetometerData latest_data = {0, 0, 0, false}; // start with no valid reading

// I2C and sensor power
I2C i2c(SDA_PIN, SCL_PIN); // create I2C connection
DigitalOut hs_power(P0_22); // sensor power control pin
DigitalOut i2c_pullup(P1_0); // enable I2C pull-up power

// Serial output and thread control
USBSerial serial; // create USB serial connection
EventFlags flag; // signal the measurement thread
Mutex data_mutex; // protect shared sensor data
Ticker ticker; // trigger sensor measurements
Ticker pwm_ticker; // update LED PWM

// Threads
Thread magnometer; // thread for sensor readings
Thread ledThread; // thread for LED brightness


// Ticker function called every 100 milliseconds
void ticker_function()
{
    flag.set(MEASURE); // signal the measurement thread
}


// Ticker function used to control LED brightness
void pwm_function()
{
    ticker_count++; // move to the next PWM count

    if (ticker_count >= pwm_period) { // check if the PWM cycle is complete
        ticker_count = 0; // restart the PWM cycle
    }

    if (ticker_count < blue_on_counts) { // check if blue should be on
        OUTCLR = LEDB; // turn blue LED on (active-low)
    } else {
        OUTSET = LEDB; // turn blue LED off
    }

    if (ticker_count < green_on_counts) { // check if green should be on
        OUTCLR = LEDG; // turn green LED on (active-low)
    } else {
        OUTSET = LEDG; // turn green LED off
    }
}


// Initialize the BMM150 sensor
bool initialize_BMM150()
{
    int result; // store I2C result
    char data = 0; // store chip ID
    char write_data[2]; // store register and value to write
    char reg; // store register address

    write_data[0] = POWER_REG; // select power register
    write_data[1] = 0x01; // turn on the sensor
    result = i2c.write(BMM150_ADDR, write_data, 2); // write power setting

    if (result != 0) { // check whether the write failed
        serial.printf("ERROR: Power write failed\r\n"); // print error
        return false; // stop initialization
    }

    thread_sleep_for(5); // wait for sensor power-up

    reg = CHIP_ID_REG; // select chip ID register
    result = i2c.write(BMM150_ADDR, &reg, 1, true); // select register without stopping I2C

    if (result == 0) { // continue if register selection worked
        result = i2c.read(BMM150_ADDR, &data, 1); // read chip ID
    }

    if (result != 0 || static_cast<unsigned char>(data) != EXPECTED_CHIP_ID) { // check chip ID
        serial.printf("ERROR: Chip ID check failed\r\n"); // print error
        return false; // stop initialization
    }

    serial.printf("SUCCESS: BMM150 chip ID verified!\r\n"); // confirm sensor

    write_data[0] = XY_REG; // select X and Y repetition register
    write_data[1] = 0x04; // set X and Y measurement repetitions
    result = i2c.write(BMM150_ADDR, write_data, 2); // write X and Y settings

    if (result != 0) { // check whether the write failed
        serial.printf("ERROR: XY configuration failed\r\n"); // print error
        return false; // stop initialization
    }

    write_data[0] = Z_REG; // select Z repetition register
    write_data[1] = 0x07; // set Z measurement repetitions
    result = i2c.write(BMM150_ADDR, write_data, 2); // write Z settings

    if (result != 0) { // check whether the write failed
        serial.printf("ERROR: Z configuration failed\r\n"); // print error
        return false; // stop initialization
    }

    write_data[0] = MODE_REG; // select operating mode register
    write_data[1] = 0x00; // set normal operating mode
    result = i2c.write(BMM150_ADDR, write_data, 2); // write operating mode

    if (result != 0) { // check whether the write failed
        serial.printf("ERROR: Mode configuration failed\r\n"); // print error
        return false; // stop initialization
    }

    serial.printf("BMM150 initialization complete\r\n"); // confirm setup
    return true; // initialization succeeded
}


// Read raw sensor data and convert it into X, Y, and Z values
bool read_BMM150(int &x, int &y, int &z)
{
    int result; // store I2C result
    char raw[8] = {}; // store the eight sensor data bytes
    char reg = DATA_REG; // select the first data register

    result = i2c.write(BMM150_ADDR, &reg, 1, true); // select data register

    if (result == 0) { // continue if register selection worked
        result = i2c.read(BMM150_ADDR, raw, 8); // read X, Y, Z, and hall data
    }

    if (result != 0) { // check whether the sensor read failed
        serial.printf("ERROR: Sensor read failed\r\n"); // print error
        return false; // reject this measurement
    }

    // Print the raw Y bytes to help debug the overflow
    serial.printf("Y bytes: %02X %02X\r\n",
        static_cast<unsigned char>(raw[2]),
        static_cast<unsigned char>(raw[3])); // show Y data bytes

    // Print all eight bytes returned by the sensor
    serial.printf("Raw data: "); // label raw data
    for (int i = 0; i < 8; i++) { // go through each byte
        serial.printf("%02X ",
            static_cast<unsigned char>(raw[i])); // print byte in hexadecimal
    }
    serial.printf("\r\n"); // move to the next line

    // Combine the bytes for each axis
    x = ((static_cast<unsigned char>(raw[1]) & 0xFF) << 5) |
        ((static_cast<unsigned char>(raw[0]) & 0xF8) >> 3); // extract X value

    y = ((static_cast<unsigned char>(raw[3]) & 0xFF) << 5) |
        ((static_cast<unsigned char>(raw[2]) & 0xF8) >> 3); // extract Y value

    z = ((static_cast<unsigned char>(raw[5]) & 0xFF) << 7) |
        ((static_cast<unsigned char>(raw[4]) & 0xFE) >> 1); // extract Z value

    // Convert X and Y to signed 13-bit values
    if (x & 0x1000) x -= 0x2000; // convert X if negative
    if (y & 0x1000) y -= 0x2000; // convert Y if negative

    // Convert Z to a signed 15-bit value
    if (z & 0x4000) z -= 0x8000; // convert Z if negative

    serial.printf("Decoded X: %d  Y: %d  Z: %d\r\n", x, y, z); // print decoded values

    // Check for invalid sensor readings
    if (x == -4096 || y == -4096 || z == -16384) { // check overflow sentinel values
        serial.printf("WARNING: Sensor measurement overflow\r\n"); // report overflow

        char status = 0; // store sensor status
        char status_reg = STATUS_REG; // select status register

        result = i2c.write(BMM150_ADDR, &status_reg, 1, true); // select status register

        if (result == 0) { // continue if register selection worked
            result = i2c.read(BMM150_ADDR, &status, 1); // read sensor status
        }

        if (result == 0) { // check whether status read worked
            serial.printf("Sensor status: 0x%02X\r\n",
                static_cast<unsigned char>(status)); // print status byte
        } else {
            serial.printf("ERROR: Status read failed\r\n"); // report status read error
        }

        return false; // reject overflow reading
    }

    serial.printf("Valid measurement\r\n"); // confirm valid data
    return true; // return valid measurement
}


// Magnetometer thread
void magnometer_thread_function()
{
    serial.printf("Magnetometer thread started\r\n"); // confirm thread started

    while (true) { // keep checking for measurements
        flag.wait_any(MEASURE); // wait for the ticker event

        int x = 0; // store X reading
        int y = 0; // store Y reading
        int z = 0; // store Z reading

        if (read_BMM150(x, y, z)) { // read sensor and check validity
            data_mutex.lock(); // protect shared sensor data
            latest_data.x = x; // save X reading
            latest_data.y = y; // save Y reading
            latest_data.z = z; // save Z reading
            latest_data.valid = true; // mark data as valid
            data_mutex.unlock(); // allow other thread to access data
        } else {
            serial.printf("Keeping previous LED reading\r\n"); // retain last valid data
        }
    }
}


// Separate thread for LED brightness
void led_thread_function()
{
    serial.printf("LED thread started\r\n"); // confirm thread started

    while (true) { // keep updating LED brightness
        int y = 0; // store latest Y reading
        bool valid = false; // store whether data is valid

        data_mutex.lock(); // protect shared sensor data
        y = latest_data.y; // copy Y reading
        valid = latest_data.valid; // copy validity status
        data_mutex.unlock(); // allow other thread to access data

        if (valid) { // only use valid sensor data
            float blue_duty = 0.0f; // blue LED brightness
            float green_duty = 0.0f; // green LED brightness

            if (y > 0) { // check for positive Y
                blue_duty = static_cast<float>(y) /
                            Y_FULL_BRIGHTNESS; // calculate blue brightness
            }

            if (y < 0) { // check for negative Y
                green_duty = static_cast<float>(-y) /
                             Y_FULL_BRIGHTNESS; // calculate green brightness
            }

            if (blue_duty > 1.0f) blue_duty = 1.0f; // limit blue brightness
            if (green_duty > 1.0f) green_duty = 1.0f; // limit green brightness

            blue_on_counts =
                static_cast<uint32_t>(pwm_period * blue_duty); // set blue on-time

            green_on_counts =
                static_cast<uint32_t>(pwm_period * green_duty); // set green on-time
        }

        thread_sleep_for(10); // wait before updating brightness again
    }
}


// Main program
int main()
{
    i2c_pullup = 1; // enable I2C pull-ups
    hs_power = 1; // turn on sensor power
    i2c.frequency(100000); // set I2C frequency to 100 kHz
    thread_sleep_for(1000); // wait for sensor power-up

    DIRSET = LEDB | LEDG; // set blue and green LEDs as outputs
    OUTSET = LEDB | LEDG; // turn both active-low LEDs off

    serial.printf("\r\nBMM150 X Y Z Test\r\n"); // print program title

    if (!initialize_BMM150()) { // initialize sensor
        serial.printf("Sensor initialization failed\r\n"); // report failure

        while (true) { // stop if initialization failed
            thread_sleep_for(1000); // wait before checking again
        }
    }

    magnometer.start(magnometer_thread_function); // start sensor thread
    ledThread.start(led_thread_function); // start LED thread

    ticker.attach(&ticker_function, 100ms); // trigger measurement every 100 ms
    pwm_ticker.attach(&pwm_function, 1ms); // update PWM every 1 ms

    while (true) { // keep main thread running
        thread_sleep_for(1000); // wait before looping again
    }
}
