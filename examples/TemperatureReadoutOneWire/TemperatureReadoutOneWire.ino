#include <KnxTpUart.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// Initialize OneWire and DallasTemperature libs
#define ONE_WIRE_BUS 53
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Define send interval
#define SEND_INTERVAL_MS 5000

// Initialize the KNX TP-UART library on the Serial1 port of Arduino Mega
KnxTpUart knx(&Serial1, "15.15.20");

unsigned long startTime;

void setup() {
  // Define group address to react on (for read requests)
  String READ_GROUP = "0/0/100";

  // Define group address to send temperature to
  String WRITE_GROUP = "0/0/101";

  Serial.begin(9600);
  Serial.println("TP-UART Test");  

  Serial1.begin(19200);
  UCSR1C = UCSR1C | B00100000; // Even Parity

  Serial.print("UCSR1A: ");
  Serial.println(UCSR1A, BIN);

  Serial.print("UCSR1B: ");
  Serial.println(UCSR1B, BIN);

  Serial.print("UCSR1C: ");
  Serial.println(UCSR1C, BIN);

  knx.uartReset();
  knx.addListenGroupAddress(READ_GROUP);
  
  startTime = millis();
}


void loop() {
  if (abs(millis() - startTime) < SEND_INTERVAL_MS) {
    delay(1);
    return;
  }

  startTime = millis();
  float temp = getTemp();
  Serial.print("Sending temp: ");
  Serial.println(temp);  
  bool result = knx.groupWrite2ByteFloat(WRITE_GROUP, temp);
  Serial.print("Sent successfully: ");
  Serial.println(result);
}

void serialEvent1() {
  KnxTpUartSerialEventType eType = knx.serialEvent();
  if (eType == KNX_TELEGRAM) {
     KnxTelegram* telegram = knx.getReceivedTelegram();

     // Is it a read request?
     if (telegram->getCommand() == KNX_COMMAND_READ) {
        knx.groupAnswer2ByteFloat(READ_GROUP, getTemp());
     }   
  }
}

float getTemp() {
  sensors.requestTemperatures();
  return sensors.getTempCByIndex(0);
}
