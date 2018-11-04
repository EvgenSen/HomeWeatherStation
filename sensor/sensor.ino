#include <OneWire.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>

#define DEBUG 1

OneWire          ds1820(2);  // Подключен к D2
Adafruit_BMP280  bmp280;     // No input args - подключение по I2C

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

	Serial.println(F("Initialization successful"));
}

void loop()
{
	byte i;
	byte present = 0;
	byte data[12];
	float temp_cels;

	Serial.println("==============================================");
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
	temp_cels = (float)raw / 16.0;

	Serial.print("DS1820: Temperature ");
	Serial.print(temp_cels);
	Serial.println(" *C ");

	// ============== BMP280 ==============

	Serial.print(F("BMP280: Temperature "));
	Serial.print(bmp280.readTemperature());
	Serial.print(" *C Pressure ");

	float P = bmp280.readPressure();
	Serial.print(P);
	Serial.print(" Pa ");
	Serial.print(P*0.0075006375542,2);
	Serial.println(" mmHg");

	delay(3000);
}
