/*
  Code to run a foxoring transmitter, or alternatively to interactively test various 
  ways of generating RF signals in the 80m band (primarily 3.5-3.6 MHz) using 
  a Raspberry Pi Pico 2 (or Pi Pico).
  
  Optional configuration switches and a 2x8 LCD can be attached for field configuration
  and status monitoring but the configuration (frequency, fox number, morse rate, callsign)
  can also be configured via a terminal and stored in non-voltile memory.

  Type "?"" or "help" to get information about what commands are available. The RF is 
  generated by using a DMA and a PIO to transmit a pre-calculated, differential, tri-level,
  sigma-delta modulated waveform to two pins. The signal has to be converted to 
  single-ended format, low-pass filtered and matched to a short antenna. A HAM license 
  is probably needed to be allowed to use this as a transmitter.
  
  The code works on both Pi Pico and Pi Pico 2, but the Pico 2 is preferred as it has lower
  power consumtion, more memory (allows longer buffers) and a floating point unit (greatly 
  speeds up buffer re-calculation).

  The processor clock is expected to be 200 MHz, but other frequencies are supported by 
  changing the constant at the top of synth.cpp.
  
  The code can be compiled in the Arduino environment using Earle F. Philhower's 
  Raspberry Pi Pico board package.

  For experimentation with using a PIO to generate HF signals, a number of other modes
  are also supported via additional commands (type help2 for a description):

  0. A PIO CLKDIV is used to set the frequency. The output is a jittery square wave, so
     lots of HD3 and spurious overtones. Single ended output. Poor frequency resolution.
     But simple.
  1. A (jittery) square wave is streamed via DMA to the PIO. Dithering can be applied to 
     reduce distortion and spurious tones. The frequency resolution is significantly improved
     compared to mode 0. The output is differential.
  2. A sigma-delta modulated waveform is sent via DMA to the PIO. HD3 is highly attenuated.
     Dithering can be applied to reduce spurious tones. The output is differential.
  3. Trinary quantization is used, otherwise the same as mode 2.
  4. Same as mode 2, except that key clicks are reduced by smooth transitions between 
     transmission and silence.
  5. Same as mode 3, except that key clicks are reduced by smooth transitions between 
     transmission and silence.

  Mode 5 is the default.

  Parameters that can be changed include:
  - Frequency
  - Encoding mode (see above)
  - Send morse or continuously
  - Morse rate
  - Morse string to be repeated
  - Call sign
  - Amount of dithering
  - Amplitude of the sinewave
  - Amplitude of HD3 compensation
  - Phase of HD3 compensation
  - Buffer size
  - Silent output (useful e.g. for output impedance measurement)

  A potentially interesting piece of code is that for approximating doubles with rational numbers
  in farey.cpp and farey.h. See:
  https://axotron.se/blog/fast-algorithm-for-rational-approximation-of-floating-point-numbers/
  for more information.

  The code is released under the MIT license, except the cmdArduino code which has its own license.

  Per Magnusson, SA5BYZ, 2024

*/

#include <LiquidCrystal.h>
#include <Bounce2.h>
#include <elapsedMillis.h>
#include "Wire.h"
#include "cmdArduino.h"
#include "commands.h"
#include "transmitter_PiPico.h"
#include "config.h"
#include <EEPROM.h>

static const uint32_t POWER_BANK_PULSE_MS = 100;          // Length of power bank keep-alive pulse
static const uint32_t POWER_BANK_PULSE_PERIOD_MS = 25000; // Time between power bank keep-alive pulses
static const uint32_t LCD_STATIC_TIME = 3000;             // Time between each LCD change

bool key_down = false; // Whether to transmit continuously

static const int LCD_RS_Pin = 10;
static const int LCD_EN_Pin = 11;
static const int LCD_D4_Pin = 12;
static const int LCD_D5_Pin = 13;
static const int LCD_D6_Pin = 14;
static const int LCD_D7_Pin = 15;
const int Button1_Pin = 16;
const int SW7_Pin = 17;
const int SW6_Pin = 18;
const int SW5_Pin = 19;
const int SW4_Pin = 20;
const int SW3_Pin = 21;
const int SW2_Pin = 22;
const int SW1_Pin = 26;
const int SW0_Pin = 27;
const int Batt_Pin = 28;

static const int LED_Pin = 25;
static const int Resistor_Pin = 3; // To periodically pull power from the power bank so that it does not power off
static const int Morse_Debug_Pin = 0;
const int First_RF_Pin = 5;
const int Second_RF_Pin = First_RF_Pin+1;

synth *rf_synth = NULL;

LiquidCrystal lcd(LCD_RS_Pin, LCD_EN_Pin, LCD_D4_Pin, LCD_D5_Pin, LCD_D6_Pin, LCD_D7_Pin);
Bounce btn1 = Bounce();

elapsedMillis global_time;

uint32_t resistor_time;


// Morse code constants
int32_t msPerUnit = 120;           // Morse rate, 120 ms per unit = 10 WPM
int32_t msPerDot = msPerUnit;      // Duration of a dot
int32_t msPerDash = msPerUnit * 3; // Duration of a dash
int32_t msPiecePause = msPerUnit;  // Pause between pieces of a character
int32_t msCharPause = msPerUnit * 3; // Pause between characters
int32_t msWordPause = msPerUnit * 7; // Pause between words

// Conversion table from ASCII to morse code.
// Dashes are encoded as ones, and dots as zeros in the LSBs.
// The MorseLengths array tells how many pieces each character has.
static const uint8_t MorseCodes[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x06, 0x12, 0x09, 0x09, 0x0C, 0x00, 0x1E,
  0x16, 0x2D, 0x00, 0x0A, 0x33, 0x21, 0x15, 0x12, 0x1F, 0x0F,
  0x07, 0x03, 0x01, 0x00, 0x10, 0x18, 0x1C, 0x1E, 0x38, 0x2A,
  0x00, 0x00, 0x00, 0x0C, 0x1A, 0x01, 0x08, 0x0A, 0x04, 0x00,
  0x02, 0x06, 0x00, 0x00, 0x07, 0x05, 0x04, 0x03, 0x02, 0x07,
  0x06, 0x0D, 0x02, 0x00, 0x01, 0x01, 0x01, 0x03, 0x09, 0x0B,
  0x0C, 0x00, 0x00, 0x00, 0x00, 0x0D, 0x1E, 0x01, 0x08, 0x0A,
  0x04, 0x00, 0x02, 0x06, 0x00, 0x00, 0x07, 0x05, 0x04, 0x03,
  0x02, 0x07, 0x06, 0x0D, 0x02, 0x00, 0x01, 0x01, 0x01, 0x03,
  0x09, 0x0B, 0x0C, 0x00, 0x00, 0x00, 0x15, 0x00
};

static const uint8_t MorseLengths[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x05, 0x06, 0x05, 0x07, 0x05, 0x00, 0x06,
  0x05, 0x06, 0x00, 0x05, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05,
  0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x06, 0x06,
  0x00, 0x00, 0x00, 0x06, 0x06, 0x02, 0x04, 0x04, 0x03, 0x01,
  0x04, 0x03, 0x04, 0x02, 0x04, 0x03, 0x04, 0x02, 0x02, 0x03,
  0x04, 0x04, 0x03, 0x03, 0x01, 0x03, 0x04, 0x03, 0x04, 0x04,
  0x04, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06, 0x02, 0x04, 0x04,
  0x03, 0x01, 0x04, 0x03, 0x04, 0x02, 0x04, 0x03, 0x04, 0x02,
  0x02, 0x03, 0x04, 0x04, 0x03, 0x03, 0x01, 0x03, 0x04, 0x03,
  0x04, 0x04, 0x04, 0x00, 0x00, 0x00, 0x05, 0x00
};


void lcd_show_status();
void lcd_show_splash();


void start_transmitting()
{
  if(!rf_synth) {
    // Initialize synth object, should not be necessary here
    rf_synth = new synth(First_RF_Pin, current_config.frequency);
  }
  rf_synth->enable_output();
}


void stop_transmitting()
{
  rf_synth->disable_output();
}


void setup()
{
  // Wait for the serial port
  Serial.begin(115200);
  cmd.begin(115200, &Serial);
  pinMode(LED_Pin, OUTPUT);

  setup_switch_pins_power_save();
  EEPROM.begin(256);
  load_EEPROM_config();
  read_switches();
  initMorseRate(current_config.wpm);
  analogReadResolution(12);

  lcd.begin(8, 2);
  lcd_show_splash();

  // Reboot life sign
  int ii = 0;
  while (ii++<20) {
    digitalWrite(LED_Pin, HIGH);
    sleep_ms(50);
    digitalWrite(LED_Pin, LOW);
    sleep_ms(50);
  }
  //delay(3000);
  digitalWrite(LED_Pin, LOW);

  RegisterCommands();

  // This will probably not show up unless there are a few seconds of delay before this.
  Serial.println("Initializing...");
  Serial.flush();
  
  pinMode(Morse_Debug_Pin, OUTPUT);
  digitalWrite(Morse_Debug_Pin, HIGH);
  
  // Initialize the power bank keepalive circuit
  digitalWrite(Resistor_Pin, LOW);
  pinMode(Resistor_Pin, OUTPUT);
  resistor_time = global_time;

  btn1.attach(Button1_Pin, INPUT);
  btn1.interval(10);

  digitalWrite(Morse_Debug_Pin, LOW);
  
  start_transmitting(); // The rf_synth object is allocated here

  print_config();

  Serial.println("End of setup");
  Serial.flush();
}


// Change the rate of the morse code in words per minute.
void initMorseRate(uint32_t WPM)
{
  if(WPM < 5) {
    WPM = 5;
  }
  if(WPM > 100) {
    WPM = 100;
  }
  msPerUnit = 1000 * 60 / (WPM * 50); // ms per minute / WPM / units per word
  msPerDot = msPerUnit;      // Duration of a dot
  msPerDash = msPerUnit * 3; // Duration of a dash
  msPiecePause = msPerUnit;  // Pause between pieces of a character
  msCharPause = msPerUnit * 3; // Pause between characters
  msWordPause = msPerUnit * 7; // Pause between words
}


double read_batt()
{
  // Read the battery voltage
  int acc = 0;
  const int niter = 10;
  double volt;

  for(int ii = 0; ii < niter; ii++) {
    // Average a few times
    acc += analogRead(Batt_Pin);
  }
  // ADC max = 4095 at 3.3 V
  // 1:2 voltage divider
  volt = 2* 3.3 * acc / (double) niter / 4095.0;
  return volt;
}


// Show some information on the LCD as soon as possible after power up.
void lcd_show_splash()
{
  char str[16];
  int foxnum;

  lcd.clear();
  // Show the battery voltage
  sprintf(str, "%.2fV ", read_batt());
  lcd.print(str);
  // Show the fox numbers
  foxnum = fox_string_to_num(current_config.fox_string);
  if(foxnum >= 0) {
    // Normal fox string
    sprintf(str, "%d", foxnum);
    lcd.print(str);
  } else {
    // Custom fox string
    lcd.print(current_config.fox_string);
  }
  if(current_config.wpm >= MIN_FAST_WPM) {
    lcd.print("F");
  }

  // Show the frequency with a dot after million and a space before hundreds, like "3.579 54" (skipping single Hz)
  lcd.setCursor(0, 1); // bottom left
  sprintf(str, "%.0f", current_config.frequency);
  str[8] = '\0';
  str[7] = str[5];
  str[6] = str[4];
  str[5] = ' ';
  str[4] = str[3];
  str[3] = str[2];
  str[2] = str[1];
  str[1] = '.';
  lcd.print(str);
}


// Call this function repeatedly to update the LCD.
// It toggles between two screens every two seconds.
// Returns quickly when no LCD update is required.
void lcd_show_status()
{
  static uint32_t lcd_time = global_time;
  static uint32_t state = 999; // Special state to initialize the LCD on first call
  char str[16];
  int foxnum;

  if(state == 0 || state > 10) {
    // Wait to show screen 1
    if(global_time > lcd_time + LCD_STATIC_TIME || state > 10) {
      // Show screen 1
      lcd.clear();
      // Show the frequency with a dot after million and a space before hundreds, like "3.579 54" (skipping single Hz)
      sprintf(str, "%.0f", current_config.frequency);
      str[8] = '\0';
      str[7] = str[5];
      str[6] = str[4];
      str[5] = ' ';
      str[4] = str[3];
      str[3] = str[2];
      str[2] = str[1];
      str[1] = '.';
      lcd.print(str);
      lcd.setCursor(0, 1); // bottom left
      foxnum = fox_string_to_num(current_config.fox_string);
      if(foxnum >= 0) {
        // Normal fox string
        sprintf(str, "%d", foxnum);
        lcd.print(str);
      } else {
        // Custom fox string
        lcd.print(current_config.fox_string);
      }
      if(current_config.wpm >= MIN_FAST_WPM) {
        lcd.print("F");
      }
      lcd.print(" ");
      lcd.print(current_config.wpm);
      lcd.print("WPM");
      lcd_time = global_time;
      state = 1;
    }
  } else if(state == 1) {
    // Wait to show screen 2
    if(global_time > lcd_time + LCD_STATIC_TIME) {
      // Show screen 2
      lcd.clear();
      if(strlen(current_config.call) > 0) {
        lcd.print(current_config.call);
      } else {
        lcd.print("No call!");
      }
      lcd.setCursor(0, 1); // bottom left
      sprintf(str, "%.3fV", read_batt());
      lcd.print(str);
      lcd_time = global_time;
      state = 0;
    }
  } else {
    lcd_time = global_time;
    state = 0;
  }
}


void loop()
{
  static uint32_t state1 = 0;
  static uint32_t state2 = 0;

  cmd.poll();
  lcd_show_status();

  if (digitalRead(Resistor_Pin) == HIGH) {
    if (global_time > resistor_time + POWER_BANK_PULSE_MS) {
      digitalWrite(Resistor_Pin, LOW);
    }
  } else {
    if (global_time > resistor_time + POWER_BANK_PULSE_PERIOD_MS) {
      digitalWrite(Resistor_Pin, HIGH);
      resistor_time = global_time;
    }
  }

  btn1.update();
  if(btn1.fell()) {
    read_switches();
    lcd.clear();
    lcd.print("Updating");
    lcd.setCursor(0, 1); // bottom left
    lcd.print("...");
    rf_synth->set_frequency(current_config.frequency);
    rf_synth->apply_settings();    
  }

  if(key_down) {
    start_transmitting();
    return;
  }

  if (state1 < 10) {
    // Send the fox string ten times, then the call sign
    initMorseRate(current_config.wpm); // Normal speed
    switch (state2) {
      case 0:
        // Transmit the fox string
        if (sendMorseString(current_config.fox_string)) {
          // String has been fully transmitted
          state2++;
        }
        break;
      case 1:
        // Pause
        if (sendWordPause()) {
          state2 = 0;
          state1++;
        }
        break;
      default:
        state2 = 0;
    }
  } else {
    if(strlen(current_config.call) == 0) {
      // No callsign to transmit
      state2 = 0;
      state1 = 0;      
    } else {
      initMorseRate(2*current_config.wpm); // Fast
      switch (state2) {
        case 0:
          // Send  a string
          if (sendMorseString(current_config.call)) {
            state2++;
          }
          break;
        case 1:
          // Pause, pulse the LED and the power bank load
          digitalWrite(LED_Pin, HIGH);
          digitalWrite(Resistor_Pin, HIGH);
          if (sendWordPause()) {
            // Done with the pause
            digitalWrite(LED_Pin, LOW);
            digitalWrite(Resistor_Pin, LOW);
            state2 = 0;
            state1 = 0;
          }
          break;
        default:
          state2 = 0;
      }
    }
  }
}


// Send a string of morse characters.
// Keep calling this function (with the same string) many times per unit 
// interval until it returns true to signal that the transmission is done.
int sendMorseString(const char *str)
{
  static int state = 0;
  static uint32_t charNo = 0;
  uint8_t currentChar;
  static uint8_t currentCode;
  static uint8_t currentLen;

  if (charNo >= strlen(str)) {
    // Done sending all characters, clean up and return 1
    charNo = 0;
    return 1;
  }

  if (state == 0) {
    // Start sending next character
    currentChar = str[charNo];
    if (currentChar == ' ') {
      // Space, send a pause
      state = 2;
      sendPause(msWordPause - msCharPause);
    } else {
      // Not a space
      currentCode = MorseCodes[currentChar];
      currentLen = MorseLengths[currentChar];
      if (currentLen > 0) {
        // Valid character
        state = 1;
        sendMorseLetter(currentCode, currentLen);
      } else {
        // Invalid character, just advance to the next one and let next call take care of it
        charNo++;
      }
    }

  } else if (state == 1) {
    // We are already sending a character, see if we are done with this one
    if (sendMorseLetter(currentCode, currentLen)) {
      // Yes, we are done with the character, advance to next one
      charNo++;
      state = 0;
    }

  } else if (state == 2) {
    // We are sending a pause
    if (sendPause(msWordPause - msCharPause)) {
      // Done with the pause
      charNo++;
      state = 0;
    }
  }
  return 0;
}


// Add the delay difference between a character delay and a word delay.
// Keep calling this function repeatedly until it returns true to signal that the delay is done.
int sendWordPause()
{
  return sendPause(msWordPause - msCharPause);
}


// Add a delay of 'pause' ms.
// Keep calling this function repeatedly until it returns true to signal that the delay is done.
int sendPause(uint32_t pause)
{
  static int state = 0;
  static uint32_t nextEventTime;

  if (state == 0) {
    nextEventTime = global_time + pause;
    state = 1;
  } else {
    if (global_time > nextEventTime) {
      // We are done
      state = 0;
      return 1;
    }
  }
  return 0;
}


// Send a morse letter encoded as the 'len' LSBs of 'code'. A dot is encoded as a 0 and a dash as a 1.
// The most significant of the bits is sent first, so P (.--.) would be specified as code = 0x06 and len = 4.
// Keep calling this function repeatedly until it returns true to signal that the transmission is done.
// Changing whether a signal is sent or not can only happen when the function is called. So it should be called
// at least five times more frequently than the 'msPerUnit' constant specifies.
int sendMorseLetter(uint32_t code, uint32_t len)
{
  static int state = 0;
  static int currentBit = 0;
  static uint32_t nextEventTime;
  static uint32_t stateBegin;

  if (state == 0) {
    /*
    Serial.print(F("Code: "));
    Serial.print(code, BIN);
    Serial.print(F(" Len: "));
    Serial.println(len, DEC);
    */
    // Start of transmission of this character, start transmitting
    stateBegin = global_time;
    start_transmitting();
    currentBit = len;
    state = 1;
    if (code & (1 << (currentBit - 1))) {
      // Transmitting a dash
      nextEventTime = stateBegin + msPerDash;
    } else {
      // Transmitting a dot
      nextEventTime = stateBegin + msPerDot;
    }

  } else {
    // Transmission has already started
    if (global_time < nextEventTime) {
      // It is not yet time to change anything, return immediately
      return 0;
    }
    // Time to do something
    switch (state) {

      case 1:
        // End transmission of this piece of a character
        stateBegin = nextEventTime;
        stop_transmitting();
        currentBit--;
        state = 2;
        nextEventTime = stateBegin + msPiecePause;
        break;

      case 2:
        // End of pause after a piece of a character
        stateBegin = nextEventTime;
        // Was it the last piece?
        if (currentBit == 0) {
          // Yes, need to wait some more for spacing between characters
          nextEventTime = stateBegin + msCharPause - msPiecePause;
          state = 3;
        } else {
          // No, start next piece of this character
          start_transmitting();
          state = 1;
          if (code & (1 << (currentBit - 1))) {
            // Transmitting a dash
            nextEventTime = stateBegin + msPerDash;
          } else {
            // Transmitting a dot
            nextEventTime = stateBegin + msPerDot;
          }
        }
        break;

      case 3:
        // We have reached the end of the character and the pause after it
        state = 0;
        return 1;
    }
  }
  return 0;
}