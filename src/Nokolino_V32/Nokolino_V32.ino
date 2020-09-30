/* Nokolino V3.2 30.09.2020 - Nikolai Radke
 *  
 *  Sketch for Mini-Noko-Monster with new DFPlayer module
 *  For ATtiny45/85 - set to 8 Mhz and remember to flash your bootloader first
 *  
 *  Flash-Usage: 3.952 (IDE 1.8.13 | AVR 1.8.3 | ATtiny 1.0.2 | Linux X86_64 | ATtiny85 )
 *  
 *  Circuit:
 *  1: RST | PB5  free
 *  2: A3  | PB3  Optional SFH300  
 *  3: A2  | PB4  Busy DFPlayer  - 16
 *  4: GND |      GND
 *  5: D0  | PB0  Button         - GND
 *  6: D1  | PB1  RX DFPLayer    - 3
 *  7: D2  | PB2  TX 1k DFPlayer - 2       
 *  8: VCC        VCC
 *  
 *  Sleepmodes:
 *  0=16ms, 1=32ms, 2=64ms, 3=128ms, 4=250ms, 5=500ms
 *  6=1sec, 7=2sec, 8=4sec, 9=8sec
 */

#include <avr/sleep.h>
#include <avr/interrupt.h>
#include <SoftwareSerial.h>
#include <EEPROM.h>

//--------------------------------------------------------------------------------
// Configuation
#define Time         10             // Say something every statistical 10 minutes
#define Volume       25             // Volume 0-30 - 25 is recommended 
#define Darkness     4              // Optional: The lower the darker the light must be

#define Button_event 40             // Last button event number (XX.mp3)
#define Time_event   163            // Last time event number -> Very last file is "beep"

//#define Breadboard                // Breadboard or PCB?
#ifdef Breadboard
  #define Offset     0.3            // Battery measuring error
  #define maxInput   500            // Max. value from busy line 
#else
  #define Offset     0.1
  #define maxInput   600
#endif

// Optional - comment out with // to disable o remove // to enable
#define StartupBeep                 // Will say "beep" when turned on
#define BatteryWarning              // Gives a warning when battery is low
//#define LightSensor               // Will be quite in the dark
//#define SleepComplain             // Will complain if button pressed while its dark

//---------------------------------------------------------------------------------

// Optional battery warning
#define minCurrent   3.40 +Offset   // Low power warning current + measuring error
#define battLow      3.30 +Offset   // Minimal voltage before DFPlayer fails

// Hardware pins
#define TX      2
#define RX      1
#define Busy    2

// DFPlayer commands
#define SetVolume   0x06 
#define Play        0x0F 
#define Count       0x4E

// ADC and BOD
#ifndef cbi
  #define cbi(sfr,bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#endif
#ifndef sbi
  #define sbi(sfr,bit) (_SFR_BYTE(sfr) |= _BV(bit))
#endif
#define BODS 7                         // BOD sleep bit in MCUCR
#define BODSE 2                        // BOD sleep enable bit in MCUCR

// Variables
uint16_t seed,files;
uint16_t address=1;
volatile boolean f_wdt = 1;            // Volatile -> it is an interrupt routine
boolean  low=false;
boolean  dark=false;
boolean  musicbox=false;
char     count_files;
uint8_t  files_byte[10];

SoftwareSerial mp3(TX,RX);            // TX to D2, RX to D1

int main(void) {

#ifdef BatteryWarning
  uint16_t current;
  double vref;
  uint16_t counter=10;                 // Check shortly after startup
#endif

init(); {
  // Power saving
  MCUCR |= _BV(BODS) | _BV(BODSE);     // Disable brown out detection - default?
  ACSR |= _BV(ACD);                    // Disable analog comparator - default?
  setup_watchdog(3);                   // Set sleep time to 128ms  

  // Port Setup
  DDRB &= ~(1<<PB0);                   // D0 INPUT
  PORTB |= (1<<PB0);                   // D0 HIGH 

  // Start DFPlayer
  newdelay(1000);
  mp3.begin(9600);                     // Init Player
  newdelay(500);
  command(SetVolume,0,Volume,245-Volume); // Send volume command

  // Count files in folder 02 for music box mode
  command(Count,0,2,171);
  for (seed=0;seed<10;seed++) {         // Read 10 HEX chars from module
    files_byte[seed]=(uint8_t) mp3.read();// and convert the chars into uint8_t
  }
  files=files_byte[6];   

  // Set play mode | Button pressed = music box mode
  if (!(PINB & (1<<PB0))) musicbox=true;  
    
  if (!musicbox) {
    // Randomize number generator
    address=eeprom_read_word(0);       // Read EEPROM address
    if ((address<2) || (address>(EEPROM.length()-3))) {           
    // Initialize EEPROM and size for first use or after end of cycle
      address=2;                       // Starting address
      eeprom_write_word(0,address);    // Write starting address
      eeprom_write_word(address,0);    // Write seed 0
    }
    seed=eeprom_read_word(address);    // Read seed
    if (seed>900) {                    // After 900 write-cyles move to another address
      seed=0;                          // to keep the EEPROM alive
      address+=2;
      eeprom_write_word(0,address);
    }
    randomSeed(seed);                  // Randomize
    seed++;                            // New seed
    eeprom_write_word(address,seed);   // Save new seed for next startup     
  }

  // Optional startup beep
  #ifdef StartupBeep    
    play(Time_event+1,1);              // Nokolino says "Beep"       
    newdelay(1000);  
  #endif 
}

// Main loop
while(1) {
  // Wait for button or time and go to sleep - ~8 times/second         
  if (!low) {                          // Quiet if battery too low
    if (!(PINB & (1<<PB0))) {          // If button is pressed then
      if (dark) {                      // if fototransistor is available
        #ifdef SleepComplain           // and complain feature enabled
          if (!musicbox) {             // and not in music box mode
            play(Time_event,1);        // complain when button pressed
          }
        #endif
      }
      else if (!musicbox) play(random(0,Button_event+1),1); // Button event
      else {
        play(address,2);                 // or single music box files 
        (address==files)?address=1:address++;
      }
    }
    else if ((!dark) && (!musicbox) && (random(0,Time*60*8)==1)) // Time event
      play(random(Button_event+1,Time_event+1),1); 
  }
  attiny_sleep();                      // Safe battery
  
  // Optional: Check current
  #ifdef BatteryWarning
    if (counter==0) {
     current=MeasureVCC();
     vref=1024*1.1f/(double)current;   // Calculate battery current
     if (vref<=minCurrent) {           // Current below minimum
       if (vref<=battLow) low=true;    // Power too low
       else play(Time_event+1,1);      // Nokolino says "Beep"
     }
     else low=false;
     counter=400;                      // Every minute, 400x128ms+some runtime ms for 60s
    }
    counter--;
  #endif

  // Optional: Check darkness
  #ifdef LightSensor
    if (analogRead(A3)<=Darkness) dark=true;
    else dark=false;
  #endif
}}

void play(uint8_t file, uint8_t folder) {
  command(Play,folder,file,236-file-folder);
  while (analogRead(A2)<maxInput) attiny_sleep(); // Check busy
  //command(0x0A,0,0,241);            // Send DFPlayer to sleep | Useless
}

void command(uint8_t CMD,uint8_t CMD2,uint8_t VAL,uint8_t CHK) {
  mp3.write("\x7E\xFF\x06");
  mp3.write(CMD);                     // Write command
  mp3.write((uint8_t) 0x00);
  mp3.write(CMD2);                    // Write second command
  mp3.write(VAL);                     // Write value
  mp3.write("\xFE");
  mp3.write(CHK);                     // Write checksum
  mp3.write("\xEF");
  newdelay(100);
}

void attiny_sleep() {                  // Sleep to save power  
  cbi(ADCSRA,ADEN);                    // Switch ADC off
  set_sleep_mode(SLEEP_MODE_PWR_DOWN); 
  sleep_mode();                                         
  sbi(ADCSRA,ADEN);                    // Switch ADC on
}

void newdelay(uint16_t z) {            // New delay function to save flash
  uint32_t zmillis=millis();
  while (millis()-zmillis<z);
}

void setup_watchdog(uint8_t mode) {    // Setup wake time
  uint8_t bb;
  bb=mode & 7;
  if (mode>7) bb|= (1<<5);
  bb|= (1<<WDCE);
  MCUSR &= ~(1<<WDRF);
  WDTCR |= (1<<WDCE) | (1<<WDE);
  WDTCR = bb;
  WDTCR |= _BV(WDIE);
}

uint16_t MeasureVCC(void) {           // Thank you, Tim!
  PRR     &=~_BV(PRADC); 
  ADCSRA  =_BV(ADEN)|_BV(ADPS2)|_BV(ADPS1)|_BV(ADPS0); 
  ADMUX   =_BV(REFS2) | 0x0c; 
  newdelay(1); 
  ADCSRA  |=_BV(ADSC);
  while (!(ADCSRA&_BV(ADIF))); 
  ADCSRA  |=_BV(ADIF);
  return ADC;
}

ISR(WDT_vect) {                       // Set global flag
  f_wdt=1; 
}