/* Индекс канала подсветки (см. backlColors[]) для фиксированных
 * цветных режимов подсветки BL_RED/BL_BLUE/BL_GREEN
 *  Входные параметры:
 *    byte mode: значение BACKL_MODE
 *  Выходные параметры:
 *    byte: 0 - красный, 1 - зелёный, 2 - синий (индекс в backlColors[])
 */
byte modeColorIndex(byte mode) {
  if (mode == BL_BLUE) return 2;    // синий
  if (mode == BL_GREEN) return 1;   // зелёный
  return 0;                         // красный (BL_RED и по умолчанию)
}

/* Вычисление и установка цвета подсветки по позиции в градиенте радуги
 * (используется в режиме подсветки BL_GRADIENT - ручной выбор цвета)
 *  Входные параметры: нет (используется глобальная gradientPos)
 *  Выходные параметры: нет
 */
void applyGradientColor() {
  int pos = gradientPos % 768;
  int segment = pos / 256;                        // 0: R->G, 1: G->B, 2: B->R
  int offset = pos % 256;
  byte r, g, b;
  switch (segment) {
    case 0:  r = 255 - offset; g = offset;       b = 0;         break;
    case 1:  r = 0;            g = 255 - offset; b = offset;    break;
    default: r = offset;       g = 0;            b = 255 - offset; break;
  }
  // масштабируем под общую яркость радуги/градиента (rainbowMaxBright) -
  // как и в режиме "радуга", индивидуальные BACKL_BRIGHT_R/G/B не учитываются
  setPWM(BACKLR, getPWM_CRT((byte)((int)r * rainbowMaxBright / 255)));
  setPWM(BACKLG, getPWM_CRT((byte)((int)g * rainbowMaxBright / 255)));
  setPWM(BACKLB, getPWM_CRT((byte)((int)b * rainbowMaxBright / 255)));
}

/* Обеспечение работы подсветки
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void backlBrightTick() {
  // Авария DC-DC (FAULT) - принудительное мигание красным поверх любого
  // выбранного режима/цвета подсветки и независимо от текущего экрана
  // (часы, настройки, температура и т.д.). Яркость фиксированная максимальная,
  // не зависит от дневных/ночных настроек - авария должна быть заметна всегда.
  // Состояние FAULT снимается только перезапуском платы (см. DCDC.ino).
  if (dcdcState == DCDCST_FAULT) {
    if (dcdcFaultBlinkTimer.isReady()) {
      dcdcFaultBlinkState = !dcdcFaultBlinkState;
      digitalWrite(BACKLG, 0);
      digitalWrite(BACKLB, 0);
      if (dcdcFaultBlinkState) setPWM(BACKLR, 255);
      else digitalWrite(BACKLR, 0);
    }
    return;
  }

  switch (curMode) {
    case SHTIME:                                  // (0) отображение часов
      if (chBL) {
        chBL = false;
        digitalWrite(BACKLR, 0);
        digitalWrite(BACKLG, 0);
        digitalWrite(BACKLB, 0);
        backlBrightFlag = false;                        // сброс перед началом дыхания/радуги заново
        backlBrightDirection = true;
        if ((BACKL_MODE == BL_RED || BACKL_MODE == BL_BLUE || BACKL_MODE == BL_GREEN) && !colorBreathing) {
          byte c = modeColorIndex(BACKL_MODE);
          setPWM(backlColors[c], backlMaxBright[c]);         // "дыхание" выключено - постоянное свечение
        } else if (BACKL_MODE == BL_GRADIENT) {
          applyGradientColor();                              // статичный цвет, выбранный по градиенту
        }
        // BL_OFF - ничего дополнительно не нужно, все каналы уже погашены выше
        // BL_RED/BL_BLUE/BL_GREEN с colorBreathing==true и BL_RAINBOW
        // обрабатываются далее в тиках дыхания/перехода
      }

      // "Дыхание" фиксированным цветом (красный/синий/зелёный) - включено,
      // пока colorBreathing == true (переключается кнопкой "плюс")
      if ((BACKL_MODE == BL_RED || BACKL_MODE == BL_BLUE || BACKL_MODE == BL_GREEN) && colorBreathing
          && backlBrightTimer.isReady()) {
        byte c = modeColorIndex(BACKL_MODE);
        if (backlMaxBright[c] > 0) {
          if (backlBrightDirection) {
            if (!backlBrightFlag) {
              backlBrightFlag = true;
              backlBrightTimer.setInterval((float)BACKL_STEP / backlMaxBright[c] / 2 * BACKL_TIME);
            }
            backlBrightCounter += BACKL_STEP;
            if (backlBrightCounter >= backlMaxBright[c]) {
              backlBrightDirection = false;
              backlBrightCounter = backlMaxBright[c];
            }
          } else {
            backlBrightCounter -= BACKL_STEP;
            if (backlBrightCounter <= BACKL_MIN_BRIGHT) {
              backlBrightDirection = true;
              backlBrightCounter = BACKL_MIN_BRIGHT;
              backlBrightTimer.setInterval(BACKL_PAUSE);
              backlBrightFlag = false;
            }
          }
          setPWM(backlColors[c], getPWM_CRT(backlBrightCounter));
        } else {
          digitalWrite(backlColors[c], 0);
        }
      }

      // "Радуга": непрерывный плавный переход одного цвета в другой (кроссфейд).
      // Яркость ОДНА ОБЩАЯ для всех цветов (rainbowMaxBright) - индивидуальные
      // BACKL_BRIGHT_R/G/B здесь не учитываются, чтобы все цвета были одинаково
      // яркими и переход выглядел равномерно. Уходящий цвет плавно гаснет, а
      // следующий одновременно и с той же скоростью разгорается - оба канала
      // светятся одновременно всё время перехода, без провала в темноту и без
      // ощущения "щёлк - и вдруг ярко" у нового цвета. Скорость перехода
      // берётся из RAINBOW_TIME_PRESETS[rainbowSpeedIndex] - переключается
      // кнопкой "плюс" (кратко), пока выбран этот режим.
      if (BACKL_MODE == BL_RAINBOW && backlBrightTimer.isReady()) {
        byte nextColor = backlColor + 1;
        if (nextColor >= 3) nextColor = 0;
        byte fadeMax = (rainbowMaxBright > 0) ? rainbowMaxBright : 1;

        if (!backlBrightFlag) {
          backlBrightFlag = true;
          backlBrightCounter = 0;                       // прогресс перехода: 0 - только текущий цвет, fadeMax - только следующий
          backlBrightTimer.setInterval((float)RAINBOW_STEP / fadeMax / 2 * RAINBOW_TIME_PRESETS[rainbowSpeedIndex]);
        }

        backlBrightCounter += RAINBOW_STEP;
        if (backlBrightCounter >= fadeMax) backlBrightCounter = fadeMax;

        int fromLevel = fadeMax - backlBrightCounter;
        int toLevel   = backlBrightCounter;

        setPWM(backlColors[backlColor], getPWM_CRT(fromLevel));
        setPWM(backlColors[nextColor], getPWM_CRT(toLevel));

        if (backlBrightCounter >= fadeMax) {
          digitalWrite(backlColors[backlColor], 0);     // переход завершён - уходящий цвет точно погашен
          backlColor = nextColor;                       // следующий цвет становится текущим
          backlBrightFlag = false;                      // и сразу начинается переход к цвету после него
        }
      }
      break;
      
    case SETTIME:                                 // (1) установка часов
    case SHALARM:                                 // (2) отображение времени будильника (5 сек)
    case SETALARM:                                // (3) установка времени будильника
      if (chBL) {
        chBL = false;
        digitalWrite(BACKLR, 0);
        digitalWrite(BACKLG, 0);
        digitalWrite(BACKLB, 0);
      }
      break;
    
    case SHTEMP:                                  // (4) отображение температуры (5 сек)
      if (chBL) {
        chBL = false;
        setPWM(BACKLR, getPWM_CRT(backlMaxBright[0]));   // 0 = индекс красного канала (см. backlColors[])
        digitalWrite(BACKLG, 0);
        digitalWrite(BACKLB, 0);
      }
      break;
    case SHATM:                                   // (5) отображение атмосферного давления
      if (chBL) {
        chBL = false;
        setPWM(BACKLG, getPWM_CRT(backlMaxBright[1] / 2));   // 1 = индекс зелёного канала
        digitalWrite(BACKLR, 0);
        digitalWrite(BACKLB, 0);
      }
      break;

    case SHHUM:                                   // (6) отображение влажности
      if (chBL) {
        chBL = false;
        setPWM(BACKLB, getPWM_CRT(backlMaxBright[2] / 2));   // 2 = индекс синего канала
        digitalWrite(BACKLR, 0);
        digitalWrite(BACKLG, 0);
      }
      break;
  }
}

/* Обеспечение смены яркости от времени суток
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void changeBright() {
#if (NIGHT_LIGHT == 1)
  // установка яркости всех светилок от времени суток
  if ( (hrs >= NIGHT_START && hrs <= 23)
       || (hrs >= 0 && hrs < NIGHT_END) ) {
    indiMaxBright = INDI_BRIGHT_N;
    dotMaxBright = DOT_BRIGHT_N;
    backlMaxBright[0] = BACKL_BRIGHT_N_R;             // ночная яркость - красный
    backlMaxBright[1] = BACKL_BRIGHT_N_G;             // ночная яркость - зелёный
    backlMaxBright[2] = BACKL_BRIGHT_N_B;             // ночная яркость - синий
    rainbowMaxBright = BACKL_BRIGHT_RAINBOW_N;        // ночная яркость радуги/градиента (общая для всех цветов)
  } else {
    indiMaxBright = INDI_BRIGHT;
    dotMaxBright = DOT_BRIGHT;
    backlMaxBright[0] = BACKL_BRIGHT_R;               // дневная яркость - красный
    backlMaxBright[1] = BACKL_BRIGHT_G;               // дневная яркость - зелёный
    backlMaxBright[2] = BACKL_BRIGHT_B;               // дневная яркость - синий
    rainbowMaxBright = BACKL_BRIGHT_RAINBOW;          // дневная яркость радуги/градиента (общая для всех цветов)
  }
#else
  indiMaxBright = INDI_BRIGHT;
  dotMaxBright = DOT_BRIGHT;
  backlMaxBright[0] = BACKL_BRIGHT_R;
  backlMaxBright[1] = BACKL_BRIGHT_G;
  backlMaxBright[2] = BACKL_BRIGHT_B;
  rainbowMaxBright = BACKL_BRIGHT_RAINBOW;
#endif

  memset(indiDimm, indiMaxBright, NUMTUB);

  // В режиме температуры точка всегда горит постоянно.
  // В остальных режимах учитываем настройку DOT_ALLOWED и будильник.
  if (curMode == SHTEMP) {
    dotSetMode(DM_FULL);
  } else {
    dotSetMode( DOT_ALLOWED ? (alm_set ? DOT_IN_ALARM : DOT_IN_TIME) : DM_NULL );
  }

  if (BACKL_MODE == BL_RAINBOW) {
    if (rainbowMaxBright > 0)
      backlBrightTimer.setInterval((float)RAINBOW_STEP / rainbowMaxBright / 2 * RAINBOW_TIME_PRESETS[rainbowSpeedIndex]);
  } else if ((BACKL_MODE == BL_RED || BACKL_MODE == BL_BLUE || BACKL_MODE == BL_GREEN) && colorBreathing) {
    byte c = modeColorIndex(BACKL_MODE);
    if (backlMaxBright[c] > 0)
      backlBrightTimer.setInterval((float)BACKL_STEP / backlMaxBright[c] / 2 * BACKL_TIME);
  }
  indiBrightCounter = indiMaxBright;

  // пересчитать статичные режимы под новую (дневную/ночную) яркость
  if ((BACKL_MODE == BL_RED || BACKL_MODE == BL_BLUE || BACKL_MODE == BL_GREEN) && !colorBreathing) {
    byte c = modeColorIndex(BACKL_MODE);
    setPWM(backlColors[c], backlMaxBright[c]);
  } else if (BACKL_MODE == BL_GRADIENT) {
    applyGradientColor();
  }
}
