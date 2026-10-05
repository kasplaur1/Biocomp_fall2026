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

//use ofr part 2
//Queue<Cycle, 9> duty_queue; // array full of 9 Cycle types for our duty queue
//MemoryPool<Cycle, 9> duty_pool; //array full of 9 Cycle types for our duty pool

// part 3 queues/ duty cycles
Queue<Cycle, 9> green_queue;
Queue<Cycle, 9> blue_queue;
Queue<Cycle, 9> red_queue;

MemoryPool<Cycle, 9> green_pool;
MemoryPool<Cycle, 9> blue_pool;
MemoryPool<Cycle, 9> red_pool;

PwmOut pwm_LED(P0_6); // PWMOut for chocolate (blue LED)

// global variable
volatile uint32_t ticker_count = 0;
const uint32_t pwm_period = 10; // how many times ticker fires to complete one PWM cycle
volatile uint32_t on_counts = 0; // how many of the pwm_period that the LED should be ON
// use for part 2, change to = 8

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
}

/*
// producer for vanilla/chocolate/strawberry
// Producter generates cycle and pushes them into the queue
void producer_thread(){
    DIRCLR = LEDG | LEDB | LEDR; // turn off all LEDS
    while(1){
        Cycle* d1 = duty_pool.try_alloc(); // allocate cycle from memorypool
        if (d1 != nullptr){
            d1->percent = 0.50f; // set duty cycle to 33%
            duty_queue.try_put(d1); // put cycle into queue
        }
        thread_sleep_for(1000);// send new duty cycle once per second
    }
}
*/

void send_duty(Queue<Cycle, 9>& queue, MemoryPool<Cycle, 9>& pool, float percent){
    Cycle* msg = pool.try_alloc();
    if(msg != nullptr){
        msg->percent = percent;
        queue.try_put(msg);
    }
}


//producer for part 3 - combining them all together
void producer_thread()
{
    DIRSET = LEDG | LEDB | LEDR;
    OUTSET = LEDG | LEDB | LEDR;

    int green_step = 0;
    int blue_step = 0;
    int red_step = 0;

    while (1)
    {
        // --------------------------------
        // GREEN
        // Slow glow
        // Maximum brightness = 33%
        // --------------------------------

        float green_duty;

        if (green_step < 100)
        {
            green_duty = 0.33f * ((float)green_step / 100.0f);
        }
        else
        {
            green_duty = 0.33f *
                ((float)(200 - green_step) / 100.0f);
        }


        // --------------------------------
        // BLUE
        // Medium glow
        // Maximum brightness = 75%
        // --------------------------------

        float blue_duty;

        if (blue_step < 50)
        {
            blue_duty = 0.75f * ((float)blue_step / 50.0f);
        }
        else
        {
            blue_duty = 0.75f *
                ((float)(100 - blue_step) / 50.0f);
        }


        // --------------------------------
        // RED
        // Fast glow
        // Maximum brightness = 50%
        // --------------------------------

        float red_duty;

        if (red_step < 25)
        {
            red_duty = 0.50f * ((float)red_step / 25.0f);
        }
        else
        {
            red_duty = 0.50f *
                ((float)(50 - red_step) / 25.0f);
        }


        // Send ALL THREE simultaneously
        send_duty(green_queue, green_pool, green_duty);
        send_duty(blue_queue, blue_pool, blue_duty);
        send_duty(red_queue, red_pool, red_duty);


        // Advance each LED independently
        green_step++;
        blue_step++;
        red_step++;


        // Repeat each glow cycle
        if (green_step >= 200)
            green_step = 0;

        if (blue_step >= 100)
            blue_step = 0;

        if (red_step >= 50)
            red_step = 0;


        thread_sleep_for(10);
    }
}


/*
// Vanilla PWM manual toggle for LEDG on/off
void vanilla_thread(){
    DIRSET = LEDG;
    float duty;

    while(1){
        Cycle *msg = nullptr;
        if(duty_queue.try_get_for(10ms, &msg)){
            if(msg != nullptr){
                duty = msg->percent;
                on_counts = (uint32_t)(pwm_period * duty); // converts duty cycle into ON counts
                duty_pool.free(msg);
            }
        }
   }
}
*/

// part 3 vanilla: same just change queues
void vanilla_thread(){
    DIRSET = LEDG;
    float duty = 0.0f;

    while(1){
        Cycle *msg = nullptr;
        if(green_queue.try_get(&msg)){
            if(msg != nullptr){
                duty = msg->percent;
                on_counts = (uint32_t)(pwm_period * duty); // converts duty cycle into ON counts
                green_pool.free(msg);
            }
        }

        thread_sleep_for(1);
   }
}

/*
//chocolate PWM has LEDB use out class
void chocolate_thread(){
    float duty;

    while(1){
        Cycle *msg = nullptr;
        
        if(duty_queue.try_get_for(10ms, &msg)){
            if(msg != nullptr){
                duty = msg->percent;
                on_counts = (uint32_t)(pwm_period * duty); // converts duty cycle to ON counts
                duty_pool.free(msg);
            }
        }

        pwm_LED.write(duty); // cycle to PWM hardware
    }
}
*/

// part 3 chocolate: change queues
void chocolate_thread(){
    float duty = 0.0f;

    while(1){
        Cycle *msg = nullptr;
        
        if(blue_queue.try_get(&msg)){
            if(msg != nullptr){
                duty = msg->percent;
                //on_counts = (uint32_t)(pwm_period * duty); // converts duty cycle to ON counts
                blue_pool.free(msg);
            }
        }

        pwm_LED.write(duty); // cycle to PWM hardware
        thread_sleep_for(1);
    }
}

/*
// strawberry PWM has LEDR use PWM hardware
void strawberry_thread(){
    uint32_t pwm_pins[4] = {
        24,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED
    };

    nrf_pwm_pins_set(NRF_PWM0, pwm_pins);
    DIRSET = LEDR;
    OUTSET = LEDR;

    nrf_pwm_configure(
        NRF_PWM0,
        NRF_PWM_CLK_1MHz,
        NRF_PWM_MODE_UP,
        2000
    );

    float duty;

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

        nrf_pwm_sequence_set(
            NRF_PWM0,
            0,
            &seq
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
*/
// part 3 strawberry: change queues
void strawberry_thread(){
    uint32_t pwm_pins[4] = {
        24,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED,
        NRF_PWM_PIN_NOT_CONNECTED
    };

    nrf_pwm_pins_set(NRF_PWM0, pwm_pins);

    nrf_pwm_configure(
        NRF_PWM0,
        NRF_PWM_CLK_1MHz,
        NRF_PWM_MODE_UP,
        2000
    );

    float duty = 0.0f;

    static uint16_t pwm_value; // PMW must stay inram so use static, cause hardware reads it
    static nrf_pwm_sequence_t seq;// two sequences for continuous looping structure

    while(1){
        Cycle *msg = nullptr;

        if(red_queue.try_get(&msg)){
            if(msg != nullptr){
                duty = msg->percent;
                red_pool.free(msg);
            }
        }

        pwm_value = (uint16_t)(2000 * duty);

        // set sequence using pwm value
        seq.values.p_raw = &pwm_value;
        seq.length = 1;
        seq.repeats = 0;
        seq.end_delay = 0;

        nrf_pwm_sequence_set(
            NRF_PWM0,
            0,
            &seq
        );

        nrf_pwm_enable(NRF_PWM0);

        //start sequence
        nrf_pwm_task_trigger(
            NRF_PWM0,
            NRF_PWM_TASK_SEQSTART0
        );

        thread_sleep_for(1);
    }
}


// main() runs in its own thread in the OS
int main()
{

    pwm_ticker.attach(&ticker_function, 2ms);
    producerThread.start(producer_thread);
    vanillaThread.start(vanilla_thread);
    chocolateThread.start(chocolate_thread);
    strawberryThread.start(strawberry_thread);

    while(1){
        thread_sleep_for(1000); //keep main alive forever  else stop all threads
    }
}