/*
    Do you want a don't ring switch? It still talks on pickup, but won't ring?
*/

const int RingerPin = 3;
const int RingerFrequency = 38; // hertz
#define ringing_delay  (1000 / 38) // because of rounding we'll be off a bit. 

// timings (need "ul" for some of these
#define RingingDuration 1000   // millis
#define BetweenRings  1500     // millis
#define StopRingingAfter (30 * 1000) // millis
#define BetweenCalls (30 * 1000) // millis

// I'm using a non-blocking sequence tool, so we can respond to pickup/hangup, etc.
#include "sequence_machine2.h"

// Ringing _sound_ is just on/off
FunctionPointer ringing_sound[] = {
    &digitalWrite<RingerPin, HIGH>, 
    &wait_for<ringing_delay>,
    &digitalWrite<RingerPin, LOW>, 
    &wait_for<ringing_delay>,
    &digitalWrite<RingerPin, LOW>,
    };

// But actual ring sequence is ringing, pause, ringing...
// SO, this machine just goes back & forth with delays. Look at ring_step for the current on/off
enum ring_step_stages { ring_on, ring_off };
ring_step_stages ring_step;

// Must be one line to fool IDE preprocessor
template <ring_step_stages s> boolean next_ring_step(byte *x) { ring_step = s; return true;} // 0..1 and around again

FunctionPointer ringing_steps[] = {
   &next_ring_step<ring_on>,
   &wait_for<RingingDuration>,
   &next_ring_step<ring_off>,
   &wait_for<BetweenRings>
   };


// On for serial/development, 0 for no serial (so you can avoid serial)
#define DEV 1

void setup() {
    if (DEV) Serial.begin(19200);
    pinMode(RingerPin, OUTPUT);
    }

enum system_states { 
    between_calls, // wait between calls
    ready_to_call, // on-hook, and it's been a while since last call
    ring_the_phone, // start ringing, hope they pickup
    play_message, // yay, say something!

    };

void loop() {
    static system_states state = ready_to_call; // we start ready!

    switch (state) {
        case ready_to_call:
            if (victim_sensed()) { // and onhook
                state = ring_the_phone;
                }
            break;

        case ring_the_phone:
            if (ring_till_pickup()) {
                if ( offhook() ) {
                    // answered!
                    state = play_message;
                    }
                else {
                    // no one answered. we are sad.
                    state = between_calls;
                    }
                }
            break;

        case between_calls:
            static byte between_timer[8];
            if ( wait_for(between_timer, BetweenCalls) ) { // i.e. expired
                state = ready_to_call;
                }
            break;

        // 
        }

    }

boolean ring_till_pickup() {
    // till pickup or timeout
    static unsigned long stop_ringing_at = 0; // unlikely we'll every hit 0 accidentally
    if (stop_ringing_at == 0) {
        stop_ringing_at = millis() + StopRingingAfter;
        }
    else if (millis() >= StopRingingAfter) {
        stop_ringing_at = 0; // for next time
        return true; // stop ringing
        }
        
    if (ring_step == 0) {run_machine(ringing_sound);}

    run_machine(ringing_steps); // update ring_step from 0,1,0,1 as per delay pattern
    return false; // still ringing
    }

//bool off_hook() {
    
