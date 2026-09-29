/*
Name: Lauren M Kasper
Assignment: Lab1_4280_2026
Date: 09/08/26
Overview: 
- create a handler thread that drives a diagnostic LED
- Make a diagnostic function
*/

#include "mbed.h"
#include "lkasper_binaryutils.hpp"
#include "USBSerial.h"

USBSerial serial;

// array full of 8 Message types for our mail queue
Mail<Message, 8> mail_queue;

volatile state current_state = NO_ERROR;


// diagnostic handler
void led_diag_handler(){
    DIRSET = LEDR | LEDG;

    while(1){
        // osEvent evt: got from MBed RTOS API documentation
        osEvent evt = mail_queue.get(0); // try to recieve message from the queue

        //if message arrives, update current state to diagnostic state
        if(evt.status == osEventMail){
            Message* msg = (Message*)evt.value.p; // get message pointer
            state old_state = current_state; // save old state before updating
            current_state = msg->diag_state; //update state
            serial.printf("State change from %d to %d\r\n", old_state, current_state);
            mail_queue.free(msg); //free memroy block
        }

        // clear LEDs before blinking begings
        OUTCLR = LEDR | LEDG | LEDB;

        // switch case statement: Annabel recommended using this logic
        switch(current_state){
            case NO_ERROR:
                OUTSET = LEDR | LEDG | LEDB;
                OUTSET = LEDG;
                thread_sleep_for(400);
                OUTCLR = LEDG;
                thread_sleep_for(400);
                break;

            case ATTN_REQ:
                OUTSET = LEDR | LEDG | LEDB;
                OUTSET = LEDG | LEDR;
                thread_sleep_for(200);
                OUTCLR = LEDR | LEDG;
                thread_sleep_for(200);
                break;

            case FATAL_ERROR:
                OUTSET = LEDR | LEDG | LEDB;
                OUTSET = LEDR;
                thread_sleep_for(100);
                OUTCLR = LEDR;
                thread_sleep_for(100);
                break;

            default:
                OUTCLR = LEDR | LEDG | LEDB;
                thread_sleep_for(200);
                break;
        }
    }
}

void diag_tester(){
    state next_state = NO_ERROR;

    while(1){
        /*
        Mail Logic: 
        Thread or ISR ->(put)-> Mail queue ->(get)-> thread or ISR ->(free)-> memory blocks ->(alloc)->thread or ISR
        */

        Message* msg = mail_queue.alloc(); // Allocate message block
        msg->diag_state = next_state; // fill message
        mail_queue.put(msg); //put message into queue

        if(next_state == FATAL_ERROR){
            serial.printf("Resetting diagnostic sequence back to NO_ERROR\r\n");
        }

        // state machine logic:
        //NO_ERROR->ATTN_REQ->FATAL_ERROR->NO_ERROR... repeat
        switch(next_state){
            case NO_ERROR:
                next_state = ATTN_REQ;
                break;
            case ATTN_REQ:
                next_state = FATAL_ERROR;
                break;
            case FATAL_ERROR:
                next_state = NO_ERROR;
                break;
        }

        thread_sleep_for(5000); //every 5 seconds
    }
}

int main(){
    Thread handler_thread;
    Thread tester_thread;

    handler_thread.start(callback(led_diag_handler));
    tester_thread.start(callback(diag_tester));

    while(1){
        thread_sleep_for(1000);
    }
}


/* PART 2 - DONT DO THIS AGAIN - terrible design
// main() runs in its own thread in the OS
int main()
{
    
    // set LEDR and LED G as output
    DIRSET = LEDR | LEDG;
    state current_state = NO_ERROR; // variable fo rcurrent state on chip

    while (1) {
        if (current_state == NO_ERROR){ 
            OUTSET = LEDR | LEDG;
            for(int i = 0; i < 5; i++){
                OUTSET = LEDG; // turn LEDG on
                thread_sleep_for(200); // turn on led for 2 secs
                OUTCLR = LEDG; // turn LEDG off
                thread_sleep_for(200);
            }
        }

        else if(current_state == ATTN_REQ){
            OUTSET = LEDR | LEDG;
            for(int i = 0; i < 10; i++){
                OUTSET = LEDG | LEDR; // turn LEDG & LEDR on
                thread_sleep_for(100); // turn on thread for 2 secs
                OUTCLR = LEDG | LEDR; // turn LEDG & LEDR off
                thread_sleep_for(100);
            }
        }

        else if (current_state == FATAL_ERROR){
            OUTSET = LEDR | LEDG;
            for(int i = 0; i < 20; i++){
                OUTSET = LEDR; // turn LED R on
                thread_sleep_for(50);
                OUTCLR = LEDR;
                thread_sleep_for(50);
            }
        }

        // state machine logic:
        //NO_ERROR->ATTN_REQ->FATAL_ERROR->NO_ERROR... repeat
        if(current_state == NO_ERROR){
            current_state = ATTN_REQ;
        }
        else if(current_state == ATTN_REQ){
            current_state = FATAL_ERROR;
        }
        else{
            current_state = NO_ERROR;
        }
    }
    

    //Part 3
    while(1){

    }
}
*/
