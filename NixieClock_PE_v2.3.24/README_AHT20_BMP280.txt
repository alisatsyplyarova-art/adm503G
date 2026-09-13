AHT20 + BMP280 version

Sensors:
- AHT20: temperature + relative humidity, I2C 0x38
- BMP280: pressure, I2C 0x76/0x77

I2C Arduino UNO:
- SDA -> A4
- SCL -> A5
- GND -> GND
- VCC according to your breakout board (3.3V recommended for bare 3.3V modules)

Display sequence with sensor button:
Temperature -> Pressure -> Humidity -> Alarm/Clock

Required libraries:
- Adafruit AHTX0
- Adafruit BMP280 Library
- Adafruit Unified Sensor
- Adafruit BusIO
- RTClib
- GyverButton


Исправление температуры:
- I2C запускается через Wire.begin().
- Указатели датчиков получаются после успешной инициализации.
- Температура берется сначала с AHT20, при его отсутствии автоматически с BMP280.
- Для BMP280 поддерживаются адреса 0x76 и 0x77.


=== ПРОГРАММНАЯ ЗАЩИТА DC-DC ===
Максимальный PWM ограничен 160. При PWM >= 150 и показании A6 < 400 более 1.5 с генератор выключается до перезапуска. При A6 >= 620 генератор также отключается. Это защита от неисправности обратной связи/перегрузки и не заменяет аппаратную защиту по току.


DC-DC protection: max PWM 180; low-feedback protection at PWM >=175 and ADC<350 for 5s; overvoltage ADC>=700 sustained 100ms.
