/*
Name: Lauren M Kasper
Assignment: Lab2_4280_2026
Date: 09/22/26
Overview: 
- Make PMW work three ways, simultaneously
- eat some neapolitan ice cream to cope with this lab
*/

#include "mbed.h"
#include "lkasper_binaryutils.hpp"
#include "USBSerial.h"
#include "nrf_pwm.h"

USBSerial serial;

Queue<Cycle, 9> duty_queue; // array full of 9 Cycle types for our duty queue
MemoryPool<Cycle, 9> duty_pool; //array full of 9 Cycle types for our duty pool

PwmOut pwm_LED(P0_6); // PWMOut for chocolate (blue LED)

// global variable
volatile uint32_t ticker_count = 0;

// Ticker
Ticker pwm_ticker;

// Threads
Thread producerThread;
Thread vanillaThread;
Thread chocolateThread;
Thread strawberryThread;

// determine how long to keep the ticker on/off
 void ticker_function(){
    ticker_count++;
}

// Producter generates cycle and pushes them into the queue
void producer_thread(){
    DIRCLR = LEDG | LEDB | LEDR; // turn off all LEDS
    while(1){
        Cycle* d1 = duty_pool.alloc();
        if (d1 != nullptr){
            d1->percent = 0.33f; // brightness
            duty_queue.put(d1);
        }
        thread_sleep_for(1000);// send new duty cycle once per second
    }
}

// Vanilla PWM manual toggle for LEDG on/off
void vanilla_thread(){
    DIRSET = LEDG;
    float duty = 0.33f; // brightness
    int32_t pwm_period = 3;

    while(1){
        Cycle *msg = nullptr;
        if(duty_queue.try_get(&msg)){
            if(msg != nullptr){
                duty = msg->percent;
                duty_pool.free(msg);
            }
        }
        
        uint32_t on_time = (uint32_t)(pwm_period * duty);
        uint32_t off_time = (pwm_period - on_time);

        //LED ON
        OUTCLR = LEDG;

        uint32_t start = ticker_count;

        while((ticker_count - start) < on_time){
            // wait for ticker to advance
        }

        //LED OFF

        OUTSET = LEDG;
        start = ticker_count;

        while((ticker_count - start) < off_time){
            //wait for ticker to advance
        }
   }
}

//chocolate PWM has LEDB use out class
void chocolate_thread(){
    pwm_LED.period_ms(2);
    float duty = 0.75f;

    while(1){
        Cycle *msg = nullptr;
        
        if(duty_queue.try_get(&msg)){
            if(msg != nullptr){
                duty = msg->percent;
                duty_pool.free(msg);
            }
        }

        pwm_LED.write(duty); // cycle to PWM hardware
        thread_sleep_for(10); //sleep for 5ms
    }
}

// strawberry PWM has LEDR use PWM hardware
void strawberry_thread(){
    uint32_t pwm_pins[4] = {
        24,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED
    };

    nrf_pwm_pins_set(NRF_PWM0, pwm_pins);
    DIRSET = LEDB;
    OUTSET = LEDB;

    nrf_pwm_configure(
        NRF_PWM0,
        NRF_PWM_CLK_1MHz,
        NRF_PWM_MODE_UP,
        2000
    );

    float duty = 0.50f;

    static uint16_t pwm_value; // PMW must stay inram so use static, cause hardware reads it
    static nrf_pwm_sequence_t seq;// two sequences for continuous looping structure

    while(1){
        Cycle *msg = nullptr;

        if(duty_queue.try_get(&msg)){
            if(msg != nullptr){
                duty = msg->percent;
                duty_pool.free(msg);
            }
        }

        pwm_value = (uint16_t)(2000 * duty);

        // set sequence using pwm value
        seq.values.p_raw = &pwm_value;
        seq.length = 1;
        seq.repeats = 0;
        seq.end_delay = 0;

        //put same sequence into sequence registers
        nrf_pwm_sequence_set(NRF_PWM0, 0, &seq);
        nrf_pwm_sequence_set(NRF_PWM0, 1, &seq);

        //repeat pattern
        nrf_pwm_loop_set(NRF_PWM0, 1);

        nrf_pwm_shorts_set(
            NRF_PWM0,
            NRF_PWM_SHORT_LOOPSDONE_SEQSTART0_MASK
        );

        nrf_pwm_enable(NRF_PWM0);

        //start sequence
        nrf_pwm_task_trigger(
            NRF_PWM0,
            NRF_PWM_TASK_SEQSTART0
        );

        thread_sleep_for(10);
    }
}

// main() runs in its own thread in the OS
int main()
{

    pwm_ticker.attach(&ticker_function, 2ms);
    producerThread.start(producer_thread);
    vanillaThread.start(vanilla_thread);
    //chocolateThread.start(chocolate_thread);
    //strawberryThread.start(strawberry_thread);

    while(1){
        thread_sleep_for(1000); //keep main alive forever  else stop all threads
    }
}
