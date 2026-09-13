/* Стабилизация высокого напряжения + программная защита DC-DC
 *
 * Защита работает без дополнительного токового шунта и имеет три состояния
 * (dcdcState, см. 0_data.ino):
 *   DCDCST_NORMAL  - всё в норме, обычная регулировка;
 *   DCDCST_WARNING - HV просело, но ещё в допустимых пределах: генератор
 *                    продолжает работать, это только сигнал "присмотреться";
 *   DCDCST_FAULT   - слишком низкое или слишком высокое HV: генератор
 *                    выключается и повторных попыток запуска не будет,
 *                    пока не произойдёт следующий сброс/перезапуск платы.
 */
inline void DCDCTick() {
  int voltage = analogRead(AV_CTRL);

  // Защита от перенапряжения с задержкой, чтобы краткий выброс
  // при запуске/перерегулировании (в т.ч. при резкой смене количества
  // горящих ламп - переход в показ будильника/датчиков и обратно)
  // не отключал преобразователь сразу.
  if (voltage >= DCDC_OVERVOLTAGE) {
    if (dcdcOverVoltageStart == 0) dcdcOverVoltageStart = millis();
    if (millis() - dcdcOverVoltageStart >= DCDC_OVERVOLTAGE_TIME_MS) {
      dcdcState = DCDCST_FAULT;
      setPWM(GEN, 0);
      r_duty = minduty;
      dcdcHighDutyStart = 0;
      dcdcWarningStart = 0;
      return;
    }
  } else {
    dcdcOverVoltageStart = 0;
  }

  // После аварии генератор не запускаем автоматически.
  // Это предотвращает циклические попытки при обрыве делителя/неисправности.
  if (dcdcState == DCDCST_FAULT) {
    setPWM(GEN, 0);
    return;
  }

  // Контроль опасного режима: большой PWM + низкое HV -> авария (FAULT).
  if (startup_delay <= 0 && r_duty >= DCDC_PROTECT_DUTY && voltage < DCDC_PROTECT_VOLTAGE) {
    if (dcdcHighDutyStart == 0) dcdcHighDutyStart = millis();
    if (millis() - dcdcHighDutyStart >= DCDC_PROTECT_TIME_MS) {
      dcdcState = DCDCST_FAULT;
      setPWM(GEN, 0);
      r_duty = minduty;
      return;
    }
  } else {
    dcdcHighDutyStart = 0;
  }

  // Контроль просевшего, но ещё допустимого HV -> предупреждение (WARNING).
  // Порог мягче, чем у FAULT: срабатывает раньше, но не отключает генератор.
  if (startup_delay <= 0 && r_duty >= DCDC_WARNING_DUTY && voltage < DCDC_WARNING_VOLTAGE) {
    if (dcdcWarningStart == 0) dcdcWarningStart = millis();
    if (millis() - dcdcWarningStart >= DCDC_WARNING_TIME_MS) {
      dcdcState = DCDCST_WARNING;
    }
  } else {
    dcdcWarningStart = 0;
    dcdcState = DCDCST_NORMAL;              // условия WARNING больше не выполняются - возврат в норму
  }

  if(startup_delay <= 0) {
    if(++idcounter == iduty) {
      duty_delta = voltage - nominallevel;
      idcounter = 0;
    } else duty_delta += voltage - nominallevel;

    if (duty_delta > 0 && duty_delta > maxerrduty) {
      duty_delta = 0;
      if (r_duty > minduty) {
        setPWM(GEN, --r_duty);
      } else {
        setPWM(GEN, 0);
      }
    } else if (duty_delta < 0 && duty_delta < -maxerrduty) {
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
}
