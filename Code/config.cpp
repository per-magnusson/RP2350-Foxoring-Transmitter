// Routines to store and retrieve the configuration from non-volatile storage
// and to interpret switch settings.

#include <EEPROM.h>
#include <arduino.h>
#include "config.h"
#include "transmitter_PiPico.h"


static const double MIN_FREQ =     3400000.0;
static const double MAX_FREQ =     3700000.0;
static const double DEFAULT_FREQ = 3550000.0;
static const int MIN_WPM = 5;
static const int MAX_WPM = 100;
static const int DEFAULT_WPM = 10;
static const char DEFAULT_FOX[] = "MO";
static const char DEFAULT_CALL[] = "";
static const int EEPROM_BASE_ADDR = 0;


typedef struct {
  int switch_val;
  double freq;
} switch2freq_t;


typedef struct {
  int switch_val;
  char fox[MAX_FOX_LEN + 1];
} switch2fox_t;


switch2freq_t frequencies[] = 
{
  // 0 means use the value from the EEPROM
  { 1, 3510000.0},
  { 2, 3520000.0},
  { 3, 3530000.0},
  { 4, 3540000.0},
  { 5, 3550000.0},
  { 6, 3560000.0},
  { 7, 3570000.0},
  { 8, 3580000.0},
  { 9, 3590000.0},
  {10, 3600000.0},
  {11, 3500000.0},
  {12, 3579545.0},
  {13, 3579900.0},
  {14, 0.0}, // End of array
};


const char foxes[][MAX_FOX_LEN + 1] = 
{
  "MO", "MOE", "MOI", "MOS", "MOH", "MO5", "MON", "MOD", ""
};


eeprom_data_t current_config;

// Write this to the field is_initialized_token in the EEPROM to signal that the EEPROM is properly initialized
const int EEPROM_INITIALIZED_TOKEN = 0x600DF00D; 


void sanitize_config()
{
  if(current_config.is_initialized_token != EEPROM_INITIALIZED_TOKEN) {
    Serial.println("Setting initialized token");
    current_config.is_initialized_token = EEPROM_INITIALIZED_TOKEN;
  }
  if(current_config.frequency < MIN_FREQ) {
    Serial.println("Setting default frequency 1");
    current_config.frequency = DEFAULT_FREQ;
  }
  if(current_config.frequency > MAX_FREQ) {
    Serial.println("Setting default frequency 2");
    current_config.frequency = DEFAULT_FREQ;
  }
  if(isnan(current_config.frequency)) {
    Serial.println("Setting default frequency 3");
    current_config.frequency = DEFAULT_FREQ;
  }
  if(current_config.wpm < MIN_WPM) {
    Serial.println("Setting default WPM 1");
    current_config.wpm = DEFAULT_WPM;
  }
  if(current_config.wpm > MAX_WPM) {
    Serial.println("Setting default WPM 2");
    current_config.wpm = DEFAULT_WPM;
  }
  if(strlen(current_config.fox_string) < 1) {
    Serial.println("Setting default fox string 1");
    strncpy(current_config.fox_string, "MO", sizeof(current_config.fox_string));
    current_config.fox_string[MAX_FOX_LEN] = '\0';
  }
  if(strlen(current_config.fox_string) > sizeof(current_config.fox_string)) {
    Serial.println("Setting default fox string 2");
    strncpy(current_config.fox_string, DEFAULT_FOX, sizeof(current_config.fox_string));
    current_config.fox_string[MAX_FOX_LEN] = '\0';
  }
  if(strlen(current_config.call) > MAX_CALL_LEN) {
    Serial.println("Setting default call sign");
    strncpy(current_config.call, DEFAULT_CALL, sizeof(current_config.call));
    current_config.call[MAX_CALL_LEN] = '\0';
  }
}


void store_EEPROM_config()
{
  sanitize_config();
  EEPROM.put(EEPROM_BASE_ADDR, current_config);
  EEPROM.commit();
}


// Retrieve the configuration from the EEPROM and return true if there was a valid configuration. Otherwise false.
// The current_config struct is populated with default values if there is no valid data in the EEPROM.
bool load_EEPROM_config()
{
  bool retval = true;

  EEPROM.get(EEPROM_BASE_ADDR, current_config);
  if(current_config.is_initialized_token != EEPROM_INITIALIZED_TOKEN) {
    Serial.println("The EEPROM does not seem to be initialized!");
    retval = false;
  }
  sanitize_config();
  return retval;
}


// Try to convert a (morse) string for a fox to the fox number.fox_string
// Returns -1 if not successful, otherwise 0 to 7.
int fox_string_to_num(const char *fox)
{
  int arrlen;

  arrlen = sizeof(foxes)/sizeof(foxes[0]);
  for(int ii = 0; ii < arrlen; ii++) {
    if(strcmp(fox, foxes[ii]) == 0) {
      return ii;
    }
  }
  return -1;
}


void print_config()
{
  sanitize_config();
  Serial.printf("Frequency: %.1f Hz\n", current_config.frequency);
  Serial.printf("Speed: %d WPM\n", current_config.wpm);
  Serial.printf("Fox: '%s' (%d)\n", current_config.fox_string, fox_string_to_num(current_config.fox_string));
  Serial.printf("Call: '%s'\n", current_config.call);
}


// Set the frequency of current_config based on a switch setting.
// Do nothing if the switch setting frequency is not defined.
void apply_frequency_switch(int n)
{
  int arrlen;
  double freq = 0;

  arrlen = sizeof(frequencies)/sizeof(frequencies[0]);
  for(int ii = 0; ii < arrlen; ii++) {
    if(frequencies[ii].switch_val == n) {
      freq = frequencies[ii].freq;
      break;
    }
  }
  if(freq != 0) {
    current_config.frequency = freq;
  }
}


// Set the fox string based on a fox number.
// Do nothing if the number is invalid.
void fox_num_to_config(int n)
{
  int arrlen;

  arrlen = sizeof(foxes)/sizeof(foxes[0]);
  if(n < arrlen) {
    strncpy(current_config.fox_string, foxes[n], sizeof(current_config.fox_string));
    current_config.fox_string[MAX_FOX_LEN] = '\0';
  } else {
    Serial.printf("Warning: Invalid fox number: %d", n);
  }
}


void setup_switch_pins_power_save()
{
  // Make the config button/switch pins low outputs to not waste current.
  // Only temporarily make them INPUT_PULLUPs when reading them.
  digitalWrite(SW7_Pin, LOW);
  digitalWrite(SW6_Pin, LOW);
  digitalWrite(SW5_Pin, LOW);
  digitalWrite(SW4_Pin, LOW);
  digitalWrite(SW3_Pin, LOW);
  digitalWrite(SW2_Pin, LOW);
  digitalWrite(SW1_Pin, LOW);
  digitalWrite(SW0_Pin, LOW);
  pinMode(SW7_Pin, OUTPUT);
  pinMode(SW6_Pin, OUTPUT);
  pinMode(SW5_Pin, OUTPUT);
  pinMode(SW4_Pin, OUTPUT);
  pinMode(SW3_Pin, OUTPUT);
  pinMode(SW2_Pin, OUTPUT);
  pinMode(SW1_Pin, OUTPUT);
  pinMode(SW0_Pin, OUTPUT);  
  pinMode(Button1_Pin, INPUT_PULLUP); // Momentary pushbutton, only grounded when pressed
}


void setup_switch_pins_readable()
{
  // Set the config button/switch pins to INPUT_PULLUPs to allow reading them.
  pinMode(Button1_Pin, INPUT_PULLUP);
  pinMode(SW7_Pin, INPUT_PULLUP);
  pinMode(SW6_Pin, INPUT_PULLUP);
  pinMode(SW5_Pin, INPUT_PULLUP);
  pinMode(SW4_Pin, INPUT_PULLUP);
  pinMode(SW3_Pin, INPUT_PULLUP);
  pinMode(SW2_Pin, INPUT_PULLUP);
  pinMode(SW1_Pin, INPUT_PULLUP);
  pinMode(SW0_Pin, INPUT_PULLUP);
  delay(1); // Delay 1 ms to allow the levels to stabilize
}


// Read the status of a pin connected to a switch.
// Store 1 in var if the pin was low (switch closed) and 0 otherwise.
void read_switch_pin(int pin, int &var)
{
  var = 1;
  if(digitalRead(pin)) {
    var = 0;
  }
}


// Read the switch settings and apply them.
void read_switches()
{
  int sw[8];
  int freq_switch, fox_switch;

  setup_switch_pins_readable();
  read_switch_pin(SW0_Pin, sw[0]);
  read_switch_pin(SW1_Pin, sw[1]);
  read_switch_pin(SW2_Pin, sw[2]);
  read_switch_pin(SW3_Pin, sw[3]);
  read_switch_pin(SW4_Pin, sw[4]);
  read_switch_pin(SW5_Pin, sw[5]);
  read_switch_pin(SW6_Pin, sw[6]);
  read_switch_pin(SW7_Pin, sw[7]);
  setup_switch_pins_power_save();
  freq_switch = (sw[7]<<3) + (sw[6]<<2) + (sw[5]<<1) + sw[4];
  if(freq_switch == 0) {
    // 0 means use what was in the EEPROM
    Serial.print("Switches set to load EEPROM");
    load_EEPROM_config(); 
    return;
  }
  // Not 0, use the switch settings
  apply_frequency_switch(freq_switch);
  fox_switch = (sw[3]<<2) + (sw[2]<<1) + sw[1];
  fox_num_to_config(fox_switch);
  if(sw[0]) {
    current_config.wpm = 15;
  } else {
    current_config.wpm = 10;
  }
  current_config.is_initialized_token = EEPROM_INITIALIZED_TOKEN;
 
}