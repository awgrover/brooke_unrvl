const int ringer_pin = 3;
const int ringer_frequency = 38; // hertz
const int ringer_delay = 1000 / 38; // because of rounding we'll be off a bit. 

// timings
const int strike_hold_delay = 10; // how long to turn on the relay to get a strike, but not hold too long
const int between_rings = 1000;

// On for serial/development, 0 for no serial (so you can avoid serial)
#define DEV 1

void setup() {
    if (DEV) Serial.begin(19200);
    pinMode(ringer_pin, OUTPUT);
    }

void loop() {
    while (ring_till_pickup()) {};

    }

boolean ring_till_pickup() {
    enum bell_state { strike, strike_hold, retreat, retreat_hold, rest };
    static int state = strike;
    static unsigned int till = 0; // init value isnt' used
    static int was = -1; // just for debugging really

    // say when the state changes
    if (DEV && was != state ) { Serial.print(state); Serial.print(" "); Serial.println(millis()); was = state; }

    switch (state) {
        case strike:
            digitalWrite(ringer_pin, HIGH); // bang
            state = strike_hold;
            till = millis() + ringer_delay;
            break;

        case strike_hold:
            if (millis() <= till) {
                break;
                }
            state = retreat;
            break;

        case retreat:
            digitalWrite(ringer_pin, LOW); // relax
            state = retreat_hold;
            till = millis() + ringer_delay;
            break;

        case retreat_hold:
            if (millis() <= till) {
                break;
                }
            state = strike;
            till = millis() + between_rings;
            break;

        case rest:
            if (millis() <= till) {
                break;
                }
            state = rest;
            till = millis() + between_rings;
            break;
        }

    // on pickup, change to false
    return true;
    }

//bool off_hook() {
    
