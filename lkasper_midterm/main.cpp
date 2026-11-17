/*
 * Name: Lauren M Kasper
 * Assignment: BIEN 4280 Midterm - BMM150 Magnetometer
 * Date: 11/16/2026
 *
 * Overview:
 * 1. Initialize the BMM150 and verify its Chip ID over I2C (inside the
 *    'magnometer' thread).
 * 2. Read X, Y, and Z magnetic-field measurements every 100 ms.
 * 3. Calculate an eight-point compass direction and control blue/green LEDs.
 * 4. Monitor the Z-axis relative to a horizontal startup calibration and
 *    blink the red LED when the reading changes beyond a threshold.
 *
 * IMPORTANT TILT NOTE:
 * The BMM150 is a magnetometer, not an accelerometer. Its Z-axis can be used
 * as a rough tilt indicator for this assignment, but magnetic-field changes
 * can also trigger it. A true gravity-based tilt detector should use the
 * Nano 33 BLE Sense Rev2's BMI270 accelerometer.
 *
 * CHANGE LOG (fixes made after the first test run):
 *  FIX 1: The sensor was initialized twice (once in main(), once in the
 *         magnometer thread). The second init left the sensor returning the
 *         same frozen sample forever. Now the init/handshake happens ONLY in
 *         the magnometer thread, and main() waits for its result.
 *  FIX 2: Added a data-ready check (bit 0 of byte 6). Stale samples are
 *         rejected instead of being treated as new measurements.
 *  FIX 3: Added stall recovery: if no fresh data arrives for ~2 s the
 *         sensor is re-armed.
 *  FIX 4: Removed the RAW BYTES / DECODED / OVERFLOW STATUS debug prints
 *         (they flooded the terminal and slowed the 100 ms loop).
 *  FIX 5: printf("%.1f") does not print floats with Mbed's minimal printf,
 *         so the heading is now printed as an integer.
 *  FIX 6: TILT_THRESHOLD lowered from 350 to 60 (horizontal Z only varied
 *         by ~30 counts in the log, so 350 could never trigger). Added
 *         hysteresis so the alert does not flicker near the threshold.
 *  FIX 8: (second test) Blue LED was always on. Causes: (a) brightness used
 *         (1+cos)/2, which is still 50% at East/West; now brightness falls
 *         linearly to zero 90 degrees away from North (or South).
 *         (b) the compass had a large fixed magnetic offset, so the heading
 *         was stuck near North-East. Added a 10 s "spin" calibration that
 *         finds the X/Y hard-iron offsets from min/max readings.
 *  FIX 9: X/Y are smoothed with a low-pass filter and the printed direction
 *         uses hysteresis, so noise no longer flips N <-> NE repeatedly.
 *  FIX 7: Calibration now retries until enough fresh samples are collected
 *         and waits 150 ms between reads instead of 1000 ms.
 */

#include "mbed.h"
#include "USBSerial.h"
#include <cmath>
#include <cstdint>

// ---------------------------- BMM150 SETTINGS ----------------------------

// BMM150's 7-bit I2C address is 0x10.
// Mbed expects the address shifted left by one bit.
#define BMM150_ADDR (0x10 << 1)

// I2C pins used in the existing working project.
#define SDA_PIN P0_14
#define SCL_PIN P0_15

// BMM150 register addresses.
#define CHIP_ID_REG 0x40
#define POWER_REG   0x4B
#define MODE_REG    0x4C
#define DATA_REG    0x42
#define XY_REG      0x51
#define Z_REG       0x52

// The BMM150 should return this chip ID.
#define EXPECTED_CHIP_ID 0x32

// ------------------------------- LED PINS --------------------------------

// These are the same LED pins used in the existing project.
// The LEDs on this board are active-low: LOW turns an LED on.
#define LED_GREEN (1u << 16)  // P0.16
#define LED_BLUE  (1u << 6)   // P0.06
#define LED_RED   (1u << 24)  // P0.24

// nRF52840 GPIO register addresses for port 0.
#define OUTSET (*(volatile uint32_t*)0x50000508)
#define OUTCLR (*(volatile uint32_t*)0x5000050C)
#define DIRSET (*(volatile uint32_t*)0x50000518)

// ---------------------------- PROGRAM SETTINGS ---------------------------

// Event flags shared between main and the magnometer thread.
#define MEASURE_EVENT (1u << 0)   // set by the 100 ms ticker
#define INIT_SUCCESS  (1u << 1)   // set by the thread when handshake passes
#define INIT_FAILURE  (1u << 2)   // set by the thread when handshake fails

// The software PWM uses 20 steps.
// A 1 ms PWM tick gives an approximately 50 Hz PWM frequency.
const uint32_t PWM_PERIOD = 20;

// Change this if the board's magnetic field is not aligned with the
// assumed compass axes. This is a starting value, not a calibrated value.
// To tune: point the board at true North (phone compass), read the printed
// heading, and set this to (360 - printed heading).
const float HEADING_OFFSET_DEG = 0.0f;

// Magnetometer calibration offsets.
// Leave at zero for the first test. Real compass accuracy requires
// collecting minimum/maximum X and Y readings while rotating the board.
// FIX 8: These are now measured at startup by the spin calibration
// (calibrate_compass_offsets) instead of being fixed constants.
float MAG_X_OFFSET = 0.0f;
float MAG_Y_OFFSET = 0.0f;

// FIX 8: Spin calibration settings. The user slowly rotates the board
// flat through a full circle during this time.
const int SPIN_CAL_TIME_MS = 10000;
const int SPIN_CAL_MIN_SPAN = 60;   // min (max-min) counts to accept

// FIX 9: Low-pass filter weight for new X/Y samples (0..1). Smaller values
// smooth more but respond more slowly.
const float FILTER_ALPHA = 0.3f;

// FIX 9: Degrees past a direction boundary needed before the printed
// direction changes.
const float DIRECTION_HYSTERESIS_DEG = 8.0f;

// FIX 6: Z-axis change that counts as "tilted" (was 350, which never
// triggered). Horizontal Z varied only ~67-100 in testing. Tune this by
// tilting the board and watching the printed Z values.
const int TILT_THRESHOLD = 60;

// FIX 6: Hysteresis. Once tilted, the board must come back within this
// smaller difference of the baseline before the alert clears. Prevents
// the LED/messages from flickering right at the threshold.
const int TILT_CLEAR_THRESHOLD = 40;

// Number of fresh horizontal readings averaged during startup calibration.
const int CALIBRATION_SAMPLES = 20;

// FIX 7: Give up calibrating after this many read attempts.
const int CALIBRATION_MAX_ATTEMPTS = 100;

// FIX 3: Number of consecutive failed/stale reads (100 ms each) before the
// sensor is re-armed. 20 reads is about 2 seconds.
const int STALE_LIMIT = 20;

// Used for converting radians to degrees.
const float PI_VALUE = 3.14159265f;

// ------------------------------- DATA TYPES -------------------------------

// Holds the most recent valid magnetometer readings.
struct MagnetometerData {
    int x;
    int y;
    int z;
    bool valid;
};

// ---------------------------- HARDWARE OBJECTS ----------------------------

// I2C connection to the BMM150.
I2C i2c(SDA_PIN, SCL_PIN);

// Power and pull-up controls retained from the existing working project.
DigitalOut hs_power(P0_22);
DigitalOut i2c_pullup(P1_0);

// USB serial output.
USBSerial serial;

// Synchronization objects.
EventFlags measurement_flags;
Mutex data_mutex;
Mutex serial_mutex;

// Timing objects.
Ticker measurement_ticker;
Ticker pwm_ticker;

// Threads required by the program.
Thread magnometer;
Thread ledThread;
Thread tiltThread;

// Shared data. Access this structure only while holding data_mutex.
MagnetometerData latest_data = {0, 0, 0, false};

// Z-axis value measured while the board is horizontal.
// The tilt thread compares new readings against this value.
int horizontal_z_baseline = 0;

// PWM values are shared with the PWM ticker interrupt.
// volatile ensures the compiler re-reads values updated by another context.
volatile uint32_t pwm_count = 0;
volatile uint32_t blue_on_counts = 0;
volatile uint32_t green_on_counts = 0;

// ----------------------------- HELPER FUNCTIONS ---------------------------

// Print one message while protecting USB serial output from other threads.
void print_message(const char* message)
{
    serial_mutex.lock();
    serial.printf("%s", message);
    serial_mutex.unlock();
}

// Write one BMM150 register.
// Mbed I2C returns 0 when the transaction succeeds.
bool write_register(uint8_t register_address, uint8_t value)
{
    char write_data[2];

    write_data[0] = static_cast<char>(register_address);
    write_data[1] = static_cast<char>(value);

    int result = i2c.write(BMM150_ADDR, write_data, 2);

    if (result != 0) {
        return false;
    }

    return true;
}

// Read one BMM150 register.
bool read_register(uint8_t register_address, uint8_t &value)
{
    char reg = static_cast<char>(register_address);
    char received_data = 0;

    // First select the register. The repeated-start flag keeps the
    // transaction active before the read.
    int result = i2c.write(BMM150_ADDR, &reg, 1, true);

    if (result != 0) {
        return false;
    }

    result = i2c.read(BMM150_ADDR, &received_data, 1);

    if (result != 0) {
        return false;
    }

    value = static_cast<uint8_t>(received_data);
    return true;
}

// Convert a signed two's-complement value with a specified bit width.
int sign_extend(int value, int bit_count)
{
    int sign_bit = 1 << (bit_count - 1);
    int full_range = 1 << bit_count;

    if ((value & sign_bit) != 0) {
        value -= full_range;
    }

    return value;
}

// Keep an angle between 0 and 360 degrees.
float normalize_heading(float heading)
{
    while (heading < 0.0f) {
        heading += 360.0f;
    }

    while (heading >= 360.0f) {
        heading -= 360.0f;
    }

    return heading;
}

// --------------------------- TIMER CALLBACKS ------------------------------

// This callback runs every 100 ms.
// It only sets an event flag; sensor I2C work happens in the thread.
void measurement_ticker_function()
{
    measurement_flags.set(MEASURE_EVENT);
}

// This callback creates software PWM for the blue and green LEDs.
// Do not print messages or perform I2C transactions inside this callback.
void pwm_ticker_function()
{
    pwm_count++;

    if (pwm_count >= PWM_PERIOD) {
        pwm_count = 0;
    }

    // Active-low LED: clear the pin to turn it on.
    if (pwm_count < blue_on_counts) {
        OUTCLR = LED_BLUE;
    } else {
        OUTSET = LED_BLUE;
    }

    if (pwm_count < green_on_counts) {
        OUTCLR = LED_GREEN;
    } else {
        OUTSET = LED_GREEN;
    }
}

// -------------------------- SENSOR INITIALIZATION -------------------------

// Initialize the BMM150 and verify the chip ID before continuing.
// FIX 1: This is now called ONLY from the magnometer thread.
bool initialize_BMM150()
{
    uint8_t chip_id = 0;

    print_message("Starting BMM150 initialization...\r\n");

    // Turn on the BMM150's power supply through the board control pin.
    hs_power = 1;
    thread_sleep_for(5);

    // Enable the I2C pull-up control used by the existing project.
    i2c_pullup = 1;
    thread_sleep_for(5);

    // Put the BMM150 into powered mode (suspend -> sleep).
    if (!write_register(POWER_REG, 0x01)) {
        print_message("ERROR: Could not enable BMM150 power.\r\n");
        return false;
    }

    // Allow the sensor time to become ready after power-up.
    thread_sleep_for(5);

    // Read and verify the sensor Chip ID (the "handshake").
    if (!read_register(CHIP_ID_REG, chip_id)) {
        print_message("ERROR: Could not read BMM150 Chip ID.\r\n");
        return false;
    }

    serial_mutex.lock();
    serial.printf("BMM150 Chip ID: 0x%02X\r\n", chip_id);
    serial_mutex.unlock();

    if (chip_id != EXPECTED_CHIP_ID) {
        print_message("ERROR: Incorrect BMM150 Chip ID. Expected 0x32.\r\n");
        return false;
    }

    print_message("SUCCESS: BMM150 Chip ID verified.\r\n");

    // Configure X and Y measurement repetition settings.
    if (!write_register(XY_REG, 0x04)) {
        print_message("ERROR: Failed to configure X/Y measurements.\r\n");
        return false;
    }

    // Configure Z measurement repetition settings.
    if (!write_register(Z_REG, 0x07)) {
        print_message("ERROR: Failed to configure Z measurements.\r\n");
        return false;
    }

    // Select normal operating mode (10 Hz output data rate).
    if (!write_register(MODE_REG, 0x00)) {
        print_message("ERROR: Failed to set normal operating mode.\r\n");
        return false;
    }

    thread_sleep_for(5);
    print_message("BMM150 initialization complete.\r\n");

    return true;
}

// FIX 3: Re-arm the sensor if it stops producing fresh data.
// Rewrites the power and mode registers without repeating the handshake.
// Only the magnometer thread calls this once the system is running.
void recover_BMM150()
{
    print_message("WARNING: Sensor stalled, re-arming.\r\n");

    write_register(POWER_REG, 0x01);   // make sure the sensor is powered
    thread_sleep_for(5);
    write_register(MODE_REG, 0x00);    // normal mode, 10 Hz
    thread_sleep_for(5);
}

// ----------------------------- SENSOR READING -----------------------------

// Read the eight data bytes beginning at register 0x42.
// Decode X and Y as signed 13-bit values and Z as a signed 15-bit value.
// Returns false on an I2C error, stale data, or an overflow reading.
bool read_BMM150(int &x, int &y, int &z)
{
    char register_address = static_cast<char>(DATA_REG);
    uint8_t raw[8] = {0};

    // Select the first data register.
    int result = i2c.write(BMM150_ADDR, &register_address, 1, true);

    if (result != 0) {
        print_message("ERROR: Could not select BMM150 data register.\r\n");
        return false;
    }

    // Read X, Y, Z, and the additional Hall data bytes.
    result = i2c.read(BMM150_ADDR, reinterpret_cast<char*>(raw), 8);

    if (result != 0) {
        print_message("ERROR: BMM150 data read failed.\r\n");
        return false;
    }

    // FIX 2: Bit 0 of byte 6 (RHALL LSB) is the data-ready flag.
    // If it is 0 the sensor has no new sample, so do not publish it.
    // (This is an expected condition, so no error message is printed.)
    if ((raw[6] & 0x01) == 0) {
        return false;
    }

    // X uses 13 bits:
    // DATAX_LSB bits 7:3 contain the five least significant bits.
    // DATAX_MSB contains the eight most significant bits.
    int raw_x = (static_cast<int>(raw[1]) << 5) |
                (static_cast<int>(raw[0]) >> 3);

    // Y uses the same 13-bit format as X.
    int raw_y = (static_cast<int>(raw[3]) << 5) |
                (static_cast<int>(raw[2]) >> 3);

    // Z uses 15 bits:
    // DATAZ_LSB bits 7:1 contain seven least significant bits.
    // DATAZ_MSB contains the eight most significant bits.
    int raw_z = (static_cast<int>(raw[5]) << 7) |
                (static_cast<int>(raw[4]) >> 1);

    // Convert each unsigned bit pattern into its signed value.
    x = sign_extend(raw_x, 13);
    y = sign_extend(raw_y, 13);
    z = sign_extend(raw_z, 15);

    // FIX 4: The RAW BYTES / DECODED / OVERFLOW STATUS debug prints were
    // removed from here. They were only needed to find the frozen-data bug.

    // The BMM150 uses these values to indicate measurement overflow.
    // (The first sample after power-up commonly overflows; it is rejected.)
    if (x == -4096 || y == -4096 || z == -16384) {
        print_message("WARNING: BMM150 measurement overflow. Reading rejected.\r\n");
        return false;
    }

    return true;
}

// -------------------------- STARTUP CALIBRATION ---------------------------

// FIX 8: Spin calibration. While the user slowly rotates the board flat
// through a full circle, record the min and max X and Y readings. The
// center of each range is the fixed (hard-iron) offset caused by nearby
// metal and the board itself. Subtracting it centers the compass.
bool calibrate_compass_offsets()
{
    int x_min = 32767, x_max = -32768;
    int y_min = 32767, y_max = -32768;
    int samples = 0;

    print_message("\r\nSPIN CALIBRATION: slowly rotate the board FLAT through\r\n");
    print_message("a full circle for 10 seconds, starting now.\r\n");

    for (int elapsed = 0; elapsed < SPIN_CAL_TIME_MS; elapsed += 100) {
        int x = 0, y = 0, z = 0;

        if (read_BMM150(x, y, z)) {
            if (x < x_min) x_min = x;
            if (x > x_max) x_max = x;
            if (y < y_min) y_min = y;
            if (y > y_max) y_max = y;
            samples++;
        }

        thread_sleep_for(100);
    }

    // Error checking: need enough samples and enough rotation.
    if (samples < 20 ||
        (x_max - x_min) < SPIN_CAL_MIN_SPAN ||
        (y_max - y_min) < SPIN_CAL_MIN_SPAN) {
        print_message("WARNING: Spin calibration failed (not enough rotation).\r\n");
        print_message("Using zero offsets; the compass may be inaccurate.\r\n");
        return false;
    }

    MAG_X_OFFSET = (x_max + x_min) / 2.0f;
    MAG_Y_OFFSET = (y_max + y_min) / 2.0f;

    serial_mutex.lock();
    serial.printf("Compass offsets: X=%d  Y=%d  (samples: %d)\r\n",
                  static_cast<int>(MAG_X_OFFSET),
                  static_cast<int>(MAG_Y_OFFSET), samples);
    serial_mutex.unlock();

    return true;
}

// The user must keep the board horizontal during this calibration.
// The average Z value becomes the reference for the provisional tilt test.
// FIX 7: Retries until CALIBRATION_SAMPLES fresh readings are collected
// (or the attempt limit is hit), using a 150 ms delay so each pass can
// see a new 10 Hz sample.
bool calibrate_horizontal_position()
{
    long z_sum = 0;
    int successful_samples = 0;
    int attempts = 0;

    print_message("\r\nHOLD THE BOARD HORIZONTAL for calibration.\r\n");

    while (successful_samples < CALIBRATION_SAMPLES &&
           attempts < CALIBRATION_MAX_ATTEMPTS) {
        int x = 0;
        int y = 0;
        int z = 0;

        if (read_BMM150(x, y, z)) {
            z_sum += z;
            successful_samples++;
        }

        attempts++;
        thread_sleep_for(150);
    }

    // Require at least 75% of the requested readings to be valid.
    if (successful_samples < (CALIBRATION_SAMPLES * 3 / 4)) {
        print_message("ERROR: Not enough valid calibration readings.\r\n");
        return false;
    }

    horizontal_z_baseline = static_cast<int>(z_sum / successful_samples);

    serial_mutex.lock();
    serial.printf("Horizontal Z baseline: %d\r\n", horizontal_z_baseline);
    serial.printf("Calibration samples accepted: %d\r\n", successful_samples);
    serial_mutex.unlock();

    return true;
}

// --------------------------- MAGNETOMETER THREAD --------------------------

// Required thread name: magnometer.
// It performs the Chip ID handshake, then waits for a ticker event and
// reads X, Y, and Z every 100 ms.
void magnometer_thread_function()
{
    print_message("Magnometer thread started.\r\n");

    // Perform the required handshake inside this thread.
    if (!initialize_BMM150()) {
        measurement_flags.set(INIT_FAILURE);
        return;
    }

    // Notify main that the handshake succeeded.
    measurement_flags.set(INIT_SUCCESS);

    // FIX 3: Counts consecutive reads that produced no usable data.
    int stale_count = 0;

    while (true) {
        // Wait until the 100 ms ticker signals a measurement.
        uint32_t flags = measurement_flags.wait_any(MEASURE_EVENT);

        if ((flags & osFlagsError) || (flags & MEASURE_EVENT) == 0) {
            print_message("ERROR: Measurement event wait failed.\r\n");
            continue;
        }

        int x = 0;
        int y = 0;
        int z = 0;

        if (read_BMM150(x, y, z)) {
            stale_count = 0;

            // Only publish a complete, valid measurement.
            data_mutex.lock();

            latest_data.x = x;
            latest_data.y = y;
            latest_data.z = z;
            latest_data.valid = true;

            data_mutex.unlock();

            // Print the three measurements for verification.
            serial_mutex.lock();
            serial.printf("Magnetic field - X: %d  Y: %d  Z: %d\r\n", x, y, z);
            serial_mutex.unlock();
        } else {
            // FIX 3: No fresh data. After ~2 s of this, re-arm the sensor.
            stale_count++;

            if (stale_count >= STALE_LIMIT) {
                recover_BMM150();
                stale_count = 0;
            }
        }
    }
}

// ---------------------------- COMPASS DIRECTION ---------------------------

// Convert the current X/Y values into one of eight compass directions.
// This assumes the board is approximately horizontal and has been calibrated.
void calculate_compass_direction(int x, int y,
                                 float &heading,
                                 const char* &direction)
{
    // Apply optional hard-iron offsets before calculating the heading.
    float corrected_x = static_cast<float>(x) - MAG_X_OFFSET;
    float corrected_y = static_cast<float>(y) - MAG_Y_OFFSET;

    // atan2 returns an angle in radians. Convert it to degrees.
    heading = atan2f(corrected_y, corrected_x) * 180.0f / PI_VALUE;

    // Apply the adjustment needed to align the board's axes with North.
    heading += HEADING_OFFSET_DEG;
    heading = normalize_heading(heading);

    // Divide the compass into eight 45-degree sections.
    // Adding 22.5 degrees makes each direction cover half of its
    // neighboring 45-degree sections.
    int direction_index =
        static_cast<int>((heading + 22.5f) / 45.0f) % 8;

    // Eight compass directions, beginning at 0 degrees.
    static const char* directions[8] = {
        "North",
        "North-East",
        "East",
        "South-East",
        "South",
        "South-West",
        "West",
        "North-West"
    };

    direction = directions[direction_index];
}

// ------------------------------- LED THREAD -------------------------------

// Calculates the compass direction and updates the blue and green LEDs.
// Blue is brightest when facing North; green is brightest when facing South.
void led_thread_function()
{
    print_message("LED/compass thread started.\r\n");

    const char* previous_direction = "";
    int previous_index = -1;

    // FIX 9: Filtered X/Y values.
    float filtered_x = 0.0f;
    float filtered_y = 0.0f;
    bool filter_ready = false;

    while (true) {
        int x = 0;
        int y = 0;
        bool valid = false;

        // Copy shared values while holding the mutex.
        data_mutex.lock();

        x = latest_data.x;
        y = latest_data.y;
        valid = latest_data.valid;

        data_mutex.unlock();

        if (valid) {
            // FIX 9: Low-pass filter to reduce sample-to-sample noise.
            if (!filter_ready) {
                filtered_x = static_cast<float>(x);
                filtered_y = static_cast<float>(y);
                filter_ready = true;
            } else {
                filtered_x += FILTER_ALPHA * (x - filtered_x);
                filtered_y += FILTER_ALPHA * (y - filtered_y);
            }

            float heading = 0.0f;
            const char* direction = "Unknown";

            calculate_compass_direction(static_cast<int>(filtered_x),
                                        static_cast<int>(filtered_y),
                                        heading, direction);

            // FIX 8: Angular distance from North (0 to 180 degrees).
            float from_north = (heading <= 180.0f) ? heading : (360.0f - heading);
            float from_south = 180.0f - from_north;

            // Brightness falls linearly from 1.0 (pointing exactly at the
            // pole) to 0.0 at 90 degrees away. East and West therefore
            // turn both LEDs off, instead of leaving blue at 50%.
            float north_strength = 1.0f - (from_north / 90.0f);
            float south_strength = 1.0f - (from_south / 90.0f);

            if (north_strength < 0.0f) north_strength = 0.0f;
            if (south_strength < 0.0f) south_strength = 0.0f;

            // Convert brightness into PWM on-time counts.
            blue_on_counts = static_cast<uint32_t>(
                north_strength * PWM_PERIOD);

            green_on_counts = static_cast<uint32_t>(
                south_strength * PWM_PERIOD);

            // FIX 9: Hysteresis on the printed direction. Only switch when
            // the heading is clearly inside the new 45-degree sector.
            int new_index = static_cast<int>((heading + 22.5f) / 45.0f) % 8;

            if (previous_index >= 0 && new_index != previous_index) {
                float center = previous_index * 45.0f;
                float diff = heading - center;

                if (diff > 180.0f)  diff -= 360.0f;
                if (diff < -180.0f) diff += 360.0f;
                if (diff < 0.0f)    diff = -diff;

                // Stay in the old sector until beyond its edge + margin.
                if (diff < 22.5f + DIRECTION_HYSTERESIS_DEG) {
                    new_index = previous_index;
                }
            }

            static const char* names[8] = {
                "North", "North-East", "East", "South-East",
                "South", "South-West", "West", "North-West"
            };
            direction = names[new_index];
            previous_index = new_index;

            // Print only when the eight-point direction changes.
            if (direction != previous_direction) {
                // FIX 5: heading printed as an integer with %d because
                // %.1f does not work with Mbed's minimal printf.
                serial_mutex.lock();
                serial.printf("Direction: %s  Heading: %d degrees\r\n",
                              direction, static_cast<int>(heading));
                serial_mutex.unlock();

                previous_direction = direction;
            }
        }

        thread_sleep_for(50);
    }
}

// ------------------------------- TILT THREAD ------------------------------

// Provisional tilt detection using the difference between the current
// magnetometer Z reading and the horizontal startup baseline.
//
// IMPORTANT: This is not as reliable as using the BMI270 accelerometer.
// Magnetic-field direction and nearby metal can also change the Z reading.
void tilt_thread_function()
{
    print_message("Tilt thread started.\r\n");

    bool was_tilted = false;

    while (true) {
        int current_z = 0;
        bool valid = false;

        data_mutex.lock();

        current_z = latest_data.z;
        valid = latest_data.valid;

        data_mutex.unlock();

        if (valid) {
            int z_difference = current_z - horizontal_z_baseline;

            // Absolute difference detects a change in either direction.
            if (z_difference < 0) {
                z_difference = -z_difference;
            }

            // FIX 6: Hysteresis. Enter the tilted state above
            // TILT_THRESHOLD, but only leave it once the difference falls
            // below the smaller TILT_CLEAR_THRESHOLD.
            bool tilted_up;

            if (was_tilted) {
                tilted_up = (z_difference > TILT_CLEAR_THRESHOLD);
            } else {
                tilted_up = (z_difference > TILT_THRESHOLD);
            }

            if (tilted_up) {
                // Report the alert only when entering the tilted state.
                if (!was_tilted) {
                    print_message("ALERT: Board tilt detected! Return board to horizontal.\r\n");
                }

                // Active-low red LED: turn on, wait, turn off, wait.
                OUTCLR = LED_RED;
                thread_sleep_for(250);

                OUTSET = LED_RED;
                thread_sleep_for(250);

            } else {
                // Turn off the red LED when the board returns to normal.
                OUTSET = LED_RED;

                // Report recovery only when leaving the tilted state.
                if (was_tilted) {
                    print_message("Board returned to horizontal position.\r\n");
                }

                thread_sleep_for(100);
            }

            was_tilted = tilted_up;
        } else {
            // Do not signal tilt when no valid measurement is available.
            OUTSET = LED_RED;
            thread_sleep_for(100);
        }
    }
}

// -------------------------------- MAIN -------------------------------------

int main()
{
    // Set up the sensor's I2C power and pull-up controls.
    hs_power = 1;
    i2c_pullup = 1;

    // Use 100 kHz I2C communication.
    i2c.frequency(100000);

    // Allow the board and sensor power to stabilize.
    thread_sleep_for(1000);

    // Configure the three LED pins as outputs.
    DIRSET = LED_BLUE | LED_GREEN | LED_RED;

    // Turn all active-low LEDs off before starting.
    OUTSET = LED_BLUE | LED_GREEN | LED_RED;

    print_message("\r\n====================================\r\n");
    print_message(" BMM150 MAGNETOMETER MIDTERM PROGRAM\r\n");
    print_message("====================================\r\n");

    // FIX 1: Start the magnometer thread FIRST. It performs the one and only
    // sensor initialization and Chip ID handshake.
    if (magnometer.start(magnometer_thread_function) != osOK) {
        print_message("FATAL ERROR: Could not start magnometer thread.\r\n");
        while (true) {
            thread_sleep_for(1000);
        }
    }

    // FIX 1: Wait (up to 3 s) for the thread to report the handshake result.
    uint32_t init_result = measurement_flags.wait_any_for(
        INIT_SUCCESS | INIT_FAILURE, 3000ms);

    // Do not start the other threads if the sensor handshake fails or times out.
    if ((init_result & osFlagsError) || (init_result & INIT_FAILURE)) {
        print_message("FATAL ERROR: Sensor initialization failed.\r\n");
        print_message("Check I2C pins, power, pull-ups, and sensor connection.\r\n");

        while (true) {
            // Keep the program stopped so the error remains visible.
            thread_sleep_for(1000);
        }
    }

    // FIX 8: Spin calibration first (rotate the board flat), then the
    // horizontal Z calibration (hold still). Not fatal if the spin fails.
    calibrate_compass_offsets();

    // Calibrate the horizontal Z-axis reference before starting tilt checks.
    // The measurement ticker is not attached yet, so the magnometer thread
    // is idle and main() is the only code using I2C right now.
    print_message("Now hold the board flat and STILL for the tilt reference.\r\n");

    if (!calibrate_horizontal_position()) {
        print_message("FATAL ERROR: Horizontal calibration failed.\r\n");

        while (true) {
            thread_sleep_for(1000);
        }
    }

    // Start the compass and LED brightness thread.
    if (ledThread.start(led_thread_function) != osOK) {
        print_message("FATAL ERROR: Could not start LED thread.\r\n");
        while (true) {
            thread_sleep_for(1000);
        }
    }

    // Start the tilt monitoring thread.
    if (tiltThread.start(tilt_thread_function) != osOK) {
        print_message("FATAL ERROR: Could not start tilt thread.\r\n");
        while (true) {
            thread_sleep_for(1000);
        }
    }

    // Trigger magnetometer measurements every 100 milliseconds.
    measurement_ticker.attach(&measurement_ticker_function, 100ms);

    // Update the blue and green LED PWM every 1 millisecond.
    pwm_ticker.attach(&pwm_ticker_function, 1ms);

    print_message("All threads started successfully.\r\n");
    print_message("Program is running. Keep the board horizontal for compass testing.\r\n");

    // Keep the main thread alive while the other threads perform the work.
    while (true) {
        thread_sleep_for(1000);
    }
}
