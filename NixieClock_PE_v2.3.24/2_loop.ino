/* Структурная функция основного цикла
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void loop() {
  if (halfsecond) calculateTime();                // каждые 500 мс пересчёт и отправка времени
  beeper();
  if (showFlag) {                                 // отображение номера эффекта цифр/режима подсветки при переходе
    if (blShowActive) {
      if (blShowTimer.isReady()) { showFlag = false; blShowActive = false; }
    } else {
      if (eshowTimer.isReady()) showFlag = false;
    }
  } else if (forceDirectTime) {                   // после смены подсветки - вернуть время напрямую,
    forceDirectTime = false;                      // без эффекта перелистывания цифр
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
    anodeStates = 0x3F;                           // на случай, если для показа использовались не все разряды
    sendTime(hrs, mins, secs);
    newTimeFlag = false;
    newSecFlag = false;
#else
    anodeStates = 0xF;
    sendTime(hrs, mins);
    newTimeFlag = false;
#endif
  } else {

#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
    if ((newSecFlag || newTimeFlag) && curMode == 0) flipTick();     // перелистывание цифр
#else
    if (newTimeFlag && curMode == 0) flipTick();  // перелистывание цифр
#endif
  }
  dotBrightTick();                                // плавное мигание точки
  backlBrightTick();                              // плавное мигание подсветки ламп
  if (GLITCH_ENABLED && GLITCH_ALLOWED && curMode == SHTIME) glitchTick();  // глюки
  buttonsTick();                                  // кнопки
  settingsTick();                                 // настройки
  DCDCTick();                                     // анодное напряжение
}
