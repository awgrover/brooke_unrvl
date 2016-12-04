/*
    Do you want a don't ring switch? It still talks on pickup, but won't ring?
    If you leave the arduino running for about 3 consecutive days, this code will get confused.
    Shouldn't we play a different message if the recording has "filled up"?
    "Tune" the area the motion-detect says in relation to the phone/room.

    Should it make noise if left off hook too long? Of if nothing moves for too long?
    Should it say something when the recording is done?
    
    todo
    * "Attach at least one of the AGND pins to ground? " For line out the AGND is correct!"?
    * mp3
        # play sine wave
        # play sound file
            # short "hello"
        * record sound & play it back
    * setvolume doesn't seem to work. differential?
    * cleanup code
    * Help with output signal from mp3: pops/clicks: dc bias? agnd?
    * use the FTDI to program the damn trinket:blink first
        * test a Serial.print when NO serial.begin
        * test a serial.print w/serial.begin
    * The mp3 library has a serial.print in it. damn it.
*/

/* Arduino "pro mini 328" https://www.adafruit.com/product/2378
    5v/328
    requires FTDI

*/
/* PIR https://www.adafruit.com/product/189
    No library needed. "digital
    HIGH on signal/motion. may need 10K pullup on pir signal. Jumper to H (retrigger on)
    Assuming stabilize times. Assuming analog: needs debounce. fixme
    Could change to edge-triggered-to-high interrupt driven
    Docs say: "delay" is 2-4 or something. BUT we see 6 to 200!
    AND, once triggered, motion keeps it triggerd, and stil 6secs+ stays on
*/
/* Random ring interval
    Instead of using the PIR, set RandomTrigger to 1 below.
    It will randomly "trigger" every so often (see random_trigger_interval)
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

    Getting pop/clicks: either differential output (put a cap in somewhere) or AGND...
*/
/* Bell 
    is on RingerPin, which is the coil of a 5V/200mA relay: LOW closes the relay (relay 9--13) and "pings" the bell.
    9 is 48V, 13 is bell, otherside of bell is ground.
    Snubber diode across it.
*/
/* Ring Now button
    For demo, use a little momentary on RingNowButton. Should be a normal open. The other side to ground.
*/

// On for serial/development, 0 for no serial (so you can avoid serial)
#define DEV 1

//
// PINS
//
#define RingNowButton 2 // an external momentary to cause it to ring now for demo
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin. 2 or 3 on UNO
#define CARDCS 4     // VS1053-breakout SDCard select pin
#define OnHookPin 5 // other side is +5, so onhook is LOW, need pulldown
#define RingerPin 6 
#define PIRPin 7
#define BREAKOUT_DCS    8      // VS1053 Data/command select pin (output)
#define BREAKOUT_RESET  9      // VS1053 all reset pin (output)
#define BREAKOUT_CS     10     // VS1053 chip select pin (output)
// SPIpins fixed (hardware): 
// CLK -> 13
// MISO -> 12
// MOSI -> 11

// ringer sound is about 30hz
#define RingerFrequency 38 // hertz. debug output slows this considerably
#define RingingDelay  (1000 / RingerFrequency) // because of rounding we'll be off a bit.

// Ringing pattern
#define RingingDuration 1500   // millis
#define BetweenRings  1000     // millis

#define StopRingingAfter (30 * 1000) // millis
#define RecordingLength (10*1000) // millis
#define BetweenCalls (30 * 1000) // millis

#define HookDebounce 10 // millis to wait for a steady value
#define PIRDebounce 20 // PIR also needs debounce (for "on")
#define PIRStabilize (30*1000) // PIR takes some initial time to stabilize on power up

#define RandomTrigger 1 // 0 for PIR, 1 for random_trigger_interval
const int random_trigger_interval[] = { 1*60, 5*60 }; // 1 to 5 minutes (in seconds) fixme

#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
Adafruit_VS1053_FilePlayer musicPlayer(BREAKOUT_RESET, BREAKOUT_CS, BREAKOUT_DCS, DREQ, CARDCS);

const char hello_file_name[] = "hello.mp3";
// Profile, 44khz, "good" quality. see datasheet.
char oggimg[] = "v44k1q05.img"; // should be const, too bad, but can't
#define RECBUFFSIZE 128  // 64 or 128 bytes.
uint8_t recording_buffer[RECBUFFSIZE];
File recording;

// I'm using a non-blocking sequence tool, so we can respond to pickup/hangup, etc.
#define DEBUG 0
#include "state_machine.h"

extern const char ring_msg[];
SIMPLESTATEAS(ring1_deb, (sm_digitalWrite<RingerPin, HIGH>), pause1)
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

#if DEV==1
    #define print(msg) Serial.print(msg)
    #define println(msg) Serial.println(msg)
#else
    #define print(msg)
    #define println(msg)
#endif

void setup() {
    if (DEV) { while(!Serial) {}; Serial.begin(115200); println(F("Start")); } // will hang if DEV && no serial possible
    pinMode(RingerPin, OUTPUT);
    // setting output on these pins seems to break it
        // pinMode(BREAKOUT_CS, OUTPUT);
        // pinMode(CARDCS, OUTPUT);
    pinMode(PIRPin, INPUT);
    pinMode(OnHookPin, INPUT);
    pinMode(RingNowButton, INPUT_PULLUP);

    // Tinkle at startup
    digitalWrite(RingerPin, HIGH); // reset
    delay(20);
    tinkle();

    print(F("Using "));println(RandomTrigger==1 ? F("random trigger") : F("pir trigger"));

    // try to get by setup even if stuff is missing

    if (! musicPlayer.begin()) { println(F("VS1053 not found. pins?")); }
    else {
        musicPlayer.setVolume(20,20); // lower is louder!
        musicPlayer.sineTest(0x44, 150); // 250ms of 0x44 (68?). sync.  bit pattern I think...
        musicPlayer.sineTest(0x66, 150);
        // If DREQ is on an interrupt pin (on uno, #2 or #3) we can do background audio playing
        if (!musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT)) { println(F("DREQ pin is not an interrupt pin")); }
        }
  
    if (!SD.begin(CARDCS)) { println(F("SD card not found. pins? card?")); }
    else {
        // fixme: have to do this each time ready to record?

        if (!SD.exists(hello_file_name)) { print(F("Doesn't exist: ")); println(hello_file_name); }

        // FIXME: if you "prepare" it will disable playing
        /*
        print(F("loading "));print(oggimg);print(F(" "));println(millis());
        if (! musicPlayer.prepareRecordOgg(oggimg)) { println(F("Couldn't load plugin!")); } // that's HIFI
        else {
            print(F("loaded "));println(millis());
            record_to.next_available();
            print(F("Next recording "));println(record_to.name);
            }
        */
        }
  
    debug_hello(500); // say something

    // 2nd tinkle means "running"
    tinkle();
    println(F("End of setup"));
    }


void loop() { // tests
    onhook(); // need to constantly poll for debounce. works.

    // while( onhook() ) test_ringing_polarity();

    // pir_change(); // fixme: needs some rework: on latches for 6 seconds..., not debounce

    // while ( onhook() ) ring_sound.run(); // works

    // while( onhook() ) { ringing_pattern.run();  } digitalWrite(RingerPin, HIGH); // works

    // while( onhook() ) { static boolean x=false; if (x!=wait_for_victim()) { x=!x; print(F("Victim? ")); println(x); if(x) tinkle(); } } // works
        
    // while ( !onhook() ) { println(F("beeep")); musicPlayer.sineTest(0x44, 250); println(musicPlayer.playingMusic); delay(300); musicPlayer.sineTest(0x22, 250); delay(300); } // works w/pops/clicks

    if (!onhook()) { delay(300); debug_hello(3000); }
    }

void tinkle() {
    digitalWrite(RingerPin, LOW);
    delay(30);
    digitalWrite(RingerPin, HIGH);
    }

void test_ringing_polarity() {
    // to test the bell polarity
    // which way is "ping"?
    // HLH..L.. would be ping..ping for low
    // but pingping...pingping for high

    // LOW ought to be "close relay"
    int a=LOW, b=HIGH;
    digitalWrite(RingerPin, b);
    delay(100);
    digitalWrite(RingerPin, a);
    delay(100);
    digitalWrite(RingerPin, b);

    delay(400);
    digitalWrite(RingerPin, a);
    delay(400);
    }

void pir_change() {
    // fixme:this was debug...
    static int last = false;
    int now = digitalRead(PIRPin);
    if (now != last) { println(now); }
    last=now;
    }

boolean wait_for_onhook() { 
    return ! onhook(); // we are waiting for it
    }

boolean onhook() {
    static boolean is_onhook = true; // got to start somewhere, true and LOW
    static int last_sample = LOW;
    static unsigned long debounce_timer;

    int now_sample = digitalRead(OnHookPin); // onhook = LOW

    if (last_sample != now_sample) {
        // it changed, start/keep debouncing
        // print(now_sample);print(F(" "));print(is_onhook);print(F(" "));println();
        debounce_timer = 0ul;
        wait_for(debounce_timer,  HookDebounce); // starts it (sets debounce_timer) 
        last_sample = now_sample;
        }

    if ( debounce_timer > 0 && wait_for(debounce_timer, 0) ) { // ignores the 2nd arg
        // timer was started is done
        println(now_sample==LOW ? "onhook" : "offhook");
        is_onhook = now_sample == LOW;
        }

    return is_onhook;
    }

boolean use_random_trigger_interval() {
    // goes true at some random interval
    static unsigned long timer = 0;

    if (timer == 0) { // we expired in the past, and now someone wants to use us again
        int seconds = random(random_trigger_interval[0], random_trigger_interval[1]);
        print(F("ring in "));print(seconds);println();
        timer = seconds * 1000ul + millis();
        return false;
        }

    if (millis() > timer) {
        timer = 0; // for next time we are used
        return true;
        }

    return false;
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
    int ring_now = digitalRead(RingNowButton) == LOW;
    boolean the_trigger = // one of motion or random:
    #if RandomTrigger==0
        motion();
    #else
        use_random_trigger_interval();
    #endif
    return !( onhook() && ( ring_now || the_trigger ) ); // we are waiting for both
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

    const char *filename = "bob"; // next file name

    if (phase == SM_Start) {
        print(F("Recording to ")); println(record_to.name);
        recording = SD.open(filename, FILE_WRITE); // FIXME
        if (! recording) { println(F("Couldn't open file to record!")); return false; }
        musicPlayer.startRecordOgg(true); // use microphone (for linein, pass in 'false')
        timer = 0;
        return true; // and continue
        }
    else if (phase == SM_Running) {
        // record for a while
        uint16_t ct = saveRecordedData(true);
        print(F("sound "));println(ct);
        return ! wait_for(timer, RecordingLength);
        }
    else if (phase == SM_Finish) {
        // cleanup fixme
        musicPlayer.stopRecordOgg();
        uint16_t ct = saveRecordedData(false);
        print(F("sound final "));println(ct);
        recording.close();

        record_to.next_available(); // we preincrement
        return false;
        }
    }

boolean debug_hello(int duration) {
    unsigned long timeout = millis() + duration;

    saying_hello(SM_Start);
    while (saying_hello(SM_Running)) { if (millis() > timeout) {println("timeout"); break;}; onhook(); }
    saying_hello(SM_Finish);
    }

boolean saying_hello(StateMachinePhase phase) {
    if (phase == SM_Start) {
        print(F("start hello "));println(millis());
        if (!musicPlayer.startPlayingFile(hello_file_name)) { // background
            println(F("startPlayingFile failed"));
            return false;
            }
        return true; // and continue
        }
    else if (phase == SM_Running) {
        return ! musicPlayer.stopped(); // while playing
        }
    else if (phase == SM_Finish) {
        print(F("end hello "));println(millis());
        // cleanup 
        musicPlayer.stopPlaying();
        print(F("closed "));println(millis());
        return false;
        }
    }

boolean ring_on_duration() {
    // run ring_sound for n millis
    static unsigned long timer = 0; 
    ring_sound.run();
    return ! wait_for(timer, 1000);
    }

uint16_t saveRecordedData(boolean isrecord) {
    /*************************************************** 
      This is an example for the Adafruit VS1053 Codec Breakout

      Designed specifically to work with the Adafruit VS1053 Codec Breakout 
      ----> https://www.adafruit.com/products/1381

      Adafruit invests time and resources providing this open source code, 
      please support Adafruit and open-source hardware by purchasing 
      products from Adafruit!

      Written by Limor Fried/Ladyada for Adafruit Industries.  
      BSD license, all text above must be included in any redistribution
     ****************************************************/
  uint16_t written = 0;
  
    // read how many words are waiting for us
  uint16_t wordswaiting = musicPlayer.recordedWordsWaiting();
  
  // FIXME: could redo this to do 512byte blocks, period. and then cleanup

  // try to process 256 words (512 bytes) at a time, for best speed
  while (wordswaiting > 256) {
    //print(F("Waiting: ")); println(wordswaiting);
    // for example 128 bytes x 4 loops = 512 bytes
    for (int x=0; x < 512/RECBUFFSIZE; x++) {
      // fill the buffer!
      for (uint16_t addr=0; addr < RECBUFFSIZE; addr+=2) {
        uint16_t t = musicPlayer.recordedReadWord();
        //println(t, HEX);
        recording_buffer[addr] = t >> 8; 
        recording_buffer[addr+1] = t;
      }
      if (! recording.write(recording_buffer, RECBUFFSIZE)) {
            print(F("Couldn't write ")); println(RECBUFFSIZE); 
            while (1);
      }
    }
    // flush 512 bytes at a time
    recording.flush();
    written += 256;
    wordswaiting -= 256;
  }
  
  wordswaiting = musicPlayer.recordedWordsWaiting();
  if (!isrecord) {
    print(wordswaiting); println(F(" remaining"));
    // wrapping up the recording!
    uint16_t addr = 0;
    for (int x=0; x < wordswaiting-1; x++) {
      // fill the buffer!
      uint16_t t = musicPlayer.recordedReadWord();
      recording_buffer[addr] = t >> 8; 
      recording_buffer[addr+1] = t;
      if (addr > RECBUFFSIZE) {
          if (! recording.write(recording_buffer, RECBUFFSIZE)) {
                println(F("Couldn't write!"));
                while (1);
          }
          recording.flush();
          addr = 0;
      }
    }
    if (addr != 0) {
      if (!recording.write(recording_buffer, addr)) {
        println(F("Couldn't write!")); while (1);
      }
      written += addr;
    }
    musicPlayer.sciRead(VS1053_SCI_AICTRL3);
    if (! (musicPlayer.sciRead(VS1053_SCI_AICTRL3) & _BV(2))) {
       recording.write(musicPlayer.recordedReadWord() & 0xFF);
       written++;
    }
    recording.flush();
  }

  return written;
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
      // print(millis()); print(F(": ")); print(F("Start delay ")); println(wait); 
      *timer = wait + millis();
      return false;
  }
  else if (*timer <= millis()) {
    // print(millis()); print(F(": ")); print(F("Finish delay ")); println(wait);
    *timer = 0;
    return true;
  }
  else {
    return false;
  } 
}

