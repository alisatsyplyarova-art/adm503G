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

    // v2.3.50: периодическая коррекция программного счётчика по DS3231.
    // SQW используется для основного тактирования, но при редкой потере
    // события/задержке loop() программные hrs/mins/secs могли отстать.
    // RTC читается только раз в 10 секунд; при расхождении 2 с и более
    // время сразу восстанавливается из DS3231.
    if ((secs % 10) == 0) {
      DateTime rtcCheck = rtc.now();
      int32_t swTotal = (int32_t)hrs * 3600 + (int32_t)mins * 60 + secs;
      int32_t rtcTotal = (int32_t)rtcCheck.hour() * 3600 +
                         (int32_t)rtcCheck.minute() * 60 + rtcCheck.second();
      int32_t diff = rtcTotal - swTotal;
      if (diff > 43200) diff -= 86400;
      if (diff < -43200) diff += 86400;

      if (diff >= 2 || diff <= -2) {
        hrs = rtcCheck.hour();
        mins = rtcCheck.minute();
        secs = rtcCheck.second();
        newTimeFlag = true;
#if HAS_SECONDS
        newSecFlag = true;
#endif
      }
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
    if (secs > 59) {
      secs = 0;
      mins++;
      changeBright();                         // v2.3.31: плавная яркость меняется каждую минуту
      newTimeFlag = true;                                // флаг что нужно поменять время (минуты и часы)
      alm_request = true;                                // нужно проверить будильник
      if ((mins == 0) && ((hrs % (BURN_PERIOD / 60)) == 0)) burnIndicators(); // v2.3.31: каждые 2 часа
    }
    if (mins > 59) {
      mins = 0;
      hrs++;
      if (hrs > 23) hrs = 0;
      if (hrs == 3) {                                   // синхронизация с RTC в 3 часа ночи
        // v2.3.31: синхронизация в 03:00 имеет ограничение 1.2 с.
        DateTime now = rtc.now();
        const uint8_t startSecond = now.second();
        const unsigned long syncStart = millis();
        while (now.second() == startSecond && millis() - syncStart < 1200) {
          now = rtc.now();
          wdt_reset();
          delay(5);
        }
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
          secs = now.second();
          SQW_counter = 0;
          dotFlag = true;
          halfsecond = false;
        }
        mins = now.minute();
        hrs = now.hour();
        newTimeFlag = true;
#if HAS_SECONDS
        newSecFlag = true;
#endif
      }
    }
    
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
