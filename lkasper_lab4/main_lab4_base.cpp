/* mbed Microcontroller Library
 * Copyright (c) 2006-2019 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include <cstdint>
#include <events/mbed_events.h>
#include "ble/BLE.h"
#include "ble/Gap.h"
#include <AdvertisingDataSimpleBuilder.h>
#include <ble/gatt/GattCharacteristic.h>
#include <ble/gatt/GattService.h>
#include "att_uuid.h"

#include <USBSerial.h>

USBSerial ser;

Thread tempthread;

static events::EventQueue event_queue(16 * EVENTS_EVENT_SIZE);

// Your BLE instance
BLE &bleinit= BLE::Instance();
// Your GAP instance
Gap& gap = bleinit.gap();
// Your GattServer instance
GattServer& gattServe = bleinit.gattServer();
// Your GattClient instance
GattClient& gattClient = bleinit.gattClient();

int16_t TOUT =0;

//LK added
ReadOnlyGattCharacteristic<int16_t> temperatureCharacteristic( // Create a read-only temperature characteristic
    UUID(0xA001), // Give the characteristic a custom 16-bit UUID
    &TOUT, // Set the starting temperature value to TOUT
    GattCharacteristic::BLE_GATT_CHAR_PROPERTIES_INDICATE // Allow the phone to subscribe to temperature updates
);

GattCharacteristic *temperatureChars[] = { &temperatureCharacteristic }; // Create an array containing the temperature characteristic

GattService temperatureService( // Create the Health Thermometer service
    UUID(0x1809), // Use the standard Health Thermometer service UUID
    temperatureChars, // Add the temperature characteristic to the service
    sizeof(temperatureChars) / sizeof(temperatureChars[0]) // Calculate the number of characteristics in the service
);
//LK done

using namespace ble;

/**
 * Event handler struct. Handle Bluetooth GAP events LK changed
 */
struct GapEventHandler : Gap::EventHandler{
    /* 
	 * Implement the functions here that you think you'll need. These are defined in the GAP EventHandler:
     * https://os.mbed.com/docs/mbed-os/v6.6/mbed-os-api-doxy/structble_1_1_gap_1_1_event_handler.html
     */
    void onAdvertisingStart(const ble::AdvertisingStartEvent &event){
        // run when advertising starts
        ser.printf("Bluetooth advertising start status: %d\n\r", event.getStatus());
    }

    void onAdvertisingEnd(const ble::AdvertisingEndEvent &event){
        // run when advertising ends
        ser.printf("Bluetooth advertising end status: %d\r\n", event.getStatus());
    }

    void onScanRequestRecieved(const ScanRequestEvent &event){
        // run whena device sends a scan request
        ser.printf("A device requested advertising information\n\r");
    }
};


GapEventHandler THE_gap_EvtHandler; // create the event handler object


void measure_temp(){
    I2C sensor_bus(I2C_SDA1, I2C_SCL1);

    uint8_t subvol_open[] = {0xA0, 0x00, 0x00};
    uint8_t subvol_close[] = {0x80, 0x00, 0x00};
    uint8_t subaddr[] = {0x1E, 0x00, 0x00};
    int resp=4;

	const int readaddr = 0x88; // 44, left shifted
	const int writeaddr = 0x88;
    //char readData[] ={0, 0, 0}; original LK changed
    char readData[4] = {0, 0, 0, 0}; // store all 4 bytes returned by HS3000
    uint16_t upperID =0;
    uint16_t lowerID =0;
    uint32_t fullID =0;
    resp = sensor_bus.write(writeaddr, (const char *) subvol_open, sizeof(subvol_open)/sizeof(subvol_open[0]), false);    
    if(  resp != 0 ){
        ser.printf("Darn! I failed to talk at the temp sensor. (Returned: %d)\n\r", resp);            
    }

    resp = sensor_bus.write(writeaddr, (const char *) subaddr, sizeof(subaddr)/sizeof(subaddr[0]), true);
              
    if( sensor_bus.read(readaddr, readData, 3)  != 0 ){
        ser.printf("Darn! I failed to listen to the temp sensor.\n\r");        
    }
    
    if( readData[0] != 0x81 ){
        ser.printf("Upper ID read not Successful.\n\r");
    }else{        
        upperID = (readData[1] << 8);
        upperID |= readData[2];
        ser.printf("Upper ID is: %d\n\r", upperID);
    }

    subaddr[0] = 0x1F;
    resp = sensor_bus.write(writeaddr, (const char *) subaddr, sizeof(subaddr)/sizeof(subaddr[0]), true);

    sensor_bus.read(readaddr, readData, 3);
    if( readData[0] != 0x81 ){
        ser.printf("Lower ID read not Successful.\n\r");
    }else{        
        lowerID = (readData[1] << 8);
        lowerID |= readData[2];
        ser.printf("Lower ID is: %d\n\r", lowerID);
    }

    if( lowerID !=0 && upperID !=0 ){
        fullID = lowerID;
        fullID |= (upperID << 16);
        ser.printf("ID is: %u\n\r", fullID);
    }

    resp = sensor_bus.write(writeaddr, (const char *) subvol_close, sizeof(subvol_close)/sizeof(subvol_close[0]), false);

    int32_t humidity = 0;
    
    while (true) {
        thread_sleep_for(1000); // Wait one second between readings

        resp = sensor_bus.write(writeaddr, (const char *) subvol_close, 0); // Use the professor's sensor command
        thread_sleep_for(35); // Wait for the sensor measurement

        resp = sensor_bus.read(readaddr, readData, 4); // Read the 4 sensor bytes
        if (resp != 0) {
            ser.printf("Failed to read HS3000 data: %d\n\r", resp);
            continue; // Skip this reading if the I2C read failed
        }

        ser.printf("Status bits: %d\n\r", (readData[0] >> 6)); // Print sensor status bits

        humidity = ((readData[0] & 0x3F) << 8) | readData[1]; // Calculate raw humidity
        ser.printf("Humidity %%: %u\n\r", (humidity * 100 / 16383)); // Print humidity
        uint16_t rawTemp = ((((uint16_t)(uint8_t)readData[2] << 8) | (uint8_t)readData[3]) >> 2); // Extract the 14-bit temperature value
        ser.printf("Raw bytes: %02X %02X %02X %02X\n\r", (uint8_t)readData[0], (uint8_t)readData[1], (uint8_t)readData[2], (uint8_t)readData[3]); // Print all sensor bytes
        float tempC = (rawTemp * 165.0f / 16383.0f) - 40.0f; // Convert raw data to Celsius
        float tempF = (tempC * 1.8f) + 32.0f; // Convert Celsius to Fahrenheit
        TOUT = (int16_t)(tempF * 100.0f); // Store Fahrenheit multiplied by 100 for BLE
        ser.printf("Temperature: %d.%02d F\n\r", TOUT / 100, (TOUT % 100 < 0) ? -(TOUT % 100) : (TOUT % 100)); // Print Fahrenheit with two decimal places

    }

}

// LK changed this part: uses callbalc already provided in the startere file
// Using this to check initialization result and print a message to the seriel terminal
void on_init_complete(BLE::InitializationCompleteCallbackContext *params){
    if (params->error == BLE_ERROR_NONE) { // Check if BLE initialized successfully
        ser.printf("BLE initialization successful!\n\r"); // Print a success message

        gap.setEventHandler(&THE_gap_EvtHandler); // Register the GAP event handler

        //register the service with Bluetooth 
        
        ble_error_t serviceError = gattServe.addService(temperatureService); // Add the temperature service to the Bluetooth server

        if (serviceError == BLE_ERROR_NONE) { // Check if the service was added successfully
            ser.printf("Temperature service added successfully!\n\r"); // Print a success message
        }
        else {
            ser.printf("Failed to add temperature service: %d\n\r", serviceError); // Print the error code
            return; // Stop setup if the service could not be added
        }

        ble::AdvertisingDataSimpleBuilder<ble::LEGACY_ADVERTISING_MAX_SIZE> builder; // Create the advertising payload builder
        builder.setFlags().setName("Lauren_Lab4"); // Set advertising flags and the Bluetooth device name

        ble_error_t error = gap.setAdvertisingPayload(ble::LEGACY_ADVERTISING_HANDLE, builder.getAdvertisingData()); // Apply the advertising payload

        if (error != BLE_ERROR_NONE) { // Check if the payload was accepted
            ser.printf("Failed to set advertising payload: %d\n\r", error); // Print the error code
            return; // Stop advertising setup if the payload failed
        }

        error = gap.startAdvertising(ble::LEGACY_ADVERTISING_HANDLE); // Start Bluetooth advertising

        if (error != BLE_ERROR_NONE) { // Check if advertising could be started
            ser.printf("Failed to start advertising: %d\n\r", error); // Print the error code
        }
    }
    else{
        ser.printf("BLE initialization failed: %d\n\r", params->error);
    }
}

/* Schedule processing of events from the BLE middleware in the event queue. */
void schedule_ble_events(BLE::OnEventsToProcessCallbackContext *context){
    event_queue.call(mbed::Callback<void()>(&context->ble, &BLE::processEvents));
}


int main(){
    DigitalOut i2cbuspull(P1_0); // Pull up i2C. resistor.
    i2cbuspull.write(1);
    DigitalOut sensor_pwr(P0_22); // Supply power to all of the sensors (VCC)
    sensor_pwr.write(1);
	thread_sleep_for(1);

    tempthread.start(measure_temp); // Start reading temperature and humidity in a separate thread
    bleinit.onEventsToProcess(schedule_ble_events);
   
    //lk added. Initialize Bluetooth and call the callback when finished
    bleinit.init(on_init_complete);

    // This will never return...
    event_queue.dispatch_forever();
}
