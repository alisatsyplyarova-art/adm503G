#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Структурная функция основного цикла
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void loop() {
#if TEST_WDT
  // v2.3.31 TEST: намеренное зависание для проверки WDT 2 s.
  // После проверки TEST_WDT вернуть в 0.
  while (true) { }
#endif
  wdt_reset();                                      // v2.3.31: watchdog 2 с
  checkSQWWatchdog();                               // контроль SQW DS3231
  burnIndicatorsTick();                              // неблокирующий anti-poisoning
  // Атомарно забираем событие SQW, чтобы прерывание не потерялось
  // между проверкой флага и его сбросом внутри calculateTime().
  bool doCalculateTime = false;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    if (halfsecond) {
      halfsecond = false;
      doCalculateTime = true;
    }
  }
  if (doCalculateTime) calculateTime();          // каждые 500 мс пересчёт и отправка времени
  beeper();
  if (showFlag) {                                 // отображение номера эффекта цифр/режима подсветки при переходе
    if (blShowActive) {
      if (blShowTimer.isReady()) { showFlag = false; blShowActive = false; }
    } else {
      if (eshowTimer.isReady()) showFlag = false;
    }
  } else if (forceDirectTime) {                   // после смены подсветки - вернуть время напрямую,
    forceDirectTime = false;                      // без эффекта перелистывания цифр
#if HAS_SECONDS
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

#if HAS_SECONDS
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
