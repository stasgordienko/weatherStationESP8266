#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SimpleDHT.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>          // LCD 
#include <Adafruit_BMP280.h>           //bmp280 библиотека датчика давления
#include <Wire.h>                
#include <time.h>
#include "Weather.h"

#define DEBUG 1
#define REFRESH 1
#define WIND_AVG_COUNT 60
#define WIND_DIVIDER 10.0
#define LOG_MINUTES 7

const String Sector[17] = {"N","NNE","NE","ENE","E","ESE","SE","SSE","S","SSW","SW","WSW","W","WNW","NW","NNW","N"};

// orientation lookup table , 0 and 31srt indexes are invalid
const int table[32] = { -1, 0, 72, 12, 144, 132, 84, 120, 216, 348, 204, 24, 156, 168, 192,
               180, 288, 300, 60, 48, 276, 312, 96, 108, 228, 336, 240, 36, 264, 324, 252, -1};

const String tableDir[32] = { "err", "N", "ENE", "N", "SE", "SE", "E", "ESE", "SSW", "NWN", "SSW", "NNE", "SSE", "SSE", "S",
               "S", "WNW", "WNW", "NE", "NE", "W", "NW", "E", "ESE", "SW", "NNW", "SW", "NNE", "W", "NW", "WSW", "err"};               

const int table_reverse[32] = { -1, 12, 300, 0, 228, 240, 288, 252, 156, 24, 168, 348, 216, 204, 180,
               192, 84, 72, 312, 324, 96, 60, 276, 264, 144, 36, 132, 336, 108, 48, 120, -1};               

const String tableDir_reverse[32] = { "err", "N", "WNW", "N", "SW", "SW", "WNW", "WSW", "SSE", "NNE", "SSE", "NNW", "SSW", "SSW", "S",
               "S", "E", "ENE", "NW", "NW", "E", "NE", "W", "W", "SE", "NNE", "SE", "NNW", "ESE", "NE", "ESE", "err"};               

const int timezone = 3;

  
char SERVER[] = "rtupdate.wunderground.com";           // Realtime update server - RapidFire
//char SERVER [] = "weatherstation.wunderground.com";  // Standard server 
char WEBPAGE [] = "GET /weatherstation/updateweatherstation.php?";

char ID [] = "IZAKARPA4"; // Here, you must change it to yours
char PASSWORD [] = "e1q9wf2v"; // Here, you must change it to yours


//const char* ssid = "glide";
//const char* password = "sunshine";

const int ssidCount = 3;
const char* ssid[ssidCount] = {"glide", "weather", "OpenWrt"};
const char* password[ssidCount] = {"sunshine", "12345678", "porozit46"};

//const int led = 0; //D3
//cannot use //15  //D8
const int wind_pin = 16;  // green //D0 
//const int wind_pin = 0;  //D3
//const int wind_pin = 2;  //D4
const int dir0_pin = 5;   //gw //D1
const int dir1_pin = 4;   //bw //D2
const int dir2_pin = 14;   //blue //D5
const int dir3_pin = 12;   //bw //D6
const int dir4_pin = 13;  //brown //D7

const int pinDHT22 = 0; //D3
SimpleDHT22 dht22;

// bmp280 stuff
//const int SDA_pin = 15; //D8
const int SDA_pin = 2; //D4
//const int SDA_pin = 1; //D10/TX

//const int SCL_pin = 2;  //D4
//const int SCL_pin = 0;  //D3
const int SCL_pin = 3;  //D9/RX

Adafruit_BMP280 bmp; // I2C baro 


#define OLED_RESET LED_BUILTIN 
Adafruit_SSD1306 display(OLED_RESET);

ESP8266WebServer server(80);
WiFiClient client;

String dataString;
char charBuf[256];

struct tm * timeinfo;

Weather wlog[LOG_MINUTES];                //

volatile unsigned int wind_interrupts_count = 0;
volatile float wind_speed = 0.0;
volatile int wind_direction = 0;
String wind_direction_word = "-";
float wind_avg_m = 0;
float wind_max_m = 0;
float wind_min_m = 25;
float wind_speed_avg = 0.0;
float wind_speed_max = 0.0;
float wind_speed_min = 25;
float wind_speed_sum = 0.0;
int wind_dir_sum = 0;
float wind_dir_min = 359;
float wind_dir_max = 0;
float wind_dir_avg_m = 0;
float humidity = 0.0;
float tempDHT = 0.0;
float tempBMP = 0.0;
float pressure = 0.0;
int currentMinute = 0;
int currentHour = 1;
int currentDate = 1;
volatile int min_timer = WIND_AVG_COUNT;
volatile int draw_timer = REFRESH;
volatile int sec_timer = 0;
volatile int timer_counter = 200;
volatile unsigned long next = 0;
volatile int unsigned wind_pin_last_state = 0;
int unsigned last_web_request_time = 0;
int unsigned tp = 1;
int rssi = 0;
time_t now;


void inline interruptSec(void){
  //calc speed
  wind_speed = (float)wind_interrupts_count / WIND_DIVIDER;
  wind_interrupts_count = 0;
  sec_timer = 0;  
}


void inline interruptWind(void){
  int unsigned wind_pin_state = digitalRead(wind_pin);
  if (wind_pin_state != wind_pin_last_state){
    wind_interrupts_count++;
    wind_pin_last_state = wind_pin_state; 
  }
  
  timer_counter--;
  if(timer_counter < 0) {
    interruptSec();
    timer_counter = 200;
  }

  next = next + 400000L; // 200Hz (ESP8266 CPU 80MHz)
  timer0_write(next);
}


String inline degToDir(int d) {
  return Sector[int((d%360)/ 22.5)]; //Rounds the wind direction out into 17 sectors. Sectors 1 and 17 are both N.
}


void timerSec() {
  if(wind_speed_max < wind_speed) {wind_speed_max = wind_speed;}
  if(wind_speed_min > wind_speed) {wind_speed_min = wind_speed;}
  wind_speed_sum += wind_speed;  

  yield();

  //calc dir (Gray One Track Code)
  int d0 = digitalRead(dir0_pin);
  int d1 = digitalRead(dir1_pin);
  int d2 = digitalRead(dir2_pin);
  int d3 = digitalRead(dir3_pin);
  int d4 = digitalRead(dir4_pin);

  int pos = (d0 * 1 
           + d1 * 2
           + d2 * 4
           + d3 * 8
           + d4 * 16);
  
  wind_direction = table_reverse[pos] ;
  if(wind_dir_max < wind_direction) {wind_dir_max = wind_direction;}
  if(wind_dir_min > wind_direction) {wind_dir_min = wind_direction;}
  wind_dir_sum += wind_direction;
  wind_direction_word = tableDir_reverse[pos];           // select from table
  //wind_direction_word = degToDir(wind_direction);          // calculate
}


void timerMin(){
  wind_avg_m = wind_speed_sum / WIND_AVG_COUNT;
  wind_max_m = wind_speed_max;
  wind_min_m = wind_speed_min;
  wind_dir_avg_m = wind_dir_sum / WIND_AVG_COUNT;

  yield();
  
  //calc temp
  //calc humidity
  if(getTemperature()) {
     if(DEBUG){ ////
      Serial.print("\t humidity: ");
      Serial.print(humidity, 1);
      Serial.print("\t\t temperature: ");
      Serial.print(tempDHT, 1);
      Serial.print("\t");
     }
  } else {
      if(DEBUG){ ////
        Serial.println("Failed to read from DHT sensor!");
      }
  }

  yield();
  
  //calc pressure 
  if(getPressure()) {
     if(DEBUG){ ////
      Serial.print("\t pressure: ");
      Serial.print(pressure, 1);
      Serial.print("\t\t tempBMP: ");
      Serial.print(tempBMP, 1);
      Serial.print("\t\t \n");
     }
  } else {
      if(DEBUG){ ////
        Serial.println("Failed to read from BMP sensor!");
      }
  }

  yield();

  wlog[currentMinute].windSpeed_min = wind_min_m;
  wlog[currentMinute].windSpeed_max = wind_max_m;
  wlog[currentMinute].windSpeed_avg = wind_avg_m;
  wlog[currentMinute].windDirection_min = wind_dir_min;
  wlog[currentMinute].windDirection_max = wind_dir_max;
  wlog[currentMinute].windDirection_avg = wind_dir_avg_m;

  wlog[currentMinute].pressure_avg = pressure;
  wlog[currentMinute].tempBMP_avg = tempBMP;
  wlog[currentMinute].humidity_avg = humidity;
  wlog[currentMinute].tempDHT_avg = tempDHT;

  wlog[currentMinute].sunny = 0;
  wlog[currentMinute].batt = 0;

  now = time(nullptr);
  timeinfo = localtime(&now);  

  wlog[currentMinute].dd = 0;
  wlog[currentMinute].hh = timeinfo->tm_hour;
  wlog[currentMinute].mm = timeinfo->tm_min;

  currentMinute++;
  if(currentMinute > LOG_MINUTES - 1) {
    currentMinute = 0;
  }

  // send data to WeatherUnderground
  wu();

  wind_speed_sum = 0.0;
  wind_speed_max = 0.0;
  wind_speed_min = 25.0;
  wind_dir_sum = 0;
  wind_dir_min = 359;
  wind_dir_max = 0;
} //end timerMin



void handleRoot() {
  time_t now = time(nullptr);
  String message = "<html>Wind speed (min/avg/max): ";
  message += wind_min_m + String(" / <b>") + wind_avg_m + String("</b> / <b>") + wind_max_m;
  message += " m/s </b><br>\n Wind direction: <b>";
  message += wind_direction;
  message += "</b> | <b>";
  message += degToDir(wind_direction);
  message += "</b>\n<br><br>\n Temperature: ";
  message += tempDHT;
  message += "\n<br>\n Humidity: ";
  message += humidity;
  message += "\n<br>\n Pressure: ";
  message += pressure;
  message += "\n<br><br>\n ";
  message += ctime(&now);
  message += "\n</html>\n";
  server.send(200, "text/html", message);
    if(DEBUG){ ////
      Serial.println("root page requested.");  
    }
}


void handleLog(){
  //time_t now = time(nullptr);
  
  int unsigned current_time = ESP.getCycleCount();
  if(current_time - last_web_request_time < 160000000) {
    server.send(200, "text/html", "BUSY...");
  }
  last_web_request_time = current_time;
  
  int pos;
  int start_pos = currentMinute - LOG_MINUTES;
  
  String message = "<html><table border=1 cellpadding=3>";
  message += "<tr><td>HH:MM</td>";
  for(int a=start_pos; a<currentMinute; a++){
    if(a < 0) { pos = a + LOG_MINUTES; } else {pos = a;}
    yield();
    message += "<td>";
    message += wlog[pos].hh;
    message += ":";
    if(wlog[pos].mm < 10) {message += "0";}
    message += wlog[pos].mm;
    message += "</td>";
  }
  message += "</tr>";
  
  message += "<tr>";
  message += "<td> wind min <br><b> wind AVG <br> wind max </b><br> dir deg <br><b> direction </b><br> temp C <br> humidity <br> pressure </td>";
  
  for(int a=start_pos; a<currentMinute; a++){
    yield();
    if(a < 0) { pos = a + LOG_MINUTES; } else {pos = a;}
    message += "<td>";
    message += String(wlog[pos].windSpeed_min, 1) + "<br><b>";
    if(wlog[pos].windSpeed_avg > 7.0) { message += "<font color=red>"; } else { message += "<font color=green>"; }
    message += String(wlog[pos].windSpeed_avg, 1);
    message += "</font></b><br><b>";
    if(wlog[pos].windSpeed_max > 7.0) { message += "<font color=red>"; } else { message += "<font color=green>"; }
    message += String(wlog[pos].windSpeed_max, 1);
    message += "</font></b><br><b>";
    yield();
    if(wlog[pos].windDirection_avg > 100 && wlog[pos].windDirection_avg < 280) { message += "<font color=green>"; } else { message += "<font color=red>"; }
    message += wlog[pos].windDirection_avg;
    message += "<br>";
    message += degToDir(wlog[pos].windDirection_avg);
    message += "</font></b><br>";
    message += String(wlog[pos].tempBMP_avg, 1);
    message += "<br>";
    message += String(wlog[pos].humidity_avg, 1);    
    message += "<br>";
    message += String(wlog[pos].pressure_avg, 1);
    message += "</td>";
  }

  message += "</tr>";
  yield();
  message += "</table><br>\n";
  //message += "Time now: ";
  //message += ctime(&now);
  message += "\n</html>\n";
  server.send(200, "text/html", message);
    if(DEBUG){ ////
      Serial.println("LOG_ page requested.");  
    }
}

void handleSys(){
  time_t now = time(nullptr);
  String message = "<html>";
  message += "RSSI: " + String(WiFi.RSSI()) + " dbm<br>";
  message += "Time now: " + String(ctime(&now)) + " (" + String(ESP.getCycleCount()) + ")";
  message += "\n</html>\n";
  server.send(200, "text/html", message);
    if(DEBUG){ ////
      Serial.println("SYS page requested.");  
    }
}


void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
    if(DEBUG){ ////
      Serial.println("UNKNOWN requested. OK.");
    }
}

bool getPressure(){
    tempBMP = bmp.readTemperature();
    pressure = bmp.readPressure() / 100;
    //alt = bmp.readAltitude(1013.25); // this should be adjusted to your local forcase
    return true;
}

bool getTemperature() {
int err = SimpleDHTErrSuccess;
  if ((err = dht22.read2(pinDHT22, &tempDHT, &humidity, NULL)) != SimpleDHTErrSuccess) {
      if(DEBUG){ ////
        Serial.print("Read DHT22 failed, err="); 
        Serial.println(err);
      }
    return false;
  }
    return true;
}


void setup_wifi() {
        if(DEBUG){ ////
        Serial.println();
        Serial.print("setup_wifi()");
      }
  delay(10);
    
  WiFi.mode(WIFI_STA);

  int retries = 0;
  while ((WiFi.status() != WL_CONNECTED) || (retries < 2)) {

    for (int ssidId = 0; (ssidId < ssidCount && (WiFi.status() != WL_CONNECTED)); ssidId++){
      WiFi.begin(ssid[ssidId], password[ssidId]);
      if(DEBUG){ ////
        Serial.println();
        Serial.print("Connecting to: ");
        Serial.println(ssid[ssidId]);
      }
      
      for (int c = 0; c < 15; c++){
        if(DEBUG){ ////
          Serial.print(".");
        }
        delay(500);
        if (WiFi.status() == WL_CONNECTED){
          break;
        }
      }
      
    }
    
    retries++;
  }

    if(DEBUG){ ////
      Serial.println("");
      Serial.println("WiFi connected");
      Serial.println("IP address: ");
      Serial.println(WiFi.localIP());
    }
  
}


//#######################
//## DRAW
//#######################
void draw(){
  display.setTextColor(WHITE);
  display.clearDisplay();

   display.setTextSize(2);
   display.setCursor(0,0);
   display.print(wind_speed, 1);
   display.print(" | ");
   display.print(wind_max_m, 1);

   display.setTextSize(2);
   display.setCursor(0,19);
   display.print(wind_direction_word);
   display.print(" | ");
   display.print(wind_direction);
   
   display.setTextSize(2);
   display.setCursor(0,38);
   display.print(rssi);
   display.print(", ");
   display.print(timeinfo->tm_hour);
   if(tp == 1) {display.print(":"); tp = 0;} else {display.print("."); tp = 1;}
   display.print(timeinfo->tm_min);
   
   display.setTextSize(1);
   display.setCursor(0, 56); 
   display.print(tempDHT, 1);
   display.print("/");
   display.print(tempBMP, 1);
   display.print(" ");
   display.print(humidity, 1);
   display.print(" ");
   display.print(pressure, 2);


//   display.print("H");
//   display.print(humidity, 1);
//   display.print(" | T");
//   display.print(tempDHT, 1);
//   display.print(" | P");
//   display.print(pressure, 1);

   display.display();
}


//#####################
//## SETUP
//#####################
void setup(void){  
  //setup_wifi();      //////// SETUP WIFI
  
  Wire.pins(SDA_pin, SCL_pin);
  //Wire.begin(SDA_pin, SCL_pin);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // initialize with the I2C addr 0x3D (for the 128x64)
  display.display();
  delay(500);
  // Clear the buffer.
  //display.clearDisplay();

  if(DEBUG){ ////
      Serial.begin(115200, SERIAL_8N1, SERIAL_TX_ONLY);
      Serial.println(ESP.getFlashChipSize());
  }

  setup_wifi();      //////// SETUP WIFI

  //bmp180 stuff
  if (!bmp.begin()) {
      if(DEBUG){ ////
        Serial.println("No Pressure Sensor connected (BMP180 / BMP280)");
      }
      //while (1) {}
  } 
  
  pinMode(dir0_pin, INPUT);  //dir0
  pinMode(dir1_pin, INPUT);  //dir1
  pinMode(dir2_pin, INPUT);  //dir2
  pinMode(dir3_pin, INPUT);  //dir3
  pinMode(dir4_pin, INPUT);  //dir4
  
  pinMode(wind_pin, INPUT);  //wind0

  noInterrupts();
  timer0_isr_init();
  timer0_attachInterrupt(interruptWind);
  next = ESP.getCycleCount() + 240000000L; //start after 3sec
  timer0_write(next);
  interrupts();

    if(DEBUG){ ////
      Serial.print("WiFi IP address: ");
      Serial.println(WiFi.localIP());
    }

  server.on("/", handleRoot);
  server.on("/log", handleLog);
  server.on("/sys", handleSys);

  server.onNotFound(handleNotFound);

  yield();
  server.begin();
    if(DEBUG){ ////
      Serial.println("HTTP server started");
    }
  
  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    if(DEBUG){ ////
      Serial.println("\nWaiting for time from Internet");
    }
  
      for (int c = 0; c < 10; c++){
        if(DEBUG){ ////
          Serial.print(".");
        }
        delay(500);
        if (time(nullptr)){
          break;
        }
      }

   if(DEBUG){ ////
      Serial.println(" Time: " + String(ctime(&now)));
   }
  now = time(nullptr);
  timeinfo = localtime(&now);  
}


//####################################
// WeatherUnderground
//###################################

void wu() {
  int hum = int(humidity);          //%
  int pressIn = pressure *0.0296;   //Inch, metric to US
  float tempF = tempBMP * 1.8 + 32; //F
 
  if (client.connect(SERVER, 80)) {
     Serial.print(F("... Connected to server: "));
     client.print(SERVER);
     char c = Serial.read();
     Serial.print(F(", Server response: "));
     Serial.write(c); 
     Serial.println(F(""));
          
     Serial.println(F("... Sending DATA "));
     Serial.println(F(""));

//############################################
     Serial.print(WEBPAGE); 
     Serial.print("ID=");
     Serial.print(ID);
     Serial.print("&PASSWORD=");
     Serial.print(PASSWORD);
     Serial.print("&dateutc=");
     Serial.print("now");    

     Serial.print("&winddir=");
     Serial.print(wind_direction);
     Serial.print("&windspeedmph=");
     Serial.print(wind_speed * 0.62);
     Serial.print("&windgustmph=");
     Serial.print(wind_max_m * 0.62);  
     Serial.print("&windspdmph_avg2m=");
     Serial.print(wind_avg_m * 0.62);
             
     Serial.print("&tempf=");
     Serial.print(tempF);
     Serial.print("&baromin=");
     Serial.print(pressIn);
     Serial.print("&humidity=");
     Serial.print(hum);
     
     //Serial.print("&dewptf=");
     //Serial.print(dewpointf);
     //Serial.print("&solarradiation=");
     //Serial.print(SRadiation);
     
        
     Serial.print("&softwaretype=ArduinoUNOv1&action=updateraw&realtime=1&rtfreq=60");   //Using Rapid Fire, sending data 1time every 30sec
     

     //Finishing the communication
    
     //Serial.println(); // Stopped working, Feb 2017
     //Serial.println("/ HTTP/1.0\r\n\r\n"); // Stopped working, first days of March 2017
     Serial.println("/ HTTP/1.1\r\nHost: host:port\r\nConnection: close\r\n\r\n"); // Working since first days of March 2017

//##################################################    
     
     client.print(WEBPAGE); 
     client.print("ID=");
     client.print(ID);
     client.print("&PASSWORD=");
     client.print(PASSWORD);
     client.print("&dateutc=");
     client.print("now");    

     client.print("&winddir=");
     client.print(wind_direction);
     client.print("&windspeedmph=");
     client.print(wind_speed * 0.62);
     client.print("&windgustmph=");
     client.print(wind_max_m * 0.62);  
     client.print("&windspdmph_avg2m=");
     client.print(wind_avg_m * 0.62);
             
     client.print("&tempf=");
     client.print(tempF);
     client.print("&baromin=");
     client.print(pressIn);
     client.print("&humidity=");
     client.print(hum);
     
     //client.print("&dewptf=");
     //client.print(dewpointf);
     //client.print("&solarradiation=");
     //client.print(SRadiation);
     
        
     client.print("&softwaretype=ArduinoUNOv1&action=updateraw&realtime=1&rtfreq=60");   //Using Rapid Fire, sending data 1time every 30sec
     

     //Finishing the communication
    
     //client.println(); // Stopped working, Feb 2017
     //client.println("/ HTTP/1.0\r\n\r\n"); // Stopped working, first days of March 2017
     client.println("/ HTTP/1.1\r\nHost: host:port\r\nConnection: close\r\n\r\n"); // Working since first days of March 2017
    
     Serial.println(F("... Server Response:"));

     while(client.connected()) {
      while (client.available()) {
        char c = client.read();
        Serial.write(c);
      }
     }
    

    client.flush();
    client.stop();
    
    
  } // if connect
    else {
    Serial.println(F("Connection failed"));
    char c = client.read();
    Serial.write(c);
    client.flush();
    client.stop();}
} //wu


//##################
//## LOOP
//##################
void loop(void){

  if (sec_timer == 0) {
    timerSec();
    sec_timer=1;
    min_timer--;
    draw_timer--;
  } 
  else if (min_timer < 1){
    timerMin();
    min_timer = WIND_AVG_COUNT;
  } 
  else if (draw_timer < 1) {
    rssi = WiFi.RSSI();
    draw();
    draw_timer = REFRESH;
    
    if(DEBUG){ ////
       Serial.print(wind_direction);
       Serial.print(" ");
       Serial.println(wind_speed);
    }
  }
  else {
    server.handleClient();
  }

  
}

