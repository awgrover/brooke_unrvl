/*
    Do you want a don't ring switch? It still talks on pickup, but won't ring?
    If you leave the arduino running for about 3 consecutive days, this code will get confused.
*/

#define OnHookPin 4
#define RingerPin 3
#define PIRPin 5
#define RingerFrequency 38 // hertz
#define RingingDelay  (1000 / 38) // because of rounding we'll be off a bit.

// timings 
#define RingingDuration 1000   // millis
#define BetweenRings  1500     // millis
#define StopRingingAfter (30 * 1000) // millis
#define BetweenCalls (30 * 1000) // millis
#define HookDebounce 10 // millis to wait for a steady value
#define PIRDebounce 20 // PIR also needs debounce (for "on")

// I'm using a non-blocking sequence tool, so we can respond to pickup/hangup, etc.
#include "sequence_machine2.h"

// Ringing _sound_ is just on/off
FunctionPointer ringing_sound[] = {
    &digitalWrite<RingerPin, HIGH>, 
    &wait_for<RingingDelay>,
    &digitalWrite<RingerPin, LOW>, 
    &wait_for<RingingDelay>,
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

FunctionPointer say_hello[] = {
    &start_playing_hello,
    &wait_for_done_playing
    };

FunctionPointer do_recording[] = {
    &start_recording,
    // &wait_for_done_recording // how do we say "record up to 30 seconds, but let us stop early if we want?"
    };

// On for serial/development, 0 for no serial (so you can avoid serial)
#define DEV 1

void setup() {
    if (DEV) Serial.begin(19200);
    pinMode(RingerPin, OUTPUT);
    }

enum system_states { 
    startup, // power on
    ready_to_call, // on-hook, and it's been a while since last call
    ring_the_phone, // start ringing, hope they pickup
    play_message, // yay, say something!
    record_response, // try to record their response
    wait_for_hangup, // after recording
    between_calls, // wait between calls
    };

void loop() {
    static system_states state = startup; // on power on

    switch (state) {

        case startup:
            if (onhook()) {
                state = ready_to_call; // could start at: between_calls. if you want a start delay
                }
            // else wait for onhook
            break;

        case ready_to_call:
            if (motion() && onhook() ) {
                state = ring_the_phone;
                }
            break;

        case ring_the_phone:
            if (ring_a_while()) {
                if ( ! onhook() ) {
                    // answered!
                    state = play_message;
                    }
                else {
                    // no one answered. we are sad.
                    state = between_calls;
                    }
                }
            break;

        case play_message:
            if( saying_hello(true) ) {
                // we are saying hello...
                if ( onhook() ) {
                    // they hung up on us before we finished
                    saying_hello(false); // have to reset
                    state = between_calls;
                    }
                }
            else {
                // it finished hello
                state = record_response;
                }
            break;

        case record_response:
            if (recording(true)) {
                if (! onhook()) {
                    recording(false); // signal stop recording
                    state = between_calls;
                    }
                }
            else {
                state = wait_for_hangup;
                }
            break;
            
        case between_calls:
            static byte between_timer[8];
            if ( wait_for(between_timer, BetweenCalls) ) { // i.e. expired
                state = ready_to_call;
                }
            break;
        }

    }

boolean ring_a_while() {
    // for StopRingingAfter millis
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

boolean onhook() {
    static boolean is_onhook = true; // got to start somewhere
    static int last_sample = HIGH;
    static unsigned long debounce_timer;

    int now_sample = digitalRead(OnHookPin); // onhook = HIGH

    if (last_sample != now_sample) {
        // it changed, start/keep debouncing
        debounce_timer = 0ul;
        wait_for(debounce_timer,  HookDebounce); // starts it (sets debounce_timer)
        last_sample = now_sample;
        }

    if ( debounce_timer > 0 && wait_for(debounce_timer, 0) ) { // ignores the 2nd arg
        // timer was started and not done yet
        is_onhook = now_sample == HIGH;
        }

    return is_onhook;
    }

boolean motion() {
    // the PIR only has "bounce" at turn on...
    static int has_motion = false;
    static unsigned long ignore_changes;

    if (has_motion && wait_for(ignore_changes, PIRDebounce)) {
        // check for change to no-motion if has-motion has stabilised
        // i.e. wait after "motion" before we check again
        has_motion = digitalRead(PIRPin); // LOW is no motion
        }
    else {
        // if no-motion, immediately react to 
        has_motion = digitalRead(PIRPin);
        }

    return has_motion;
    }

boolean saying_hello(boolean playing) {
    // you have to tell us if you stopped early by passing false for playing
    declare_machine(say_hello);

    if (!playing) {
        machine_from(say_hello).idx=0;
        // need to tell player to stop playing...
        return false; // not really relevant
        }

    else {
        return machine_from(say_hello).run_once();
        }
    }

boolean start_playing_hello(byte *x) {
    // start playing the hello sound ...
    return false; // we are immediately done
    }

boolean wait_for_done_playing(byte *x) {
    boolean its_done = true; // actually false when the code gets written

    // look for the player to tell us it's done with that sound (or error)...
    return !its_done; // return true to keep checking
    }


boolean recording(boolean still_recording) {
    // you have to tell us if you stopped early by passing false for still_recording
    declare_machine(do_recording);

    if (!still_recording) {
        machine_from(do_recording).idx=0;
        // need to tell player to stop playing...
        return false; // not really relevant
        }

    else {
        return machine_from(do_recording).run_once();
        }
    }

boolean start_recording(byte *x) {
    // start recording ...
    // have to tell it a new recording number each time, I assume...
    return false; // we are immediately done
    }

