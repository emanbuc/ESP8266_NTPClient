/*

 Udp NTP Client
 
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "WiFiConfig.h"
#include "ProductInfo.h"


#define NTP_PACKET_SIZE 48 // NTP time stamp is in the first 48 bytes of the message
#define NTP_LOCAL_PORT  2390   // local port to listen for UDP packets default: 2390
#define NTP_REMOTE_PORT 123   //default 123

//1: ->usa hostname e ottieni IP da pool 
//0: -> usa indirizzo ip statico
#define NTP_USE_SERVER_HOSTNAME 1 

#define NTP_SERVER_NAME  "time.nist.gov" //NTP Server

//NTP server static IP Address in format 
//IPAddress(NTP_SERVER_IP1, NTP_SERVER_IP2, NTP_SERVER_IP3, NTP_SERVER_IP4);
//IPAddress(129, 6, 15, 28) => time.nist.gov NTP server
#define NTP_SERVER_IP1 192
#define NTP_SERVER_IP2 168
#define NTP_SERVER_IP3 1
#define NTP_SERVER_IP4 100
#define NTP_POOLING_INTERVAL 10000000 //microseconds -> 10 sec
#define NTP_WAIT_TIME 500000 //microseconds -->500ms 
#define NTP_WAIT_TIMEOUT 5000000 //microseconds -->5000s 

#define NTP_STATE_IDLE 0
#define NTP_STATE_SEND 1
#define NTP_STATE_WAIT_RESPONSE 2


IPAddress timeServerIP; 
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

// A UDP instance to let us send and receive packets over UDP
WiFiUDP udp;
unsigned long systemTick = 0;
unsigned long lastNtpRequestTimestamp =0;

int ntpClientState = NTP_STATE_IDLE;

void setup()
{
  Serial.begin(115200);
  Serial.println();
  Serial.println();
  //print Product Info
  Serial.print("FW Name: ");
  Serial.println(FW_NAME);
  Serial.print("FW Revision: ");
  Serial.println(FW_REVISION);

  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting UDP");
  udp.begin(NTP_LOCAL_PORT);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}

void loop()
{
  systemTick = micros();

  switch(ntpClientState){
    case NTP_STATE_IDLE:
      if(systemTick>lastNtpRequestTimestamp+NTP_POOLING_INTERVAL) {
        ntpClientNextState(NTP_STATE_SEND);
      }
      break;
    case NTP_STATE_SEND:
        getNtpTimeServerIp(timeServerIP);
        sendNTPpacket(timeServerIP);
        ntpClientNextState(NTP_STATE_WAIT_RESPONSE);
      break;
    case NTP_STATE_WAIT_RESPONSE:
        if(systemTick>lastNtpRequestTimestamp+NTP_WAIT_TIME){
          if(systemTick>lastNtpRequestTimestamp+NTP_WAIT_TIMEOUT){
            //timeOut => resend
             Serial.print(systemTick);
             Serial.println(" - no packet yet - TIMEOUT  ==> send new request");
             ntpClientNextState(NTP_STATE_SEND);
          }else{
            int cb = udp.parsePacket();
              if (cb) {
                Serial.print("packet received, length=");
                Serial.println(cb);
                parseNtpResponse();
              }
          }
        }
      break;
  }
}


void getNtpTimeServerIp(IPAddress& address){
  Serial.print(systemTick);
  Serial.print(" - ");
  Serial.println("getNtpTimeServerIp");
  Serial.print("NTP_USE_SERVER_HOSTNAME = ");
  Serial.println(NTP_USE_SERVER_HOSTNAME);
  
  if(NTP_USE_SERVER_HOSTNAME){
    //get a random server from the pool
    WiFi.hostByName(NTP_SERVER_NAME, timeServerIP); 
  }else{
    timeServerIP = IPAddress(NTP_SERVER_IP1, NTP_SERVER_IP2, NTP_SERVER_IP3, NTP_SERVER_IP4);
  }
  
}

void ntpClientNextState(int nextState){
  ntpClientState =nextState;
}

void parseNtpResponse(){
    // We've received a packet, read the data from it
    udp.read(packetBuffer, NTP_PACKET_SIZE); // read the packet into the buffer

    //the timestamp starts at byte 40 of the received packet and is four bytes,
    // or two words, long. First, esxtract the two words:

    unsigned long highWord = word(packetBuffer[40], packetBuffer[41]);
    unsigned long lowWord = word(packetBuffer[42], packetBuffer[43]);
    // combine the four bytes (two words) into a long integer
    // this is NTP time (seconds since Jan 1 1900):
    unsigned long secsSince1900 = highWord << 16 | lowWord;
    Serial.print("Seconds since Jan 1 1900 = " );
    Serial.println(secsSince1900);

    // now convert NTP time into everyday time:
    Serial.print("Unix time = ");
    // Unix time starts on Jan 1 1970. In seconds, that's 2208988800:
    const unsigned long seventyYears = 2208988800UL;
    // subtract seventy years:
    unsigned long epoch = secsSince1900 - seventyYears;
    // print Unix time:
    Serial.println(epoch);


    // print the hour, minute and second:
    Serial.print("The UTC time is ");       // UTC is the time at Greenwich Meridian (GMT)
    Serial.print((epoch  % 86400L) / 3600); // print the hour (86400 equals secs per day)
    Serial.print(':');
    if ( ((epoch % 3600) / 60) < 10 ) {
      // In the first 10 minutes of each hour, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.print((epoch  % 3600) / 60); // print the minute (3600 equals secs per minute)
    Serial.print(':');
    if ( (epoch % 60) < 10 ) {
      // In the first 10 seconds of each minute, we'll want a leading '0'
      Serial.print('0');
    }
    Serial.println(epoch % 60); // print the second  
}

// send an NTP request to the time server at the given address
unsigned long sendNTPpacket(IPAddress& address)
{
  Serial.println("sending NTP packet...");
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12]  = 49;
  packetBuffer[13]  = 0x4E;
  packetBuffer[14]  = 49;
  packetBuffer[15]  = 52;

  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  udp.beginPacket(address, NTP_REMOTE_PORT); //NTP requests are to port 123
  udp.write(packetBuffer, NTP_PACKET_SIZE);
  udp.endPacket();
  lastNtpRequestTimestamp = systemTick;
}
