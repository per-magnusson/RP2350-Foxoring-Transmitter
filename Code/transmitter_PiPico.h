#pragma once

#include "synth.h"

extern synth *rf_synth;

extern bool key_down;     // Whether to transmit continuously

extern const int fox_len; // Length of fox_string
extern char fox_string[]; // String to send as fox identifier

extern const int First_RF_Pin;
extern const int Second_RF_Pin;

extern const int Button1_Pin;
extern const int SW7_Pin;
extern const int SW6_Pin;
extern const int SW5_Pin;
extern const int SW4_Pin;
extern const int SW3_Pin;
extern const int SW2_Pin;
extern const int SW1_Pin;
extern const int SW0_Pin;


void initMorseRate(uint32_t WPM);
double read_batt();