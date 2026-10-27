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

using namespace ble;

/**
 * Event handler struct
 */
struct GapEventHandler : Gap::EventHandler{
    /* 
	 * Implement the functions here that you think you'll need. These are defined in the GAP EventHandler:
     * https://os.mbed.com/docs/mbed-os/v6.6/mbed-os-api-doxy/structble_1_1_gap_1_1_event_handler.html
     */
	 void onScanRequestReceived (const ScanRequestEvent &event){
		 ser.printf("Rude words");
	 }
};


GapEventHandler THE_gap_EvtHandler;


void measure_temp(){
    I2C sensor_bus(I2C_SDA1, I2C_SCL1);

    uint8_t subvol_open[] = {0xA0, 0x00, 0x00};
    uint8_t subvol_close[] = {0x80, 0x00, 0x00};
    uint8_t subaddr[] = {0x1E, 0x00, 0x00};
    int resp=4;

	const int readaddr = 0x88; // 44, left shifted
	const int writeaddr = 0x88;
    char readData[] ={0, 0, 0};
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
        thread_sleep_for(1000);
        resp = sensor_bus.write(writeaddr, (const char *) subvol_close, 0);
        thread_sleep_for(35);
        sensor_bus.read(readaddr, readData, 4);
        
        ser.printf("Status bits: %d\n\r", readData[0]>>6);

        humidity = ((readData[0] & 0x3F) << 8) | readData[1];
        ser.printf("Humidity %: %u\n", (humidity*100/16383)); 
        
    }
}

void on_init_complete(BLE::InitializationCompleteCallbackContext *params){



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
    bleinit.onEventsToProcess(schedule_ble_events);
   

    // This will never return...
    event_queue.dispatch_forever();
}
