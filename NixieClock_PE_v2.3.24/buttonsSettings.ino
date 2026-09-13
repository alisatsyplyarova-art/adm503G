/* Поведение отображаемых значений в режимах установки
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
inline bool readHumidityInteger(int &h) {
  return readSensorHumidityInteger(h);
}

void settingsTick() {
  if (curMode == SETTIME || curMode == SETALARM) {
    if (curMode == SETALARM && !alm_set) {          // мигать отображением времени будильника, если будильник не установлен
      if (!(anodeStates == 0 || anodeStates == 0xF)) anodeStates = 0;
      if (blinkTimer.isReady()) anodeStates ^= 0xF;
    } else {
      if (blinkTimer.isReady()) {
        lampState = !lampState;
        if (lampState) anodeStates = 0xF;
        else if (!currentDigit) anodeStates = 0x0C;
        else anodeStates = 0x3; 
      }
    }
  }
}

/* Возврат к отображению времени
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void retToTime() {
  curMode = SHTIME;
  isFreeze = false;                               // сброс заморозки автопрокрутки датчиков при выходе

#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
  anodeStates = 0x3F;
  sendTime(hrs, mins, secs);
#else
  anodeStates = 0xF;
  sendTime(hrs, mins);
#endif

  dotSetMode( DOT_ALLOWED ? (alm_set ? DOT_IN_ALARM : DOT_IN_TIME) : DM_NULL );
  chBL = true;
}

/* Обработка нажатий кнопок
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
inline void buttonsTick() {

  btnA.tick();                                    // определение, нажата ли кнопка Alarm
  // сначала - особый вариант реагирования на кнопки в режиме сработавшего будильника
  if (alm_flag) {
    if (btnA.isClick() || btnA.isHolded()) alm_flag = false;
    return;
  }

  int analog = analogRead(A7);                    // чтение нажатой кнопки
  btnSet.tick(analog <= 1023 && analog > 950);    // определение, нажата ли кнопка Set
  btnL.tick(analog <= 860 && analog > 450);       // определение, нажата ли кнопка Up
  btnR.tick(analog <= 380 && analog > 100);       // определение, нажата ли кнопка Down

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
        memset(indiDimm, indiMaxBright, NUMTUB);
        memset(indiDigits, FLIP_EFFECT + 1, NUMTUB);  // показываем номер, начиная с 1 (а не с 0)

#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
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
          if (gradientPos >= 768) gradientPos -= 768;
          applyGradientColor();

#if (NUMTUB == 6)
                                                  // пока крутим - показываем 0-255 на индикаторах 3,4,5
          showFlag = true;
          blShowActive = true;
          blShowTimer.reset();
          forceDirectTime = true;
          anodeStates = 0x1C;                     // только индикаторы 3,4,5 (индексы 2,3,4)
          testShowHalf(2, gradientPos % 256);
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
        memset(indiDimm, indiMaxBright, NUMTUB);
        memset(indiDigits, BACKL_MODE + 1, NUMTUB);   // показываем номер, начиная с 1 (а не с 0)

#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
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
      if (btnA.isClick() || btnA.isHolded()) {    // переход в режим отображения температуры
        curMode = SHTEMP;
        float temperature = NAN;
        readSensorTemperature(temperature);

        if (!isnan(temperature)) {
          int t = (int)(temperature * 10.0 + (temperature >= 0 ? 0.5 : -0.5));
          if (t < 0) t = -t;
          if (t > 999) t = 999;
          indiDigits[0] = (byte)(t / 100);
          indiDigits[1] = (byte)((t / 10) % 10);
          indiDigits[2] = (byte)(t % 10);
        } else {
          indiDigits[0] = 0;
          indiDigits[1] = 0;
          indiDigits[2] = 0;
        }
        measurementsTimer.reset();
        anodeStates = 0x07;
        autoTimer.setInterval(TEMP_SH_TIME);
        autoTimer.reset();
        dotSetMode( DM_FULL );
        chBL = true;
      }
#endif
      // SENSOR_MODE == 3: датчика нет - кнопка "сенсор" не показывает параметры
      // (короткое нажатие/удержание в режиме часов ни на что не влияет;
      // остановка сигнала будильника обрабатывается отдельно, в начале buttonsTick())
      
      if (btnSet.isDouble()) {                    // переход в режим установки времени
        anodeStates = 0x0F;
        currentDigit = false;
        curMode = SETTIME;
        changeHrs = hrs;
        changeMins = mins;
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
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
        changeHrs = alm_hrs;
        changeMins = alm_mins;
        
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
        sendTime(changeHrs, changeMins, 0);
#else
        sendTime(changeHrs, changeMins);
#endif

        dotSetMode( DM_NULL );
        chBL = true;
      }

      break;
      
    /*------------------------------------------------------------------------------------------------------------------------------*/
    case SETTIME:                                 // (1) установка часов
    case SETALARM:                                // (3) установка времени будильника

      if (!(curMode == SETALARM && !alm_set)) {
                                                  // переход между разрядами      
        if (btnSet.isClick()) currentDigit = !currentDigit;
        if (btnSet.isHolded()) {                  // обнуление текущего рвазряда
          if (!currentDigit) changeHrs = 0;
          else changeMins = 0;
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
          sendTime(changeHrs, changeMins, 0);
#else
          sendTime(changeHrs, changeMins);
#endif

        }

        if (btnL.isClick()) {                     // уменьшить значение
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
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
          sendTime(changeHrs, changeMins, 0);
#else
          sendTime(changeHrs, changeMins);
#endif
        }

        if (btnL.isHolded()) {                     // уменьшить значение на 5
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
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
          sendTime(changeHrs, changeMins, 0);
#else
          sendTime(changeHrs, changeMins);
#endif
        }
      
        if (btnR.isClick()) {                     // увеличить значение
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
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
          sendTime(changeHrs, changeMins, 0);
#else
          sendTime(changeHrs, changeMins);
#endif
        }
      
        if (btnR.isClick()) {                     // увеличить значение на 5
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
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
          sendTime(changeHrs, changeMins, 0);
#else
          sendTime(changeHrs, changeMins);
#endif
        }
      }
                                                  
      if (btnA.isHolded()) {                      // переход в режим отображения времени без сохранения или смена установки будильника
        if (curMode == SETTIME) retToTime();
        else if (alm_set) alm_set = 0;
        else alm_set = 1;
      }

      if (btnA.isClick()) {
        if (curMode == SETTIME) 
        {
          hrs = changeHrs;
          mins = changeMins;
          secs = 0;
          DateTime now = rtc.now();
          rtc.adjust(DateTime(now.year(), now.month(), now.day(), hrs, mins, 0));
          SQW_counter = 0;
          changeBright(); 
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
    case SHALARM:                                 // (2) отображение времени будильника (5 сек)
      if (autoTimer.isReady() || btnA.isClick() || btnA.isHolded()) retToTime();
      
      break;

    /*------------------------------------------------------------------------------------------------------------------------------*/ 
    case SHTEMP:                                  // отображение температуры
      if (measurementsTimer.isReady()) {
        float temperature = NAN;
        readSensorTemperature(temperature);

        if (!isnan(temperature)) {
          int t = (int)(temperature * 10.0 + (temperature >= 0 ? 0.5 : -0.5));
          if (t < 0) t = -t;
          if (t > 999) t = 999;

          indiDigits[0] = (byte)(t / 100);
          indiDigits[1] = (byte)((t / 10) % 10);
          indiDigits[2] = (byte)(t % 10);
        }
      }

      if (btnA.isHolded()) retToTime();
      if (btnSet.isHolded()) isFreeze = !isFreeze;

      if ((autoTimer.isReady() || btnA.isClick()) && !isFreeze) {
#if SENSOR_MODE == 0
        // Только AHT20: давления нет, сразу переходим к влажности.
        curMode = SHHUM;

        int h = 0;
        if (readHumidityInteger(h)) {
          if (h < 0) h = 0;
          if (h > 100) h = 100;
          if (h >= 100) {
            anodeStates = 0x0E;
            indiDigits[1] = 1;
            indiDigits[2] = 0;
            indiDigits[3] = 0;
          } else {
            anodeStates = 0x0C;
            indiDigits[2] = (byte)(h / 10);
            indiDigits[3] = (byte)(h % 10);
            indiDigits[1] = 0;
          }
        }
        measurementsTimer.reset();
        dotSetMode(DM_NULL);
        autoTimer.setInterval(HUMIDITY_SH_TIME);
        autoTimer.reset();
        chBL = true;
#else
        curMode = SHATM;

        int pmm = 0;
        if (readSensorPressureMMHg(pmm)) {
          indiDigits[1] = (byte)(pmm / 100);
          indiDigits[2] = (byte)((pmm / 10) % 10);
          indiDigits[3] = (byte)(pmm % 10);
        } else {
          indiDigits[1] = 0;
          indiDigits[2] = 0;
          indiDigits[3] = 0;
        }

        anodeStates = 0x0E;
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
        if (readSensorPressureMMHg(pmm)) {
          indiDigits[1] = (byte)(pmm / 100);
          indiDigits[2] = (byte)((pmm / 10) % 10);
          indiDigits[3] = (byte)(pmm % 10);
        }
      }

      if (btnA.isHolded()) retToTime();
      if (btnSet.isHolded()) isFreeze = !isFreeze;

      if ((autoTimer.isReady() || btnA.isClick()) && !isFreeze) {
        curMode = SHHUM;

        // Сразу читаем влажность при входе в режим.
        // Раньше здесь оставалось старое значение давления,
        // потому что SHHUM ждал следующего measurementsTimer.
        int h = 0;
        if (readHumidityInteger(h)) {
          if (h < 0) h = 0;
          if (h > 100) h = 100;

          // 0..99%: индикаторы №3 и №4.
          // 100%: индикаторы №2, №3 и №4.
          if (h >= 100) {
            anodeStates = 0x0E;
            indiDigits[1] = 1;
            indiDigits[2] = 0;
            indiDigits[3] = 0;
          } else {
            anodeStates = 0x0C;
            indiDigits[2] = (byte)(h / 10);
            indiDigits[3] = (byte)(h % 10);
            indiDigits[1] = 0;
          }
        } else {
          // Датчик влажности отсутствует — очищаем поле влажности.
          anodeStates = 0x0C;
          indiDigits[1] = 0;
          indiDigits[2] = 0;
          indiDigits[3] = 0;
        }

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
        if (readHumidityInteger(h)) {
          if (h < 0) h = 0;
          if (h > 100) h = 100;

          if (h >= 100) {
            // 100% — индикаторы №2, №3 и №4.
            anodeStates = 0x0E;
            indiDigits[1] = 1;
            indiDigits[2] = 0;
            indiDigits[3] = 0;
          } else {
            // 0..99% — индикаторы №3 и №4.
            // Влажность смещена на один индикатор вправо.
            anodeStates = 0x0C;
            indiDigits[2] = (byte)(h / 10);
            indiDigits[3] = (byte)(h % 10);
            indiDigits[1] = 0;
          }
        }
      }

      if (btnA.isHolded()) retToTime();
      if (btnSet.isHolded()) isFreeze = !isFreeze;

      if ((autoTimer.isReady() || btnA.isClick()) && !isFreeze) {
        if (alm_set) {
          curMode = SHALARM;
          anodeStates = 0x0F;
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
          sendTime(alm_hrs, alm_mins, 0);
#else
          sendTime(alm_hrs, alm_mins);
#endif
          autoTimer.setInterval(ALARM_SH_TIME);
          autoTimer.reset();
          dotSetMode(DM_NULL);
          chBL = true;
        } else {
          retToTime();
        }
      }
      break;
  }
}