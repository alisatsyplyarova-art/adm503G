#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Сервисные функции
 *
 * v2.3.31: anti-poisoning переведён с блокирующих delay() на небольшой
 * автомат состояний. Поэтому во время очистки продолжают работать DCDC,
 * watchdog, кнопки и мультиплексирование.
 *
 * v2.3.50: антиотравление переработано.
 *  - Запуск теперь действительно происходит (см. calculateTime()).
 *  - burnIndicators() только ЗАПРАШИВАЕТ цикл. Начинается он, когда на
 *    индикаторах штатные часы: не меню настройки, не датчик/дата/будильник,
 *    не показ номера эффекта и закончился эффект смены минут. Если за
 *    BURN_WAIT_MAX_MS такого момента не было - цикл пропускается.
 *  - Пока цикл идёт, flipTick() не работает (см. 2_loop.ino), поэтому эффекты
 *    смены цифр и обновление секунд не портят цифры очистки.
 *  - Если во время цикла пользователь сменил экран (кнопка, меню) - цикл
 *    немедленно прекращается и чужие данные на индикаторах не затираются.
 *  - На время цикла включаются все лампы (даже при BLANK_LEADING_ZERO),
 *    после цикла sendTime() возвращает штатное состояние.
 */

/* Запрос на антиотравление (вызывается из calculateTime()). */
inline void burnIndicators() {
  if (burnActive || burnPending) return;
  burnPending = true;
  burnPendingSince = millis();
}

inline void burnIndicatorsTick() {
  // ---- 1. Цикл запрошен, но ещё не начат ----
  if (burnPending && !burnActive) {
    const bool clockScreen = (curMode == SHTIME) && !showFlag && !forceDirectTime && !newTimeFlag;
    if (!clockScreen) {
      if (millis() - burnPendingSince >= BURN_WAIT_MAX_MS) burnPending = false;  // не дождались - пропускаем
      return;
    }
    burnPending = false;
    burnActive = true;
    burnLoop = 0;
    burnDigit = 0;
    flipInit = false;
    newTimeFlag = false;
#if HAS_SECONDS
    newSecFlag = false;
    anodeStates = 0x3F;
#else
    anodeStates = 0x0F;
#endif
    for (byte i = 0; i < NUMTUB; i++) indiDimm[i] = indiMaxBright;
    burnTimer.setInterval(BURN_TIME);
    burnTimer.reset();
    return;
  }

  if (!burnActive) return;

  // ---- 2. Цикл идёт ----
  // Экран сменили (меню, датчик, показ номера эффекта/подсветки) - не мешаем.
  if (curMode != SHTIME || showFlag || forceDirectTime) {
    burnActive = false;
    return;
  }

  if (!burnTimer.isReady()) return;

  // Все разряды одновременно "прокручиваются" на одну цифру назад. За 10 шагов
  // каждая цифра на каждой лампе проходит все 10 катодов ровно по одному разу.
  for (byte i = 0; i < NUMTUB; i++) {
    if (indiDigits[i] <= 0) indiDigits[i] = 9;
    else --indiDigits[i];
  }

  if (++burnDigit >= 10) {
    burnDigit = 0;
    if (++burnLoop >= BURN_LOOPS) {
      burnActive = false;
      // После очистки немедленно возвращаем нормальное время.
      flipInit = false;
      newTimeFlag = false;
#if HAS_SECONDS
      newSecFlag = false;
      sendTime(hrs, mins, secs);
#else
      sendTime(hrs, mins);
#endif
    }
  }
}

/* v2.3.31: контроль наличия SQW от DS3231.
 * Если импульсы пропали более чем на 1 с, все аноды гасим.
 *
 * v2.3.50: раньше после возвращения SQW аноды НИКТО не включал обратно
 * (anodeStates оставался 0 до ближайшей смены экрана), а время, пока SQW
 * отсутствовал, не шло. Теперь при возвращении SQW время заново берётся из
 * DS3231 и восстанавливается экран часов.
 */
inline void checkSQWWatchdog() {
  uint32_t edges;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    edges = sqwEdgeCounter;
  }

  if (edges != sqwLastSeenCounter) {
    sqwLastSeenCounter = edges;
    sqwLastSeenMillis = millis();
    if (sqwLost) {
      sqwLost = false;
      syncClockFromRTC();
      retToTime();
    }
  } else if (millis() - sqwLastSeenMillis >= SQW_TIMEOUT_MS) {
    sqwLost = true;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      anodeStates = 0;
      setPin(opts[curIndi], 0);
    }
  }
}
