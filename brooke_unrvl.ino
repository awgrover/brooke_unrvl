/*
    Do you want a don't ring switch? It still talks on pickup, but won't ring?
    If you leave the arduino running for about 3 consecutive days, this code will get confused.
    Shouldn't we play a different message if the recording has "filled up"?
    "Tune" the area the motion-detect says in relation to the phone/room.

    Should it make noise if left off hook too long? Of if nothing moves for too long?
    
*/

// On for serial/development, 0 for no serial (so you can avoid serial)
#define DEV 1

#define OnHookPin 4
#define RingerPin 13 // fixme 3
#define PIRPin 5
// mp3 stuff i2c I think ...

#define RingerFrequency 38 // hertz
#define RingingDelay  (1000 / 38) // because of rounding we'll be off a bit.

#define RingingDuration 1000   // millis
#define BetweenRings  1500     // millis
#define StopRingingAfter (30 * 1000) // millis
#define BetweenCalls (30 * 1000) // millis
#define HookDebounce 10 // millis to wait for a steady value
#define PIRDebounce 20 // PIR also needs debounce (for "on")

// I'm using a non-blocking sequence tool, so we can respond to pickup/hangup, etc.
#include "state_machine.h"

// ringer sound is about 30hz
SIMPLESTATEAS(ring1, (sm_digitalWrite<RingerPin, HIGH>), pause1)
SIMPLESTATEAS(pause1, sm_delay<RingingDelay>, ring2)
SIMPLESTATEAS(ring2, (sm_digitalWrite<RingerPin, LOW>), pause2)
SIMPLESTATEAS(pause2, sm_delay<RingingDelay>, ring1)

STATEMACHINE(ring_sound, ring1)

// But actual ring pattern is ringing, pause, ringing...
SIMPLESTATE(ring_on_duration, ring_finish)
SIMPLESTATEAS(ring_finish, (sm_digitalWrite<RingerPin, LOW>), ring_pause) // nicety
SIMPLESTATEAS(ring_pause, sm_delay<RingingDuration>, ring_on_duration)

STATEMACHINE(ringing_pattern, ring_on_duration);

/*
FunctionPointer say_hello[] = {
    &start_playing_hello,
    &wait_for_done_playing
    };

FunctionPointer do_recording[] = {
    &start_recording,
    // &wait_for_done_recording // how do we say "record up to 30 seconds, but let us stop early if we want?"
    };
*/

//
// Overall state machine
//
    // Startup waits till onhook
    SIMPLESTATE(wait_for_onhook, wait_for_victim)
    // ready, wait for motion (and still onhook)
    SIMPLESTATE(wait_for_victim, ring_the_phone)
    // ring the phone for a while, then give up.
    STATE(ring_the_phone, between_calls)
        GOTOWHEN(SM_not< onhook >, pause_before_message) // somebody answered!
    END_STATE
    // Hello
    SIMPLESTATEAS(pause_before_message, sm_delay<600>, saying_hello) // give time to get to ear
    STATE(saying_hello, record_response) // start recording after saying hello
        GOTOWHEN(onhook, between_calls) // they hung up on us
    END_STATE
    // Record
    STATE(record_response, wait_for_hangup) // record, and wait for hangup
        GOTOWHEN(onhook, between_calls) // they hung up before timeout
    END_STATE
    SIMPLESTATEAS(wait_for_hangup, wait_for_onhook, between_calls) // could do a delay<xx> and get annoyed
    // nothing can happen for a bit
    SIMPLESTATEAS(between_calls, sm_delay<BetweenCalls>, wait_for_victim)

STATEMACHINE(the_system, wait_for_onhook);

void setup() {
    if (DEV) { Serial.begin(19200); Serial.println("Start"); }
    pinMode(RingerPin, OUTPUT);
    digitalWrite(RingerPin, LOW);
    }

void loop() { // tests
    // ring_sound.run();
    // ringing_pattern.run();
    if (true) { // ring_the_phone
        Serial.print("start ");  Serial.println(millis());
        if (!ring_the_phone(SM_Start)) { Serial.println("failed on sm_start"); }
        while(ring_the_phone(SM_Running)) {}; 
        Serial.print("Stop "); Serial.println(millis()); 
        delay(2000);
        }
    }

void xxloop() {
    the_system.run();
    }

boolean wait_for_onhook() { 
    return ! onhook(); // we are waiting for it
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

boolean wait_for_victim() {
    // shouldn't have to worry about onhook here, but it makes it clear
    return !( onhook() && motion() ); // we are waiting for both
    }

boolean ring_the_phone(StateMachinePhase phase) {
    static unsigned long timer = 0;
    if (phase == SM_Start) {
        RESTART(ringing_pattern, ring_on_duration); // we always restart the pattern 
        }
    ringing_pattern.run();
    return !wait_for(timer, StopRingingAfter);
    }

boolean record_response(StateMachinePhase phase) {
    // start record, wait for n seconds, cleanup
    }

boolean wait_for_hangup(StateMachinePhase phase) {
    // start record, wait for n seconds, cleanup
    }

boolean saying_hello(StateMachinePhase phase) {
    // start the sound, wait for it to finish, cleanup
    }

boolean ring_on_duration() {
    // run ring_sound for n millis
    static unsigned long timer = 0; 
    ring_sound.run();
    return ! wait_for(timer, 1000);
    }
/*
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

*/
// Use this instead of delay.
// Also, an example of a function for use in the sequences.
// Convenient to use like: void xyz() { static unsigned long w; wait_for(&w, 2000) }
// "wait" is milliseconds.
// e.g.: static unsigned long timer = 0; if (wait_for(timer, 100)) { running...}
boolean wait_for(byte *state, int wait) { return wait_for(state, (unsigned long) wait); }
boolean wait_for(unsigned long &state, int wait) { return wait_for((byte *)&state, (unsigned long) wait); }
boolean wait_for(byte *state, long unsigned int wait) {
  // true on expire
  unsigned long *timer = (unsigned long *)state;
  if (*timer == 0) {
      // Serial.print(millis()); Serial.print(": "); Serial.print("Start delay "); Serial.println(wait); 
      *timer = wait + millis();
      return false;
  }
  else if (*timer <= millis()) {
    // Serial.print(millis()); Serial.print(": "); Serial.print("Finish delay "); Serial.println(wait);
    *timer = 0;
    return true;
  }
  else {
    return false;
  } 
}

