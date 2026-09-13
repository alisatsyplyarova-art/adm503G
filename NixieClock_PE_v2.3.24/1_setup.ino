/* Структурная функция начальной установки
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void setup() {
  // случайное зерно для генератора случайных чисел
  randomSeed(analogRead(6) + analogRead(7));

#if (NUMTUB == 6)
  // проверяем сразу при включении: зажата ли кнопка "минус" -
  // если да, после инициализации запустится стартовый тест
  boolean testRequested = startupTestRequested();
#endif

  // настройка пинов на вход
  pinMode(ALARM_STOP, INPUT);

  // настройка пинов на выход
  pinMode(DECODER0, OUTPUT);
  pinMode(DECODER1, OUTPUT);
  pinMode(DECODER2, OUTPUT);
  pinMode(DECODER3, OUTPUT);
  pinMode(KEY0, OUTPUT);
  pinMode(KEY1, OUTPUT);
  pinMode(KEY2, OUTPUT);
  pinMode(KEY3, OUTPUT);
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
  pinMode(KEY4, OUTPUT);
  pinMode(KEY5, OUTPUT);
//#else
  // временно, для выключения неиспользуемых выводов
//#define KEY4              0                                 // - секунды (десятки) - исправить в v.1 подключение!!!! было подключено к 6
//#define KEY5              7                                 // - секунды (единицы)
//  pinMode(KEY4, OUTPUT);
//  pinMode(KEY5, OUTPUT);
//  digitalWrite(KEY4, 0);
//  digitalWrite(KEY5, 0);
#endif
  pinMode(PIEZO, OUTPUT);
  pinMode(GEN, OUTPUT);
  pinMode(DOT, OUTPUT);
  pinMode(BACKLR, OUTPUT);
  pinMode(BACKLG, OUTPUT);
  pinMode(BACKLB, OUTPUT);
  
  digitalWrite(GEN, 0);                           // устранение возможного "залипания" выхода генератора
  btnSet.setTimeout(400);                         // установка параметров библиотеки реагирования на кнопки
  btnSet.setDebounce(90);                         // защитный период от дребезга
  btnR.setDebounce(90);                           // защитный период от дребезга
  btnR.setStepTimeout(GRADIENT_STEP_TIME);        // интервал повтора isStep() при удержании (режим "градиент")
  btnL.setDebounce(90);                           // защитный период от дребезга

 // ---------- RTC -----------
  rtc.begin();
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  pinMode(RTC_SYNC, INPUT_PULLUP);                // объявляем вход для синхросигнала RTC
                                                  // заставляем входной сигнал генерировать прерывания
  attachInterrupt(digitalPinToInterrupt(RTC_SYNC), RTC_handler, RISING);
  rtc.writeSqwPinMode(DS3231_SquareWave8kHz);     // настраиваем DS3231 для вывода сигнала 8кГц

  // настройка быстрого чтения аналогового порта (mode 4)
  sbi(ADCSRA, ADPS2);
  cbi(ADCSRA, ADPS1);
  cbi(ADCSRA, ADPS0);
  analogRead(A6);                                 // устранение шума
  analogRead(A7);
  // ------------------
  boolean time_sync = false;
  DateTime now = rtc.now();
  do {
    if (!time_sync) {
      time_sync = true;
      secs = now.second();
      mins = now.minute();
      hrs = now.hour();
    }
    now = rtc.now();
  } while (secs != now.second());
  secs = now.second();
  SQW_counter = 0;
  mins = now.minute();
  hrs = now.hour();
      
  // задаем частоту ШИМ на 9 и 10 выводах 31 кГц
  TCCR1B = TCCR1B & 0b11111000 | 1;               // ставим делитель 1

  // перенастраиваем частоту ШИМ на пинах 3 и 11 для соответствия таймеру 0
  // Пины D3 и D11 - 980 Гц
  TCCR2B = 0b00000011;                            // x32
  TCCR2A = 0b00000001;                            // phase correct

  // EEPROM
  if (EEPROM.read(1023) != 103) {                 // первый запуск
    EEPROM.put(1023, 103);
    EEPROM.put(FLIPEFF, FLIP_EFFECT);
    EEPROM.put(LIGHTEFF, BACKL_MODE);
    EEPROM.put(GLEFF, GLITCH_ALLOWED);
    EEPROM.put(ALHOUR, 0);
    EEPROM.put(ALMIN, 0);
    EEPROM.put(ALIFSET, false);
    EEPROM.put(BLCOLOR, 1);
    EEPROM.put(DOTEFF, DOT_ALLOWED);
    EEPROM.put(GRADPOS, 0);
    EEPROM.put(BLBREATH, colorBreathing);
    EEPROM.put(RBSPEED, rainbowSpeedIndex);
  }
  EEPROM.get(FLIPEFF, FLIP_EFFECT);
  EEPROM.get(LIGHTEFF, BACKL_MODE);
  if (BACKL_MODE >= BL_MODE_COUNT) BACKL_MODE = 0;             // защита от несовместимого значения со старой прошивки
  EEPROM.get(GLEFF, GLITCH_ALLOWED);
  EEPROM.get(ALHOUR, alm_hrs);
  EEPROM.get(ALMIN, alm_mins);
  EEPROM.get(ALIFSET, alm_set);
  EEPROM.get(BLCOLOR, backlColor);
  DOT_ALLOWED = (EEPROM.read(DOTEFF) == 0) ? false : true;   // для плат, обновлённых со старой прошивки (байт может быть "пустым" = 0xFF)
  EEPROM.get(GRADPOS, gradientPos);
  if (gradientPos < 0 || gradientPos >= 768) gradientPos = 0;  // для плат, обновлённых со старой прошивки (значение может быть "пустым")
  colorBreathing = (EEPROM.read(BLBREATH) == 0) ? false : true;  // для плат, обновлённых со старой прошивки
  EEPROM.get(RBSPEED, rainbowSpeedIndex);
  if (rainbowSpeedIndex >= RAINBOW_SPEED_COUNT) rainbowSpeedIndex = 3;  // для плат, обновлённых со старой прошивки

  // включаем ШИМ
  r_duty = (DUTY > maxduty) ? maxduty : DUTY;
  setPWM(GEN, r_duty);

#if (NUMTUB == 6)
  // запуск стартового теста (если при включении была зажата кнопка "минус")
  // на этом месте уже готовы: пины, прерывание SQW (мультиплексирование
  // индикаторов) и генератор HV - без них тесту нечего было бы показывать
  if (testRequested) runStartupTest();
#endif

#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6) 
  sendTime(hrs, mins, secs);                      // отправить время на индикаторы
#else
  sendTime(hrs, mins);                            // отправить время на индикаторы
#endif

  changeBright();                                 // изменить яркость согласно времени суток

  // стартовый период глюков
  glitchTimer.setInterval(random(GLITCH_MIN * 1000L, GLITCH_MAX * 1000L));

  // скорость режима при запуске
  flipTimer.setInterval(FLIP_SPEED[FLIP_EFFECT]);

  // I2C: Arduino UNO SDA=A4, SCL=A5
  Wire.begin();
  delay(20);

  // ---------- Датчик I2C ----------
#if SENSOR_MODE == 2
  isBME280here = bme.begin(0x76, &Wire);
  if (!isBME280here) isBME280here = bme.begin(0x77, &Wire);
  if (isBME280here) {
    bme.setSampling(Adafruit_BME280::MODE_NORMAL, Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::SAMPLING_X16, Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::FILTER_X4, Adafruit_BME280::STANDBY_MS_500);
    bme_temp = bme.getTemperatureSensor();
    bme_pressure = bme.getPressureSensor();
    bme_humidity = bme.getHumiditySensor();
  }
#endif
#if SENSOR_MODE == 1
    isBMP280here = bmp.begin(0x76);
    if (!isBMP280here) isBMP280here = bmp.begin(0x77);
    if (isBMP280here) {
      bmp.setSampling(Adafruit_BMP280::MODE_NORMAL, Adafruit_BMP280::SAMPLING_X16,
                      Adafruit_BMP280::SAMPLING_X16, Adafruit_BMP280::FILTER_X4,
                      Adafruit_BMP280::STANDBY_MS_500);
      bmp_temp = bmp.getTemperatureSensor();
      bmp_pressure = bmp.getPressureSensor();
    }
    isAHT20here = aht.begin(&Wire);
    if (isAHT20here) {
      aht_temp = aht.getTemperatureSensor();
      aht_humidity = aht.getHumiditySensor();
    }
#endif
#if SENSOR_MODE == 0
  isAHT20here = aht.begin(&Wire);
  if (isAHT20here) {
    aht_temp = aht.getTemperatureSensor();
    aht_humidity = aht.getHumiditySensor();
  }
#endif
  // SENSOR_MODE == 3: датчика нет - инициализация пропускается

  // ---------- конец инициализации датчиков ----------

}