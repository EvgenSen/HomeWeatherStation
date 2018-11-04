#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>

// Структура передаваемых данных
typedef struct
{
	float ds1820_temp;
	float bmp280_temp;
	float bmp280_pres;
	float voltage;
}
Message;
Message msg;

RF24  nrf24(9, 10);  // Пины CE и CSN подключены к D9 и D10

int setup_nrf24(void)
{
	nrf24.begin();             // активировать модуль
	nrf24.setAutoAck(1);       // режим подтверждения приёма, 1 вкл 0 выкл
	nrf24.setRetries(0, 15);   // время между попыткой достучаться и число попыток
	nrf24.enableAckPayload();  // разрешить отсылку данных в ответ на входящий сигнал
	nrf24.setPayloadSize(32);  // размер пакета, в байтах
	nrf24.openReadingPipe(1, 0xF0F0F0F0F0);  // открываем канал для получения данных по адресу 0xF0F0F0F0F0
	nrf24.setChannel(0x60);          // выбираем канал (в котором нет шумов!)
	nrf24.setPALevel(RF24_PA_HIGH);  // уровень мощности передатчика. {RF24_PA_MIN, RF24_PA_LOW, RF24_PA_HIGH, RF24_PA_MAX}
	nrf24.setDataRate(RF24_1MBPS);   // скорость обмена.              {RF24_2MBPS, RF24_1MBPS, RF24_250KBPS}
	// Скорость должна быть одинакова на приёмнике и передатчике!
	// При самой низкой скорости имеем самую высокую чувствительность и дальность, но большее потребление энергии

	nrf24.powerUp();         // начать работу
	nrf24.startListening();  // начинаем слушать эфир, мы приёмный модуль

	return 0;
}


void setup()
{
	Serial.begin(9600);
	setup_nrf24();
}

void loop()
{
	while(nrf24.available())
	{
		nrf24.read(&msg, sizeof(msg));         // чиатем входящий сигнал

		Serial.print("Recieved: ds1820_temp: "); Serial.println(msg.ds1820_temp);
		Serial.print("          bmp280_temp: "); Serial.println(msg.bmp280_temp);
		Serial.print("          bmp280_pres: "); Serial.println(msg.bmp280_pres*0.0075006375542,2);
		Serial.print("          voltage:     "); Serial.println(msg.voltage);
	}
}
