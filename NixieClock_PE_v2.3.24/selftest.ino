/* Стартовый аппаратный тест устройства
 *
 * Запускается ОДИН РАЗ из setup(), только если при включении питания
 * (или сбросе) зажата кнопка "минус" (btnL). Если кнопку не держать -
 * тест не запускается, часы стартуют как обычно, без задержки.
 *
 * Тест доступен только для плат с 6 лампами (NUMTUB == 6), так как
 * использует все 6 разрядов для одновременного показа двух троек цифр.
 */
#if (NUMTUB == 6)

// Калибровка перевода показания АЦП (вход A6) в вольты для шага "HV".
// Линейная аппроксимация: ADC = HV_CAL_ADC соответствует напряжению
// HV_CAL_VOLTS. Значения по умолчанию соответствуют номинальному уровню
// регулятора (nominallevel = 550 -> ~170В) - подберите под свой делитель.
#define HV_CAL_ADC          550                             // опорное значение АЦП
#define HV_CAL_VOLTS        170                             // соответствующее ему напряжение, В

#define TEST_DIGIT_TIME     1000                            // время показа каждой цифры на шаге 1, мс
#define TEST_PERTUBE_TIME   300                             // время показа каждой цифры на шаге 2 (по лампам), мс
#define TEST_DOT_TIME       1000                             // время показа неоновой точки на шаге 3, мс
#define TEST_RGB_TIME       1000                             // время показа каждого цвета на шаге 4, мс
#define TEST_VCC_TIME       3000                            // время показа шага "питание", мс
#define TEST_HV_TIME        3000                            // время показа шага HV, мс
#define TEST_DCDC_TIME      3000                            // время показа шага DC-DC, мс

/* Проверка, зажата ли кнопка "минус" при включении питания
 *  Входные параметры: нет
 *  Выходные параметры: bool - true, если кнопка "минус" удерживается
 */
bool startupTestRequested() {
  analogRead(A7);                                  // холостое чтение - первое измерение после включения питания
                                                    // бывает неточным, поэтому не учитываем его
  int analog = analogRead(A7);                     // тот же диапазон АЦП, что и в buttonsTick() для btnL
  return (analog <= 860 && analog > 450);
}

/* Вывод трёхзначного числа (0-999) в половину разрядов индикаторов
 *  Входные параметры:
 *    byte offset: 0 - первые 3 разряда, 3 - последние 3 разряда
 *    int value: отображаемое число
 *  Выходные параметры: нет
 */
void testShowHalf(byte offset, int value) {
  if (value < 0) value = 0;
  if (value > 999) value = 999;
  indiDigits[offset + 0] = (byte)(value / 100);
  indiDigits[offset + 1] = (byte)((value / 10) % 10);
  indiDigits[offset + 2] = (byte)(value % 10);
}

/* Измерение реального напряжения питания (Vcc) по встроенному
 * опорному источнику 1.1В - без использования отдельного аналогового
 * входа. Работает независимо от того, чем в реальности запитана плата
 * (USB, блок питания, батарея и т.д.), так как меряет само Vcc
 * относительно внутренней опоры, а не наоборот.
 *  Входные параметры: нет
 *  Выходные параметры: int - "сырое" значение АЦП этого измерения (0-1023)
 */
int readVccRawADC() {
  ADMUX = (1 << REFS0) | (1 << MUX3) | (1 << MUX2) | (1 << MUX1);  // опора - AVcc, канал - внутренние 1.1В
  delay(2);                                        // даём опорному напряжению устояться
  ADCSRA |= (1 << ADSC);                            // запускаем преобразование
  while (ADCSRA & (1 << ADSC));                     // ждём завершения
  uint8_t low  = ADCL;                              // сначала читаем младший байт (обязательно перед старшим)
  uint8_t high = ADCH;
  return (high << 8) | low;
}

/* Выполнение полного стартового теста устройства
 *  Вызывается из setup() после инициализации пинов, RTC, прерывания SQW
 *  и запуска генератора HV - иначе индикаторы и HV показывать нечего.
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void runStartupTest() {
  anodeStates = 0x3F;                             // все 6 разрядов активны

  // Мелодия играет ОДНОВРЕМЕННО с шагом 1 (проверка ламп), а не до него.
  // Временно включаем alm_flag и дёргаем ту же beeper(), что и настоящий
  // будильник, прямо внутри цикла перебора цифр этого шага.
  boolean savedAlmFlag = alm_flag;
  unsigned long melodyTotal = 0;
  for (byte i = 0; i < notecounter; i++) melodyTotal += NoteLength[i];
  alm_flag = true;
  note_ip = false;
  unsigned long melodyStart = millis();
  boolean melodyPlaying = true;

  // ---------- 1) IN-14: все лампы последовательно показывают 0..9 ----------
  for (byte d = 0; d <= 9; d++) {
    for (byte i = 0; i < NUMTUB; i++) {
      indiDigits[i] = d;
      indiDimm[i] = indiMaxBright;
    }
    unsigned long stepStart = millis();
    while (millis() - stepStart < TEST_DIGIT_TIME) {
      if (melodyPlaying) {
        beeper();                                 // тот же путь, что и у настоящего будильника
        btnA.tick();
        if (btnA.isClick() || btnA.isHolded() || (millis() - melodyStart >= melodyTotal)) {
          alm_flag = false;
          beeper();                               // корректно гасит пищалку
          alm_flag = savedAlmFlag;
          melodyPlaying = false;
        }
      }
      delay(2);
    }
  }

  if (melodyPlaying) {                             // лампы закончились раньше мелодии - останавливаем её
    alm_flag = false;
    beeper();
    alm_flag = savedAlmFlag;
  }

  // ---------- 2) Поочерёдное перелистывание каждого индикатора отдельно (1..9,0) ----------
  for (byte i = 0; i < NUMTUB; i++) {
    anodeStates = (1 << i);                       // включена только текущая лампа, остальные погашены
    for (byte d = 1; d <= 10; d++) {
      indiDigits[i] = d % 10;                     // 1,2,3...9,0
      indiDimm[i] = indiMaxBright;
      delay(TEST_PERTUBE_TIME);
    }
  }
  anodeStates = 0x3F;                             // возвращаем все 6 разрядов перед следующим шагом

  // ---------- 3) Неоновая точка - принудительное включение ----------
  setPWM(DOT, getPWM_CRT(dotMaxBright));
  delay(TEST_DOT_TIME);
  setPWM(DOT, getPWM_CRT(0));

  // ---------- 4) RGB - последовательная проверка R -> G -> B ----------
  const byte testColors[3] = { BACKLR, BACKLG, BACKLB };
  for (byte c = 0; c < 3; c++) {
    digitalWrite(BACKLR, 0);
    digitalWrite(BACKLG, 0);
    digitalWrite(BACKLB, 0);
    setPWM(testColors[c], 255);
    delay(TEST_RGB_TIME);
  }
  digitalWrite(BACKLR, 0);
  digitalWrite(BACKLG, 0);
  digitalWrite(BACKLB, 0);

  // ---------- 5) Питание: реальное напряжение Vcc платы, например 5.00 ----------
  int vccRaw = readVccRawADC();
  long vccMv = (vccRaw > 0) ? 1125300L / vccRaw : 0;  // напряжение питания, мВ (1.1 В опора * 1023 * 1000)
  int vccCentivolts = (int)(vccMv / 10);              // например, 500 -> цифры 5,0,0
  anodeStates = 0x0E;                                 // только индикаторы 2,3,4 (индексы 1,2,3) - под ними точка
  testShowHalf(1, vccCentivolts);                     // цифры напряжения на индикаторах 2,3,4
  indiDimm[0] = 0;                                    // явно гасим лампы 1, 5 и 6 (на всякий случай)
  indiDimm[4] = 0;
  indiDimm[5] = 0;
  setPWM(DOT, getPWM_CRT(dotMaxBright));              // точка включена принудительно, независимо от DOT_ALLOWED
  delay(TEST_VCC_TIME);
  setPWM(DOT, getPWM_CRT(0));
  anodeStates = 0x3F;                                 // возвращаем все 6 разрядов перед следующим шагом
  for (byte i = 0; i < NUMTUB; i++) indiDimm[i] = indiMaxBright;  // и их яркость - тоже

  // ---------- 6) HV: расчётное напряжение + сырое значение АЦП A6 ----------
  int adcValue = analogRead(AV_CTRL);
  int hvVolts = (int)((long)adcValue * HV_CAL_VOLTS / HV_CAL_ADC);
  testShowHalf(0, hvVolts);                       // первые 3 цифры - расчётное HV, В
  testShowHalf(3, adcValue);                      // последние 3 цифры - фактический ADC
  delay(TEST_HV_TIME);

  // ---------- 7) DC-DC: текущий PWM + состояние защиты ----------
  int dcdcStatusCode;
  switch (dcdcState) {
    case DCDCST_WARNING: dcdcStatusCode = 111; break;   // HV просело, но допустимо
    case DCDCST_FAULT:   dcdcStatusCode = 999; break;   // авария, генератор выключен
    default:             dcdcStatusCode = 0;   break;   // всё в норме
  }
  testShowHalf(0, r_duty);                        // первые 3 цифры - текущий PWM
  testShowHalf(3, dcdcStatusCode);                // последние 3 цифры - 000 OK / 111 WARNING / 999 FAULT

  if (dcdcState == DCDCST_FAULT) {
    // при аварии дублируем показания миганием красным - так же, как
    // это происходит в обычной работе часов (см. dcdcFaultBlinkTimer в bright.ino)
    unsigned long testStart = millis();
    boolean blinkState = false;
    while (millis() - testStart < TEST_DCDC_TIME) {
      blinkState = !blinkState;
      digitalWrite(BACKLG, 0);
      digitalWrite(BACKLB, 0);
      if (blinkState) setPWM(BACKLR, 255);
      else digitalWrite(BACKLR, 0);
      delay(300);
    }
    digitalWrite(BACKLR, 0);
  } else {
    delay(TEST_DCDC_TIME);
  }

  // возврат в исходное состояние перед обычным запуском часов
  digitalWrite(BACKLR, 0);
  digitalWrite(BACKLG, 0);
  digitalWrite(BACKLB, 0);
  setPWM(DOT, getPWM_CRT(0));
}

#endif
