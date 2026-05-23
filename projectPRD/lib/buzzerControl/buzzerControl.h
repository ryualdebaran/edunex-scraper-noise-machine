#ifndef BUZZERCONTROL_H
#define BUZZERCONTROL_H

#include <Arduino.h>

int chaosPhase = 0;

void BUZZER_setup(int BUZZER_PIN);

void beepSOS(int BUZZER_PIN);

void beepPanic(int BUZZER_PIN);

void beepAlarm(int BUZZER_PIN);

void beepAlert(int BUZZER_PIN);

void beepChaos(int BUZZER_PIN);

void BUZZER_BEGIN(int BUZZER_PIN);

#endif