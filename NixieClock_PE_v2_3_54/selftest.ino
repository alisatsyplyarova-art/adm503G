#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Стартовый аппаратный тест устройства
 *
 * v2.3.31:
 *  - ожидания стали кооперативными: вместо длинных delay() вызывается
 *    startupService(), поэтому DCDCTick(), WDT и кнопка STOP работают;
 *  - HV во время теста действительно регулируется и защищается;
 *  - Vcc-калибровка вынесена в одну константу;
 *  - тестовая индикация DC-DC показывает уже реальное состояние после
 *    завершения проверки.
 */
#if (NUMTUB == 6)

#define HV_CAL_ADC          550
#define HV_CAL_VOLTS        170
#define VCC_CAL_FACTOR      1125300L   // подстройте один раз по мультиметру

#define TEST_VERSION_TIME   3000   // v2.3.49: версия прошивки показывается 3 с
#define TEST_DIGIT_TIME     1000
#define TEST_PERTUBE_TIME   300
#define TEST_DOT_TIME       1000
#define TEST_RGB_TIME       1000
#define TEST_VCC_TIME       3000
#define TEST_HV_TIME        3000
#define TEST_DCDC_TIME      3000

bool startupTestRequested() {
  analogRead(A7);                                  // первое ADC чтение отбрасываем
  int analog = analogRead(A7);
  return (analog <= 860 && analog > 450);
}

void testShowHalf(byte offset, int value) {
  if (value < 0) value = 0;
  if (value > 999) value = 999;
  indiDigits[offset + 0] = (byte)(value / 100);
  indiDigits[offset + 1] = (byte)((value / 10) % 10);
  indiDigits[offset + 2] = (byte)(value % 10);
}

int readVccRawADC() {
  // Внутренняя опора 1.1 В измеряется относительно AVcc.
  ADMUX = (1 << REFS0) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1);
  delay(2);
  ADCSRA |= (1 << ADSC);
  while (ADCSRA & (1 << ADSC)) { wdt_reset(); }
  uint8_t low  = ADCL;
  uint8_t high = ADCH;
  return (high << 8) | low;
}

/*
 * v2.3.31: вместо delay() используем короткие кооперативные интервалы.
 * Это всё ещё отдельный startup-test, поэтому обычный loop() не выполняется,
 * но критические сервисы работают: DCDC, watchdog и кнопка остановки мелодии.
 */
void startupService(uint32_t durationMs, bool serviceAlarmButton) {
  const unsigned long start = millis();
  while (millis() - start < durationMs) {
    DCDCTick();
    wdt_reset();

    if (serviceAlarmButton) {
      btnA.tick();
      if (btnA.isClick() || btnA.isHolded()) {
        alm_flag = false;
      }
    }

    delay(2);
  }
}


// v2.3.32: отображение версии прошивки при стартовом тесте.
// Раскладка (лампы слева направо): [ ] [2] [ ] [3] [3] [2]
// 2-я лампа = MAJOR, 4-я лампа = MINOR, 5-я и 6-я лампы = PATCH (две цифры).
// Например, для версии 2.3.33 показывается: _ 2 _ 3 3 3.
void testShowFirmwareVersion() {
  anodeStates = 0x3A;                             // лампы 2, 4, 5, 6 (биты 1, 3, 4, 5)
  for (byte i = 0; i < NUMTUB; i++) indiDimm[i] = indiMaxBright;
  indiDigits[1] = FW_VERSION_MAJOR;
  indiDigits[3] = FW_VERSION_MINOR;
  indiDigits[4] = FW_VERSION_PATCH / 10;
  indiDigits[5] = FW_VERSION_PATCH % 10;

  // Точка: два коротких включения (общий вывод DOT).
  dotSetMode(DM_FULL);
  startupService(900, false);
  dotSetMode(DM_NULL);
  startupService(250, false);
  dotSetMode(DM_FULL);
  startupService(900, false);
  dotSetMode(DM_NULL);
  startupService(TEST_VERSION_TIME - 2050, false);  // v2.3.49: всего 3 с (900+250+900 - точка)
  anodeStates = 0x3F;                             // дальше тест использует все лампы
}

void runStartupTest() {
  anodeStates = 0x3F;

  // v2.3.32: версия первой строкой стартового теста.
  testShowFirmwareVersion();

  boolean savedAlmFlag = alm_flag;
  unsigned long melodyTotal = 0;
  for (byte i = 0; i < notecounter; i++) melodyTotal += NoteLength[i];
  alm_flag = true;
  note_ip = false;
  unsigned long melodyStart = millis();
  boolean melodyPlaying = true;

  // ---------- 1) Все лампы: 0..9 ----------
  for (byte d = 0; d <= 9; d++) {
    for (byte i = 0; i < NUMTUB; i++) {
      indiDigits[i] = d;
      indiDimm[i] = indiMaxBright;
    }

    unsigned long stepStart = millis();
    while (millis() - stepStart < TEST_DIGIT_TIME) {
      DCDCTick();
      wdt_reset();
      if (melodyPlaying) {
        beeper();
        btnA.tick();
        if (btnA.isClick() || btnA.isHolded() || (millis() - melodyStart >= melodyTotal)) {
          alm_flag = false;
          beeper();
          alm_flag = savedAlmFlag;
          melodyPlaying = false;
        }
      }
      delay(2);
    }
  }

  if (melodyPlaying) {
    alm_flag = false;
    beeper();
    alm_flag = savedAlmFlag;
  }

  // ---------- 2) Поочерёдно каждая лампа ----------
  for (byte i = 0; i < NUMTUB; i++) {
    anodeStates = (1 << i);
    for (byte d = 1; d <= 10; d++) {
      indiDigits[i] = d % 10;
      indiDimm[i] = indiMaxBright;
      startupService(TEST_PERTUBE_TIME, false);
    }
  }
  anodeStates = 0x3F;

  // ---------- 3) Неоновая точка ----------
  setPWM(DOT, getPWM_CRT(dotMaxBright));
  startupService(TEST_DOT_TIME, false);
  setPWM(DOT, getPWM_CRT(0));

  // ---------- 4) RGB ----------
  const byte testColors[3] = { BACKLR, BACKLG, BACKLB };
  for (byte c = 0; c < 3; c++) {
    digitalWrite(BACKLR, 0);
    digitalWrite(BACKLG, 0);
    digitalWrite(BACKLB, 0);
    setPWM(testColors[c], getPWM_CRT(255));
    startupService(TEST_RGB_TIME, false);
  }
  digitalWrite(BACKLR, 0);
  digitalWrite(BACKLG, 0);
  digitalWrite(BACKLB, 0);

  // ---------- 5) Vcc ----------
  int vccRaw = readVccRawADC();
  long vccMv = (vccRaw > 0) ? VCC_CAL_FACTOR / vccRaw : 0;
  int vccCentivolts = (int)(vccMv / 10);
  anodeStates = 0x0E;
  testShowHalf(1, vccCentivolts);
  indiDimm[0] = 0;
  indiDimm[4] = 0;
  indiDimm[5] = 0;
  setPWM(DOT, getPWM_CRT(dotMaxBright));
  startupService(TEST_VCC_TIME, false);
  setPWM(DOT, getPWM_CRT(0));
  anodeStates = 0x3F;
  for (byte i = 0; i < NUMTUB; i++) indiDimm[i] = indiMaxBright;

  // ---------- 6) HV ----------
  int adcValue = analogRead(AV_CTRL);
  int hvVolts = (int)((long)adcValue * HV_CAL_VOLTS / HV_CAL_ADC);
  testShowHalf(0, hvVolts);
  testShowHalf(3, adcValue);
  startupService(TEST_HV_TIME, false);

  // ---------- 7) DC-DC ----------
  // v2.3.31 FINAL: 6 цифр = PPP S SS, где P=PWM, S-разделитель
  // (погашен), SS=00 NORMAL / 11 WARNING / 99 FAULT.
  // v2.3.32: индикатор 4 гасится маской анодов. Значение 10 в indiDigits
  // выходит за пределы digitMask[] и зажигало случайную цифру.
  testShowHalf(0, r_duty);
  indiDigits[3] = 0;
  anodeStates = 0x37;                             // лампы 1-3, 5-6; лампа 4 погашена
  byte dcdcCode = (dcdcState == DCDCST_WARNING) ? 11 : ((dcdcState == DCDCST_FAULT) ? 99 : 0);
  indiDigits[4] = dcdcCode / 10;
  indiDigits[5] = dcdcCode % 10;

  if (dcdcState == DCDCST_FAULT) {
    unsigned long testStart = millis();
    boolean blinkState = false;
    while (millis() - testStart < TEST_DCDC_TIME) {
      DCDCTick();
      wdt_reset();
      blinkState = !blinkState;
      digitalWrite(BACKLG, 0);
      digitalWrite(BACKLB, 0);
      if (blinkState) setPWM(BACKLR, getPWM_CRT(255));
      else digitalWrite(BACKLR, 0);
      delay(300);
    }
    digitalWrite(BACKLR, 0);
  } else {
    startupService(TEST_DCDC_TIME, false);
  }

  // v2.3.32: этап 8 (вывод RAW ADC A7 на лампы) отключён.

  anodeStates = 0x3F;                             // вернуть все лампы для обычной работы
  digitalWrite(BACKLR, 0);
  digitalWrite(BACKLG, 0);
  digitalWrite(BACKLB, 0);
  setPWM(DOT, getPWM_CRT(0));
}

#endif
