// мини-класс таймера, версия 2.0
// использован улучшенный алгоритм таймера на millis
// алгоритм чуть медленнее, но обеспечивает кратные интервалы и защиту от пропусков и переполнений

class timerMinim
{
  public:
    timerMinim(uint32_t interval);				            // объявление таймера с указанием интервала
    void setInterval(uint32_t interval);	            // установка интервала работы таймера
    boolean isReady();						                    // возвращает true, когда пришло время. Сбрасывается в false сам (AUTO) или вручную (MANUAL)
    void reset();							                        // ручной сброс таймера на установленный интервал

  private:
    uint32_t _timer = 0;
    uint32_t _interval = 0;
};

timerMinim::timerMinim(uint32_t interval) {
  _interval = (interval != 0) ? interval : 5;
  _timer = millis();
}

void timerMinim::setInterval(uint32_t interval) {
  // v2.3.29: исправлено. Раньше следующая строка затирала защиту от 0:
  // _interval = interval;
  _interval = (interval != 0) ? interval : 5;
}

boolean timerMinim::isReady() {
  // v2.3.29: дополнительная защита от повреждённого/нулевого интервала.
  if (_interval == 0) _interval = 5;
  uint32_t thisMls = millis();
  if (thisMls - _timer >= _interval) {
    do {
      _timer += _interval;
      if (_timer < _interval) break;                  // переполнение uint32_t
    } while (_timer < thisMls - _interval);           // защита от пропуска шага
    return true;
  } else {
    return false;
  }
}

void timerMinim::reset() {
  _timer = millis();
}
