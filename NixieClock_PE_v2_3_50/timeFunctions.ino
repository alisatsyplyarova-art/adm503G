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
