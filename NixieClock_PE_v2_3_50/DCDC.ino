#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Стабилизация высокого напряжения + программная защита DC-DC
 *
 * v2.3.31:
 *  - безопасный soft-start: PWM начинается с minduty;
 *  - DCDCTick() работает и во время стартового self-test;
 *  - после FAULT выполняется до 3 автоматических повторных запусков;
 *  - после трёх неудач остаётся финальный FAULT до перезапуска платы;
 *  - перенапряжение контролируется и во время soft-start;
 *  - startup_delay больше не зависит от тиков RTC.
 *
 * ВАЖНО: это программная защита. Она НЕ заменяет аппаратную OVP и
 * ограничение тока DC-DC.
 */

inline void dcdcEnterFault() {
  dcdcState = DCDCST_FAULT;
  setPWM(GEN, 0);
  r_duty = minduty;
  duty_delta = 0;
  idcounter = 0;
  dcdcHighDutyStart = 0;
  dcdcWarningStart = 0;
  dcdcOverVoltageStart = 0;

  if (dcdcRetryCount < DCDC_MAX_RETRIES) {
    dcdcRetryAt = millis() + DCDC_RETRY_DELAY_MS;
  } else {
    dcdcRetryAt = 0; // финальный FAULT
  }
}

inline void dcdcStartRetry() {
  ++dcdcRetryCount;
  dcdcState = DCDCST_NORMAL;
  dcdcHighDutyStart = 0;
  dcdcWarningStart = 0;
  dcdcOverVoltageStart = 0;
  duty_delta = 0;
  idcounter = 0;
  r_duty = minduty;
  setPWM(GEN, r_duty);
  dcdcStartupStart = millis();
  dcdcLastStartupStep = millis();
}

inline void DCDCTick() {
  const unsigned long now = millis();
  const int voltage = analogRead(AV_CTRL);

  // ---------- 1. Аварийное перенапряжение ----------
  if (voltage >= DCDC_OVERVOLTAGE) {
    if (dcdcOverVoltageStart == 0) dcdcOverVoltageStart = now;
    if (now - dcdcOverVoltageStart >= DCDC_OVERVOLTAGE_TIME_MS) {
      dcdcEnterFault();
      return;
    }
  } else {
    dcdcOverVoltageStart = 0;
  }

  // ---------- 2. Повторный запуск после FAULT ----------
  if (dcdcState == DCDCST_FAULT) {
    setPWM(GEN, 0);

    if (dcdcRetryAt != 0 && (long)(now - dcdcRetryAt) >= 0) {
      // v2.3.31: вместо вечного FAULT делаем контролируемый повторный старт.
      dcdcStartRetry();
    } else {
      return;
    }
  }

  // ---------- 3. Безопасный soft-start ----------
  const uint8_t targetDuty = (DUTY > maxduty) ? maxduty : DUTY;
  const bool startupActive = (now - dcdcStartupStart < DCDC_STARTUP_TIME_MS);

  if (startupActive) {
    // На каждом шаге сначала проверяем перенапряжение. Если HV уже слишком
    // высокое, dcdcEnterFault() сработает выше и PWM будет немедленно отключён.
    if (now - dcdcLastStartupStep >= DCDC_STARTUP_STEP_MS) {
      dcdcLastStartupStep = now;
      if (r_duty < targetDuty) {
        uint16_t nextDuty = (uint16_t)r_duty + DCDC_STARTUP_STEP;
        r_duty = (nextDuty > targetDuty) ? targetDuty : (uint8_t)nextDuty;
        setPWM(GEN, r_duty);
      }
    }
    startup_delay = 1; // совместимость со старым кодом/индикацией
    return;
  }
  startup_delay = 0;

  // ---------- 4. Опасная просадка HV при большом PWM ----------
  if (r_duty >= DCDC_PROTECT_DUTY && voltage < DCDC_PROTECT_VOLTAGE) {
    if (dcdcHighDutyStart == 0) dcdcHighDutyStart = now;
    if (now - dcdcHighDutyStart >= DCDC_PROTECT_TIME_MS) {
      dcdcEnterFault();
      return;
    }
  } else {
    dcdcHighDutyStart = 0;
  }

  // ---------- 5. Мягкое предупреждение ----------
  if (r_duty >= DCDC_WARNING_DUTY && voltage < DCDC_WARNING_VOLTAGE) {
    if (dcdcWarningStart == 0) dcdcWarningStart = now;
    if (now - dcdcWarningStart >= DCDC_WARNING_TIME_MS) {
      dcdcState = DCDCST_WARNING;
    }
  } else {
    dcdcWarningStart = 0;
    if (dcdcState == DCDCST_WARNING) dcdcState = DCDCST_NORMAL;
  }

  // ---------- 6. Обычная регулировка ----------
  if (++idcounter == iduty) {
    duty_delta = voltage - nominallevel;
    idcounter = 0;
  } else {
    duty_delta += voltage - nominallevel;
  }

  if (duty_delta > maxerrduty) {
    duty_delta = 0;
    if (r_duty > minduty) {
      --r_duty;
      setPWM(GEN, r_duty);
    } else {
      setPWM(GEN, 0);
    }
  } else if (duty_delta < -maxerrduty) {
    duty_delta = 0;
    if (r_duty < maxduty) {
      ++r_duty;
      setPWM(GEN, r_duty);
    } else {
      r_duty = maxduty;
      setPWM(GEN, r_duty);
    }
  }
}
