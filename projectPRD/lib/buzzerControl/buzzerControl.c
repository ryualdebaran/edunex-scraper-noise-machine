#include <buzzerControl.h>
#include <Arduino.h>


int chaosPhase = 0;

void BUZZER_setup(int BUZZER_PIN) {
  pinMode(BUZZER_PIN, OUTPUT);
}

void beepSOS(int BUZZER_PIN) {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);

    digitalWrite(BUZZER_PIN, LOW);
    delay(80);
  }

  delay(200);

  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(400);

    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }

  delay(200);

  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(100);

    digitalWrite(BUZZER_PIN, LOW);
    delay(80);
  }
}

void beepPanic(int BUZZER_PIN) {
  int pattern[] = {50, 30, 200, 50, 50, 300, 80, 40};
  int gaps[]    = {30, 20, 100, 30, 20, 150, 40, 30};

  for (int i = 0; i < 8; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(pattern[i]);

    digitalWrite(BUZZER_PIN, LOW);
    delay(gaps[i]);
  }
}

void beepAlarm(int BUZZER_PIN) {
  for (int i = 0; i < 5; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(80);

    digitalWrite(BUZZER_PIN, LOW);
    delay(40);

    digitalWrite(BUZZER_PIN, HIGH);
    delay(160);

    digitalWrite(BUZZER_PIN, LOW);
    delay(40);
  }
}

void beepAlert(int BUZZER_PIN) {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(150);

    digitalWrite(BUZZER_PIN, LOW);
    delay(100);
  }
}

void beepChaos(int BUZZER_PIN) {
  switch (chaosPhase % 4) {
    case 0:
      beepPanic(BUZZER_PIN);
      break;

    case 1:
      beepSOS(BUZZER_PIN);
      break;

    case 2:
      beepAlarm(BUZZER_PIN);
      break;

    case 3:
      beepPanic(BUZZER_PIN);
      beepAlarm(BUZZER_PIN);
      break;
  }

  chaosPhase++;
}

void BUZZER_BEGIN(int BUZZER_PIN) {

  beepSOS(BUZZER_PIN);
  delay(1000);

  beepPanic(BUZZER_PIN);
  delay(1000);

  beepAlarm(BUZZER_PIN);
  delay(1000);

  beepAlert(BUZZER_PIN);
  delay(1000);

  beepChaos(BUZZER_PIN);
  delay(2000);
}