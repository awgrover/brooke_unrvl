/*
    Do you want a don't ring switch? It still talks on pickup, but won't ring?
    If you leave the arduino running for about 3 consecutive days, this code will get confused.
    Shouldn't we play a different message if the recording has "filled up"?
    "Tune" the area the motion-detect says in relation to the phone/room.

    Should it make noise if left off hook too long? Of if nothing moves for too long?
    
*/

/* Arduino "pro mini 328" https://www.adafruit.com/product/2378
    5v/328
    requires FTDI

*/
/* PIR https://www.adafruit.com/product/189 ?
    No library needed.
    Assuming HIGH on signal. may need 10K pullup on pir signal. retriggering off 
    Assuming stabilize times. Assuming analog: needs debounce. fixme
    Could change to edge-triggered-to-high interrupt driven
*/
/* MP3 play/record https://www.adafruit.com/products/1381
    https://github.com/adafruit/Adafruit_VS1053_Library/archive/master.zip
    SPI, so, Pins:
        CLK -> 13, MISO -> 12, MOSI -> 11, CS -> 10, RST -> 9, XDCS(DCS) -> 8, SDCS(CARDCS) -> 4, DREQ -> 3
    Need:
        hello.mp3
        v44k1q05.img
    Handset MIC to MIC+
    Handset SPKR to LOUT

    oggvorbis doc: https://cdn-shop.adafruit.com/datasheets/VorbisEncoder170c.pdf
    The SD card is a separate SPI object!, e.g. SD.begin(cardseelectpin)
*/

// On for serial/development, 0 for no serial (so you can avoid serial)
#define DEV 1

#define OnHookPin 5 // other side is +5, so onhook is LOW
#define RingerPin 6 
#define PIRPin 7
// MP3
// SPIpins fixed: 11,12,13 
#define BREAKOUT_RESET  9      // VS1053 reset pin (output)
#define BREAKOUT_CS     10     // VS1053 chip select pin (output)
#define BREAKOUT_DCS    8      // VS1053 Data/command select pin (output)
#define CARDCS 4     // Card chip select pin
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin. 2 or 3 on UNO

#define RingerFrequency 38 // hertz
#define RingingDelay  (1000 / 38) // because of rounding we'll be off a bit.

#define RingingDuration 1000   // millis
#define BetweenRings  1500     // millis
#define StopRingingAfter (30 * 1000) // millis
#define RecordingLength (10*1000) // millis
#define BetweenCalls (30 * 1000) // millis
#define HookDebounce 10 // millis to wait for a steady value
#define PIRDebounce 20 // PIR also needs debounce (for "on")
#define PIRStabilize (30*1000) // PIR takes some initial time to stabilize on power up

#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
Adafruit_VS1053_FilePlayer musicPlayer(BREAKOUT_RESET, BREAKOUT_CS, BREAKOUT_DCS, DREQ, CARDCS);

// I'm using a non-blocking sequence tool, so we can respond to pickup/hangup, etc.
#include "state_machine.h"

// ringer sound is about 30hz
extern const char ring_msg[];
const char ring_msg[] = "BOB";
SIMPLESTATEAS(ring1_deb, sm_msg<ring_msg>, ring1)
// SIMPLESTATEAS(ring1_deb, (sm_digitalWrite<RingerPin, HIGH>), pause1)
SIMPLESTATEAS(ring1, (sm_digitalWrite<RingerPin, LOW>), pause1)
SIMPLESTATEAS(pause1, sm_delay<RingingDelay>, ring2)
SIMPLESTATEAS(ring2, (sm_digitalWrite<RingerPin, HIGH>), pause2)
SIMPLESTATEAS(pause2, sm_delay<RingingDelay>, ring1)

STATEMACHINE(ring_sound, ring1_deb)

// But actual ring pattern is ringing, pause, ringing...
SIMPLESTATE(ring_on_duration, ring_finish)
SIMPLESTATEAS(ring_finish, (sm_digitalWrite<RingerPin, LOW>), ring_pause) // nicety
SIMPLESTATEAS(ring_pause, sm_delay<RingingDuration>, ring_on_duration)

STATEMACHINE(ringing_pattern, ring_on_duration);

//
// Overall state machine
//
    // Startup: various things to wait for
    SIMPLESTATE(wait_for_onhook, pir_stabilize) // don't start till on hook
    SIMPLESTATEAS(pir_stabilize, startup_delay<PIRStabilize>, wait_for_victim) // let pir stabilize

    // ready, wait for motion (and still onhook)
    SIMPLESTATE(wait_for_victim, ring_the_phone)
    // ring the phone for a while, then give up.
    STATE(ring_the_phone, between_calls)
        GOTOWHEN(SM_not< onhook >, pause_before_message) // somebody answered!
    END_STATE
    // Hello
    SIMPLESTATEAS(pause_before_message, sm_delay<600>, saying_hello) // give time to get to ear
    STATE(saying_hello, record_response) // wait for "hello" to finish
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

// for SD filenames
struct {
    char name[8] = "000.ogg";
    int count = 0;

    void next_available() {
        // will get stuck at 999
        while (SD.exists(name) && count < 999) {
            count++;
            name[0] = '0' + (count / 100);
            name[1] = '0' + ((count % 10) / 100);
            name[2] = '0' + (count % 100);
            }
        return;
        }
        
    } record_to;

char oggimg[] = "v44k1q05.img"; // should be const, too bad

void setup() {
    if (DEV) { while(!Serial) {}; Serial.begin(19200); Serial.println("Start"); }
    pinMode(RingerPin, OUTPUT);
    pinMode(PIRPin, INPUT);
    pinMode(OnHookPin, INPUT);

    // Tinkle
    digitalWrite(RingerPin, HIGH);
    digitalWrite(RingerPin, LOW);

    // try to get by setup even if stuff is missing

    if (! musicPlayer.begin()) { Serial.println("VS1053 not found. pins?"); }
    else {
        musicPlayer.setVolume(20,20); // lower is louder!
        // If DREQ is on an interrupt pin (on uno, #2 or #3) we can do background audio playing
        if (!musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)) { Serial.println(F("DREQ pin is not an interrupt pin")); }
        }

    if (!SD.begin(CARDCS)) { Serial.println("SD card not found. pins? card?"); }
    else {
        // fixme: have to do this each time ready to record?
        if (! musicPlayer.prepareRecordOgg(oggimg)) { Serial.println("Couldn't load plugin!"); } // that's HIFI
        else {
            record_to.next_available();
            if (DEV) { Serial.print("Next recording ");Serial.println(record_to.name); }
            }
        }

    if (DEV) Serial.println("End of setup");

    // tinkle
    digitalWrite(RingerPin, HIGH);
    digitalWrite(RingerPin, LOW);

    }

void loop() { // tests
    // ring_sound.run();
    // ringing_pattern.run();
    while(true) {
        if (onhook()) ringing_pattern.run();
        }
        
    if (false) { // ring_the_phone
        Serial.print("start ");  Serial.println(millis());
        if (!ring_the_phone(SM_Start)) { Serial.println("failed on sm_start"); }
        while(ring_the_phone(SM_Running)) {}; 
        Serial.print("Stop "); Serial.println(millis()); 
        delay(2000);
        }
    // musicPlayer.sineTest(0x44, 500);
    }

void xxxloop() {
    digitalWrite(RingerPin, LOW);
    delay(300);
    digitalWrite(RingerPin, HIGH);
    delay(300);
    Serial.println("Ring");
    }

void xxloop() {
    the_system.run();
    // these debounce, so let them poll
    onhook();
    motion();
    }

boolean wait_for_onhook() { 
    return ! onhook(); // we are waiting for it
    }

boolean onhook() {
    static boolean is_onhook = true; // got to start somewhere
    static int last_sample = LOW;
    static unsigned long debounce_timer;

    int now_sample = digitalRead(OnHookPin); // onhook = LOW

    if (last_sample != now_sample) {
        // it changed, start/keep debouncing
        debounce_timer = 0ul;
        wait_for(debounce_timer,  HookDebounce); // starts it (sets debounce_timer) 
        last_sample = now_sample;
        }

    if ( debounce_timer > 0 && wait_for(debounce_timer, 0) ) { // ignores the 2nd arg
        // timer was started is done
        Serial.println(now_sample==LOW ? "onhook" : "offhook");
        is_onhook = now_sample == LOW;
        }

    Serial.print(now_sample);Serial.print(" ");Serial.print(is_onhook);Serial.print(" ");Serial.println();
    return is_onhook;
    }

boolean motion() {
    // the PIR only has "bounce" at turn on...
    static int has_motion = false;
    static unsigned long ignore_changes;

    if (has_motion && wait_for(ignore_changes, PIRDebounce)) {
        // check for change to no-motion if has-motion has stabilised
        // i.e. wait after "motion" before we check again
        has_motion = digitalRead(PIRPin) == HIGH; // LOW is no motion
        }
    else {
        // if no-motion, immediately react to 
        has_motion = digitalRead(PIRPin) == HIGH;
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
    static unsigned long timer = 0;
    static File recording;

    const char *filename = "bob"; // next file name

    if (phase == SM_Start) {
        if (DEV) {Serial.print("Recording to "); Serial.println(record_to.name);}
        recording = SD.open(filename, FILE_WRITE);
        if (! recording) { Serial.println("Couldn't open file to record!"); return false; }
        musicPlayer.startRecordOgg(true); // use microphone (for linein, pass in 'false')
        timer = 0;
        return true; // and continue
        }
    else if (phase == SM_Running) {
        // record for a while
        // see the record_ogg::saveRecordedData() example
        // round to 512 blocks, write using flushoneveryNblocks(thatmany,4)
        return ! wait_for(timer, RecordingLength);
        }
    else if (phase == SM_Finish) {
        // cleanup fixme
        musicPlayer.stopRecordOgg();
        // save data fixme
        // do 512 blocks, then flushoneveryNblocks(whateverisleft, left/x)
        record_to.next_available(); // we preincrement
        return false;
        }
    }

boolean saying_hello(StateMachinePhase phase) {
    if (phase == SM_Start) {
        musicPlayer.startPlayingFile("hello.mp3"); // background
        return true; // and continue
        }
    else if (phase == SM_Running) {
        return ! musicPlayer.stopped(); // while playing
        }
    else if (phase == SM_Finish) {
        // cleanup 
        musicPlayer.stopPlaying();
        return false;
        }
    }

boolean ring_on_duration() {
    // run ring_sound for n millis
    static unsigned long timer = 0; 
    ring_sound.run();
    return ! wait_for(timer, 1000);
    }

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

