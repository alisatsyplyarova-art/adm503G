#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
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
  // v2.3.46: gradientPos теперь пользовательская шкала 0..255,
  // но она покрывает ВСЮ палитру R->G->B->R.
  // 0 = красный, 85 = зелёный, 170 = синий, 255 = снова красный.
  // Ранее 0..255 занимал только один цветовой сегмент, из-за чего
  // полный круг требовал примерно 768 шагов.
  uint16_t pos = (uint16_t)gradientPos * 3U;
  // v2.3.47: при gradientPos = 255 было pos = 765 -> segment = 3 -> ветка default
  // давала СИНИЙ (b = 255) между почти красным (254) и красным (0) - одиночная
  // синяя вспышка на границе круга. Теперь 255 = почти красный (r = 254, b = 1).
  if (pos > 764U) pos = 764U;
  byte segment = pos / 255U;
  byte offset = pos % 255U;
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
  // v2.3.31: RTC потерял питание. В течение 6 с мигаем красным,
  // затем продолжаем обычную работу. Время уже восстановлено в setup().
  if (rtcLostPowerFlag) {
    if (rtcLostPowerBlinkTimer.isReady()) {
      rtcLostPowerBlinkState = !rtcLostPowerBlinkState;
      digitalWrite(BACKLG, 0);
      digitalWrite(BACKLB, 0);
      if (rtcLostPowerBlinkState) setPWM(BACKLR, getPWM_CRT(255));
      else digitalWrite(BACKLR, 0);
    }
    if (rtcLostPowerTimer.isReady()) {
      rtcLostPowerFlag = false;
      rtcLostPowerBlinkState = false;
      digitalWrite(BACKLR, 0);
      chBL = true;
    }
    return;
  }

  // v2.3.31: WARNING тоже должен быть виден в обычной работе.
  // Короткая жёлтая вспышка (R+G) раз в ~700 мс не зависит от выбранного цвета.
  if (dcdcState == DCDCST_WARNING) {
    if (dcdcWarningBlinkTimer.isReady()) {
      dcdcWarningBlinkState = !dcdcWarningBlinkState;
      if (dcdcWarningBlinkState) {
        digitalWrite(BACKLB, 0);
        setPWM(BACKLR, getPWM_CRT(80));
        setPWM(BACKLG, getPWM_CRT(80));
        return;
      } else {
        chBL = true; // вернуть выбранный пользователем режим подсветки
      }
    }
  } else if (dcdcWarningBlinkState) {
    dcdcWarningBlinkState = false;
    chBL = true;
  }

  // Авария DC-DC (FAULT) - принудительное мигание красным поверх любого
  // выбранного режима/цвета подсветки и независимо от текущего экрана
  // (часы, настройки, температура и т.д.). Яркость фиксированная максимальная,
  // не зависит от дневных/ночных настроек - авария должна быть заметна всегда.
  // v2.3.31: FAULT снимается после контролируемого retry или остаётся финальным после 3 попыток.
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
          setPWM(backlColors[c], getPWM_CRT(backlMaxBright[c])); // v2.3.31: статичный режим использует ту же гамму
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
              backlBrightTimer.setInterval(BACKL_INTERVAL_MS(backlMaxBright[c]));
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
          // v2.3.31: один переход = fadeMax шагов, поэтому /2 здесь ошибочно.
          backlBrightTimer.setInterval(RAINBOW_INTERVAL_MS(fadeMax));
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
    case SHDATE:                                  // (7) дата
    case SETALARM:                                // (3) установка времени будильника
      if (chBL) {
        chBL = false;
        digitalWrite(BACKLR, 0);
        digitalWrite(BACKLG, 0);
        digitalWrite(BACKLB, 0);
      }
      break;
    
    case SHTEMP:                                  // (4) отображение температуры (5 сек)
      // v2.3.42: температура может сменить знак уже внутри режима SHTEMP.
      // В этом случае принудительно переинициализируем цвет подсветки.
      static bool lastTempNegative = false;
      if (tempNegative != lastTempNegative) {
        lastTempNegative = tempNegative;
        chBL = true;
      }
      if (chBL) {
        chBL = false;
        digitalWrite(BACKLR, 0);
        digitalWrite(BACKLG, 0);
        digitalWrite(BACKLB, 0);

        // Положительная температура — красная подсветка,
        // отрицательная температура — синяя.
        if (tempNegative) {
          setPWM(BACKLB, getPWM_CRT(backlMaxBright[2])); // синий 100%
        } else {
          setPWM(BACKLR, getPWM_CRT(backlMaxBright[0])); // красный 100%
        }
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
  // v2.3.31: вместо скачка ровно в 23:00/07:00 используется 30-минутный
  // переход. calculateTime() вызывает changeBright() каждую минуту.
  const int totalMinutes = (int)hrs * 60 + mins;
  const int nightStart = NIGHT_START * 60;
  const int nightEnd = NIGHT_END * 60;
  const int transition = NIGHT_TRANSITION;

  uint16_t nightBlend = 0; // 0 = день, 255 = ночь

  // 22:30 -> 23:00: день -> ночь
  if (totalMinutes >= nightStart - transition && totalMinutes < nightStart) {
    nightBlend = (uint16_t)(totalMinutes - (nightStart - transition)) * 255UL / transition;
  }
  // 23:00 -> 06:30: полная ночь
  else if (totalMinutes >= nightStart || totalMinutes < nightEnd - transition) {
    nightBlend = 255;
  }
  // 06:30 -> 07:00: ночь -> день
  else if (totalMinutes >= nightEnd - transition && totalMinutes < nightEnd) {
    nightBlend = (uint16_t)(nightEnd - totalMinutes) * 255UL / transition;
  }
  else {
    nightBlend = 0;
  }

  // Линейная интерполяция дневного/ночного уровня без float.
  indiMaxBright = (uint8_t)((uint16_t)INDI_BRIGHT * (255 - nightBlend) / 255UL
                           + (uint16_t)INDI_BRIGHT_N * nightBlend / 255UL);
  dotMaxBright = (uint8_t)((uint16_t)DOT_BRIGHT * (255 - nightBlend) / 255UL
                          + (uint16_t)DOT_BRIGHT_N * nightBlend / 255UL);
  backlMaxBright[0] = (uint8_t)((uint16_t)BACKL_BRIGHT_R * (255 - nightBlend) / 255UL
                               + (uint16_t)BACKL_BRIGHT_N_R * nightBlend / 255UL);
  backlMaxBright[1] = (uint8_t)((uint16_t)BACKL_BRIGHT_G * (255 - nightBlend) / 255UL
                               + (uint16_t)BACKL_BRIGHT_N_G * nightBlend / 255UL);
  backlMaxBright[2] = (uint8_t)((uint16_t)BACKL_BRIGHT_B * (255 - nightBlend) / 255UL
                               + (uint16_t)BACKL_BRIGHT_N_B * nightBlend / 255UL);
  rainbowMaxBright = (uint8_t)((uint16_t)BACKL_BRIGHT_RAINBOW * (255 - nightBlend) / 255UL
                              + (uint16_t)BACKL_BRIGHT_RAINBOW_N * nightBlend / 255UL);
#else
  indiMaxBright = INDI_BRIGHT;
  dotMaxBright = DOT_BRIGHT;
  backlMaxBright[0] = BACKL_BRIGHT_R;
  backlMaxBright[1] = BACKL_BRIGHT_G;
  backlMaxBright[2] = BACKL_BRIGHT_B;
  rainbowMaxBright = BACKL_BRIGHT_RAINBOW;
#endif

  if (indiMaxBright < 1) indiMaxBright = 1;
  for (byte i = 0; i < NUMTUB; i++) indiDimm[i] = indiMaxBright;

  if (curMode == SHTEMP) {
    dotSetMode(tempNegative ? DM_THREE : DM_FULL);
  } else {
    dotSetMode(DOT_ALLOWED ? (alm_set ? DOT_IN_ALARM : DOT_IN_TIME) : DM_NULL);
  }

  if (BACKL_MODE == BL_RAINBOW) {
    if (rainbowMaxBright > 0)
      backlBrightTimer.setInterval(RAINBOW_INTERVAL_MS(rainbowMaxBright));
  } else if ((BACKL_MODE == BL_RED || BACKL_MODE == BL_BLUE || BACKL_MODE == BL_GREEN) && colorBreathing) {
    byte c = modeColorIndex(BACKL_MODE);
    if (backlMaxBright[c] > 0)
      backlBrightTimer.setInterval(BACKL_INTERVAL_MS(backlMaxBright[c]));
  }
  indiBrightCounter = indiMaxBright;

  if ((BACKL_MODE == BL_RED || BACKL_MODE == BL_BLUE || BACKL_MODE == BL_GREEN) && !colorBreathing) {
    byte c = modeColorIndex(BACKL_MODE);
    setPWM(backlColors[c], getPWM_CRT(backlMaxBright[c]));
  } else if (BACKL_MODE == BL_GRADIENT) {
    applyGradientColor();
  }
}
