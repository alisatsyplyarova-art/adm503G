#ifndef NIXIECLOCK_BME280_MINI_H
#define NIXIECLOCK_BME280_MINI_H

// ============================================================
// v2.3.47: компактный драйвер BME280 (только I2C, только целые числа).
//
// Заменяет Adafruit_BME280 + Adafruit_BusIO + Adafruit_Unified_Sensor +
// SPI: они занимали около 5-6 КБ Flash (64-битная математика, float,
// malloc, Serial). Режим измерения тот же, что был в v2.3.46:
//   FORCED, температура x2, давление x2, влажность x2, IIR-фильтр x2.
// Формулы компенсации - целочисленные 32-битные из даташита Bosch
// (BME280, раздел 4.2.3). Результат:
//   temp100 - температура, 0.01 °C
//   pressPa - давление, Па
//   hum1024 - влажность, %RH * 1024
// Никакой динамической памяти и float не используется.
// ============================================================

#include <Arduino.h>
#include <Wire.h>

class BME280Mini {
  public:
    int16_t  temp100 = 0;
    uint32_t pressPa = 0;
    uint32_t hum1024 = 0;

    // Ищет датчик по адресам 0x76 и 0x77. true - датчик найден и настроен.
    bool begin() {
      for (uint8_t a = 0x76; a <= 0x77; a++) {
        _a = a;
        uint8_t id = 0;
        if (!rd(0xD0, &id, 1) || id != 0x60) continue;     // 0x60 = BME280

        if (!wr(0xE0, 0xB6)) continue;                     // программный сброс
        delay(10);
        uint8_t st = 1;
        for (uint8_t i = 0; i < 20; i++) {                 // ждём копирования калибровки
          if (!rd(0xF3, &st, 1) || !(st & 1)) break;
          delay(5);
        }

        uint8_t c[24], h[7];
        if (!rd(0x88, c, 24) || !rd(0xA1, &_h1, 1) || !rd(0xE1, h, 7)) continue;
        _t1 = u16(c, 0);   _t2 = s16(c, 2);   _t3 = s16(c, 4);
        _p1 = u16(c, 6);   _p2 = s16(c, 8);   _p3 = s16(c, 10);
        _p4 = s16(c, 12);  _p5 = s16(c, 14);  _p6 = s16(c, 16);
        _p7 = s16(c, 18);  _p8 = s16(c, 20);  _p9 = s16(c, 22);
        _h2 = s16(h, 0);
        _h3 = h[2];
        _h4 = ((int16_t)(int8_t)h[3] << 4) | (h[4] & 0x0F);
        _h5 = ((int16_t)(int8_t)h[5] << 4) | (h[4] >> 4);
        _h6 = (int8_t)h[6];

        // Как в v2.3.46: фильтр x2, пауза 1000 мс (в FORCED не используется),
        // влажность x2. Температура/давление x2 и FORCED пишутся в measure().
        if (!wr(0xF5, 0xA4)) continue;
        if (!wr(0xF2, 0x02)) continue;
        return true;
      }
      return false;
    }

    // Один запуск измерения (FORCED) и чтение всех трёх величин.
    // false - ошибка I2C, таймаут или датчик вернул "пропущено".
    bool measure() {
      if (!wr(0xF4, 0x49)) return false;                   // T x2, P x2, FORCED
      delay(12);                                           // типичное время измерения ~16 мс
      uint8_t st = 0x08;
      for (uint8_t i = 0; i < 30; i++) {
        if (!rd(0xF3, &st, 1)) return false;
        if (!(st & 0x08)) break;
        delay(1);
      }
      if (st & 0x08) return false;

      uint8_t d[8];
      if (!rd(0xF7, d, 8)) return false;
      const int32_t adcP = ((int32_t)d[0] << 12) | ((int32_t)d[1] << 4) | (d[2] >> 4);
      const int32_t adcT = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
      const int32_t adcH = ((int32_t)d[6] << 8) | d[7];
      if (adcT == 0x80000L || adcP == 0x80000L || adcH == 0x8000L) return false;

      // --- температура (0.01 °C) ---
      int32_t v1 = (((adcT >> 3) - ((int32_t)_t1 << 1)) * (int32_t)_t2) >> 11;
      int32_t v2 = (adcT >> 4) - (int32_t)_t1;
      v2 = (((v2 * v2) >> 12) * (int32_t)_t3) >> 14;
      const int32_t tFine = v1 + v2;
      temp100 = (int16_t)((tFine * 5 + 128) >> 8);

      // --- давление (Па) ---
      v1 = (tFine >> 1) - 64000L;
      v2 = (((v1 >> 2) * (v1 >> 2)) >> 11) * (int32_t)_p6;
      v2 += (v1 * (int32_t)_p5) << 1;
      v2 = (v2 >> 2) + ((int32_t)_p4 << 16);
      v1 = (((int32_t)_p3 * (((v1 >> 2) * (v1 >> 2)) >> 13)) >> 3) + (((int32_t)_p2 * v1) >> 1);
      v1 >>= 18;
      v1 = ((32768L + v1) * (int32_t)_p1) >> 15;
      if (v1 == 0) return false;
      uint32_t p = ((uint32_t)(1048576L - adcP) - (uint32_t)(v2 >> 12)) * 3125UL;
      if (p < 0x80000000UL) p = (p << 1) / (uint32_t)v1;
      else p = (p / (uint32_t)v1) * 2;
      v1 = ((int32_t)_p9 * (int32_t)(((p >> 3) * (p >> 3)) >> 13)) >> 12;
      v2 = ((int32_t)(p >> 2) * (int32_t)_p8) >> 13;
      pressPa = (uint32_t)((int32_t)p + ((v1 + v2 + _p7) >> 4));

      // --- влажность (%RH * 1024) ---
      int32_t x = tFine - 76800L;
      x = (((((adcH << 14) - ((int32_t)_h4 << 20) - ((int32_t)_h5 * x)) + 16384L) >> 15) *
           (((((((x * (int32_t)_h6) >> 10) * (((x * (int32_t)_h3) >> 11) + 32768L)) >> 10) + 2097152L) *
             (int32_t)_h2 + 8192L) >> 14));
      x -= (((((x >> 15) * (x >> 15)) >> 7) * (int32_t)_h1) >> 4);
      if (x < 0) x = 0;
      if (x > 419430400L) x = 419430400L;
      hum1024 = (uint32_t)(x >> 12);
      return true;
    }

  private:
    uint8_t  _a = 0x76;
    uint16_t _t1 = 0, _p1 = 0;
    int16_t  _t2 = 0, _t3 = 0;
    int16_t  _p2 = 0, _p3 = 0, _p4 = 0, _p5 = 0, _p6 = 0, _p7 = 0, _p8 = 0, _p9 = 0;
    int16_t  _h2 = 0, _h4 = 0, _h5 = 0;
    uint8_t  _h1 = 0, _h3 = 0;
    int8_t   _h6 = 0;

    static uint16_t u16(const uint8_t *b, uint8_t i) { return (uint16_t)b[i] | ((uint16_t)b[i + 1] << 8); }
    static int16_t  s16(const uint8_t *b, uint8_t i) { return (int16_t)u16(b, i); }

    bool wr(uint8_t reg, uint8_t val) {
      Wire.beginTransmission(_a);
      Wire.write(reg);
      Wire.write(val);
      return Wire.endTransmission() == 0;
    }

    bool rd(uint8_t reg, uint8_t *buf, uint8_t n) {
      Wire.beginTransmission(_a);
      Wire.write(reg);
      if (Wire.endTransmission(false) != 0) return false;
      if (Wire.requestFrom(_a, n) != n) return false;
      for (uint8_t i = 0; i < n; i++) buf[i] = Wire.read();
      return true;
    }
};

#endif
