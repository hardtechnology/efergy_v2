// Version 2020.8 - August 2020

// This requires the following Libraries to be installed (see includes for links)
// ArduinoJSON 6.x
// ESP8266 1.0.0

#include <efergy.h>
#include <ESP8266WiFi.h>

#define inpin 16          //Input pin (DATA_OUT from A72C01) on pin 2 (D3) (Pin D0 on the Wemos D1 mini)
#define voltage 240
#define DEBUG 0

efergy Efergymon(inpin,DEBUG,voltage);  // Define Efergy Module to look after radio and packet decoding

void setup() {
  Efergymon.begin(74880);
  //setID (int id, int depth, unsigned long statusmA, int intervalsecs)
  Efergymon.setID(2526,20,80,60);  // Washing Machine - depth of 20 records (@6 seconds = 2 minutes), On is considered over 80mA, report every 60 sec
}

void loop() {
  if (Efergymon.mainloop()) {
    if (bool(Efergymon.getjsonevent()["changed"])) {
      bool tx_status = bool(Efergymon.getjsonevent()["status"]);
      int tx_id = int(Efergymon.getjsonevent()["id"]);
      if (tx_id == 2526) {  //Washing Machine
        if ( !tx_status ) {
          Efergymon.eflog("Washing has finished.",true);
        } else {
          Efergymon.eflog("Washing has started.",true);
        }
      }
    }
  yield();
  }
}
