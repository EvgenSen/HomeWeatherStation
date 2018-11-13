#include <nRF24L01.h>
#include <RF24.h>
#include <DHT.h>
#include <iarduino_OLED_txt.h>

#define EXEL_OUTPUT 1

#define VERSION "v0.1 (Not release)"

#define PIN_BUTTON A0  // кнопка подключена к A5

#define DISP_WORK_TIME 10000 // Дисплей работает в течении 10 секунд

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

RF24 nrf24(9, 10);     // Пины CE и CSN подключены к D9 и D10
DHT  dht22(4, DHT22);  // DHT22 подключен к D4
iarduino_OLED_txt oled128x64(0x3C);  // Объявляем объект OLED, указывая адрес дисплея на шине I2C: 0x3C или 0x3D.

boolean butt_now = 0;   // Состояние пина кнопки в данный момент
boolean butt_prev = 0;  // Состояние пина кнопки в предыдущий момент

// Последнее время активации дислпея
// Первоначальное значение большое, чтобы дисплей не отключался, до получения первых данных
unsigned long disp_last_time = 1000000;

// Подключаем шрифты
// extern uint8_t MediumFont[];
// extern uint8_t SmallFont[];
extern uint8_t MediumFontRus[];
extern uint8_t SmallFontRus[];

unsigned long last_msg_id = 0;
unsigned int duplicate_count = 0;

float dht22_temp;
float dht22_hum;

void print_uptime(unsigned long time_millis)
{
	unsigned long time=time_millis/1000; // переводим милисекунды в секунды
	// Считаем и выводим дни
	Serial.print (time/60/60/24);
	Serial.print (" days ");
	// Вычитаем количество целых дней 
	time-=(time/60/60/24)*60*60*24;
	// Считаем и выводим часы
	if (time/60/60<10) { Serial.print ("0"); }
	Serial.print (time/60/60);
	Serial.print (":");
	// Считаем и выводим минуты
	if (time/60%60<10) { Serial.print ("0"); }
	Serial.print ((time/60)%60);
	Serial.print (":");
	// Считаем и выводим секунды
	if (time%60<10) { Serial.print ("0"); }
	Serial.println (time%60);
}

int get_data_dht(void)
{
  // Reading temperature or humidity takes about 250 milliseconds!
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  dht22_hum  = dht22.readHumidity();
  // Read temperature as Celsius (the default)
  dht22_temp = dht22.readTemperature();

  // Check if any reads failed and exit early (to try again).
  if (isnan(dht22_hum) || isnan(dht22_temp))
  {
    Serial.println("Failed to read from DHT sensor!");
    return -1;
  }

  // Compute heat index in Celsius (isFahreheit = false)
  // float hic = dht.computeHeatIndex(t, h, false);

  return 0;
}

void print_data_port(void)
{
	msg.bmp280_pres=msg.bmp280_pres*0.0075006375542; // Перевод в мм. р. ст.
#if EXEL_OUTPUT
	Serial.print(msg.id); Serial.print("\t");
	Serial.print(msg.ds1820_temp); Serial.print("\t");
	Serial.print(dht22_temp); Serial.print("\t");
	Serial.print(dht22_hum); Serial.print("\t");
	Serial.print(msg.bmp280_temp); Serial.print("\t");
	Serial.print(msg.bmp280_pres); Serial.print("\t");
	Serial.print(msg.voltage); Serial.print("\t");
	Serial.print(millis());  Serial.print("\t");  // msg.uptime
	Serial.print(msg.send_err); Serial.print("\t");
	Serial.println(duplicate_count);
#else
	Serial.print("Recieved: id:          "); Serial.println(msg.id);
	Serial.print("          ds1820_temp: "); Serial.println(msg.ds1820_temp);
	Serial.print("          dht22_temp:  "); Serial.println(dht22_temp);
	Serial.print("          dht22_hum:   "); Serial.println(dht22_hum);
	Serial.print("          bmp280_temp: "); Serial.println(msg.bmp280_temp);
	Serial.print("          bmp280_pres: "); Serial.println(msg.bmp280_pres);
	Serial.print("          voltage:     "); Serial.println(msg.voltage);
	Serial.print("          uptime:      "); print_uptime(millis());
	Serial.print("          send_err:    "); Serial.println(msg.send_err);
	Serial.print("          duplicate:   "); Serial.println(duplicate_count);
	Serial.println("=================================");
#endif
}

/*
 * Выводи информацию на дисплей
 * Если дисплей уже включен, то просто обновляет время
 */
void print_data_display(void)
{
	if((millis() > disp_last_time) && (millis() - disp_last_time > DISP_WORK_TIME))
	{
		oled128x64.clrScr();
		oled128x64.print("dht22:  ", OLED_L, 2);  oled128x64.print(dht22_temp,      OLED_N, 2, 2);  oled128x64.print(" \370C", OLED_N, 2);
		oled128x64.print("dht22:  ", OLED_L, 3);  oled128x64.print(dht22_hum,       OLED_N, 3, 2);  oled128x64.print(" %",     OLED_N, 3);
		oled128x64.print("ds1820: ", OLED_L, 4);  oled128x64.print(msg.ds1820_temp, OLED_N, 4, 2);  oled128x64.print(" \370C", OLED_N, 4);
		oled128x64.print("bmp280: ", OLED_L, 5);  oled128x64.print(msg.bmp280_pres, OLED_N, 5, 2);  oled128x64.print(" mmHg",  OLED_N, 5);
		oled128x64.print("volt:   ", OLED_L, 6);  oled128x64.print(msg.voltage,     OLED_N, 6, 2);  oled128x64.print(" v",     OLED_N, 6);
	}
	disp_last_time = millis();
}

int setup_oled(void)
{
	oled128x64.begin();
	oled128x64.setFont(MediumFontRus);
	oled128x64.setCoding(TXT_UTF8);
	oled128x64.print("Weather", OLED_L, 1);
	//               "----------"
	oled128x64.setFont(SmallFontRus);
	oled128x64.print(VERSION, OLED_L, 3);
	oled128x64.print("Wait sensor...", OLED_L, 4);

	return 0;
}

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
	Serial.print(F("Sketch:   " __FILE__ "\n"
	               "Compiled: " __DATE__ " " __TIME__ "\n"
	               "Type: server\n"
	               "Version:  " VERSION "\n\n"));
	pinMode(PIN_BUTTON, INPUT_PULLUP); // включаем резистор для кнопки
	dht22.begin();
	setup_nrf24();
	setup_oled();
#if EXEL_OUTPUT
	Serial.println("id\tds1820\tdht22\tdht22\tbmp280\tbmp280\tvolt\tuptime\terr\tduplicate");
#endif
}

void loop()
{
	while(nrf24.available()) // Проверяем входные данные
	{
		// Если есть считываем сообщение
		nrf24.read(&msg, sizeof(msg));
		if(last_msg_id < msg.id)
		{
			// Если это не дупликат, считываем показания dht и выводим в послед. порт
			get_data_dht();
			if (last_msg_id == 0)
			{
				// Если это первое сообщение - обновляем дисплей
				disp_last_time = 0;
				print_data_display();
			}
			last_msg_id =  msg.id;
			print_data_port();
		}
		else
		{
			duplicate_count++;
		}
	}
	butt_now = !digitalRead(PIN_BUTTON); // получаем сотояние кнопки
	if ( butt_now == 1 && butt_prev == 0 ) // кнопка нажата
	{
		butt_prev = 1;
	}
	if ( butt_now == 0 && butt_prev == 1 ) // кнопка отпущена
	{
		print_data_display();
		butt_prev = 0;
	}
	if((millis() > disp_last_time) && (millis() - disp_last_time > DISP_WORK_TIME))
	{
		// Отключаем дисплей через DISP_WORK_TIME милисекунд
		oled128x64.clrScr();
	}
}
