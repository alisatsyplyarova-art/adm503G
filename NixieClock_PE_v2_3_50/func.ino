#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Сервисные функции
 *
 * v2.3.31: anti-poisoning переведён с блокирующих delay() на небольшой
 * автомат состояний. Поэтому во время очистки продолжают работать DCDC,
 * watchdog, кнопки и мультиплексирование.
 */

inline void burnIndicators() {
  if (burnActive) return;
  burnActive = true;
  burnLoop = 0;
  burnDigit = 0;
  burnTimer.reset();
}

inline void burnIndicatorsTick() {
  if (!burnActive) return;
  if (!burnTimer.isReady()) return;

  for (byte i = 0; i < NUMTUB; i++) {
    if (indiDigits[i] == 0) indiDigits[i] = 9;
    else --indiDigits[i];
  }

  ++burnDigit;
  if (burnDigit >= 10) {
    burnDigit = 0;
    ++burnLoop;
    if (burnLoop >= BURN_LOOPS) {
      burnActive = false;
      // После очистки немедленно возвращаем нормальное время.
      #if HAS_SECONDS
      sendTime(hrs, mins, secs);
      #else
      sendTime(hrs, mins);
      #endif
    }
  }
}

/* v2.3.31: контроль наличия SQW от DS3231.
 * Если импульсы пропали более чем на 1 с, все аноды гасим.
 * Как только SQW вернулся, мультиплексирование продолжится автоматически.
 */
inline void checkSQWWatchdog() {
  uint32_t edges;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    edges = sqwEdgeCounter;
  }

  if (edges != sqwLastSeenCounter) {
    sqwLastSeenCounter = edges;
    sqwLastSeenMillis = millis();
  } else if (millis() - sqwLastSeenMillis >= SQW_TIMEOUT_MS) {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
      anodeStates = 0;
      setPin(opts[curIndi], 0);
    }
  }
}
