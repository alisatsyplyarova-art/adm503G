#include "project_config.h"
/* v2.3.51: дневной / ночной режим без плавного перехода.
 *
 * Два времени, минуты от полуночи:
 *   dayStartMin   0..719   - начало дня  (00:00..11:59)
 *   nightStartMin 720..1440 - начало ночи (12:00..23:59; 1440 = 00:00)
 * Ночь = (t >= nightStartMin) || (t < dayStartMin). Никакой арифметики через полночь.
 * Если начало дня 00:00 и начало ночи 00:00 (1440), ночь не наступает никогда:
 * ночной режим выключен (яркость всегда дневная).
 *
 * Настройка: при включении питания удерживать "плюс".
 *   "М" (коротко)      - следующее поле: день ЧЧ -> день ММ -> ночь ЧЧ -> ночь ММ
 *   "минус"/"плюс"     - значение -1/+1 (удержание -5/+5)
 *   "М" (удержание)    - обнулить поле (для ночи ЧЧ: 00 = "24:00")
 *   "сенсор" (клик)    - сохранить и выйти;  "сенсор" (удержание) или 60 с - выйти без сохранения
 * Подсветка: красный = редактируется день, синий = ночь, зелёный = оба 00:00 (выключено).
 */

/* Значения по умолчанию из project_config.h */
static void nightDefaults() {
#if (NIGHT_LIGHT == 1) && (NIGHT_END >= 0) && (NIGHT_END <= 11) && (NIGHT_START >= 12) && (NIGHT_START <= 24)
  dayStartMin = (uint16_t)NIGHT_END * 60;
  nightStartMin = (uint16_t)NIGHT_START * 60;
#else
  dayStartMin = 0;
  nightStartMin = 1440;
#endif
}

void loadNightSettings() {
  const uint8_t dh = EEPROM.read(FW_EEPROM_NIGHT_ADDR);
  const uint8_t dm = EEPROM.read(FW_EEPROM_NIGHT_ADDR + 1);
  const uint8_t nh = EEPROM.read(FW_EEPROM_NIGHT_ADDR + 2);
  const uint8_t nm = EEPROM.read(FW_EEPROM_NIGHT_ADDR + 3);
  if (dh <= 11 && dm <= 59 && nh >= 12 && nh <= 24 && nm <= 59 && (nh != 24 || nm == 0)) {
    dayStartMin = (uint16_t)dh * 60 + dm;
    nightStartMin = (uint16_t)nh * 60 + nm;
  } else {
    nightDefaults();                                // пусто (0xFF) или мусор
  }
}

/* Ночь ли в указанную минуту суток (0..1439) */
bool isNightMinute(uint16_t t) {
  return (t >= nightStartMin) || (t < dayStartMin);
}

/* Ночной режим выключен (в редактируемой копии) */
static bool nightIsOff() {
  return nDayH == 0 && nDayM == 0 && nNightH == 24;
}

static uint8_t nightWrapAdd(uint8_t v, int8_t d, uint8_t lo, uint8_t hi) {
  const int n = hi - lo + 1;
  int x = ((int)v - lo + d) % n;
  if (x < 0) x += n;
  return (uint8_t)(lo + x);
}

/* Показать редактируемое поле: пара ЧЧ:ММ на четырёх лампах, цвет подсветки */
static void nightShow() {
  uint8_t h = (nightField < 2) ? nDayH : nNightH;
  const uint8_t m = (nightField < 2) ? nDayM : nNightM;
  if (h >= 24) h = 0;                               // 24:00 показываем как 00:00
  anodeStates = 0x0F;
  sendTime(h, m, 0);
  nightColorCode = nightIsOff() ? 2 : ((nightField < 2) ? 0 : 1);
}

static void nightAdjust(int8_t d) {
  switch (nightField) {
    case 0: nDayH = nightWrapAdd(nDayH, d, 0, 11); break;
    case 1: nDayM = nightWrapAdd(nDayM, d, 0, 59); break;
    case 2:
      nNightH = nightWrapAdd(nNightH, d, 12, 24);
      if (nNightH == 24) nNightM = 0;
      break;
    default:
      if (nNightH != 24) nNightM = nightWrapAdd(nNightM, d, 0, 59);
      break;
  }
  nightShow();
}

static void nightZeroField() {
  switch (nightField) {
    case 0: nDayH = 0; break;
    case 1: nDayM = 0; break;
    case 2: nNightH = 24; nNightM = 0; break;
    default: nNightM = 0; break;
  }
  nightShow();
}

/* Вход в режим настройки (вызывается в конце setup()) */
void enterNightSetup() {
  nDayH = dayStartMin / 60;
  nDayM = dayStartMin % 60;
  nNightH = nightStartMin / 60;                     // 1440 -> 24
  nNightM = nightStartMin % 60;
  nightField = 0;
  curMode = SETNIGHT;
  isFreeze = false;
  flipInit = false;
  newTimeFlag = false;
#if HAS_SECONDS
  newSecFlag = false;
#endif
  forceDirectTime = false;
  showFlag = false;
  lampState = true;
  blinkTimer.reset();
  nightWaitRelease = true;                          // "плюс" ещё может быть зажат - его события игнорируем
  setModeLastAction = millis();
  dotSetMode(DM_NULL);
  nightShow();
  chBL = true;
}

static void nightSave() {
  dayStartMin = (uint16_t)nDayH * 60 + nDayM;
  nightStartMin = (uint16_t)nNightH * 60 + nNightM;
  EEPROM.update(FW_EEPROM_NIGHT_ADDR, nDayH);
  EEPROM.update(FW_EEPROM_NIGHT_ADDR + 1, nDayM);
  EEPROM.update(FW_EEPROM_NIGHT_ADDR + 2, nNightH);
  EEPROM.update(FW_EEPROM_NIGHT_ADDR + 3, nNightM);
  changeBright();
  retToTime();
}

/* Обработка кнопок в режиме SETNIGHT (вызывается из buttonsTick()).
 *   ev - события readSettingButtonEvents(): 0x01 М-клик, 0x02 М-удержание,
 *        0x04/0x08 минус клик/удержание, 0x10/0x20 плюс клик/удержание
 *   anyPressed - хотя бы одна кнопка сейчас нажата
 */
void nightHandle(uint8_t ev, bool anyPressed) {
  if (millis() - setModeLastAction >= 60000UL) {    // 60 с без действий - выход без сохранения
    retToTime();
    return;
  }
  if (nightWaitRelease) {
    if (!anyPressed) nightWaitRelease = false;
    return;
  }
  if (ev & 0x01) {
    nightField = (nightField + 1) & 3;
    lampState = true;
    blinkTimer.reset();
    nightShow();
  }
  if (ev & 0x02) nightZeroField();
  if (ev & 0x04) nightAdjust(-1);
  if (ev & 0x08) nightAdjust(-5);
  if (ev & 0x10) nightAdjust(1);
  if (ev & 0x20) nightAdjust(5);

  if (btnA.isHolded()) {                            // выход без сохранения
    retToTime();
    return;
  }
  if (btnA.isClick()) nightSave();                  // сохранить и выйти
}
