// библиотеки
#include "timer2Minim.h"
#include <GyverButton.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#if SENSOR_MODE == 2
  #include <Adafruit_BME280.h>
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
  Adafruit_BME280 bme;
  Adafruit_Sensor *bme_temp = nullptr;
  Adafruit_Sensor *bme_pressure = nullptr;
  Adafruit_Sensor *bme_humidity = nullptr;
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
#define MEASURE_PERIOD      2000                  // период обновления показаний
timerMinim autoTimer(ALARM_SH_TIME);              // таймер автоматического выхода из режимов
timerMinim measurementsTimer(MEASURE_PERIOD);     // таймер обновления показаний
timerMinim dcdcFaultBlinkTimer(300);              // таймер мигания красным при аварии DC-DC (FAULT)
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
                               DCDCST_FAULT };// слишком низкое/высокое HV, генератор выключен без автоповтора
volatile DCDC_STATE_T dcdcState = DCDCST_NORMAL;
unsigned long dcdcHighDutyStart = 0;
unsigned long dcdcWarningStart = 0;
unsigned long dcdcOverVoltageStart = 0;
// =========================================
const uint8_t minduty = 10;                       // защита от выключения
uint8_t r_duty;                                   // актуальная скважность ШИМ анодного напряжения
int8_t startup_delay = 5;                         // задержка в применении нового значения ШИМ после запуска

/* макроопределения битовых операций */
#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit)) 
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))


volatile boolean dotFlag = false;                 // признак фазы внутри половины секунды

int8_t hrs, mins, secs;                           // часы, минуты, секунды

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

/* всё про подсветку */
byte backlColors[3] = { BACKLR, BACKLG, BACKLB };
byte backlColor;
int gradientPos;                                  // позиция в цветовом градиенте радуги (0-767) для BACKL_MODE BL_GRADIENT
boolean colorBreathing = true;                    // "дыхание" вкл/выкл для текущего цвета в режимах BL_RED/BL_BLUE/BL_GREEN
byte rainbowSpeedIndex = 3;                       // индекс в RAINBOW_TIME_PRESETS (по умолчанию - 8000 мс, "обычно")
boolean forceDirectTime;                          // после этого перерисовать время напрямую, без эффекта перелистывания

/* всё про точку */
DOT_MODES dotMode;                                // текущий установленный режим работы точки
boolean dotBrightFlag, dotBrightDirection;        // индикатор времени начала отображения точки, точка по яркости возрастает/уменьшается
byte dotMaxBright = DOT_BRIGHT;                   // максимальная яркость точки
int dotBrightCounter;                             // текущая яркость точки в процессе эффекта
byte dotBrightStep;                               // шаг изменения яркости точки в нормальных условиях
byte dotNumBlink;                                 // количество включений точки за период

/* данные выбранного датчика */
#if SENSOR_MODE == 2
sensors_event_t bme_temp_event, bme_pressure_event, bme_humidity_event;
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
inline bool readSensorTemperature(float &temperature) {
  temperature = NAN;
#if SENSOR_MODE == 2
  if (isBME280here && bme_temp) { bme_temp->getEvent(&bme_temp_event); temperature = bme_temp_event.temperature; }
#elif SENSOR_MODE == 1
  if (isAHT20here && aht_temp) { aht_temp->getEvent(&aht_temp_event); temperature = aht_temp_event.temperature; }
  else if (isBMP280here && bmp_temp) { bmp_temp->getEvent(&temp_event); temperature = temp_event.temperature; }
#elif SENSOR_MODE == 0
  if (isAHT20here && aht_temp) { aht_temp->getEvent(&aht_temp_event); temperature = aht_temp_event.temperature; }
#endif
  // SENSOR_MODE == 3: датчика нет - temperature остаётся NAN, функция вернёт false
  if (!isnan(temperature)) {
    // Пользовательская калибровочная поправка температуры.
    temperature += TEMP_CORRECTION;
  }
  return !isnan(temperature);
}

inline bool readSensorHumidityInteger(int &h) {
#if SENSOR_MODE == 3
  return false;                                   // датчика нет
#else
  float rh = NAN;
#if SENSOR_MODE == 2
  if (isBME280here && bme_humidity) { bme_humidity->getEvent(&bme_humidity_event); rh = bme_humidity_event.relative_humidity; }
#elif SENSOR_MODE == 1
  if (isAHT20here && aht_humidity) { aht_humidity->getEvent(&aht_humidity_event); rh = aht_humidity_event.relative_humidity; }
#elif SENSOR_MODE == 0
  if (isAHT20here && aht_humidity) { aht_humidity->getEvent(&aht_humidity_event); rh = aht_humidity_event.relative_humidity; }
#endif
  if (isnan(rh) || rh < 0.0f || rh > 100.0f) return false;
  h = (int)(rh + 0.5f);
  if (h > 100) h = 100;
  return true;
#endif
}

inline bool readSensorPressureMMHg(int &pmm) {
#if SENSOR_MODE == 3
  return false;                                   // датчика нет
#else
  float pressurePa = NAN;
#if SENSOR_MODE == 2
  if (isBME280here && bme_pressure) { bme_pressure->getEvent(&bme_pressure_event); pressurePa = bme_pressure_event.pressure; }
#elif SENSOR_MODE == 1
  if (isBMP280here && bmp_pressure) { bmp_pressure->getEvent(&pressure_event); pressurePa = pressure_event.pressure; }
#endif
  if (isnan(pressurePa) || pressurePa <= 0.0f) return false;
  pmm = (int)(pressurePa / 1.333223684f + 0.5f);
  if (pmm < 0) pmm = 0;
  if (pmm > 999) pmm = 999;
  return true;
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
#if (BOARD_TYPE == 0) || (BOARD_TYPE == 1) || (BOARD_TYPE == 2) || (BOARD_TYPE == 3) || (BOARD_TYPE == 6)
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
                     SHHUM};                      // (6) отображение влажности
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
                     GRADPOS,                     // (9-10) позиция в градиенте радуги (int, 2 байта)
                     BLBREATH,                    // (11) "дыхание" вкл/выкл для текущего цвета
                     RBSPEED };                   // (12) индекс скорости радуги (RAINBOW_TIME_PRESETS)

boolean currentDigit = false;
int8_t changeHrs, changeMins;
boolean lampState = false;
#if (NUMTUB == 6)
byte anodeStates = 0x3F;                          // в оригинальном скетче было массивом логических переменных
                                                  // заменено на байт, биты (начиная с 0), которого определяют
                                                  // необходимость включения разрядов (начиная со старшего)
#else
byte anodeStates = 0x0F;                          // в оригинальном скетче было массивом логических переменных
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


/* Быстрое управление ШИМ Ардуино (аналог analogWrite). Работает только на пинах с аппаратной поддержкой.
 *  Входные параметры:
 *    byte pin: номер пина в нотации Ардуино, подлежащий изменению
 *    byte duty: относительное значение длительности импульса на выбранном pin
 *  Выходные параметры: нет
 */
void setPWM(byte pin, byte duty) {
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
