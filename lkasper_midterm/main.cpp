/*
Name: Lauren M Kasper
Assignment: midterm-4280-2026
Date: 10/20/26
Overview:
- Read X, Y, and Z magnetic field data from the BMM150 sensor
- Verify the sensor chip ID using I2C
- Use a ticker and event flag to read measurements every 100 ms
*/

#include "mbed.h" // Mbed OS library
#include "USBSerial.h" // USB serial library

// BMM150 I2C address (7-bit address shifted for Mbed)
#define BMM150_ADDR (0x10 << 1)
#define SDA_PIN P0_14 // I2C data pin
#define SCL_PIN P0_15 // I2C clock pin

// BMM150 register addresses
#define CHIP_ID_REG 0x40 // sensor chip ID
#define POWER_REG 0x4B // sensor power control
#define MODE_REG 0x4C // sensor operating mode
#define DATA_REG 0x42 // starting register for X, Y, and Z data
#define XY_REG 0x51 // X and Y measurement settings
#define Z_REG 0x52 // Z measurement settings
#define STATUS_REG 0x4A // sensor status information
#define EXPECTED_CHIP_ID 0x32 // expected BMM150 chip ID

// Event flag used to signal a new measurement
#define MEASURE (1 << 0)

I2C i2c(SDA_PIN, SCL_PIN); // I2C interface
DigitalOut hs_power(P0_22); // sensor power control
DigitalOut i2c_pullup(P1_0); // I2C pull-up control

USBSerial serial; // USB serial connection
EventFlags flag; // signals a measurement event
Ticker ticker; // triggers a function at regular intervals
Thread magnometer; // magnetometer worker thread


// Ticker function called every 100 milliseconds
void ticker_thread()
{
    flag.set(MEASURE); // signal the measurement thread
}


// Read X, Y, and Z measurements from the BMM150
bool read_BMM150()
{
    int result; // stores the result of an I2C operation
    char data = 0; // stores the chip ID
    char write_data[2]; // holds a register address and value
    char reg; // stores a register address

    // Turn on the sensor
    write_data[0] = POWER_REG;
    write_data[1] = 0x01;
    result = i2c.write(BMM150_ADDR, write_data, 2);

    if (result != 0) {
        serial.printf("ERROR: Power write failed\r\n");
        return false;
    }

    thread_sleep_for(5); // wait for the sensor to power up

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

    // Read the measurement data
    char raw[8] = {}; // stores the X, Y, Z, and Hall resistance bytes
    reg = DATA_REG;

    // Select the first data register
    result = i2c.write(BMM150_ADDR, &reg, 1, true);

    // Read 8 bytes from the sensor
    if (result == 0) {
        result = i2c.read(BMM150_ADDR, raw, 8);
    }

    if (result != 0) {
        serial.printf("ERROR: Sensor read failed\r\n");
        return false;
    }

    // Read the sensor status register
    char status = 0;
    reg = STATUS_REG;
    result = i2c.write(BMM150_ADDR, &reg, 1, true);

    if (result == 0) {
        result = i2c.read(BMM150_ADDR, &status, 1);
    }

    if (result != 0) {
        serial.printf("ERROR: Status read failed\r\n");
        return false;
    }

    // Check the overflow status bit
    int overflow = (static_cast<unsigned char>(status) >> 6) & 1;

    // Combine the bytes for each axis
    int x = (static_cast<unsigned char>(raw[1]) << 5) |
            (static_cast<unsigned char>(raw[0]) >> 3);

    int y = (static_cast<unsigned char>(raw[3]) << 5) |
            (static_cast<unsigned char>(raw[2]) >> 3);

    int z = (static_cast<unsigned char>(raw[5]) << 7) |
            (static_cast<unsigned char>(raw[4]) >> 1);

     // Convert X and Y to signed 13-bit values
    if (x & 0x1000) x -= 0x2000;
    if (y & 0x1000) y -= 0x2000;

    // Convert Z to a signed 15-bit value
    if (z & 0x4000) z -= 0x8000;

    // Check whether X or Y contains the overflow sentinel
    if (x == -4096 || y == -4096) {
        serial.printf("WARNING: X/Y measurement overflow\r\n");
    } else {
        // Print valid X and Y measurements
        serial.printf("X: %d  Y: %d  ", x, y);
    }

    // Print Z separately
    serial.printf("Z: %d\r\n", z);

    return true; // measurement was read successfully
}


// Magnetometer thread
void magnometer_thread_function()
{
    serial.printf("Magnetometer thread started\r\n");

    while (true) {
        flag.wait_any(MEASURE); // wait for the ticker event

        // Read the BMM150 sensor
        if (!read_BMM150()) {
            serial.printf("Magnetometer measurement failed\r\n");
        }
    }
}


// Main program
int main()
{
    i2c_pullup = 1; // enable the I2C pull-up circuit
    hs_power = 1; // power up the sensor
    i2c.frequency(100000); // set I2C speed to 100 kHz
    thread_sleep_for(1000); // allow the sensor to power up

    serial.printf("\r\nBMM150 X Y Z Test\r\n");

    magnometer.start(magnometer_thread_function); // start the sensor thread
    ticker.attach(ticker_thread, 100ms); // trigger a measurement every 100 ms

    // Keep the main thread running
    while (true) {
        thread_sleep_for(1000);
    }
}