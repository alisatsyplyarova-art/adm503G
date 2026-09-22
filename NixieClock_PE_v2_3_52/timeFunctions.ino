#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* v2.3.31: преобразование внутреннего 24-часового значения в отображаемое. */
byte displayHour(byte hours) {
#if DISPLAY_12H
  byte h = hours % 12;
  return (h == 0) ? 12 : h;
#else
  return hours;
#endif
}

/* check 28.10.20 
 *  
*/

/* Заполнение массива отображаемых данных
 *  Входные параметры:
 *    byte hours: двузначное число, отображаемое в разрядах часов;
 *    byte minutes: двузначное число, отображаемое в разрядах минут;
 *    byte seconds: двузначное число, отображаемое в разрядах секунд.
 *  Выходные параметры: нет
 */
#if HAS_SECONDS
void sendTime(byte hours, byte minutes, byte seconds) {
  hours = displayHour(hours);
  indiDigits[0] = (byte)hours / 10;
  indiDigits[1] = (byte)hours % 10;

  indiDigits[2] = (byte)minutes / 10;
  indiDigits[3] = (byte)minutes % 10;
#if BLANK_LEADING_ZERO
  if (hours < 10) anodeStates &= ~(1 << 0);
  else anodeStates |= (1 << 0);
#endif

  indiDigits[4] = (byte)seconds / 10;
  indiDigits[5] = (byte)seconds % 10;
}
#else
void sendTime(byte hours, byte minutes) {
  hours = displayHour(hours);
  indiDigits[0] = (byte)hours / 10;
  indiDigits[1] = (byte)hours % 10;

  indiDigits[2] = (byte)minutes / 10;
  indiDigits[3] = (byte)minutes % 10;
#if BLANK_LEADING_ZERO
  if (hours < 10) anodeStates &= ~(1 << 0);
  else anodeStates |= (1 << 0);
#endif
}
// v2.3.47: на 4-ламповых платах настройка времени вызывает sendTime(ч, м, 0) -
// без этой перегрузки BOARD_TYPE 4/5 не линковался (undefined reference).
void sendTime(byte hours, byte minutes, byte) {
  sendTime(hours, minutes);
}
#endif

/* Заполнение массива новых данных для работы эффектов
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void setNewTime() {
  byte dh = displayHour(hrs);
  newTime[0] = (byte)dh / 10;
  newTime[1] = (byte)dh % 10;

  newTime[2] = (byte)mins / 10;
  newTime[3] = (byte)mins % 10;
#if HAS_SECONDS
  newTime[4] = (byte)secs / 10;
  newTime[5] = (byte)secs % 10;  
#endif
}

/* ===================================================================
 * v2.3.50: синхронизация внутренних часов с DS3231
 * ===================================================================
 * Внутренний счётчик времени считает импульсы SQW (8192 Гц) в прерывании и
 * раз в 500 мс отдаёт событие в loop(). Раньше DS3231 читался только при старте
 * и один раз в сутки в 03:00, поэтому любая потерянная полусекунда (или пауза
 * loop(), например, после стартового теста) превращалась в отставание, которое
 * не исправлялось до перезагрузки. Теперь ход часов сверяется с DS3231 раз в
 * RTC_CHECK_PERIOD_SEC секунд.
 */

/* Чтение часов/минут/секунд из DS3231 напрямую (регистры 0x00..0x02, BCD).
 * Возвращает false при ошибке шины, некорректном BCD, значениях вне диапазона
 * или 12-часовом режиме микросхемы - тогда выходные значения использовать нельзя.
 */
bool rtcReadHMS(uint8_t &h, uint8_t &m, uint8_t &s) {
  Wire.beginTransmission(0x68);
  Wire.write((uint8_t)0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)0x68, (uint8_t)3) != 3) return false;
  const uint8_t rs = Wire.read();
  const uint8_t rm = Wire.read();
  const uint8_t rh = Wire.read();
  if ((rs | rm) & 0x80) return false;               // зарезервированный бит должен быть 0
  if (rh & 0x40) return false;                      // 12-часовой режим (RTClib пишет 24-часовой)
  if ((rs & 0x0F) > 9 || (rm & 0x0F) > 9 || (rh & 0x0F) > 9) return false;
  const uint8_t ss = (rs >> 4) & 0x07;
  const uint8_t mm = (rm >> 4) & 0x07;
  const uint8_t hh = (rh >> 4) & 0x03;
  if (ss > 5 || mm > 5) return false;
  s = ss * 10 + (rs & 0x0F);
  m = mm * 10 + (rm & 0x0F);
  h = hh * 10 + (rh & 0x0F);
  return (h <= 23);
}

/* Точная синхронизация: ждёт смену секунды в DS3231 (максимум 1.2 с, WDT сбрасывается)
 * и в этот момент выставляет hrs/mins/secs и обнуляет SQW_counter. Ошибок шины и
 * "залипшей" секунды не боится: при любой неудаче возвращает false и ничего не меняет.
 * Обнуляет счётчик необработанных событий SQW, поэтому после неё старые события
 * не будут применены поверх свежего времени.
 */
bool syncClockFromRTC() {
  uint8_t h = 0, m = 0, s = 0, s0 = 0;
  if (!rtcReadHMS(h, m, s0)) return false;
  s = s0;
  const unsigned long t0 = millis();
  while (s == s0) {
    if (millis() - t0 >= 1200UL) return false;      // секунда не сменилась - RTC не идёт или не отвечает
    wdt_reset();
    delay(2);
    if (!rtcReadHMS(h, m, s)) return false;
  }

  const bool minuteChanged = ((uint8_t)hrs != h) || ((uint8_t)mins != m);
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    SQW_counter = 0;                                // граница секунды DS3231 = начало секунды внутренних часов
    dotFlag = true;
    halfsecPending = 0;
  }
  hrs = h;
  mins = m;
  secs = s;

  uint32_t edges;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    edges = sqwEdgeCounter;
  }
  sqwLastSeenCounter = edges;
  sqwLastSeenMillis = millis();
  rtcCheckDue = false;
  rtcCheckCounter = 0;

#if HAS_SECONDS
  newSecFlag = true;
#endif
  if (minuteChanged) {
    newTimeFlag = true;
    alm_request = true;                             // время могло перескочить через минуту будильника
    changeBright();
  }
  setNewTime();
  return true;
}

/* Периодическая проверка (вызывается из loop()). Читает DS3231 в СЕРЕДИНЕ секунды
 * внутренних часов (SQW_counter 3600..4400 из 8192), когда границы секунд обоих
 * источников далеко, поэтому вердикт "совпадает/не совпадает" однозначен. Чтение
 * двойное - случайный сбой шины не приводит к ложной коррекции. Если расхождение
 * секунда и более - выполняется точная синхронизация по границе секунды.
 */
void rtcCheckTick() {
  if (!rtcCheckDue) return;
  uint16_t c;
  uint8_t pend;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    c = SQW_counter;
    pend = halfsecPending;
  }
  if (pend != 0 || c < 3600 || c > 4400) return;    // не середина секунды - попробуем в следующую
  rtcCheckDue = false;

  uint8_t h1, m1, s1, h2, m2, s2;
  if (!rtcReadHMS(h1, m1, s1)) return;
  if (!rtcReadHMS(h2, m2, s2)) return;
  if (h1 != h2 || m1 != m2 || s1 != s2) return;     // сомнительное чтение - пропускаем
  if (h1 == (uint8_t)hrs && m1 == (uint8_t)mins && s1 == (uint8_t)secs) return;   // ход верный

  syncClockFromRTC();
}
