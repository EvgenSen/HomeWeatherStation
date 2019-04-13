# HomeWeatherStation

Простая метеостанция измеряющая температуру, влажность и давление на базе arduino


## Версия

v1.0 (Release)


## Папки

### libs

Папка с бибилиотеками, необходимыми для компиляции проекта. Включает в себя:
* **AdafruitSensor** - универсальная библиотека для работы с датчиками. Нужна для работы библиотеки DHT
* **BMP280** - библиотека для работы с датчиком BMP280
* **DHT-sensor-library** - библиотека для работы с датчиками DHT11/DHT22
* **LowPower** - библиотека для перевода Arduino в режим пониженного энергопотребления
* **OneWire** - библиотека для работы с устройствами по протоколу 1-Wire. Используется для датчика DS18B20
* **RF24** - библиотека для работы с модулями беспроводной связи NRF24

После клонирования репозитория нужно проинициализировать подмодули:

`git submodule init && git submodule update`

А затем скопировать библиотеки в папку `.../Arduino/libraries/`

### sensor

Скетч для автономного датчика.

### server

Скетч для сервера


## Схема подключения

Схема датчика:
![sensor](https://github.com/EvgenSen/HomeWeatherStation/blob/master/sensor.png)
Схема сервера:
![server](https://github.com/EvgenSen/HomeWeatherStation/blob/master/server.png)
