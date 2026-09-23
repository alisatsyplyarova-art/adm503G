#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
/* Структурная функция начальной установки
 *  Входные параметры: нет
 *  Выходные параметры: нет
 */
void setup() {
  // v2.3.31: WDT мог оставить старый reset-флаг после watchdog-сброса.
  // Сначала гарантированно выключаем WDT, затем включим его в конце setup().
  MCUSR = 0;
  wdt_disable();

  // I2C запускаем до RTC и сразу включаем аппаратный timeout.
  // Это предотвращает вечные while() при зависшем DS3231/шине.
  Wire.begin();
  Wire.setWireTimeout(25000, true);

  // случайное зерно для генератора случайных чисел
  // (v2.3.47: только если random() вообще используется - глюки или разброс FM_SLOT)
#if GLITCH_ENABLED || (SLOT_JITTER_CYCLES > 0)
  randomSeed(analogRead(6) + analogRead(7));
#endif

  // v2.3.51: какая кнопка удержана при включении: "М" - сброс настроек, "плюс" - настройка
  // дневного/ночного режима, "минус" - стартовый тест. Читается ОДНО значение A7.
  const uint8_t bootKey = readBootKey();

#if (NUMTUB == 6)
  // проверяем сразу при включении: зажата ли кнопка "минус" -
  // если да, после инициализации запустится стартовый тест
  boolean testRequested = startupTestRequested();
#endif

  // настройка пинов на вход
  pinMode(ALARM_STOP, INPUT);
  pinMode(A7, INPUT);                                   // v2.3.31 FINAL: явный вход резистивной клавиатуры

  // настройка пинов на выход
  pinMode(DECODER0, OUTPUT);
  pinMode(DECODER1, OUTPUT);
  pinMode(DECODER2, OUTPUT);
  pinMode(DECODER3, OUTPUT);
  pinMode(KEY0, OUTPUT);
  pinMode(KEY1, OUTPUT);
  pinMode(KEY2, OUTPUT);
  pinMode(KEY3, OUTPUT);
#if HAS_SECONDS
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

  // v2.3.51: "М" при включении - сброс настроек. Выполняется ДО чтения EEPROM ниже.
  // Подсветка работает без высокого напряжения: красный -> зелёный -> синий по 2 с,
  // при удержании до конца - сброс (белая вспышка), раньше отпустили - ничего.
  if (bootKey == BOOTKEY_SET) bootFactoryReset();
  btnSet.setTimeout(400);                         // установка параметров библиотеки реагирования на кнопки
  btnSet.setDebounce(90);                         // защитный период от дребезга
  btnL.setTimeout(400);                           // v2.3.31: одинаковая задержка HOLD для +/-
  btnR.setTimeout(400);                           // v2.3.31: одинаковая задержка HOLD для +/-
  btnR.setDebounce(90);                           // защитный период от дребезга
  btnR.setStepTimeout(GRADIENT_STEP_TIME);        // интервал повтора isStep() при удержании (режим "градиент")
  btnL.setDebounce(90);                           // защитный период от дребезга

 // ---------- RTC -----------
  rtc.begin();
  // v2.3.32: RTC синхронизируется со временем компьютера (время компиляции
  // скетча + 10 с на загрузку) ОДИН раз после каждой новой прошивки.
  // Раньше adjust() выполнялся при каждом включении/сбросе и затирал
  // выставленные вручную время и дату. Метка сборки хранится в EEPROM.
  bool rtcHadLostPower = rtc.lostPower();
  DateTime compileTime(F(__DATE__), F(__TIME__));
  const uint32_t buildStamp = compileTime.unixtime();
  uint32_t savedBuildStamp = 0;
  EEPROM.get(FW_EEPROM_BUILD_STAMP_ADDR, savedBuildStamp);
  if (rtcHadLostPower || savedBuildStamp != buildStamp) {
    rtc.adjust(DateTime(buildStamp + 10));
    EEPROM.put(FW_EEPROM_BUILD_STAMP_ADDR, buildStamp);
  }
  // v2.3.51: признак "время не выставлено" хранится в EEPROM: rtc.adjust() выше сбрасывает
  // флаг потери питания в DS3231, и при следующем включении его уже не видно.
  // Снимается сохранением времени в режиме SETTIME.
  if (rtcHadLostPower) {
    timeNotSet = true;
    EEPROM.update(FW_EEPROM_TIMEFLAG_ADDR, 1);
  } else {
    timeNotSet = (EEPROM.read(FW_EEPROM_TIMEFLAG_ADDR) == 1);
  }
  pinMode(RTC_SYNC, INPUT_PULLUP);                // объявляем вход для синхросигнала RTC
                                                  // заставляем входной сигнал генерировать прерывания
  attachInterrupt(digitalPinToInterrupt(RTC_SYNC), RTC_handler, RISING);
  rtc.writeSqwPinMode(DS3231_SquareWave8kHz);     // настраиваем DS3231 для вывода сигнала 8кГц

  // v2.3.31 TEST-FIX: возвращаем исходный делитель ADC /16 (~1 МГц).
  // Причина: A7 используется как резистивная клавиатура, и перед выпуском
  // нужно исключить влияние изменения скорости ADC на пороги кнопок.
  // После калибровки A7 можно отдельно перейти на /32 или /64.
  sbi(ADCSRA, ADPS2);
  cbi(ADCSRA, ADPS1);
  cbi(ADCSRA, ADPS0);
  analogRead(A6);                                 // устранение шума
  analogRead(A7);
  // ------------------
  // v2.3.31: синхронизация без бесконечного while().
  DateTime now = rtc.now();
  const uint8_t startSecond = now.second();
  const unsigned long syncStart = millis();
  while (now.second() == startSecond && millis() - syncStart < 1200) {
    now = rtc.now();
    wdt_reset();
    delay(5);
  }
  secs = now.second();
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    SQW_counter = 0;
    dotFlag = true;
    halfsecPending = 0;
  }
  mins = now.minute();
  hrs = now.hour();
  sqwLastSeenCounter = sqwEdgeCounter;
  sqwLastSeenMillis = millis();
      
  // задаем частоту ШИМ на 9 и 10 выводах 31 кГц
  TCCR1B = TCCR1B & 0b11111000 | 1;               // ставим делитель 1

  // перенастраиваем частоту ШИМ на пинах 3 и 11 для соответствия таймеру 0
  // Пины D3 и D11 - 980 Гц
  TCCR2B = 0b00000011;                            // x32
  TCCR2A = 0b00000001;                            // phase correct

  // EEPROM
  // v2.3.31: версия структуры параметров повышена 103 -> 104.
  // При новой структуре безопаснее вернуть все параметры к значениям
  // по умолчанию, чем читать потенциально несовместимые байты.
  const byte EEPROM_VERSION = 105;
  if (EEPROM.read(1023) != EEPROM_VERSION) {
    EEPROM.put(1023, EEPROM_VERSION);
    EEPROM.put(FLIPEFF, FLIP_EFFECT);
    EEPROM.put(LIGHTEFF, BACKL_MODE);
    EEPROM.put(GLEFF, GLITCH_ALLOWED);
    EEPROM.put(ALHOUR, (int8_t)0);                // v2.3.51: типизированно (было int = 2 байта, затирало соседний адрес)
    EEPROM.put(ALMIN, (int8_t)0);
    EEPROM.put(ALIFSET, false);
    EEPROM.put(BLCOLOR, (byte)1);
    EEPROM.put(DOTEFF, DOT_ALLOWED);
    EEPROM.put(GRADPOS, 0);
    EEPROM.put(BLBREATH, colorBreathing);
    EEPROM.put(RBSPEED, rainbowSpeedIndex);
#if (NUMTUB == 6)
    EEPROM.put(DATESHOW, true);
#endif
  }

  EEPROM.get(FLIPEFF, FLIP_EFFECT);
  if (FLIP_EFFECT >= FLIP_EFFECT_NUM) {
    FLIP_EFFECT = FM_SMOOTH;
    EEPROM.put(FLIPEFF, FLIP_EFFECT);
  }
  EEPROM.get(LIGHTEFF, BACKL_MODE);
  if (BACKL_MODE >= BL_MODE_COUNT) {
    BACKL_MODE = BL_RED;
    EEPROM.put(LIGHTEFF, BACKL_MODE);
  }
  EEPROM.get(GLEFF, GLITCH_ALLOWED);
  EEPROM.get(ALHOUR, alm_hrs);
  if (alm_hrs < 0 || alm_hrs > 23) {
    alm_hrs = 0;
    EEPROM.put(ALHOUR, alm_hrs);
  }
  EEPROM.get(ALMIN, alm_mins);
  if (alm_mins < 0 || alm_mins > 59) {
    alm_mins = 0;
    EEPROM.put(ALMIN, alm_mins);
  }
  EEPROM.get(ALIFSET, alm_set);
  EEPROM.get(BLCOLOR, backlColor);
  if (backlColor >= 3) {
    backlColor = 1;
    EEPROM.put(BLCOLOR, backlColor);
  }
  DOT_ALLOWED = (EEPROM.read(DOTEFF) != 0);
  EEPROM.get(GRADPOS, gradientPos);
  if (gradientPos < 0 || gradientPos > 255) {
    gradientPos = 0;
    EEPROM.put(GRADPOS, gradientPos);
  }
  colorBreathing = (EEPROM.read(BLBREATH) != 0);
  EEPROM.get(RBSPEED, rainbowSpeedIndex);
  if (rainbowSpeedIndex >= RAINBOW_SPEED_COUNT) {
    rainbowSpeedIndex = 3;
    EEPROM.put(RBSPEED, rainbowSpeedIndex);
  }
#if (NUMTUB == 6)
  // v2.3.32: раньше DATESHOW нигде не читался из EEPROM, а сброс даты по
  // умолчанию ошибочно находился внутри проверки rainbowSpeedIndex.
  dateShowEnabled = (EEPROM.read(DATESHOW) != 0);
#endif

  loadNightSettings();                            // v2.3.51: начало дня/ночи (или значения по умолчанию)

  // v2.3.31: безопасный мягкий старт HV. Генератор начинает с minduty,
  // затем DCDCTick() постепенно доводит PWM до целевого DUTY.
  r_duty = minduty;
  setPWM(GEN, r_duty);
  dcdcStartupStart = millis();
  dcdcStartupDone = false;
  dcdcLastStartupStep = millis();
  dcdcState = DCDCST_NORMAL;
  dcdcRetryCount = 0;

  // v2.3.31: НЕ выставляем здесь DUTY=180. Soft-start начинается с minduty.

#if (NUMTUB == 6)
  // v2.3.31: WDT уже активен во время стартового теста.
  wdt_enable(WDTO_2S);
  // запуск стартового теста (если при включении была зажата кнопка "минус")
  // на этом месте уже готовы: пины, прерывание SQW (мультиплексирование
  // индикаторов) и генератор HV - без них тесту нечего было бы показывать
  if (testRequested) {
    runStartupTest();
    // v2.3.50: тест длится ~45 с, loop() всё это время не выполнялся и внутренние
    // часы стояли. Раньше после теста часы отставали на длительность теста до 03:00
    // (или до перезагрузки). Теперь время заново берётся из DS3231.
    syncClockFromRTC();
  }
#endif

#if HAS_SECONDS 
  sendTime(hrs, mins, secs);                      // отправить время на индикаторы
#else
  sendTime(hrs, mins);                            // отправить время на индикаторы
#endif

  changeBright();                                 // изменить яркость согласно времени суток

  // стартовый период глюков
#if GLITCH_ENABLED
  glitchTimer.setInterval(random(GLITCH_MIN * 1000L, GLITCH_MAX * 1000L));
#endif

  // скорость режима при запуске
  flipTimer.setInterval(FLIP_SPEED[FLIP_EFFECT]);

  // I2C уже запущен в начале setup(); timeout 25 ms установлен там.
  delay(20);

  // ---------- Датчик I2C ----------
#if SENSOR_MODE == 2
  // v2.3.47: собственный драйвер (bme280_mini.h). begin() сам перебирает
  // адреса 0x76/0x77 и настраивает режим: FORCED, температура/давление/
  // влажность x2, IIR-фильтр x2 (как в v2.3.31...v2.3.46: читается раз в ~2 с,
  // поэтому NORMAL/x16 избыточен и сильнее греет датчик).
  isBME280here = bme.begin();
#endif
#if SENSOR_MODE == 1
    isBMP280here = bmp.begin(0x76);
    if (!isBMP280here) isBMP280here = bmp.begin(0x77);
    if (isBMP280here) {
      // v2.3.31: BMP280 также переводим в FORCED/x2.
      bmp.setSampling(Adafruit_BMP280::MODE_FORCED, Adafruit_BMP280::SAMPLING_X2,
                      Adafruit_BMP280::SAMPLING_X2, Adafruit_BMP280::FILTER_X2,
                      Adafruit_BMP280::STANDBY_MS_1000);
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

  // v2.3.31: watchdog 2 с. loop() и self-test регулярно сбрасывают его.
  // Если программа зависнет, генератор HV не останется бесконтрольно включённым.
  wdt_enable(WDTO_2S);

  // v2.3.51: "плюс" при включении - режим настройки дневного/ночного режима.
  // Иначе, если время не выставлено, а сейчас не ночь - показать предупреждение.
  if (bootKey == BOOTKEY_PLUS) enterNightSetup();
  else if (timeNotSet && !isNightNow) rtcWarnRequest = true;
}
