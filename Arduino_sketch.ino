
#include <SoftwareSerial.h>
#include <Adafruit_GPS.h>
#include <Adafruit_FONA.h>
#include <JsonParser.h>

#include "Location.h"
#include "keys.h"

#define FONA_RST 10
#define FONA_PS 9
#define FONA_KEY 8
#define FONA_RI 2    // <-- must be connected to external interrupt pin
#define FONA_RX 4
#define FONA_TX 3

#define GPS_RX 6
#define GPS_TX 7

#define BUZZER_PIN 11
#define FIX_LED 13

#define GPS_INTERVAL_SECONDS 10
// this is a large buffer for replies
char replybuffer[255];

SoftwareSerial gpsSerial = SoftwareSerial(GPS_TX, GPS_RX);
Adafruit_GPS gps = Adafruit_GPS(&gpsSerial);

SoftwareSerial fonaSerial = SoftwareSerial(FONA_TX, FONA_RX);
Adafruit_FONA fona = Adafruit_FONA(&fonaSerial, FONA_RST);

using namespace ArduinoJson::Parser;

Location current_location = Location();

unsigned long timer = 0;
volatile boolean ringing = false;


// Interrupt is called once a millisecond, looks for any new GPS data, and stores it
SIGNAL(TIMER0_COMPA_vect) {
  char c = gps.read();
  //if (c) UDR0 = c;
}


void ringInterrupt () {
  ringing = true;
}

char *device_serial;

void setup () {
  Serial.begin(115200);
  Serial.println(F("GPS Tracker."));

  pinMode(FONA_KEY, OUTPUT);
  digitalWrite(FONA_KEY, HIGH);

  // Power up the FONA if it needs it
  if (digitalRead(FONA_PS) == LOW) {
    Serial.print(F("Powering FONA on..."));
    while (digitalRead(FONA_PS) == LOW) {
      digitalWrite(FONA_KEY, LOW);
      delay(500);
    }
    digitalWrite(FONA_KEY, HIGH);
    Serial.println(F(" done."));
    delay(500);
  }

  // Start the FONA
  Serial.print(F("Initializing FONA..."));
  while (1) {
    boolean fonaStarted = fona.begin(4800);
    if (fonaStarted) break;
    delay (1000);
  }
  Serial.println(F(" done."));



  // wait for a valid network, nothing works w/o that
  Serial.print(F("Waiting for GSM network..."));
  while (1) {
    uint8_t network_status = fona.getNetworkStatus();
    if (network_status == 1 || network_status == 5) break;
    delay(250);
  }

  while(1){
    Serial.println(F(" setting serial.."));
    //char *serial = &device_serial;
    boolean receivedSerial = getDeviceSerial();
    Serial.println(device_serial);
    if (receivedSerial) break;

  }


  // Sometimes the FONA can't recieve SMSs until it sends one first.
  //fonaSendStatusSMS(MY_PHONE_NUMBER);
  Serial.println(F(" done."));

  // Attach the RI interrupt
  //attachInterrupt(0, ringInterrupt, FALLING);


  // We need to start the GPS second... By default, the last intialized port is listening.
  Serial.print(F("Starting GPS..."));
  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  gps.begin(9600);

  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  gps.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  // uncomment this line to turn on only the "minimum recommended" data
  //GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCONLY);
  // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
  // the parser doesn't care about other sentences at this time

  // Set the update rate
  gps.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);   // 1 Hz update rate
  // For the parsing code to work nicely and have time to sort thru the data, and
  // print it out we don't suggest using anything higher than 1 Hz

  // Request updates on antenna status, comment out to keep quiet
  gps.sendCommand(PGCMD_ANTENNA);

  // the nice thing about this code is you can have a timer0 interrupt go off
  // every 1 millisecond, and read data from the GPS for you. that makes the
  // loop code a heck of a lot easier!
  pinMode(FIX_LED, OUTPUT);
  Serial.println(F(" done."));


  // Timer0 is already used for millis() - we'll just interrupt somewhere
  // in the middle and call the "Compare A" function above
  OCR0A = 0xAF;
  TIMSK0 |= _BV(OCIE0A);
}


void loop () {

  //if (ringing && digitalRead(FONA_PS) == HIGH) handleRing();
  //getNewData();
  int status;
  getStatus(&status);

  switch (status) {

    case 0: { //The device is not lost

      break;
    }

    case 1: {//The device is in lost mode
      findLocation();
      Serial.println(F(" sending location..."));
      break;
    }

    case 2: {//
    Serial.println(F(" case 2."));

    }
  }


  // if millis() wraps around, reset the timer
  //if (timer > millis()) timer = millis();
}

void findLocation() {
  gpsSerial.listen();
  if (gps.newNMEAreceived() && gps.parse(gps.lastNMEA())) {
    boolean isValidFix = gps.fix && gps.HDOP < 5 && gps.HDOP != 0;  // HDOP == 0 is an error case that comes up on occasion.

    digitalWrite(FIX_LED, isValidFix ? HIGH : LOW);

    if (isValidFix) current_location.set(gps);

    if (millis() - timer > GPS_INTERVAL_SECONDS * 1000) {
      Serial.print(F("Satellites: ")); Serial.println((int)gps.satellites);

      if (isValidFix) {
        Serial.print(F("Location: "));
        Serial.print(current_location.latitude, 6);
        Serial.print(F(", "));
        Serial.print(current_location.longitude, 6);
        Serial.print(F(", "));
        Serial.println(current_location.altitude, 2);

        Serial.print(F("HDOP: ")); Serial.println(gps.HDOP);

        //if (digitalRead(FONA_PS) == HIGH && !current_location.isEqual(last_location)) {
        if (digitalRead(FONA_PS) == HIGH) {
          sendLocation();
        //  last_location.set(gps);
        }
      } else {
        Serial.println(F("No valid fix."));
        findLocation();
      }
      Serial.println();

      timer = millis(); // reset the timer
    }
  }

  // if millis() wraps around, reset the timer
  if (timer > millis()) timer = millis();
}

void sendLocation () {
  char *url;
  uint16_t statuscode;
  int16_t length;
  char *response;


  //sprintf (url, "http://data.sparkfun.com/input/%s?private_key=%s&latitude=%s&longitude=%s&hdop=%s&altitude=%s",
    //SPARKFUN_PUBLIC_KEY, SPARKFUN_PRIVATE_KEY, current_location.latitude_c, current_location.longitude_c, current_location.hdop_c, current_location.altitude_c);
  url = "http://frozen-ocean-8287.herokuapp.com/devices/1.json";
  Serial.print(F("Sending: ")); Serial.println(url);

  // Make the FONA listen, we kinda need that...
  fonaSerial.listen();

  uint8_t rssi = fona.getRSSI();

  if (rssi > 5) {
    // Make an attempt to turn GPRS mode on.  Sometimes the FONA gets freaked out and GPRS doesn't turn off.
    // When this happens you can't turn it on aagin, but you don't need to because it's on.  So don't sweat
    // the error case here -- GPRS could already be on -- just keep on keeping on and let HTTP_GET_start()
    // error if there's a problem with GPRS.
    if (!fona.enableGPRS(true)) {
      Serial.println(F("Failed to turn GPRS on!"));
    }

    if (fona.HTTP_GET_start(url, &statuscode, (uint16_t *)&length)) {
      char responseJson[125];
      int index = 0;
      while (length > 0) {
        while (fona.available()) {
          char c = fona.read();

//          // Serial.write is too slow, we'll write directly to Serial register!
//         loop_until_bit_is_set(UCSR0A, UDRE0); /* Wait until data register empty. */
//          UDR0 = c;
          //responseJson[index] = c;
          responseJson[index] = c;
          //response = c;
          length--;
          index++;
          if (! length) break;
        }
      }
      fona.HTTP_GET_end();
      Serial.println(F("response from web"));
      response = responseJson;
      Serial.println(response);
      JsonParser<16> parser;

      JsonObject root = parser.parse(response);

      if (!root.success())
      {
          Serial.println("JsonParser.parse() failed");
      }

      char *latitude  = root["latitude"];
      char *longitude = root["longitude"];
      char *ten = "10.0";
      if (latitude == ten){
        Serial.println(F("this is cool"));
      }


      Serial.println(latitude);
      Serial.println(longitude);

    } else {
      Serial.println(F("Failed to send GPRS data!"));
    }

    if (!fona.enableGPRS(false)) {
      Serial.println(F("Failed to turn GPRS off!"));
    }
  } else {
    Serial.println(F("Can't transmit, network signal strength is crap!"));
  }

  // Put the GPS back into listen mode
  gpsSerial.listen();
}

boolean getDeviceSerial() {

  char *url ;
  char urlc[255];
  uint16_t statuscode;
  int16_t length;
  char *response;

  // Gets SIM card IMEI number.
  char imei[15] = {0}; // MUST use a 16 character buffer for IMEI!
  uint8_t imeiLen = fona.getIMEI(imei);
  Serial.print("SIM card IMEI: "); Serial.println(imei);
  if (imeiLen > 0) {
    sprintf(urlc, "http://frozen-ocean-8287.herokuapp.com/api/getserial/%s", imei);
    Serial.println(F("The url")); Serial.println(urlc);
    uint8_t rssi = fona.getRSSI();

    if (rssi > 5) {
      // Make an attempt to turn GPRS mode on.  Sometimes the FONA gets freaked out and GPRS doesn't turn off.
      // When this happens you can't turn it on aagin, but you don't need to because it's on.  So don't sweat
      // the error case here -- GPRS could already be on -- just keep on keeping on and let HTTP_GET_start()
      // error if there's a problem with GPRS.
      if (!fona.enableGPRS(true)) {
        Serial.println(F("Failed to turn GPRS on!"));
        fona.enableGPRS(false);
        return false;
      }

      if (fona.HTTP_GET_start(urlc, &statuscode, (uint16_t *)&length)) {
        char responseJson[length];
        int index = 0;
        while (length > 0) {
          while (fona.available()) { //While device is available it will itirate the json responce into a char

             char c = fona.read();

//             // Serial.write is too slow, we'll write directly to Serial register!
//             loop_until_bit_is_set(UCSR0A, UDRE0); /* Wait until data register empty. */
//             UDR0 = c;
            responseJson[index] = c;
            length--;
            index++;
            if (! length) break;
          }
        }
        fona.HTTP_GET_end();
        Serial.println(F("response from web"));

        response = responseJson;

        Serial.println(response);
        JsonParser<8> parser;

        JsonObject root = parser.parse(response);

        if (!root.success())
        {
            Serial.println("JsonParser.parse() failed");
            return false;
        }

        char *jsonSerial  = root["serial"];

        device_serial = (char *) malloc(strlen(jsonSerial) + 1); // allocate space
        strcpy(device_serial, jsonSerial); // copy jsonSerial to device_serial
        Serial.println(device_serial);
        if (!fona.enableGPRS(false)) {
          Serial.println(F("Failed to turn GPRS off!"));
        }

        return true;

    } else {
      Serial.println(F("Failed to send GPRS data!"));
      return false;
    }

    if (!fona.enableGPRS(false)) {
      Serial.println(F("Failed to turn GPRS off!"));
    }


  } else {
    Serial.println(F("Can't transmit, network signal strength is crap!"));
    return false;
  }

 }
}

void getStatus(int *status){
  *status = 1;
}

uint8_t barsFromRSSI (uint8_t rssi) {
  // https://en.wikipedia.org/wiki/Mobile_phone_signal#ASU
  //
  // In GSM networks, ASU maps to RSSI (received signal strength indicator, see TS 27.007[1] sub clause 8.5).
  //   dBm = 2 × ASU - 113, ASU in the range of 0..31 and 99 (for not known or not detectable).

  int8_t dbm = 2 * rssi - 113;

  if (rssi == 99 || rssi == 0) {
    return 0;
  } else if (dbm < -107) {
    return 1;
  } else if (dbm < -98) {
    return 2;
  } else if (dbm < -87) {
    return 3;
  } else if (dbm < -76) {
    return 4;
  }

  return 5;
}
