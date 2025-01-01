#pragma once

const int MAX_FOX_LEN = 15;
const int MAX_CALL_LEN = 31;
const int MIN_FAST_WPM = 14; // Minimum morse rate that is counted as fast

typedef struct {
  double frequency;
  int wpm;
  char fox_string[MAX_FOX_LEN+1];      // E.g. "MOS"
  char call[MAX_CALL_LEN+1];            // E.g. "SA5BYZ"
  int is_initialized_token;
} eeprom_data_t;

extern const int EEPROM_INITIALIZED_TOKEN;
extern eeprom_data_t current_config;


void sanitize_config();
void store_EEPROM_config();
bool load_EEPROM_config();
void print_config();
void apply_frequency_switch(int n);
void fox_num_to_config(int n);
int fox_string_to_num(const char *fox);
void setup_switch_pins_power_save();
void setup_switch_pins_readable();
void read_switches();
