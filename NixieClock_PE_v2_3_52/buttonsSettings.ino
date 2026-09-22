#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Поведение отображаемых значений в режимах установки
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
inline bool readHumidityInteger(int &h) {
  return readSensorHumidityInteger(h);
}

// v2.3.32: единая раскладка ламп для ВСЕХ датчиков (как у BME280):
//   температура - лампы 1-3 (см. showDisplayTemperature, anodeStates 0x07);
//   давление    - лампы 2-4, три цифры мм рт. ст.;
//   влажность   - лампы 3-4; 100% - лампы 2-4.
// Если чтение не удалось, на тех же лампах показывается ноль.
void showPressureFields(bool ok, int pmm) {
  if (!ok || pmm < 0) pmm = 0;
  if (pmm > 999) pmm = 999;
  indiDigits[1] = (byte)(pmm / 100);
  indiDigits[2] = (byte)((pmm / 10) % 10);
  indiDigits[3] = (byte)(pmm % 10);
  anodeStates = 0x0E;
}

void showHumidityFields(bool ok, int h) {
  if (!ok || h < 0) h = 0;
  if (h >= 100) {
    indiDigits[1] = 1;
    indiDigits[2] = 0;
    indiDigits[3] = 0;
    anodeStates = 0x0E;
  } else {
    indiDigits[1] = 0;
    indiDigits[2] = (byte)(h / 10);
    indiDigits[3] = (byte)(h % 10);
    anodeStates = 0x0C;
  }
}


// v2.3.31 FINAL: резервная обработка резистивной клавиатуры непосредственно
// в SETTIME/SETALARM. Она не зависит от внутренних таймингов GyverButton.
// Короткое нажатие = событие на отпускании, удержание = одно событие через 400 мс.

boolean settingAnyPressedDebounced = false;   // v2.3.52: результат последнего readSettingButtonEvents(), без дребезга

// v2.3.52: показание кнопки принимается только после того, как оно продержалось
// стабильным SETTING_DEBOUNCE_MS. Раньше единичный "провал" чтения АЦП (помеха,
// дребезг) мгновенно засчитывался как отпускание с последующим "кликом", отсюда
// самопроизвольное листание значений при удержании кнопки.
static bool debounce(bool raw, bool &rawPrev, unsigned long &rawSince, bool &stable, bool &initDone) {
  const unsigned long now = millis();
  if (!initDone) { initDone = true; rawPrev = raw; rawSince = now; stable = raw; }  // v2.3.52: первое
  // показание принимается СРАЗУ (кнопка могла уже удерживаться до первого вызова - например,
  // "плюс" при входе в настройку дня/ночи, зажатый ещё при включении питания), иначе на
  // старте stable ошибочно значился бы "не нажата" и waitRelease/детектор кликов путались.
  if (raw != rawPrev) { rawPrev = raw; rawSince = now; }
  if (now - rawSince >= SETTING_DEBOUNCE_MS) stable = rawPrev;
  return stable;
}

uint8_t readSettingButtonEvents(bool setPressedRaw, bool leftPressedRaw, bool rightPressedRaw) {
  static bool pSet = false, pLeft = false, pRight = false;
  static bool hSet = false, hLeft = false, hRight = false;
  static unsigned long tSet = 0, tLeft = 0, tRight = 0;
  static bool rpSet = false, rpLeft = false, rpRight = false;               // debounce(): raw на прошлой итерации
  static unsigned long rtSet = 0, rtLeft = 0, rtRight = 0;                  // debounce(): когда raw изменился
  static bool dSet = false, dLeft = false, dRight = false;                  // debounce(): текущее стабильное состояние
  static bool iSet = false, iLeft = false, iRight = false;                  // debounce(): было ли первое показание
  uint8_t e = 0;
  const unsigned long HOLD_MS = 400UL;
  const unsigned long now = millis();

  const bool setPressed = debounce(setPressedRaw, rpSet, rtSet, dSet, iSet);
  const bool leftPressed = debounce(leftPressedRaw, rpLeft, rtLeft, dLeft, iLeft);
  const bool rightPressed = debounce(rightPressedRaw, rpRight, rtRight, dRight, iRight);

  if (setPressed && !pSet) { pSet = true; hSet = false; tSet = now; }
  if (leftPressed && !pLeft) { pLeft = true; hLeft = false; tLeft = now; }
  if (rightPressed && !pRight) { pRight = true; hRight = false; tRight = now; }

  if (setPressed && !hSet && now - tSet >= HOLD_MS) { hSet = true; e |= 0x02; }
  if (leftPressed && !hLeft && now - tLeft >= HOLD_MS) { hLeft = true; e |= 0x08; }
  if (rightPressed && !hRight && now - tRight >= HOLD_MS) { hRight = true; e |= 0x20; }

  if (!setPressed && pSet) { if (!hSet) e |= 0x01; pSet = false; hSet = false; }
  if (!leftPressed && pLeft) { if (!hLeft) e |= 0x04; pLeft = false; hLeft = false; }
  if (!rightPressed && pRight) { if (!hRight) e |= 0x10; pRight = false; hRight = false; }

  settingAnyPressedDebounced = setPressed || leftPressed || rightPressed;  // v2.3.52: для nightHandle()
  return e;
}

#if (NUMTUB == 6)
// v2.3.32: вывод полей даты DD MM YY на все 6 ламп при настройке
void showDateFields() {
  indiDigits[0] = changeDay / 10;
  indiDigits[1] = changeDay % 10;
  indiDigits[2] = changeMonth / 10;
  indiDigits[3] = changeMonth % 10;
  indiDigits[4] = changeYear / 10;
  indiDigits[5] = changeYear % 10;
  anodeStates = 0x3F;
  lampState = true;                               // после нажатия поле видно полный полупериод,
  blinkTimer.reset();                             // затем мигание продолжается
}
#endif

void settingsTick() {
  if (curMode == SETNIGHT) {                        // v2.3.51: мигает редактируемое поле (часы или минуты)
    if (blinkTimer.isReady()) {
      lampState = !lampState;
      if (lampState) anodeStates = 0x0F;
      else anodeStates = (nightField & 1) ? 0x03 : 0x0C;
    }
    return;
  }
  if (curMode == SETTIME || curMode == SETALARM) {
    if (curMode == SETALARM && !alm_set) {          // мигать отображением времени будильника, если будильник не установлен
      if (!(anodeStates == 0 || anodeStates == 0xF)) anodeStates = 0;
      if (blinkTimer.isReady()) anodeStates ^= 0xF;
    } else {
      if (blinkTimer.isReady()) {
        lampState = !lampState;
        if (lampState) {
#if (NUMTUB == 6)
          anodeStates = (curMode == SETTIME && currentDigit >= 2) ? 0x3F : 0x0F;
#else
          anodeStates = 0x0F;
#endif
        } else {
          // Фаза "погашено": гасим ТОЛЬКО выбранное поле.
          // Лампы: 0-1 = часы/день, 2-3 = минуты/месяц, 4-5 = год.
#if (NUMTUB == 6)
          if (curMode == SETTIME && currentDigit >= 2) {
            if (currentDigit == 2) anodeStates = 0x3C;      // мигает день
            else if (currentDigit == 3) anodeStates = 0x33; // мигает месяц
            else anodeStates = 0x0F;                        // мигает год
          } else
#endif
          if (!currentDigit) anodeStates = 0x0C;            // мигают часы
          else anodeStates = 0x03;                          // мигают минуты
        }
      }
    }
  }
}

/* v2.3.34: переход к показу даты после режимов датчика/будильника.
 *  Входные параметры:
 *    unsigned long showMs: время показа даты, мс
 *  Выходные параметры:
 *    boolean: true  - перешли в режим SHDATE;
 *             false - дата запрещена (или плата на 4 лампы), переход не выполнен.
 */
boolean gotoDateShow(unsigned long showMs, bool forceManual) {
#if HAS_SECONDS
  if (!forceManual && !dateShowEnabled) return false; // автопоказ даты выключен в меню

  DateTime dateNow = rtc.now();
  indiDigits[0] = dateNow.day() / 10;
  indiDigits[1] = dateNow.day() % 10;
  indiDigits[2] = dateNow.month() / 10;
  indiDigits[3] = dateNow.month() % 10;
  indiDigits[4] = (dateNow.year() % 100) / 10;
  indiDigits[5] = dateNow.year() % 10;
  anodeStates = 0x3F;
  curMode = SHDATE;
  isFreeze = false;
  dotSetMode(DM_FULL);
  autoTimer.setInterval(showMs);
  autoTimer.reset();
  chBL = true;
  return true;
#else
  (void)showMs;
  (void)forceManual;                                // v2.3.46: не используется на 4-ламповых платах
  return false;
#endif
}

/* v2.3.34: конец цепочки показа датчиков - сначала дата (3 с), затем время. */
inline void endSensorChain() {
  if (!gotoDateShow(SENSOR_DATE_SH_TIME, false)) retToTime();
}

/* Возврат к отображению времени
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void retToTime() {
  // v2.3.31: полностью сбрасываем состояние незавершённого эффекта.
  curMode = SHTIME;
  isFreeze = false;
  flipInit = false;
  newTimeFlag = false;
#if HAS_SECONDS
  newSecFlag = false;
#endif
  forceDirectTime = true;
  lampState = false;
  tempNegative = false;

#if HAS_SECONDS
  anodeStates = 0x3F;
  sendTime(hrs, mins, secs);
#else
  anodeStates = 0xF;
  sendTime(hrs, mins);
#endif

  dotSetMode( DOT_ALLOWED ? (alm_set ? DOT_IN_ALARM : DOT_IN_TIME) : DM_NULL );
  chBL = true;
}

/* v2.3.51: "заморозка" экрана датчика (удержание "М") сама заканчивается через FREEZE_TIMEOUT_MS -
 * раньше часы могли остаться на температуре навсегда. */
inline void toggleFreeze() {
  isFreeze = !isFreeze;
  if (isFreeze) freezeSince = millis();
}

/* Обработка нажатий кнопок
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
inline void buttonsTick() {

  btnA.tick();                                    // определение, нажата ли кнопка Alarm
  // сначала - особый вариант реагирования на кнопки в режиме сработавшего будильника
  if (alm_flag) {
    if (btnA.isHolded()) {
      // v2.3.31: удержание кнопки A = snooze на 5 минут; обычный click = полное отключение.
      // v2.3.51: берём внутренние часы, а не непроверенное чтение I2C
      uint16_t total = (uint16_t)hrs * 60 + mins + ALM_SNOOZE_MIN;
      total %= 1440;
      snoozeHrs = total / 60;
      snoozeMins = total % 60;
      snoozeActive = true;
      alm_flag = false;
      alm_fired = false;
    } else if (btnA.isClick()) {
      alm_flag = false;
      snoozeActive = false;
    }
    return;
  }

  int analog = analogRead(A7);                    // чтение нажатой кнопки
  const bool setPressed = (analog <= 1023 && analog > 950);
  const bool leftPressed = (analog <= 860 && analog > 450);
  const bool rightPressed = (analog <= 380 && analog > 100);
  btnSet.tick(setPressed);
  btnL.tick(leftPressed);
  btnR.tick(rightPressed);
  uint8_t settingEvents = readSettingButtonEvents(setPressed, leftPressed, rightPressed);



  // Любое физическое нажатие продлевает таймаут меню.
  if (setPressed || leftPressed || rightPressed) {
    setModeTimer.reset();
    setModeLastAction = millis();
  }

  // v2.3.51: таймаут "заморозки" экрана датчика
  if (isFreeze && (curMode == SHTEMP || curMode == SHATM || curMode == SHHUM) &&
      millis() - freezeSince >= FREEZE_TIMEOUT_MS) {
    retToTime();
  }

  switch (curMode) {
    /*------------------------------------------------------------------------------------------------------------------------------*/
    case SHTIME:                                  // (0) отображение часов
      if (btnR.isClick()) {                       // "плюс" (кратко) - всегда переключение эффекта перелистывания цифр
        if (++FLIP_EFFECT >= FLIP_EFFECT_NUM) FLIP_EFFECT = FM_NULL;
        EEPROM.put(FLIPEFF, FLIP_EFFECT);
                                                  // для показа номера эффекта
        eshowTimer.reset();
        showFlag = true;
        blShowActive = false;                    // это показ эффекта цифр, а не режима подсветки
        flipInit = false;                        // сброс состояния предыдущего эффекта, иначе новый
                                                  // может решить, что переход уже завершён, и не
                                                  // обновить цифры до следующей смены минуты
        for (byte i = 0; i < NUMTUB; i++) indiDimm[i] = indiMaxBright; // v2.3.31: без memset() для volatile
        for (byte i = 0; i < NUMTUB; i++) indiDigits[i] = FLIP_EFFECT + 1; // v2.3.31: без memset() для volatile

#if HAS_SECONDS
        anodeStates = 0x3F;
        newSecFlag = true;
#else
        anodeStates = 0xF;
#endif 
        newTimeFlag = true;
      }

      if (btnR.isHolded()) {                      // "плюс" (удержание) - действие зависит от режима подсветки
        if (BACKL_MODE == BL_RED || BACKL_MODE == BL_BLUE || BACKL_MODE == BL_GREEN) {
                                                  // вкл/выкл "дыхание" для текущего цвета
          colorBreathing = !colorBreathing;
          EEPROM.put(BLBREATH, colorBreathing);
          chBL = true;                            // переинициализировать вывод под новое состояние
        } else if (BACKL_MODE == BL_RAINBOW) {
                                                  // переключение скорости перехода радуги
          if (++rainbowSpeedIndex >= RAINBOW_SPEED_COUNT) rainbowSpeedIndex = 0;
          EEPROM.put(RBSPEED, rainbowSpeedIndex);
          backlBrightFlag = false;                // форсируем пересчёт интервала на новой скорости

                                                  // показать номер пресета скорости на 4-м индикаторе, 2 сек
          blShowTimer.reset();
          showFlag = true;
          blShowActive = true;
          forceDirectTime = true;
          anodeStates = 0x08;                     // только 4-й индикатор (индекс 3), остальные погашены
          indiDigits[3] = rainbowSpeedIndex + 1;   // показываем номер, начиная с 1 (а не с 0)
        }
        // BL_GRADIENT: обрабатывается ниже через isStep()/isRelease() -
        // штатный механизм повтора при удержании из самой библиотеки кнопок
      }

      if (BACKL_MODE == BL_GRADIENT) {
        if (btnR.isStep()) {                      // повторяется, пока зажата кнопка "плюс" (см. setStepTimeout)
          gradientPos += GRADIENT_STEP;
          if (gradientPos >= 256) gradientPos = 0;
          applyGradientColor();

#if (NUMTUB == 6)
                                                  // пока крутим - показываем 0-255 на индикаторах 3,4,5
          showFlag = true;
          blShowActive = true;
          blShowTimer.reset();
          forceDirectTime = true;
          anodeStates = 0x1C;                     // только индикаторы 3,4,5 (индексы 2,3,4)
          testShowHalf(2, gradientPos);
#endif
        }
        if (btnR.isRelease()) {                   // отпустили - сохраняем в EEPROM только один раз за жест
          EEPROM.put(GRADPOS, gradientPos);
        }
      }
    
      if (btnL.isClick()) {                       // переключение режимов подсветки (см. BL_* в главном файле)
        if (++BACKL_MODE >= BL_MODE_COUNT) BACKL_MODE = 0;
        EEPROM.put(LIGHTEFF, BACKL_MODE);
        chBL = true;

                                                  // показать номер режима подсветки на индикаторах (2 сек),
                                                  // без эффекта перелистывания цифр при возврате к времени
        blShowTimer.reset();
        showFlag = true;
        blShowActive = true;
        forceDirectTime = true;
        for (byte i = 0; i < NUMTUB; i++) indiDimm[i] = indiMaxBright; // v2.3.31: без memset() для volatile
        for (byte i = 0; i < NUMTUB; i++) indiDigits[i] = BACKL_MODE + 1; // v2.3.31: без memset() для volatile

#if HAS_SECONDS
        anodeStates = 0x3F;
#else
        anodeStates = 0xF;
#endif
      }

      if (btnL.isHolded()) {                      // включение/отключение секундной точки
        DOT_ALLOWED = !DOT_ALLOWED;
        EEPROM.put(DOTEFF, DOT_ALLOWED);
        dotSetMode( DOT_ALLOWED ? (alm_set ? DOT_IN_ALARM : DOT_IN_TIME) : DM_NULL );
      }

#if SENSOR_MODE != 3
      if (btnA.isClick() || btnA.isHolded()) {    // показ внешнего датчика
        curMode = SHTEMP;
        int t10 = 0;
        bool tempOk = readDisplayTemperature(t10);
        showDisplayTemperature(tempOk, t10);
        measurementsTimer.reset();
        anodeStates = 0x07;
        autoTimer.setInterval(TEMP_SH_TIME);
        autoTimer.reset();
        chBL = true;
      }
#else
      // SENSOR_MODE=3: внешнего датчика нет. Кнопка "сенсор"
      // используется для ручного показа даты. Ручной показ не зависит
      // от разрешения автоматического показа даты в меню.
      if (btnA.isClick()) {
        if (!gotoDateShow(SENSOR_DATE_SH_TIME, true)) retToTime();
      }
#endif // SENSOR_MODE=3: кнопка сенсора показывает дату
      
      if (btnSet.isDouble()) {                    // переход в режим установки времени
        anodeStates = 0x0F;
        currentDigit = false;
        curMode = SETTIME;
        setModeTimer.reset();
        setModeLastAction = millis();                 // v2.3.31: независимый таймаут
        changeHrs = hrs;
        changeMins = mins;
#if (NUMTUB == 6)
        // v2.3.32: поля даты начинаются с ТЕКУЩЕЙ даты из RTC.
        DateTime dt = rtc.now();
        changeDay = dateShowEnabled ? dt.day() : 0;
        changeMonth = dateShowEnabled ? dt.month() : 0;
        changeYear = dt.year() % 100;
#endif
#if HAS_SECONDS
        sendTime(changeHrs, changeMins, 0);
#else
        sendTime(changeHrs, changeMins);
#endif

        chBL = true;
      }

      if (btnSet.isHolded()) {                   // переход в режим установки будильника и времени его
        anodeStates = 0x0F;
        currentDigit = false;
        curMode = SETALARM;
        setModeTimer.reset();
        setModeLastAction = millis();                 // v2.3.31: независимый таймаут
        changeHrs = alm_hrs;
        changeMins = alm_mins;
        
#if HAS_SECONDS
        sendTime(changeHrs, changeMins, 0);
#else
        sendTime(changeHrs, changeMins);
#endif

        dotSetMode( DM_NULL );
        chBL = true;
      }

      break;
      
    /*------------------------------------------------------------------------------------------------------------------------------*/
    case SETTIME:                                 // (1) установка часов + даты
    case SETALARM:                                // (3) установка времени будильника
      // v2.3.31 FINAL: таймаут не зависит от timerMinim и гарантированно
      // срабатывает даже если кнопка была нажата до входа в этот switch.
      if (millis() - setModeLastAction >= 60000UL) {
        retToTime();                               // 60 с без действий -> выход БЕЗ сохранения
        break;
      }

      // В SETALARM дата не настраивается: здесь оставляем исходную логику
      // двух полей HH/MM. В SETTIME на 6 лампах после HH/MM идут DD/MM/YY.
      if (!(curMode == SETALARM && !alm_set)) {
        if ((settingEvents & 0x01)) {
#if (NUMTUB == 6)
          if (curMode == SETTIME) {
            if (++currentDigit > 4) currentDigit = 0;   // ЧЧ -> ММ -> ДД -> МЕС -> ГГ -> ЧЧ
            if (currentDigit >= 2) {
              showDateFields();
            } else {
              anodeStates = 0x0F;
              sendTime(changeHrs, changeMins, 0);
            }
          } else {
            currentDigit = !currentDigit;
          }
#else
          currentDigit = !currentDigit;
#endif
          // v2.3.32: выбранное поле сразу видно, мигание начинается заново
          lampState = true;
          blinkTimer.reset();
          setModeLastAction = millis();
        }

        if ((settingEvents & 0x02)) {
#if (NUMTUB == 6)
          if (curMode == SETTIME && currentDigit >= 2) {
            if (currentDigit == 2) changeDay = 0;
            else if (currentDigit == 3) changeMonth = 0;
            else changeYear = FW_DATE_YEAR_MIN;
            showDateFields();
          } else {
#endif
            if (!currentDigit) changeHrs = 0;
            else changeMins = 0;
            sendTime(changeHrs, changeMins, 0);
#if (NUMTUB == 6)
          }
#endif
          setModeLastAction = millis();
        }

        if ((settingEvents & 0x04)) {
#if (NUMTUB == 6)
          if (curMode == SETTIME && currentDigit >= 2) {
            if (currentDigit == 2) changeDay = (changeDay == 0) ? 31 : changeDay - 1;
            else if (currentDigit == 3) changeMonth = (changeMonth == 0) ? 12 : changeMonth - 1;
            else changeYear = (changeYear <= FW_DATE_YEAR_MIN) ? 99 : changeYear - 1;
            showDateFields();
          } else {
#endif
            if (!currentDigit) {
              changeHrs--;
              if (changeHrs < 0) changeHrs = 23;
            } else {
              changeMins--;
              if (changeMins < 0) {
                changeMins = 59;
                changeHrs--;
                if (changeHrs < 0) changeHrs = 23;
              }
            }
            sendTime(changeHrs, changeMins, 0);
#if (NUMTUB == 6)
          }
#endif
          setModeLastAction = millis();
        }

        if ((settingEvents & 0x08)) {
#if (NUMTUB == 6)
          if (curMode == SETTIME && currentDigit >= 2) {
            if (currentDigit == 2) changeDay = (changeDay < 5) ? 0 : changeDay - 5;
            else if (currentDigit == 3) changeMonth = (changeMonth < 5) ? 0 : changeMonth - 5;
            else changeYear = (changeYear < FW_DATE_YEAR_MIN + 5) ? FW_DATE_YEAR_MIN : changeYear - 5;
            showDateFields();
          } else {
#endif
            if (!currentDigit) {
              changeHrs -= 5;
              if (changeHrs < 0) changeHrs += 24;
            } else {
              changeMins -= 5;
              if (changeMins < 0) {
                changeMins += 60;
                changeHrs--;
                if (changeHrs < 0) changeHrs = 23;
              }
            }
            sendTime(changeHrs, changeMins, 0);
#if (NUMTUB == 6)
          }
#endif
          setModeLastAction = millis();
        }

        if ((settingEvents & 0x10)) {
#if (NUMTUB == 6)
          if (curMode == SETTIME && currentDigit >= 2) {
            if (currentDigit == 2) changeDay = (changeDay >= 31) ? 0 : changeDay + 1;
            else if (currentDigit == 3) changeMonth = (changeMonth >= 12) ? 0 : changeMonth + 1;
            else changeYear = (changeYear >= 99) ? FW_DATE_YEAR_MIN : changeYear + 1;
            showDateFields();
          } else {
#endif
            if (!currentDigit) {
              changeHrs++;
              if (changeHrs > 23) changeHrs = 0;
            } else {
              changeMins++;
              if (changeMins > 59) {
                changeMins = 0;
                changeHrs++;
                if (changeHrs > 23) changeHrs = 0;
              }
            }
            sendTime(changeHrs, changeMins, 0);
#if (NUMTUB == 6)
          }
#endif
          setModeLastAction = millis();
        }

        if ((settingEvents & 0x20)) {
#if (NUMTUB == 6)
          if (curMode == SETTIME && currentDigit >= 2) {
            if (currentDigit == 2) changeDay = (changeDay >= 26) ? changeDay - 25 : changeDay + 5;
            else if (currentDigit == 3) changeMonth = (changeMonth >= 8) ? changeMonth - 7 : changeMonth + 5;
            else changeYear = FW_DATE_YEAR_MIN + (changeYear - FW_DATE_YEAR_MIN + 5) % (100 - FW_DATE_YEAR_MIN);
            showDateFields();
          } else {
#endif
            if (!currentDigit) {
              changeHrs += 5;
              if (changeHrs > 23) changeHrs -= 24;
            } else {
              changeMins += 5;
              if (changeMins > 59) {
                changeMins -= 60;
                changeHrs++;
                if (changeHrs > 23) changeHrs = 0;
              }
            }
            sendTime(changeHrs, changeMins, 0);
#if (NUMTUB == 6)
          }
#endif
          setModeLastAction = millis();
        }
      }

      if (btnA.isHolded()) {
        // SETTIME: удержание A = выход без сохранения. SETALARM: включить/выключить.
        if (curMode == SETTIME) retToTime();
        else if (alm_set) alm_set = 0;
        else alm_set = 1;
        setModeLastAction = millis();
      }

      if (btnA.isClick()) {
        if (curMode == SETTIME) {
          hrs = changeHrs;
          mins = changeMins;
          secs = 0;
          DateTime now = rtc.now();
#if (NUMTUB == 6)
          // День или месяц 00 = отключить автоматический показ даты.
          // В RTC при этом оставляем действующую календарную дату.
          if (changeDay == 0 || changeMonth == 0) {
            dateShowEnabled = false;
            EEPROM.put(DATESHOW, false);
            rtc.adjust(DateTime(now.year(), now.month(), now.day(), hrs, mins, 0));
          } else {
            dateShowEnabled = true;
            EEPROM.put(DATESHOW, true);

            // Корректируем день под выбранный месяц, чтобы не записывать
            // 31 февраля и аналогичные невозможные даты.
            uint16_t fullYear = 2000 + changeYear;
            uint8_t maxDay = 31;
            if (changeMonth == 2) {
              bool leap = ((fullYear % 4) == 0 && (fullYear % 100) != 0) || ((fullYear % 400) == 0);
              maxDay = leap ? 29 : 28;
            } else if (changeMonth == 4 || changeMonth == 6 || changeMonth == 9 || changeMonth == 11) {
              maxDay = 30;
            }
            if (changeDay > maxDay) changeDay = maxDay;
            rtc.adjust(DateTime(fullYear, changeMonth, changeDay, hrs, mins, 0));
          }
#else
          rtc.adjust(DateTime(now.year(), now.month(), now.day(), hrs, mins, 0));
#endif
          // v2.3.51: время выставлено пользователем - предупреждение "время не выставлено" снимается
          timeNotSet = false;
          rtcWarnRequest = false;
          EEPROM.update(FW_EEPROM_TIMEFLAG_ADDR, 0);
          ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            SQW_counter = 0;
            dotFlag = true;
            halfsecPending = 0;
          }
          sqwLastSeenCounter = sqwEdgeCounter;
          sqwLastSeenMillis = millis();
          flipInit = false;
          newTimeFlag = true;
#if HAS_SECONDS
          newSecFlag = true;
#endif
          changeBright();
#if (NUMTUB == 6)
          dateCycleSeconds = 0;
#endif
          if (alm_set && hrs == alm_hrs && mins == alm_mins) {
            alm_fired = true;
            alm_flag = true;
            almTimer.reset();
          }
        } else {
          alm_hrs = changeHrs;
          alm_mins = changeMins;
          EEPROM.put(ALHOUR, alm_hrs);
          EEPROM.put(ALMIN, alm_mins);
          EEPROM.put(ALIFSET, alm_set);
        }
        retToTime();
      }

      break;

    /*------------------------------------------------------------------------------------------------------------------------------*/ 
    case SETNIGHT:                                // (8) v2.3.51: настройка начала дня/ночи (nightMode.ino)
      nightHandle(settingEvents, settingAnyPressedDebounced);   // v2.3.52: без дребезга, иначе waitRelease мог снятся раньше времени
      break;

    /*------------------------------------------------------------------------------------------------------------------------------*/
    case SHALARM:                                 // (2) отображение времени будильника (5 сек)
      if (btnA.isHolded()) {
        retToTime();
      } else if (autoTimer.isReady() || btnA.isClick()) {
        // v2.3.34: дата показывается только если её автопоказ разрешён.
        if (!gotoDateShow(SENSOR_DATE_SH_TIME, false)) retToTime();
      }
      break;

    /*------------------------------------------------------------------------------------------------------------------------------*/
    case SHDATE:
      if (autoTimer.isReady() || btnA.isClick() || btnA.isHolded()) retToTime();
      break;

    /*------------------------------------------------------------------------------------------------------------------------------*/
    case SHTEMP:                                  // отображение температуры
      if (measurementsTimer.isReady()) {
        int t10 = 0;
        bool tempOk = readDisplayTemperature(t10);
        showDisplayTemperature(tempOk, t10);
      }

      if (btnA.isHolded()) retToTime();
      if (btnSet.isHolded()) toggleFreeze();

      if ((autoTimer.isReady() || btnA.isClick()) && !isFreeze) {
#if SENSOR_MODE == 3
        // v2.3.31: без внешнего датчика после температуры показываем будильник.
        if (alm_set) {
          curMode = SHALARM;
          anodeStates = 0x0F;
#if HAS_SECONDS
          sendTime(alm_hrs, alm_mins, 0);
#else
          sendTime(alm_hrs, alm_mins);
#endif
          autoTimer.setInterval(ALARM_SH_TIME);
          autoTimer.reset();
          dotSetMode(DM_NULL);
          chBL = true;
        } else {
          endSensorChain();                       // v2.3.34: дата 3 с, затем время
        }
#elif SENSOR_MODE == 0
        // Только AHT20: давления нет, сразу переходим к влажности.
        curMode = SHHUM;

        int h = 0;
        bool humOk = readHumidityInteger(h);
        showHumidityFields(humOk, h);
        measurementsTimer.reset();
        dotSetMode(DM_NULL);
        autoTimer.setInterval(HUMIDITY_SH_TIME);
        autoTimer.reset();
        chBL = true;
#else
        curMode = SHATM;

        int pmm = 0;
        bool presOk = readSensorPressureMMHg(pmm);
        showPressureFields(presOk, pmm);
        dotSetMode(DM_NULL);
        autoTimer.setInterval(ATMOSPHERE_SH_TIME);
        autoTimer.reset();
        chBL = true;
#endif
      }
      break;


    /*------------------------------------------------------------------------------------------------------------------------------*/ 
    case SHATM:                                   // отображение атмосферного давления
      if (measurementsTimer.isReady()) {
        int pmm = 0;
        bool presOk = readSensorPressureMMHg(pmm);
        showPressureFields(presOk, pmm);
      }

      if (btnA.isHolded()) retToTime();
      if (btnSet.isHolded()) toggleFreeze();

      if ((autoTimer.isReady() || btnA.isClick()) && !isFreeze) {
        curMode = SHHUM;

        // Сразу читаем влажность при входе в режим.
        // Раньше здесь оставалось старое значение давления,
        // потому что SHHUM ждал следующего measurementsTimer.
        int h = 0;
        bool humOk = readHumidityInteger(h);
        showHumidityFields(humOk, h);

        measurementsTimer.reset();
        dotSetMode(DM_NULL);
        autoTimer.setInterval(HUMIDITY_SH_TIME);
        autoTimer.reset();
        chBL = true;
      }
      break;

    /*------------------------------------------------------------------------------------------------------------------------------*/
    case SHHUM:                                   // отображение влажности
      if (measurementsTimer.isReady()) {
        int h = 0;
        bool humOk = readHumidityInteger(h);
        showHumidityFields(humOk, h);
      }

      if (btnA.isHolded()) retToTime();
      if (btnSet.isHolded()) toggleFreeze();

      if ((autoTimer.isReady() || btnA.isClick()) && !isFreeze) {
        if (alm_set) {
          curMode = SHALARM;
          anodeStates = 0x0F;
#if HAS_SECONDS
          sendTime(alm_hrs, alm_mins, 0);
#else
          sendTime(alm_hrs, alm_mins);
#endif
          autoTimer.setInterval(ALARM_SH_TIME);
          autoTimer.reset();
          dotSetMode(DM_NULL);
          chBL = true;
        } else {
          endSensorChain();                       // v2.3.34: дата 3 с, затем время
        }
      }
      break;
  }
}