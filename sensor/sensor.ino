#include <OneWire.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BMP280.h>
#include <DHT.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <LowPower.h>

#define DEBUG 0

#if DEBUG
#include <stdio.h>
static int serial_fputchar(const char ch, FILE *stream) { Serial.write(ch); return ch; }
static FILE *serial_stream = fdevopen(serial_fputchar, NULL);
#endif

#if DEBUG
//#define PRINTF_DBG(format, ...) printf (format, __VA_ARGS__);
#define PRINTF_DBG(...) printf (__VA_ARGS__);
#else
#define PRINTF_DBG(format, ...)
#endif

// Коэфициент для интревала обновления показаний датчиков и отправки данных
#define INTERVAL_1_MIN  7   // ~ 65  секунд
#define INTERVAL_10_MIN 69  // ~ 599 секундa

#define SEND_COUNT 15  // Количество попыток отправить данные в случае ошибки при отправке
#define SEND_DELAY 20  // Интревал между попытками отправить

// Структура передаваемых данных
typedef struct
{
	float ds1820_temp;  // Температура с датчика ds1820
	float bmp280_temp;  // Температура с датчика bmp280
	float bmp280_pres;  // Давление с датчика bmp280
	float voltage;      // Напряжение аккумулятора
	unsigned long id;   // Номер сообщения или Количество всех попыток отправить данные (MAX 65535)
	unsigned int send_err;  // Количество неудачных попыток отправить данные
	//unsigned long uptime;   // Время работы датчика
}
Message;
Message msg;

OneWire          ds1820(2);     // Подключен к D2
Adafruit_BMP280  bmp280;        // No input args - подключение по I2C
RF24             nrf24(9, 10);  // Пины CE и CSN подключены к D9 и D10

byte ds1820_addr[8];
byte ds1820_type_s;
boolean wake_flag;
int sleep_count;

void get_data_bmp280(void)
{
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
}

void get_data_ds1820(void)
{
	byte i;
	byte present = 0;
	byte data[12];

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

	// Вместо %f можно использовать также функцию dtostrf
	PRINTF_DBG("DS1820: Temperature %d.%02d *C\n", (int)msg.ds1820_temp, (int)(msg.ds1820_temp*100)%100);
}

/*
 * Расчитывает напряжение аккумулятора подклбченного к А0
 * на основании напряжения на повышающем модуле
 */
void get_voltage(void)
{
	float Vcc = 5.12; // Примерное напряжение на повышающем модуле
	int value = analogRead(0); // читаем показания с А0
	msg.voltage = (value / 1023.0) * Vcc;
}

/*
 * Функция отправляет данные на сервер при помощи модулей nrf24.
 * Структура передаваемых данных Message описана выше
 * В случае ошибки при передаче данных будет сделано SEND_COUNT 
 * попыток с интервалом в SEND_DELAY. Если данные не отправлены
 * создается соответсвуюшая запись в msg.send_err.
 * Возвращает 0 в случае успешной отправки, иначе -1.
 */
int send_data(void)
{
	//msg.uptime = millis(); // не будет корректно работать из-за прерываний

	byte send_count = 0;

	msg.id++;

	while ( !nrf24.write(&msg, sizeof(msg)) && send_count < SEND_COUNT )
	{
		PRINTF_DBG("Send error, retrying... (%d/%d)\n", send_count + 1, SEND_COUNT);
		send_count++;
		delay(SEND_DELAY);
	}
	if (send_count >= SEND_COUNT)
	{
		msg.send_err++;
	}

	PRINTF_DBG("Send count: %d, Send err: %d\n", msg.id, msg.send_err);

	return send_count >= SEND_COUNT ? -1 : 0;
}

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
		PRINTF_DBG("Error: Could not find DS18x20 sensor!\n");
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
		PRINTF_DBG("Error: CRC is not valid!\n");
		return -1;
	}
	// определяем тип датчика
	switch (ds1820_addr[0])
	{
	case 0x10:
		PRINTF_DBG("Founded chip: DS18S20/DS1820\n");
		ds1820_type_s = 1;
		break;
	case 0x28:
		PRINTF_DBG("Founded chip: DS18B20\n");
		ds1820_type_s = 0;
		break;
	case 0x22:
		PRINTF_DBG("Founded chip: DS1822\n");
		ds1820_type_s = 0;
		break;
	default:
		PRINTF_DBG("Error: Founded device is not a DS18x20 family\n");
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
#if DEBUG
	nrf24.printDetails();
#endif
	return 0;
}

void setup()
{
#if DEBUG
	stdout = serial_stream;
	Serial.begin(9600);
	Serial.print(F("Sketch:   " __FILE__ "\n"
	               "Compiled: " __DATE__ " " __TIME__ "\n"
	               "Type:     sensor\n"
	               "Version:  v1.0 (Release)\n\n"));
	Serial.println(F("Start temperature test"));
#endif

	// Китайский bmp280 использует нестандартный адрес (0x76)
	if (!bmp280.begin(0x76))
	{
		PRINTF_DBG("Error: Could not find BMP280 sensor! Check wiring or address\n");
		while (1);
	}
	if (setup_ds1820())
	{
		PRINTF_DBG("Error: Could not init DS18x20 sensor!\n");
		while (1);
	}

	setup_nrf24();

	msg.id=0;
	msg.send_err=0;
	wake_flag=1; // На первой итерации делаем измерения
	PRINTF_DBG("Initialization successful\n");
}

void loop()
{
	if (wake_flag)
	{
		PRINTF_DBG("==============================================\n");
		nrf24.powerUp();
		get_data_ds1820();
		get_data_bmp280();
		get_voltage();
		send_data();
		nrf24.powerDown(); // Переводим nrf24 в режим энергосбережения
		wake_flag = 0;
	}

	LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);  // спать 8 сек. (макс. значение библиотеки) mode POWER_OFF, АЦП выкл
	sleep_count++;
	if (sleep_count >= INTERVAL_10_MIN) // задержка около 10 минут
	{
		wake_flag = 1;
		sleep_count = 0;
		delay(2);  // задержка для стабильности
	}
}
