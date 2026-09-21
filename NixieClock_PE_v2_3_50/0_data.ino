#include "project_config.h"   // v2.3.34: настройки и распиновка (не зависит от порядка вкладок)
// библиотеки
#include "timer2Minim.h"
#include <GyverButton.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <util/atomic.h>
#include <avr/wdt.h>

// v2.3.31: ранняя очистка WDT/reset-флага для AVR. Это снижает риск
// бесконечного reset-loop при watchdog-сбросе, особенно с не самым новым bootloader.
uint8_t mcusr_mirror __attribute__((section(".noinit")));
void wdtEarlyDisable(void) __attribute__((naked)) __attribute__((used)) __attribute__((section(".init3")));
void wdtEarlyDisable(void) {
  mcusr_mirror = MCUSR;
  MCUSR = 0;
  wdt_disable();
}
#if SENSOR_MODE == 2
  // v2.3.47: собственный компактный драйвер вместо Adafruit_BME280
  // (см. bme280_mini.h): -5...6 КБ Flash, библиотеки Adafruit для BME280 не нужны.
  #include "bme280_mini.h"
#elif SENSOR_MODE == 1
  #include <Adafruit_BMP280.h>
  #include <Adafruit_AHTX0.h>
#elif SENSOR_MODE == 0
  #include <Adafruit_AHTX0.h>
#elif SENSOR_MODE == 3
  // датчика нет вообще - библиотеки не подключаются, показ параметров по кнопке исключён
#else
  #error "SENSOR_MODE: 0=AHT20, 1=AHT20+BMP280, 2=BME280, 3=без датчика"
#endif

// периферия
RTC_DS3231 rtc;                                   // создаём класс управления DS3231 (используем интерфейс I2C)
#if SENSOR_MODE == 2
  BME280Mini bme;
#endif
#if SENSOR_MODE == 1
  Adafruit_BMP280 bmp;
  Adafruit_AHTX0 aht;
  Adafruit_Sensor *bmp_temp = nullptr;
  Adafruit_Sensor *bmp_pressure = nullptr;
  Adafruit_Sensor *aht_temp = nullptr;
  Adafruit_Sensor *aht_humidity = nullptr;
#elif SENSOR_MODE == 0
  Adafruit_AHTX0 aht;
  Adafruit_Sensor *aht_temp = nullptr;
  Adafruit_Sensor *aht_humidity = nullptr;
#endif

// таймеры
timerMinim dotBrightTimer(DOT_TIMER);             // таймер шага яркости точки
timerMinim backlBrightTimer(30);                  // таймер шага яркости подсветки
timerMinim almTimer((long)ALM_TIMEOUT * 1000);    // таймер времени звучания будильника
timerMinim flipTimer(FLIP_SPEED[FLIP_EFFECT]);    // таймер эффектов
timerMinim glitchTimer(1000);                     // таймер глюков
timerMinim blinkTimer(500);                       // таймер моргания
#define L16   127                                 // длительность 1/16 в мс при темпе 118 четвёртых в минуту
timerMinim noteTimer(L16);                        // таймер длительности ноты
timerMinim eshowTimer(300);                       // таймер демонстрации номера эффекта цифр
timerMinim blShowTimer(2000);                     // таймер демонстрации номера режима подсветки (дольше, чем у эффекта цифр)
boolean blShowActive;                             // сейчас показывается номер режима подсветки (а не эффекта цифр)
#define ALARM_SH_TIME       5000                  // время отображения установленного будильника
#define TEMP_SH_TIME        6000                  // время показа температуры
#define ATMOSPHERE_SH_TIME  6000                  // время показа давления
#define HUMIDITY_SH_TIME     6000                  // время показа влажности
// v2.3.34: после показа параметров датчика дополнительно показывается дата
// (если автопоказ даты разрешён - dateShowEnabled / пункт меню DATESHOW).
#define SENSOR_DATE_SH_TIME  3000                  // время показа даты после датчиков, мс
#define MEASURE_PERIOD      2000                  // период обновления показаний
timerMinim autoTimer(ALARM_SH_TIME);              // таймер автоматического выхода из режимов
timerMinim measurementsTimer(MEASURE_PERIOD);     // таймер обновления показаний
timerMinim dcdcFaultBlinkTimer(300);              // таймер мигания красным при аварии DC-DC (FAULT)
timerMinim dcdcWarningBlinkTimer(700);            // v2.3.31: редкая индикация WARNING
boolean dcdcWarningBlinkState = false;
timerMinim rtcLostPowerBlinkTimer(300);          // v2.3.31: индикация потери питания RTC
timerMinim rtcLostPowerTimer(6000);              // длительность предупреждения RTC
boolean rtcLostPowerBlinkState = false;
timerMinim setModeTimer(60000);                  // v2.3.31: таймаут SETTIME/SETALARM без действий
unsigned long setModeLastAction = 0;             // v2.3.31: независимый таймаут настройки через millis()
timerMinim burnTimer(BURN_TIME);                  // v2.3.31: неблокирующий anti-poisoning
boolean burnActive = false;
byte burnLoop = 0;
byte burnDigit = 0;
boolean dcdcFaultBlinkState = false;              // текущая фаза мигания (вкл/выкл)

// кнопки
GButton btnSet(BTN_NO_PIN, LOW_PULL, NORM_OPEN);  // инициализируем кнопку Set ("М")
GButton btnL(BTN_NO_PIN, LOW_PULL, NORM_OPEN);    // инициализируем кнопку Down (Left) ("минус")
GButton btnR(BTN_NO_PIN, LOW_PULL, NORM_OPEN);    // инициализируем кнопку Up (Right) ("плюс")
GButton btnA(ALARM_STOP, LOW_PULL, NORM_OPEN);    // инициализируем кнопку Alarm ("сенсор")

// переменные
volatile int8_t indiDimm[NUMTUB];                 // величина диммирования (0-24)
volatile int8_t indiCounter[NUMTUB];              // счётчик каждого индикатора (0-24)
volatile int8_t indiDigits[NUMTUB];               // цифры, которые должны показать индикаторы (0-10)
volatile int8_t curIndi;                          // текущий индикатор (0-5)

// синхронизация
const unsigned int SQW_FREQ = 8192;               // частота SQW сигнала
volatile unsigned int SQW_counter = 0;            // новый таймер
volatile boolean halfsecond = false;              // полсекундный таймер
// v2.3.31: счётчик SQW используется как аппаратный watchdog мультиплексирования.
volatile uint32_t sqwEdgeCounter = 0;
uint32_t sqwLastSeenCounter = 0;
unsigned long sqwLastSeenMillis = 0;
const uint16_t SQW_TIMEOUT_MS = 1000;

/* мелодия - Бетховен, "Ода к радости" (9-я симфония, финал).
 * Общественное достояние (Бетховен умер в 1827; с 1972 года это ещё и
 * официальный гимн Евросоюза - собственно поэтому мелодия и выбрана:
 * простая, без скачков, хорошо узнаётся даже на пищалке с квадратной
 * волной). Две фразы (вопрос-ответ), 28 нот всего.
 * Ноты в той же низкой октаве (B2-G3), что и в предыдущей, точно
 * рабочей мелодии - более высокая октава на этом железе давала полную
 * потерю звука (см. историю версий), поэтому сюда взяты те же самые,
 * уже проверенные значения делителя (21-31).
 * Это упрощённая транскрипция для пищалки, не нотный оригинал
 * один-в-один - при желании можно подстроить на слух.
 */
                                                  // мелодия (длительность импульса, длительность паузы - в циклах таймера, длительность ноты - в мс)
// v2.3.31: мелодия восстановлена без изменения относительно v2.3.24.
// Не меняем частоту нот, чтобы сохранить исходное звучание часов.
const uint8_t NotePrescalerHigh[] = { 25, 25, 23, 21, 21, 23, 25, 28, 31, 31, 28, 25, 25, 28,
                                       28, 28, 25, 31, 28, 25, 23, 25, 28, 31, 28, 25, 28, 31 };
const uint8_t NotePrescalerLow[] =  { 25, 25, 23, 21, 21, 23, 25, 28, 31, 31, 28, 25, 25, 28,
                                       28, 28, 25, 31, 28, 25, 23, 25, 28, 31, 28, 25, 28, 31 };
const uint16_t NoteLength[] =       { L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*4, L16*6,
                                       L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*3, L16*4, L16*6 };
                                                  // длительность мелодии в нотах
const uint8_t notecounter = sizeof(NotePrescalerHigh);
volatile unsigned int note_num = 0;               // номер ноты в мелодии
volatile unsigned int note_count = 0;             // фаза сигнала
volatile boolean note_up_low = false;             // true - высокий уровень, false - низкий уровень
volatile boolean note_ip = false;                 // true - сигнал звучит, false - сигнал не звучит

/* регулировка напряжения */
const uint8_t iduty = 10;                         // период интегрирования ошибки напряжения
uint8_t idcounter = 0;                            // текущий период интегрирования
const int8_t maxerrduty = 10;                     // максимальное значение интегрированной ошибки напряжения
int duty_delta = 0;                               // текущая интегральная ошибка
const int nominallevel = 550;                     // номинальное значение напряжения
// ===== ПРОГРАММНАЯ ЗАЩИТА DC-DC =====
// Жёсткий предел PWM. Начальное DUTY автоматически ограничивается этим значением.
const uint8_t maxduty = 180;
// Если PWM долго находится около максимума, а напряжение остаётся низким,
// считаем, что есть неисправность обратной связи/нагрузка и выключаем генератор.
const uint8_t DCDC_PROTECT_DUTY = 175;
const int DCDC_PROTECT_VOLTAGE = 350;       // ADC A6; ниже этого уровня при большом PWM — авария (FAULT)
const uint16_t DCDC_PROTECT_TIME_MS = 5000; // время до аварийного отключения
const int DCDC_OVERVOLTAGE = 700;            // аварийное отключение при слишком высоком A6 (FAULT)
const uint16_t DCDC_OVERVOLTAGE_TIME_MS = 300;  // время удержания перенапряжения до срабатывания (было 100 мс - слишком
                                                 // чувствительно к кратковременным выбросам от смены числа горящих ламп)
// Более мягкий порог: HV просело, но ещё в допустимых пределах - только предупреждение,
// генератор продолжает работать в обычном режиме регулировки.
const uint8_t DCDC_WARNING_DUTY = 165;       // при этом и большем PWM следим за просадкой HV
const int DCDC_WARNING_VOLTAGE = 450;        // ADC A6; ниже этого при большом PWM - WARNING
const uint16_t DCDC_WARNING_TIME_MS = 2000;  // время до фиксации WARNING (фильтр кратковременных просадок)

enum DCDC_STATE_T : uint8_t { DCDCST_NORMAL,  // всё в норме
                               DCDCST_WARNING,// HV просело, но ещё допустимо, генератор работает
                               DCDCST_FAULT };// авария; после нескольких неудачных повторных запусков остаётся FAULT
volatile DCDC_STATE_T dcdcState = DCDCST_NORMAL;
unsigned long dcdcHighDutyStart = 0;
unsigned long dcdcWarningStart = 0;
unsigned long dcdcOverVoltageStart = 0;
unsigned long dcdcStartupStart = 0;
unsigned long dcdcRetryAt = 0;
uint8_t dcdcRetryCount = 0;
const uint8_t DCDC_MAX_RETRIES = 3;
const uint16_t DCDC_RETRY_DELAY_MS = 500;
const uint16_t DCDC_STARTUP_TIME_MS = 5000;
const uint8_t DCDC_STARTUP_STEP = 1;
const uint16_t DCDC_STARTUP_STEP_MS = 20;
unsigned long dcdcLastStartupStep = 0;
// =========================================
const uint8_t minduty = 10;                       // безопасная минимальная скважность
uint8_t r_duty;                                   // актуальная скважность ШИМ анодного напряжения
// v2.3.31: startup_delay больше не зависит от calculateTime(); DCDC сам отсчитывает
// время мягкого старта по millis(), поэтому защита работает и во время self-test.
int8_t startup_delay = 5;

/* макроопределения битовых операций */
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit)) 
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))


volatile boolean dotFlag = false;                 // признак фазы внутри половины секунды

int8_t hrs, mins, secs;                           // часы, минуты, секунды
#if (NUMTUB == 6)
uint8_t changeDay = 1;                             // день при настройке даты (00 = отключить показ даты)
uint8_t changeMonth = 1;                           // месяц при настройке даты (00 = отключить показ даты)
uint8_t changeYear = FW_DATE_YEAR_MIN;             // год FW_DATE_YEAR_MIN..99 при настройке даты
// v2.3.32: сама дата берётся из RTC (rtc.now()), отдельная копия даты не хранится.
boolean dateShowEnabled = true;                    // автоматический показ даты разрешён (EEPROM: DATESHOW)
uint8_t dateCycleSeconds = 0;                      // счётчик 0..99 с до автоматического показа
#endif

/* всё про будильник */
#define ALARM_RESET     true                     // будильник не установлен
#define ALARM_SET       false                      // будильник установлен

#define ALARM_WAIT      false                     // будильник не сработал
#define ALARM_FIRED     true                      // будильник сработал

#define ALARM_WAIT_1    false                     // будильник ещё не сработал
#define ALARM_IN_MIN    true                      // будильник сработал меньше минуты назад

#define ALARM_NOREQ     false                     // запроса на проверку будильника нет
#define ALARM_REQ       true                      // запрос на проверку будильника

int8_t alm_hrs, alm_mins;                         // сохранение времени будильника
boolean alm_set;                                  // будильник установлен? (берётся из памяти)
boolean alm_flag = ALARM_WAIT;                    // будильник сработал?
boolean alm_fired = ALARM_WAIT_1;                 // запрет повторного срабатывания будильника в ту же минуту
boolean alm_request = ALARM_NOREQ;                // признак необходимости проверить совпадение времени с будильником
boolean snoozeActive = false;                      // v2.3.31: отложенный будильник
int8_t snoozeHrs = 0, snoozeMins = 0;

/* всё про подсветку */
byte backlColors[3] = { BACKLR, BACKLG, BACKLB };
byte backlColor;
int gradientPos;                                  // v2.3.46: пользовательская позиция 0-255 для всей палитры
boolean colorBreathing = true;                    // "дыхание" вкл/выкл для текущего цвета в режимах BL_RED/BL_BLUE/BL_GREEN
byte rainbowSpeedIndex = 3;                       // индекс в RAINBOW_TIME_PRESETS (по умолчанию - 8000 мс, "обычно")
boolean forceDirectTime;                          // после этого перерисовать время напрямую, без эффекта перелистывания
boolean rtcLostPowerFlag = false;                 // RTC терял питание и время было восстановлено из времени сборки
boolean tempNegative = false;                      // знак температуры для индикации

/* всё про точку */
DOT_MODES dotMode;                                // текущий установленный режим работы точки
boolean dotBrightFlag, dotBrightDirection;        // индикатор времени начала отображения точки, точка по яркости возрастает/уменьшается
byte dotMaxBright = DOT_BRIGHT;                   // максимальная яркость точки
int dotBrightCounter;                             // текущая яркость точки в процессе эффекта
byte dotBrightStep;                               // шаг изменения яркости точки в нормальных условиях
byte dotNumBlink;                                 // количество включений точки за период

/* данные выбранного датчика */
#if SENSOR_MODE == 2
boolean isBME280here = false;
#endif
#if SENSOR_MODE == 1
sensors_event_t temp_event, pressure_event;
sensors_event_t aht_temp_event, aht_humidity_event;
boolean isBMP280here = false;
boolean isAHT20here = false;
#elif SENSOR_MODE == 0
sensors_event_t aht_temp_event, aht_humidity_event;
boolean isAHT20here = false;
#endif
// SENSOR_MODE == 3: датчика нет, никаких переменных не заводим

// Унифицированное чтение датчиков. Остальная программа не зависит от типа датчика.
// v2.3.47: температура передаётся целым числом в десятых долях градуса
// (без float), поправка TEMP_CORRECTION уже учтена. Для BME280 (SENSOR_MODE 2)
// вся цепочка измерений целочисленная; float остаётся только в режимах 0/1,
// где его возвращают библиотеки AHT20/BMP280.
static const int TEMP_CORR_X100 = (int)((TEMP_CORRECTION) * 100 + ((TEMP_CORRECTION) >= 0 ? 0.5 : -0.5));

// t100 - температура в сотых долях градуса, без поправки -> десятые доли с поправкой
inline int tempX100ToX10(int t100) {
  t100 += TEMP_CORR_X100;
  return (t100 + (t100 >= 0 ? 5 : -5)) / 10;      // округление "от нуля", как раньше
}

inline bool readSensorTemperature(int &t10) {
#if SENSOR_MODE == 2
  if (!isBME280here || !bme.measure()) return false;
  t10 = tempX100ToX10(bme.temp100);
  return true;
#elif (SENSOR_MODE == 1) || (SENSOR_MODE == 0)
  float tf = NAN;
  if (isAHT20here && aht_temp) { aht_temp->getEvent(&aht_temp_event); tf = aht_temp_event.temperature; }
#if SENSOR_MODE == 1
  else if (isBMP280here && bmp_temp) { bmp_temp->getEvent(&temp_event); tf = temp_event.temperature; }
#endif
  if (isnan(tf)) return false;
  t10 = tempX100ToX10((int)(tf * 100.0f + (tf >= 0 ? 0.5f : -0.5f)));
  return true;
#else
  (void)t10;
  return false;                                   // SENSOR_MODE == 3: датчика нет
#endif
}

// v2.3.31 FIX: Adafruit RTClib в установленной у пользователя версии
// не содержит RTC_DS3231::getTemperature(). Поэтому при SENSOR_MODE=3
// читаем встроенный термодатчик DS3231 напрямую через регистры 0x11/0x12.
// DS3231: 0x11 = signed integer °C, 0x12 = дробная часть с шагом 0.25 °C.
inline bool readDS3231Temperature(int &t10) {
  Wire.beginTransmission(0x68);
  Wire.write((uint8_t)0x11);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((uint8_t)0x68, (uint8_t)2) != 2) return false;

  int8_t whole = (int8_t)Wire.read();
  uint8_t frac = Wire.read();
  t10 = tempX100ToX10((int)whole * 100 + (frac >> 6) * 25);
  return true;
}

inline bool readDisplayTemperature(int &t10) {
#if SENSOR_MODE == 3
  return readDS3231Temperature(t10);
#else
  return readSensorTemperature(t10);
#endif
}

// ok == false (ошибка чтения) - на лампах 000 вместо старого значения.
inline void showDisplayTemperature(bool ok, int t) {
  if (!ok) {
    tempNegative = false;
    indiDigits[0] = 0;
    indiDigits[1] = 0;
    indiDigits[2] = 0;
    return;
  }

  tempNegative = (t < 0);
  if (t < 0) t = -t;
  if (t > 999) t = 999;

  indiDigits[0] = (byte)(t / 100);
  indiDigits[1] = (byte)((t / 10) % 10);
  indiDigits[2] = (byte)(t % 10);

  // У стандартного 10-катодного декодера нет отдельного символа "-".
  // Поэтому отрицательная температура кодируется тройным миганием точки,
  // положительная — постоянной точкой. Сам знак в переменной tempNegative не теряется.
  dotSetMode(tempNegative ? DM_THREE : DM_FULL);
}

inline bool readSensorHumidityInteger(int &h) {
#if SENSOR_MODE == 2
  if (!isBME280here || !bme.measure()) return false;
  h = (int)((bme.hum1024 + 512UL) >> 10);         // %RH * 1024 -> целые %, с округлением
  if (h > 100) h = 100;
  return true;
#elif (SENSOR_MODE == 1) || (SENSOR_MODE == 0)
  float rh = NAN;
  if (isAHT20here && aht_humidity) { aht_humidity->getEvent(&aht_humidity_event); rh = aht_humidity_event.relative_humidity; }
  if (isnan(rh) || rh < 0.0f || rh > 100.0f) return false;
  h = (int)(rh + 0.5f);
  if (h > 100) h = 100;
  return true;
#else
  (void)h;
  return false;                                   // датчика нет
#endif
}

inline bool readSensorPressureMMHg(int &pmm) {
#if SENSOR_MODE == 2
  if (!isBME280here || !bme.measure() || bme.pressPa == 0) return false;
  // мм рт. ст. = Па / 133.3224 ~ Па * 7865 / 2^20 (ошибка < 0.001 %, помещается в 32 бита)
  uint32_t mm = (bme.pressPa * 7865UL + 524288UL) >> 20;
  pmm = (mm > 999UL) ? 999 : (int)mm;
  return true;
#elif SENSOR_MODE == 1
  float pressurePa = NAN;
  if (isBMP280here && bmp_pressure) { bmp_pressure->getEvent(&pressure_event); pressurePa = pressure_event.pressure; }
  if (isnan(pressurePa) || pressurePa <= 0.0f) return false;
  pmm = (int)(pressurePa / 1.333223684f + 0.5f);
  if (pmm < 0) pmm = 0;
  if (pmm > 999) pmm = 999;
  return true;
#else
  (void)pmm;
  return false;                                   // SENSOR_MODE 0/3: давления нет
#endif
}
boolean isFreeze = false;

boolean chBL = false;                             // для обнаружения необходимости смены подсветки

/* переменнные из исходного скетча */
boolean changeFlag;
boolean blinkFlag;
byte indiMaxBright = INDI_BRIGHT;
byte backlMaxBright[3] = { BACKL_BRIGHT_R, BACKL_BRIGHT_G, BACKL_BRIGHT_B };  // макс. яркость подсветки по каналам R,G,B (индексы как в backlColors[])
byte rainbowMaxBright = BACKL_BRIGHT_RAINBOW;     // макс. яркость эффекта "радуга" - одна общая для всех цветов
boolean backlBrightFlag, backlBrightDirection, indiBrightDirection;
int backlBrightCounter, indiBrightCounter;
boolean newTimeFlag;
#if HAS_SECONDS
boolean newSecFlag;                               // добавлен для исключения секунд из части эффектов
#endif
boolean flipIndics[NUMTUB];
byte newTime[NUMTUB];
boolean flipInit;
byte startCathode[NUMTUB], endCathode[NUMTUB];
byte slotSpinsLeft[NUMTUB];                       // для эффекта FM_SLOT - оставшееся число "прокруток" по разряду
byte slotCurrentTube;                             // для эффекта FM_SLOT - какой разряд крутится сейчас (по очереди)
byte glitchCounter, glitchMax, glitchIndic;
boolean glitchFlag, indiState;

/* дополнительные переменные */
boolean showFlag = false;                         // признак демонстрации номера эффекта

enum SH_MODES: byte
                   { SHTIME,                      // (0) отображение часов
                     SETTIME,                     // (1) установка часов
                     SHALARM,                     // (2) отображение времени будильника (5 сек)
                     SETALARM,                    // (3) установка времени будильника
                     SHTEMP,                      // (4) отображение температуры
                     SHATM,                       // (5) отображение атмосферного давления
                     SHHUM,                       // (6) отображение влажности
                     SHDATE};                     // (7) отображение даты DDMMYY
SH_MODES curMode = SHTIME;

enum SAVE_PARAMS: byte
                   { FLIPEFF,                     // (0) эффект для цифр
                     LIGHTEFF,                    // (1) эффект для подсветки
                     GLEFF,                       // (2) параметр совместимости
                     VSET,                        // (3) установка стабилизатора напряжения (не используется)
                     ALHOUR,                      // (4) сохранение времени будильника/часы
                     ALMIN,                       // (5) сохранение времени будильника/минуты
                     ALIFSET,                     // (6) сохранение статуса будильника (заведён/сброшен)
                     BLCOLOR,                     // (7) текущий цвет подсветки
                     DOTEFF,                      // (8) включена/отключена секундная точка
                     GRADPOS_RESERVED,            // (9) v2.3.32: зарезервировано (раньше здесь был GRADPOS)
                     BLBREATH,                    // (11) "дыхание" вкл/выкл для текущего цвета
                     RBSPEED,                    // (12) индекс скорости радуги (RAINBOW_TIME_PRESETS)
#if (NUMTUB == 6)
                     DATEDAY,                    // (13) зарезервировано (v2.3.32: не используется, дата берётся из RTC)
                     DATEMONTH,                  // (14) зарезервировано (не используется)
                     DATEYEAR,                   // (15) зарезервировано (не используется)
                     DATESHOW,                   // (16) разрешение автоматического показа даты
#endif
                     GRADPOS                     // v2.3.32: позиция в градиенте радуги (int, 2 байта) - в конце,
                                                 // чтобы не перекрывать соседние однобайтовые параметры
                     };

uint8_t currentDigit = 0; // v2.3.31: 0=часы, 1=минуты, 2=день, 3=месяц, 4=год
int8_t changeHrs, changeMins;
boolean lampState = false;
#if (NUMTUB == 6)
volatile byte anodeStates = 0x3F;                          // в оригинальном скетче было массивом логических переменных
                                                  // заменено на байт, биты (начиная с 0), которого определяют
                                                  // необходимость включения разрядов (начиная со старшего)
#else
volatile byte anodeStates = 0x0F;                          // в оригинальном скетче было массивом логических переменных
                                                  // заменено на байт, биты (начиная с 0), которого определяют
                                                  // необходимость включения разрядов (начиная со старшего)

#endif
byte currentLamp, flipEffectStages;
bool trainLeaving;

const uint8_t CRTgamma[256] PROGMEM = {
  0,    0,    1,    1,    1,    1,    1,    1,
  1,    1,    1,    1,    1,    1,    1,    1,
  2,    2,    2,    2,    2,    2,    2,    2,
  3,    3,    3,    3,    3,    3,    4,    4,
  4,    4,    4,    5,    5,    5,    5,    6,
  6,    6,    7,    7,    7,    8,    8,    8,
  9,    9,    9,    10,   10,   10,   11,   11,
  12,   12,   12,   13,   13,   14,   14,   15,
  15,   16,   16,   17,   17,   18,   18,   19,
  19,   20,   20,   21,   22,   22,   23,   23,
  24,   25,   25,   26,   26,   27,   28,   28,
  29,   30,   30,   31,   32,   33,   33,   34,
  35,   35,   36,   37,   38,   39,   39,   40,
  41,   42,   43,   43,   44,   45,   46,   47,
  48,   49,   49,   50,   51,   52,   53,   54,
  55,   56,   57,   58,   59,   60,   61,   62,
  63,   64,   65,   66,   67,   68,   69,   70,
  71,   72,   73,   74,   75,   76,   77,   79,
  80,   81,   82,   83,   84,   85,   87,   88,
  89,   90,   91,   93,   94,   95,   96,   98,
  99,   100,  101,  103,  104,  105,  107,  108,
  109,  110,  112,  113,  115,  116,  117,  119,
  120,  121,  123,  124,  126,  127,  129,  130,
  131,  133,  134,  136,  137,  139,  140,  142,
  143,  145,  146,  148,  149,  151,  153,  154,
  156,  157,  159,  161,  162,  164,  165,  167,
  169,  170,  172,  174,  175,  177,  179,  180,
  182,  184,  186,  187,  189,  191,  193,  194,
  196,  198,  200,  202,  203,  205,  207,  209,
  211,  213,  214,  216,  218,  220,  222,  224,
  226,  228,  230,  232,  233,  235,  237,  239,
  241,  243,  245,  247,  249,  251,  253,  255,
};


/* Гамма-характеристика светимости из программной памяти
 *  Входные параметры:
 *    byte val: абсолютное значение светимости
 *  Выходные параметры:
 *    byte: скорректированное значение светимости в соответствии с кривой гамма
 */
byte getPWM_CRT(byte val) {
  return pgm_read_byte(&(CRTgamma[val]));
}

/* Быстрое управление выходными пинами Ардуино (аналог digitalWrite)
 *  Входные параметры:
 *    byte pin: номер пина в нотации Ардуино, подлежащий изменению
 *    byte х: значение, в которое устанавливается pin (0 или 1)
 *  Выходные параметры: нет
 */
void setPin(byte pin, byte x) {
  // v2.3.31: setPin вызывается и из SQW ISR, и из loop().
  // ATOMIC_BLOCK не допускает разрыва последовательности bit/TCCR/PORT операций.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
  switch (pin) {                                  // откл pwm
    case 3:                                       // 2B
      bitClear(TCCR2A, COM2B1);
      break;
    case 5:                                       // 0B
      bitClear(TCCR0A, COM0B1);
      break;
    case 6:                                       // 0A
      bitClear(TCCR0A, COM0A1);
      break;
    case 9:                                       // 1A
      bitClear(TCCR1A, COM1A1);
      break;
    case 10:                                      // 1B
      bitClear(TCCR1A, COM1B1);
      break;
    case 11:                                      // 2A
      bitClear(TCCR2A, COM2A1);
      break;
  }

  x = ((x != 0) ? 1 : 0);
  if (pin < 8) bitWrite(PORTD, pin, x);
  else if (pin < 14) bitWrite(PORTB, (pin - 8), x);
  else if (pin < 20) bitWrite(PORTC, (pin - 14), x);
  else return;
  }
}


/* Быстрое управление ШИМ Ардуино (аналог analogWrite). Работает только на пинах с аппаратной поддержкой.
 *  Входные параметры:
 *    byte pin: номер пина в нотации Ардуино, подлежащий изменению
 *    byte duty: относительное значение длительности импульса на выбранном pin
 *  Выходные параметры: нет
 */
void setPWM(byte pin, byte duty) {
  // v2.3.31: аппаратные регистры PWM также меняются из loop(),
  // поэтому изменение COM/OCR выполняется атомарно.
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
  if (duty == 0) setPin(pin, LOW);
  else {
    switch (pin) {
      case 5:
        bitSet(TCCR0A, COM0B1);
        OCR0B = duty;
        break;
      case 6:
        bitSet(TCCR0A, COM0A1);
        OCR0A = duty;
        break;
      case 10:
        bitSet(TCCR1A, COM1B1);
        OCR1B = duty;
        break;
      case 9:
        bitSet(TCCR1A, COM1A1);
        OCR1A = duty;
        break;
      case 3:
        bitSet(TCCR2A, COM2B1);
        OCR2B = duty;
        break;
      case 11:
        bitSet(TCCR2A, COM2A1);
        OCR2A = duty;
        break;
      default:
        break;
    }
  }
  }
}
