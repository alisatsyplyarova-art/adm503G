#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* check 28.10.20 */
/* 
 */

/* Обеспечение хода времени и запуск зависящих от времени процессов
 *  ("антиотравления" катодов, синхронизация с RTC (в 3 часа ночи),
 *  работа будильника)
 *  
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void calculateTime() {
  dotFlag = !dotFlag;
  if (dotFlag) {
    dotBrightFlag = true;
    dotBrightDirection = true;
    dotBrightCounter = 0;
    secs++;
    // v2.3.50: раз в RTC_CHECK_PERIOD_SEC секунд просим loop() сверить ход часов с DS3231.
    if (++rtcCheckCounter >= RTC_CHECK_PERIOD_SEC) {
      rtcCheckCounter = 0;
      rtcCheckDue = true;
    }
#if (NUMTUB == 6)
    // v2.3.31 FIX: счётчик даты должен идти каждую секунду, а не только
    // при смене минуты. Поэтому 100 секунд теперь действительно = 100 с.
    if (curMode == SHTIME && dateShowEnabled && ++dateCycleSeconds >= DATE_AUTO_PERIOD_SEC) {
      dateCycleSeconds = 0;
      // v2.3.32: показываем реальную дату из RTC, а не сохранённую копию.
      DateTime dateNow = rtc.now();
      indiDigits[0] = dateNow.day() / 10;
      indiDigits[1] = dateNow.day() % 10;
      indiDigits[2] = dateNow.month() / 10;
      indiDigits[3] = dateNow.month() % 10;
      indiDigits[4] = (dateNow.year() % 100) / 10;
      indiDigits[5] = dateNow.year() % 10;
      anodeStates = 0x3F;
      curMode = SHDATE;
      dotSetMode(DM_FULL);
      autoTimer.setInterval(DATE_SH_TIME);
      autoTimer.reset();
      chBL = true;
    }
#endif
#if HAS_SECONDS
    newSecFlag = true;
#endif
    bool minuteRolled = false;                          // v2.3.50: только что началась новая минута
    if (secs > 59) {
      secs = 0;
      mins++;
      minuteRolled = true;
      // v2.3.51: changeBright() перенесён ниже, после переноса минут в часы
      newTimeFlag = true;                                // флаг что нужно поменять время (минуты и часы)
      alm_request = true;                                // нужно проверить будильник
      // v2.3.50: запуск антиотравления перенесён ниже, ПОСЛЕ переноса минут в часы.
      // Раньше здесь стояло (mins == 0), но в этой точке mins принимает значения 1..60,
      // поэтому условие не выполнялось никогда и антиотравление не запускалось вообще.
    }
    if (mins > 59) {
      mins = 0;
      hrs++;
      if (hrs > 23) hrs = 0;
      if (hrs == 3) {                                   // точная синхронизация с RTC в 03:00 (по границе секунды)
        // v2.3.50: чтение RTC проверяется (BCD/диапазоны); при ошибке шины часы просто идут дальше.
        // Само расхождение теперь ловится раз в RTC_CHECK_PERIOD_SEC секунд (rtcCheckTick()).
        syncClockFromRTC();
      }
    }
    
    if (minuteRolled) {
      changeBright();                                   // v2.3.51: день/ночь меняется скачком, проверка раз в минуту
      // v2.3.51: "время не выставлено" - предупреждение по подсветке раз в RTC_WARN_PERIOD_MIN минут (не ночью)
      if (timeNotSet && (mins % RTC_WARN_PERIOD_MIN) == 0 && !isNightNow) rtcWarnRequest = true;
    }

#if BURN_PERIOD > 0
    // v2.3.50: антиотравление каждые BURN_PERIOD минут от полуночи. Проверка делается
    // после переноса минут в часы, поэтому hrs/mins уже относятся к НОВОЙ минуте.
    if (minuteRolled && (((int)hrs * 60 + mins) % BURN_PERIOD) == 0) burnIndicators();
#endif

#if HAS_SECONDS
    if (newTimeFlag || newSecFlag) setNewTime();        // обновляем массив времени
#else
    if (newTimeFlag) setNewTime();        // обновляем массив времени
#endif

    if (alm_request) {                                  // при смене минуты
      alm_request = false;                              // сбрасываем признак
      if (alm_fired) alm_fired = false;                 // в следующую минуту - сбрасываем признак звучания будильника
      if (alm_set && !alm_fired) {                      // если установлен будильник и он не звучал в текущую минуту
                                                        // во всех режимах, кроме установки будильника, сравниваем время с установленным в будильнике
        if (hrs == alm_hrs && mins == alm_mins && curMode != SETALARM) {
          alm_fired = true;                             // будильник звучит в текущую минуту
          alm_flag = true;                              // будильник сработал
          almTimer.reset();                             // запуск таймера предельного звучания будильника
        }
      }
    }

    // v2.3.31: повтор snooze не зависит от сохранённого времени будильника.
    if (snoozeActive && hrs == snoozeHrs && mins == snoozeMins) {
      snoozeActive = false;
      alm_fired = true;
      alm_flag = true;
      almTimer.reset();
    }
    if (alm_flag) {                                     // если звучит будильник
      if (almTimer.isReady()) {                         // ждём предельного времени звучания будильника
        alm_flag = false;                               // сбрасываем сработку будильника
      }
    }
  }
}
