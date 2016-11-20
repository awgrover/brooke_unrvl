// Run several sequences concurrently.
/* Useage
  // add to your libraries!
  #include <sequence_machine2.h>

  // Define some sequences:
  // "sequence1" is YOUR name:
  FunctionPointer sequence1[] = {
    // Use the functions provided in sequence_machine2.h
    // Note the "&" and "<" ">"
    &digitalWrite<led1_pin, HIGH>, 
    &wait_for<200>,  // non-blocking replacement for delay()
    &digitalWrite<led1_pin, LOW>, 
    &wait_for<200>,
    };
  FunctionPointer sequence2[] = {
   &wait_for<190>, // starts at the end of the sequence1's first ON
   &digitalWrite<led2_pin, HIGH>, &wait_for<200>,
   &digitalWrite<led2_pin, LOW>, &wait_for<10>,
   };

  ...

  void loop() {
    // Your normal loop
    
    // do stuff here...
 
    // declare that you are running the machines, those are the names from above:
    run_machine_debug(sequence1);  // with _debug, spits out progress on the serial monitor
    run_machine(sequence2);

    struct variants {
      int some_pin;
      int interval;
      };
    variants my_user_data = { led2_pin, 40 };
    run_machine(sequence3, &my_user_data);  // passing in some struct for use by the machine, init'd or not
    
    // You could do anything you want here, just like normal
    if (digitalRead(9) == HIGH) {
        buttonx = true;
    }
    
    // But, DO NOT use delay()
    // Don't do anything that would take too much time,
    // because that will prevent getting to the next step of the sequences
  }
*/
/*
  Writing your own "step" functions:
  Follow this pattern (see the examples too!):

  template<int initial_arguments> // <- list the "arguments" here. e.g. <int pin, int pwm>
  boolean count_down(byte *state) { <- name of "step" function, always has argument (byte *state)
    // If you need to maintain state, you use "state", which is 8 bytes. Cast it.
    unsigned long *count = (unsigned long *) state;

    // Beginning of step, "state" will be all zeros
    if (*count == 0) {
      *count = initial_argument;  // you can use the <> arguments anywhere
      }

    // Decide if you are still working, you get called everytime in loop() (when on this step)
    if (*count > 0) {
      *count--; // <- doing work!
      return false; // <- still working, so return false
      }
    else {
      return true; // <- Done! return true. You won't get called again until the sequence runs through all the other steps
      }
    }
    // that's it. You can use it in a sequence like: [ ... count_down<10>, ...
*/

#include <Arduino.h>
#define debugm(msg)

// need magic for arraysize
template <typename T,unsigned S> inline unsigned arraysize(const T (&v)[S]) { return S; };

typedef boolean(*FunctionPointer)(byte []); // preserve our sanity

// you could type this out, but this removes some repetition
// this lets you separate the declare & run:
//  declare_machine(your_FunctionPointer_list); ... machine_from(your_FunctionPointer_list).run();
#define declare_machine(sequence) static machine sequence ## _machine = {sequence, arraysize(sequence) };
#define declare_machine_debug(sequence) static machine sequence ## _machine = {sequence, arraysize(sequence), true, #sequence };
// You want user data? do it the hard wayy:
//  static machine your_machine_name = {sequence, arraysize(sequence), false, NULL, (unsigned long) user_data }; ....run
#define declare_machine_as(sequence,as) static machine as = {sequence, arraysize(sequence) };
#define machine_from(sequence) sequence ## _machine
// This declares/calls the machine runner in one statement
#define run_machine(sequence) declare_machine(sequence); \
  sequence ## _machine.run();
#define run_machine_debug(sequence) declare_machine_debug(sequence); \
  sequence ## _machine.run();
  
struct machine {
  public:
  // keep track of how far we are in the sequence
  FunctionPointer *sequence;
  unsigned len;  // need length, from arraysize(), implies literal-time sequences only
  boolean debug;  // optional, turn on serial debug messages
  char *name; // my name, for debugging
 
  int idx;  // how far. don't bother setting
  byte state[8]; // per step, gets reset, see "Writing your own..." above
  unsigned long user_data; // per machine, we don't mess with this. use it as (castyours) &state[8]

  // This runs the machine
  void run() {
    if ((*sequence[idx])(state)) { // call the step, move on if it finished (==true)
        if (debug) {
            Serial.print(name); Serial.print(" ");
            Serial.print(millis()); Serial.print(": "); Serial.print("Did "); Serial.print(idx); Serial.println(" ");
        }
        memset(state, 0, 8);
        idx = (idx + 1) % len;  // start over at 0
    } 
  }

  // this runs it all the way through once, do machine.idx=0 if you stop early
  boolean run_once() {
    run();
    if (idx==0) {
      return false;
    }
    else {
      return true; // still running
    }
  }


};

// Use this instead of delay.
// Also, an example of a function for use in the sequences.
// Convenient to use like: void xyz() { static unsigned long w; wait_for(&w, 2000) }
// "wait" is milliseconds.
boolean wait_for(byte *state, int wait) { return wait_for(state, (unsigned long) wait); }
boolean wait_for(unsigned long &state, int wait) { return wait_for((byte *)&state, (unsigned long) wait); }
boolean wait_for(byte *state, long unsigned int wait) {
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

// I don't think any of the <T *v> templates work

template<unsigned long *wait> 
boolean wait_for(byte *state) {
  return wait_for(state, *wait);
  }
template<const long *wait> 
boolean wait_for(byte *state) {
  return wait_for(state, (unsigned long) *wait);
  }
template<int wait>
boolean wait_for(byte *state) {
  return wait_for(state, (unsigned long) wait);
  }
template<int *wait>
boolean wait_for(byte *state) {
  return wait_for(state, (unsigned long) *wait);
  }
  
// Put a random number in the location  
template<long *current_r, int from, int to>
boolean set_rand(byte *state) {
  *current_r = random(from,to);
  return true;
  }

template<int *current_r, int from, int to>
boolean set_rand(byte *state) {
  *current_r = (int) random(from,to);
  return true;
  }


// rand_with<wait_for, 100,1000>
//  or any other function that takes (byte*,unsigned long)
// template<boolean (*int_fn)(long unsigned int *,long unsigned int), int to, int from> // from..to just like random
template<
  boolean (*ul_fn)(byte *, long unsigned), 
  int from, int to // from..to just like random
  >
boolean with_rand(byte *state) {
  // we use state[4...]
  unsigned long *count = (unsigned long*)state;
  unsigned long *current_r = (unsigned long*)(&state[4]);

  // Beginning of step, "state" will be all zeros
  if (*count == 0) {
    *current_r = random(from,to);
    }
  return ul_fn(state, *current_r);
  }

template <int pin,  uint8_t hilo> boolean digitalWrite(byte state[]) { digitalWrite(pin, hilo); return true; }
template <int pin,  uint8_t pwm> boolean analogWrite(byte state[]) { analogWrite(pin, pwm); return true; }

// No, you can't pass floats as template arguments. Because this is the only tiime c++ doesn't let you shoot yourself in the foot.
template <int *value, int multiplicand>
boolean mult(byte *s) {
  *value = *value * multiplicand;
  return true;
  }

// This is multiply by ratio
template <int *value, int num, int den>
boolean mult(byte *s) {
  *value = (*value * num) / den;
  return true;
  }

template <int *value, int divisor>
boolean div(byte *s) {
  *value = *value / divisor;
  return true;
  }

typedef void (*TriangleFunction)(int value, byte *userdata, byte bytes[]);

void triangle_analog_write(int value, byte* userdata, byte bytes[]) {
  int *pin = (int*) userdata;
  analogWrite(*pin, value);
  }

void triangle(byte bytes[], TriangleFunction fn, int min, int max, int up_inc, int down_inc, int step_delay, boolean once, byte *userdata=NULL) {
  // Set state to 1 to start running. We will set state to 0 when we want to stop!
  // we use 8 bytes of state.
  long *state = (long*) &(bytes[0]);
  byte *waiter =  &(bytes[4]);

  if (step_delay && ! wait_for(waiter, step_delay) ) { 
    if (*state == 0) { *state = 1; }
    return; 
    }

  debugm(min);debugm('<');debugm(*state);debugm('<');debugm(max);debugm(' ');
  if (*state < 0) {
    debugm("N<");debugm(min);debugm(' ');
    *state += down_inc;
    if (*state >= -min) {
      if (once) {
        debugm("once'd\n");
        fn(0, bytes, userdata); // was analogWrite(pin, 0);
        *state = 0; // mark not running
        return;
        // return true; // done
        }
      else {
        *state = min;
        }
      }
    }
  else {
    debugm("N>");debugm(min);debugm(' ');
    if (*state == max) {
      debugm("peak'd ");
      *state = -max; // was max last time, so wrap
      }
    else {
      *state += up_inc;
      if (*state > max) {
        *state = max; // clip to max, and allow it for next time
        }
      }
    }
  if (*state == 0) *state = 1; // don't allow 0
  fn(abs(*state), userdata, bytes); // was analogWrite(pin, abs(*state));    
  debugm("=");debugm(*state);debugm("\n");
  // return false;
  }

void triangle(byte bytes[], int pin, int min, int max, int up_inc, int down_inc, int step_delay, boolean once) {
  // stash the pin after the trianglestate
  triangle(bytes, &triangle_analog_write, min, max, up_inc, down_inc, step_delay, once, (byte*) &pin);
  }

// This one is constants for constant values
template <int pin, int min, int max, int up_inc, int down_inc, int step_delay>
boolean triangle(byte *bytes) {
  long *state = (long*)bytes;
  triangle(bytes, pin, min, max, up_inc, down_inc, step_delay, true);
  return *state == 0; // 0 means done
  }

typedef struct { 
  int min; 
  int max;
  int up_inc;
  int down_inc;
  int step_delay;
  } TriangleSettings;

// This one has values in the TriangleSettings, so you can mess with them as it is running
template <TriangleFunction fn, TriangleSettings &ts>
boolean triangle(byte *bytes) {
  long *state = (long*)bytes;
  triangle(bytes, fn, ts.min, ts.max, ts.up_inc, ts.down_inc, ts.step_delay, true);
  return *state == 0; // 0 means done
  }

template <int pin, TriangleSettings &ts>
boolean triangle(byte *bytes) {
  long *state = (long*)bytes;
  triangle(bytes, pin, ts.min, ts.max, ts.up_inc, ts.down_inc, ts.step_delay, true);
  return *state == 0; // 0 means done
  }

/* ****************************
// This function makes the sequence wait till the button goes HIGH, or 5 seconds, whichever is first
boolean wait_for_button() {
  static unsigned long wait;
  // maybe this should be LOW:
  if (digitalRead(button_pin) == HIGH || wait_for(&wait, 5000)) {
     wait = 0; // reset our timer: maybe the button happened!
     return true;
  } 
  return false;
}


*/
