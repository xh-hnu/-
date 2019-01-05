#include "HX711.h"
#include "U8glib.h"
#include <string.h>
#include <SoftwareSerial.h>

#define ESP8266_TX 4
#define ESP8266_RX 5

//接线
/**
 * 心率传感器 - --> arduino GND
             + --> arduino VCC
             s --> arduino A0
   称重传感器  VCC --> arduino VCC
              GND --> arduino GND
              HX711_SCK --> arduino 2
              HX711_DT --> arduino 3
   OLED显示屏 VCC --> arduino VCC
              GND --> arduino GND
              SCL -->arduino A5
              SDA -->arduino A4
   由于arduino的地和电源VCC不够，所以引出了地和电源VCC
 */

//  Variables
int pulsePin = 0;                 // 心率传感器模拟输入引脚
int blinkPin = 13;                // 心跳灯引脚，13引脚有一个led
float Weight = 0; 
uint8_t weight_update = 0;
char weight_str[6];
char BPM_str[6];
//const char* ssid = "TP-LINK_EF57";
//const char* password = "bbb117117";
//const char* host = "192.168.1.127";
//String path = "/SpringMvcDemo/index.jsp";//file path
//String postPath = "/SpringMvcDemo/login";// post adrress

// Volatile Variables, used in the interrupt service routine!
volatile int BPM;                   // 心率值，每2mS更新一次
volatile int Signal;                // 模拟输入电压值
volatile int IBI = 600;             // 两次心跳之间的时间间隔
volatile boolean Pulse = false;     // "True" 发现手指. "False" 没有手指.
volatile boolean QS = false;        // 发现一次心跳时为true，需要置为false

static unsigned char zhong[] U8G_PROGMEM = 
{0x00,0x00,0x00,0x06,0xF8,0x01,0x40,0x08,0xFC,0x1F,0x48,0x04,0xF8,0x07,
0x48,0x04,0xF8,0x07,0x48,0x04,0xF8,0x07,0x40,0x04,0xFC,0x03,0x40,0x00,
0xFE,0x1F,0x00,0x00};
static unsigned char liang[] U8G_PROGMEM = 
{0x00,0x00,0xF8,0x07,0x08,0x04,0xF8,0x07,0xF8,0x07,0x08,0x14,0xFE,0x1F,
0xB8,0x07,0x48,0x04,0xF8,0x07,0x48,0x04,0xF8,0x07,0xF8,0x07,0x40,0x00,
0xFC,0x1F,0x00,0x00};
static unsigned char xin[] U8G_PROGMEM = 
{0x00,0x00,0x00,0x00,0x40,0x00,0x80,0x00,0x80,0x00,0x30,0x00,0x10,0x00,
0x10,0x08,0x14,0x18,0x14,0x10,0x16,0x10,0x10,0x04,0x10,0x04,0xE0,0x07,
0x00,0x00,0x00,0x00};
static unsigned char lv[] U8G_PROGMEM = 
{0x00,0x00,0x40,0x00,0x80,0x08,0x7C,0x07,0x44,0x00,0x28,0x05,0xF8,0x02,
0x40,0x02,0x28,0x0D,0xF6,0x08,0x40,0x00,0xFC,0x1F,0x40,0x00,0x40,0x00,
0x40,0x00,0x00,0x00};


U8GLIB_SSD1306_128X64 u8g(U8G_I2C_OPT_NONE);  // I2C / TWI 
SoftwareSerial mySerial(ESP8266_TX,ESP8266_RX);//RX TX

void drawWeightAndBPM(char *weight,char *bpm){
  uint8_t h;
  u8g_uint_t w, d;
  //中文字符显示
  u8g.drawXBMP(0, 20, 16, 16, zhong); //在坐标X:26  Y:16的位置显示中文字符重
  u8g.drawXBMP(16, 20, 16, 16, liang); //在坐标X:26  Y:16的位置显示中文字符量
  u8g.drawXBMP(0, 50, 16, 16, xin); //在坐标X:26  Y:16的位置显示中文字符心
  u8g.drawXBMP(16, 50, 16, 16, lv); //在坐标X:26  Y:16的位置显示中文字符率
  u8g.setFont(u8g_font_9x15);
  u8g.setFontRefHeightText();
  u8g.setFontPosTop();
  
  h = u8g.getFontAscent()-u8g.getFontDescent();
  w = u8g.getWidth();
  d = u8g.getStrWidth(":") + 32;
  u8g.setDefaultForegroundColor();
  u8g.drawStr(32, 1.7*h, ":");
  u8g.drawStr(d, 1.7*h, weight);
  d = d + u8g.getStrWidth(weight) + 2;
  u8g.drawStr(d, 1.7*h, "g");
  d = 32;
  u8g.drawStr(d, 4*h, ":");
  d = u8g.getStrWidth(":") + d;
  u8g.drawStr(d, 4*h, bpm);
}

void setup()
{
	Init_Hx711();				//初始化HX711模块连接的IO设置
  pinMode(blinkPin,OUTPUT);         // 心跳灯
  Serial.begin(115200);             
  interruptSetup();                 // 2 mS 中断一次采样，并判断是否为一次心跳
	delay(3000);
	Get_Maopi();		//获取毛皮
  weight_update = 1;//force to init
  // set the data rate for the SoftwareSerial port wifi 串口通信
  mySerial.begin(115200);
  mySerial.println("AT");
  delay(2000);
  while (mySerial.available() > 0) {
    Serial.write(mySerial.read());
  }
  mySerial.println("AT+CIPMUX=0"); // 单连接模式
  delay(2000);
  mySerial.println("AT+CIPMODE=1"); // 透传模式
  delay(2000);
  while(mySerial.read()>= 0){}//clear myserial buffer
}

void loop()
{
	Weight = Get_Weight();	//计算放在传感器上的重物重量
	Serial.print(float(Weight/1000),3);	//串口显示重量
	Serial.print(" kg\n");	//显示单位
	Serial.print("\n");		//显示单位
  Serial.println(BPM);
  itoa(Weight, weight_str, 10);//int to string 10进制
  itoa(BPM,BPM_str,10);
  connectAliyun("303NO.7",456,Weight,300,BPM);
  if (  weight_update != 0 ) {
    u8g.firstPage();
    do  {
      drawWeightAndBPM(weight_str,BPM_str);
    } while( u8g.nextPage() );
    weight_update = 1;
  }
  // ledFadeToBeat();                      // 心跳灯
	delay(1000);				//延时1s
}

void connectAliyun(String bedId,int initWeight,int currentWeight,int dWeight,int BPM){
  mySerial.println("AT+CIPSTART=\"TCP\",\"39.106.190.41\",8888"); // 建立TCP连接
  delay(2000);
  while(mySerial.read()>= 0){}//clear myserial buffer
  mySerial.println("AT+CIPSEND"); //开始发送数据
  delay(2000);
  while(mySerial.read()>= 0){}//clear myserial buffer
  String url = "GET http://39.106.190.41:8888/postData?bedId="+bedId+"&initWeight="+initWeight+"&currentWeight="+currentWeight+"&dWeight="+dWeight+"&BPM="+BPM;
  Serial.println(url);
  mySerial.println(url); //发送get请求
  delay(2000);
  while(mySerial.available() > 0){
    Serial.write(mySerial.read());
  }
}




  
