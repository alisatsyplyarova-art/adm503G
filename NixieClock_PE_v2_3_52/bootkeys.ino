#include "project_config.h"
/* v2.3.51: кнопки, удержанные при включении питания
 *
 * Все три кнопки висят на одном аналоговом входе A7, поэтому при включении
 * читается ОДНО значение и срабатывает только одно действие:
 *   "М"     (950..1023) - сброс настроек к заводским (удерживать ~6 с, подсветка
 *                          красный -> зелёный -> синий по FACTORY_RESET_STEP_MS);
 *   "плюс"  (100..380)  - режим настройки начала дня/ночи (nightMode.ino);
 *   "минус" (450..860)  - стартовый тест (selftest.ino, только 6 ламп).
 */

static uint8_t classifyBootKey(int a) {
  if (a <= 1023 && a > 950) return BOOTKEY_SET;
  if (a <= 860 && a > 450) return BOOTKEY_MINUS;
  if (a <= 380 && a > 100) return BOOTKEY_PLUS;
  return BOOTKEY_NONE;
}

/* Чтение кнопки при включении. Два независимых чтения должны совпасть. */
uint8_t readBootKey() {
  analogRead(A7);                                   // первое чтение АЦП отбрасываем
  const uint8_t ka = classifyBootKey(analogRead(A7));
  delay(5);
  const uint8_t kb = classifyBootKey(analogRead(A7));
  return (ka == kb) ? ka : BOOTKEY_NONE;
}

static void bootBacklightOff() {
  digitalWrite(BACKLR, 0);
  digitalWrite(BACKLG, 0);
  digitalWrite(BACKLB, 0);
}

/* Сброс настроек к заводским. Вызывается из setup() ДО чтения EEPROM, поэтому
 * достаточно испортить байт версии: обычная инициализация запишет значения по
 * умолчанию из project_config.h. Время RTC, метка сборки и признак "время не
 * выставлено" не затрагиваются. "М" должна удерживаться непрерывно (провал
 * короче 200 мс прощается). Отпустили раньше - сброса нет.
 * Подсветка не зависит от высокого напряжения, поэтому сигнал виден сразу.
 */
bool bootFactoryReset() {
  const byte pins[3] = { BACKLR, BACKLG, BACKLB };
  unsigned long lastHeld = millis();
  for (byte step = 0; step < 3; step++) {
    bootBacklightOff();
    setPWM(pins[step], getPWM_CRT(BL_SIGNAL_LEVEL));
    const unsigned long t0 = millis();
    while (millis() - t0 < FACTORY_RESET_STEP_MS) {
      if (readBootKey() == BOOTKEY_SET) lastHeld = millis();
      else if (millis() - lastHeld > 200UL) {       // кнопку отпустили - отмена
        bootBacklightOff();
        return false;
      }
      delay(20);
    }
  }
  EEPROM.update(1023, 0);                           // версия != EEPROM_VERSION -> все параметры по умолчанию
  for (byte i = 0; i < 4; i++) EEPROM.update(FW_EEPROM_NIGHT_ADDR + i, 0xFF);  // дневной/ночной -> по умолчанию
  bootBacklightOff();                               // "готово": белая вспышка ~1 с
  setPWM(BACKLR, getPWM_CRT(255));
  setPWM(BACKLG, getPWM_CRT(255));
  setPWM(BACKLB, getPWM_CRT(255));
  delay(1000);
  bootBacklightOff();
  return true;
}
