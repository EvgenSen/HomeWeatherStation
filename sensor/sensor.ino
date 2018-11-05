#include <OneWire.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <nRF24L01.h>
#include <RF24.h>

#define DEBUG 0

#define SEND_COUNT 15
#define SEND_DELAY 20

// Структура передаваемых данных
typedef struct
{
	float ds1820_temp;  // Температура с датчика ds1820
	float bmp280_temp;  // Температура с датчика bmp280
	float bmp280_pres;  // Давление с датчика bmp280
	float voltage;      // Напряжение аккумулятора
	unsigned int id;    // Номер сообщения или Количество всех попыток отправить данные (MAX 65535)
	byte send_err;      // Количество неудачных попыток отправить данные
}
Message;
Message msg;

OneWire          ds1820(2);     // Подключен к D2
Adafruit_BMP280  bmp280;        // No input args - подключение по I2C
RF24             nrf24(9, 10);  // Пины CE и CSN подключены к D9 и D10

byte ds1820_addr[8];
byte ds1820_type_s;

/*
 * Функция ищет датчик ds1820 и определяет его тип.
 * Мы используем только один ds1820, поэтому ограничимся
 * только одним циклом поиска и настройки датчика при
 * старте Arduino.
 */
int setup_ds1820(void)
{
	if ( !ds1820.search(ds1820_addr))
	{
		Serial.println("Error: Could not find DS18x20 sensor!");
		ds1820.reset_search();
		//delay(250);
		return -1;
	}
#if DEBUG
	byte i;
	Serial.print("ROM =");
	for( i = 0; i < 8; i++)
	{
		Serial.write(' ');
		Serial.print(ds1820_addr[i], HEX);
	}
	Serial.println();
#endif
	// считаем crc
	if (OneWire::crc8(ds1820_addr, 7) != ds1820_addr[7])
	{
		Serial.println("Error: CRC is not valid!");
		return -1;
	}
	// определяем тип датчика
	switch (ds1820_addr[0])
	{
	case 0x10:
		Serial.println("Founded chip: DS18S20/DS1820");
		ds1820_type_s = 1;
		break;
	case 0x28:
		Serial.println("Founded chip: DS18B20");
		ds1820_type_s = 0;
		break;
	case 0x22:
		Serial.println("Founded chip: DS1822");
		ds1820_type_s = 0;
		break;
	default:
		Serial.println("Error: Founded device is not a DS18x20 family");
		return -1;
	}
	return 0;
}

int setup_nrf24(void)
{
	nrf24.begin();             // активировать модуль
	nrf24.setAutoAck(1);       // режим подтверждения приёма, 1 вкл 0 выкл
	nrf24.setRetries(0, 15);   // время между попыткой достучаться и число попыток
	nrf24.enableAckPayload();  // разрешить отсылку данных в ответ на входящий сигнал
	nrf24.setPayloadSize(32);  // размер пакета, в байтах
	nrf24.openWritingPipe(0xF0F0F0F0F0);  // открываем канал для передачи данных по адресу 0xF0F0F0F0F0
	nrf24.setChannel(0x60);           // выбираем канал (в котором нет шумов!)
	nrf24.setPALevel (RF24_PA_HIGH);  // уровень мощности передатчика. {RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX}
	nrf24.setDataRate (RF24_1MBPS);   // скорость обмена.              {RF24_2MBPS, RF24_1MBPS, RF24_250KBPS}
	// Скорость должна быть одинакова на приёмнике и передатчике!
	// При самой низкой скорости имеем самую высокую чувствительность и дальность, но большее потребление энергии

	//nrf24.powerUp();        // начать работу
	nrf24.stopListening();  // не слушаем радиоэфир, мы передатчик

	//nrf24.printDetails();
	return 0;
}

void setup()
{
	Serial.begin(9600);
	Serial.println(F("Start temperature test"));

	// Китайский bmp280 использует нестандартный адрес (0x76)
	if (!bmp280.begin(0x76))
	{
		Serial.println(F("Error: Could not find BMP280 sensor! Check wiring or address"));
		while (1);
	}
	if (setup_ds1820())
	{
		Serial.println(F("Error: Could not init DS18x20 sensor!"));
		while (1);
	}

	setup_nrf24();

	msg.id=0;
	msg.send_err=0;

	Serial.println(F("Initialization successful"));
}

void loop()
{
	byte i;
	byte present = 0;
	byte data[12];
#if DEBUG
	Serial.println("==============================================");
#endif
	nrf24.powerUp();
	// ============== DS1820 ==============
	// Wait a few seconds between measurements.
	delay(3000);
	ds1820.reset();
	ds1820.select(ds1820_addr);
	ds1820.write(0x44, 1);  // Датчик подключен с выделенной линией питания

	delay(1200);  // 750 может быть достаточно, а может быть и не хватит
	// мы могли бы использовать тут ds.depower(), но reset позаботится об этом
	present = ds1820.reset();
	ds1820.select(ds1820_addr);
	//ds1820.write(0xCC);
	ds1820.write(0xBE);  // Read Scratchpad

	// Считываем 9 байт с датчика
	for ( i = 0; i < 9; i++)
	{
		data[i] = ds1820.read();
	}

	int16_t raw = (data[1] << 8) | data[0];
	if (ds1820_type_s)
	{
		raw = raw << 3;
		if (data[7] == 0x10)
		{
			raw = (raw & 0xFFF0) + 12 - data[6];
		}
	}
	else
	{
		byte cfg = (data[4] & 0x60);
		// at lower res, the low bits are undefined, so let's zero them
		if (cfg == 0x00) raw = raw & ~7;      // 9 bit resolution, 93.75 ms
		else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
		else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
		// default is 12 bit resolution, 750 ms conversion time
	}
	msg.ds1820_temp = (float)raw / 16.0;

#if DEBUG
	Serial.print("DS1820: Temperature ");
	Serial.print(msg.ds1820_temp);
	Serial.println(" *C ");
#endif

	// ============== BMP280 ==============

	msg.bmp280_temp = bmp280.readTemperature();
	msg.bmp280_pres = bmp280.readPressure();

#if DEBUG
	Serial.print(F("BMP280: Temperature "));
	Serial.print(msg.bmp280_temp);
	Serial.print(" *C Pressure ");

	Serial.print(msg.bmp280_pres);
	Serial.print(" Pa ");
	Serial.print(msg.bmp280_pres*0.0075006375542,2);
	Serial.println(" mmHg");
#endif

	// ============== Check V ==============

	float Vcc = 5.12; // Примерное напряжение на повышающем модуле
	int value = analogRead(0); // читаем показания с А0
	msg.voltage = (value / 1023.0) * Vcc;

	// ============== SEND DATA ==============

	byte send_num = 0;

	msg.id++;

	while ( !nrf24.write(&msg, sizeof(msg)) || send_num > SEND_COUNT )
	{
#if DEBUG
		Serial.println("Send error, retrying...");
#endif
		send_num++;
		delay(SEND_DELAY);
	}
	if (send_num > SEND_COUNT)
	{
		msg.send_err++;
	}

#if DEBUG
	Serial.print("Send count: ");
	Serial.print(msg.id);
	Serial.print(", Send err: ");
	Serial.println(msg.send_err);
#endif

	nrf24.powerDown(); // Переходим в режим энергосбережения

	delay(60000);
}
