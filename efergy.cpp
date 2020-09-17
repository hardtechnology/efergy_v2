//Based on code from:
//http://rtlsdr-dongle.blogspot.com.au/2013/11/finally-complete-working-prototype-of.html
//http://electrohome.pbworks.com/w/page/34379858/Efergy%20Elite%20Wireless%20Meter%20Hack
//DATA Out from Receiver in pin D3 (remember 3.3v!!)

#include "Arduino.h"
#include <limits.h>			  //Required for our version of PulseIn
#include "wiring_private.h"	  //Required for our version of PulseIn
#include "pins_arduino.h"		  //Required for our version of PulseIn
#include "efergy.h"
#include <ArduinoJson.h>		  //https://github.com/bblanchon/ArduinoJson


// Used for Accurate Clock cycle timing for receiving the bits from the Efergy Receiver
#define RSR_CCOUNT(r)	 __asm__ __volatile__("rsr %0,ccount":"=a" (r))


#define EFERGY_PINWAIT(state) \
	while (digitalRead(pin) != (state)) { if (get_ccount() - start_cycle_count > timeout_cycles) { return 0; } }

int _rxpin;
int _debug;
//unsigned char _bytearray[8];	  //Stores the decoded 8 byte packet
bool _startcom = false;		  //True when a packet is in the process of being received
unsigned long _processingtime;  //needs to be unsigned long to be compatible with PulseIn()
unsigned long _incomingtime[limit];  //stores processing time for eac
int _bytecount;


efergy::efergy(int inputpin, int _debug, int volts) {
	_rxpin = inputpin;
	_debug = _debug;
	_voltage = volts;
}

void efergy::begin(int baudrate) {
	pinMode(_rxpin, INPUT);
	Serial.begin(baudrate);  // Setup Serial Port to allow for logging -74880 is for esp8266 compatibility
	milliswait(1500);  // This ensures our serial has had time to get ready
	if (_debug) {
		eflog("_debug=ON - T=Timeout, S=Start, b=bit, o=Rxtimeout, L=loop routine, E=End of Packet", true);
	}
	eflog("Efergy Monitor has intitialized.",true);
}

// Routing to Output log messages, nl = new line 1(default=true) 0=append to existing line
void efergy::eflog(const char* LOGMSG, bool nl) {
	char logbuff[(strlen(LOGMSG) + 20)];
	if (nl) {
		sprintf(logbuff, "\n[%s] %s", timesinceboot(), LOGMSG);
	} else {
		sprintf(logbuff, "%s", LOGMSG);  
	}
	Serial.print(logbuff);
}

// Wait a while - but loop in a yield to make our time useful
void efergy::milliswait(unsigned long wait_ms) {
	unsigned long future = millis() + wait_ms;
	while (future >= millis()) {
		yield();
	}
}

// Obtain time since boot in 00d 00:00.00 format
char * efergy::timesinceboot() {
	unsigned long runMillis = millis();
	unsigned long allSeconds = runMillis / 1000;
	int runDays = allSeconds / 86400;
	int secsRemaining = allSeconds % 86400;
	int runHours = allSeconds / 3600;
	secsRemaining = allSeconds % 3600;
	int runMinutes = secsRemaining / 60;
	int runSeconds = secsRemaining % 60;
	static char timebuf[13];
	sprintf(timebuf, "%02dd %02d:%02d.%02ds", runDays, runHours, runMinutes, runSeconds);
	return timebuf;
}

// Make 16-bit Transmitter ID from received by array
unsigned long efergy::RXdecodeID(unsigned char _bytearray[8]) {
	return (((unsigned int)_bytearray[1] * 256) + (unsigned int)_bytearray[2]);
}

// Power of a number - normal Arduino function uses float and 2KB of Flash - this is much smaller
// Used the milliamp decode reoutine
unsigned long efergy::power2 (unsigned char exp1) {
	unsigned long pow1 = 1048576;
	exp1 = exp1 + 5;
	for (int x = exp1; x > 0; x--) {
		pow1 = pow1 / 2;
	}
	return pow1;
}

// Decode from the RX packet what the milliAmp current draw is
unsigned long efergy::RXdecodeMA(unsigned char _bytearray[8]) {
	unsigned long rxma = ((1000 * ((unsigned long)((_bytearray[4] * 256) + (unsigned long)_bytearray[5]))) / power2(_bytearray[6]));
	if ( rxma > 100000 ) {
		return NULL;  // Return Null if value is invalid/out of range (TEST THIS)
	} else {
		return rxma;  // Return mA (milliamps) of load
	}
}

// Decode from the RX pcket what the Wattage is (using a custom voltage provided)
unsigned long efergy::RXdecodeW(unsigned char _bytearray[8], int volts) {
	unsigned long rxma = RXdecodeMA(_bytearray);
	unsigned long rx_watts = ( rxma * volts ) / 1000;
	return rx_watts;
}

// Decode the Interval between refreshes of the data from the transmitter
int efergy::RXdecodeI(unsigned char _bytearray[8]) {
	//Transmit Intervals
	if ( (_bytearray[3] & 0x30) == 0x10) {  //xx11xxxx = 12 seconds
		return 12;
	} else if ( (_bytearray[3] & 0x30) == 0x20) {  //xx10xxxx = 18 seconds
		return 18;
	} else if ( (_bytearray[3] & 0x30) == 0x00) {  //xx00xxxx = 6 seconds
		return 6;
	} else {
		return 0;
	}
}

// Decode from RX packet if the Link marker is set. Transmitter is in pairing mode
bool efergy::RXdecodeP(unsigned char _bytearray[8]) {
	if ( (_bytearray[3] & 0x80) == 0x80) {
		return true;
	} else if ( (_bytearray[3] & 0x80) == 0x00 ) {
		return false;
	}
	return false;
}

// Decode the Battery Status from the 8 byte received array
bool efergy::RXdecodeB(unsigned char _bytearray[8]) {
	//Check the Battery status of the Transmitter - False means low battery
	if ( (_bytearray[3] & 0x40) == 0x40) {
		return true;  // true=ok
	} else {
		return false;  // false=bad
	}
	return false;
}

// Decode the Checksum from the received packet
bool efergy::RXdecodeCS(unsigned char bytes[]) {
	unsigned char tbyte = 0;
	bool OK1 = false;
	for (int cs = 0; cs < 7; cs++) {
		tbyte += bytes[cs];
	}
	tbyte &= 0xff;
	if ( tbyte == bytes[7] ) {
		if ( bytes[0] == 7 || bytes[0] == 9 ) {
			OK1 = true;
		}
	}
	return OK1;
}

// unsigned long _incomingtime[limit]
void efergy::RXdecodeRAW(unsigned long _incomingtime[],unsigned char * _bytearray) {
	int dbit = 0;
	int bitpos = 0;
	_bytecount = 0;
	unsigned char bytedata = 0;
	for (int k = 1; k <= limit; k++) { //Start at 1 because the first bit (0) is our long 500uS start
		if (_incomingtime[k] != 0) {
			if (_incomingtime[k] > 20UL ) { //Original Code was 20 - smallest is about 70us - so 40 to be safe with loop overheads
				dbit++;
				bitpos++;
				bytedata = bytedata << 1;
				if (_incomingtime[k] > 85UL) { // 0 is approx 70uS, 1 is approx 140uS
					bytedata = bytedata | 0x1;
				}
				if (bitpos > 7) {
					_bytearray[_bytecount] = bytedata;
					bytedata = 0;
					bitpos = 0;
					_bytecount++;
				}
			}
		}
	}
}

// Perform reset ready for new packet to be received
void efergy::RESET_PKT() {
	_startcom = false;
	//memset (_incomingtime, -1, sizeof(_incomingtime));
	memset (_incomingtime, 0, sizeof(_incomingtime));
	memset (_bytearray, 0, sizeof(_bytearray));
}

void efergy::Serial_BitTimes(int z) {
	Serial.print("{\"BituSec\":[");
	for (int y = 0; y <= z; y++) {
		Serial.print(_incomingtime[y]);
		if (y < z ) {
			Serial.print(",");
		} else {
			Serial.println("]}");
		}
	}
}

// Dump out the raw byte array to the Serial port (used for debugging)
void efergy::Serial_RAW(unsigned char bytes[]) {
	if (!RXdecodeCS(bytes)) {
		char buf[40];
		sprintf(buf, "{\"RAW\":[%d,%d,%d,%d,%d,%d,%d,%d]}", bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6], bytes[7]);
		Serial.print(buf);
	}
}

//static inline uint32_t efergy::get_ccount(void) {
uint32_t efergy::get_ccount(void) {
	uint32_t ccount;
	RSR_CCOUNT(ccount);
	return ccount;
}

// max timeout is 27 seconds at 160MHz clock and 54 seconds at 80MHz clock
unsigned long efergy::Efergy_pulseIn(uint8_t pin, uint8_t state, unsigned long timeout) {
	if (timeout > 1000000) {
		timeout = 1000000;
	}
	const uint32_t timeout_cycles = microsecondsToClockCycles(timeout);
	const uint32_t start_cycle_count = get_ccount();
	EFERGY_PINWAIT(!state);
	EFERGY_PINWAIT(state);
	const uint32_t pulse_start_cycle_count = get_ccount();
	EFERGY_PINWAIT(!state);
	return clockCyclesToMicroseconds(get_ccount() - pulse_start_cycle_count);
}




bool efergy::mainloop() {
  //Loop through and store the high pulse length
  int flag = false;
  int p = 0;
  int prevlength = 0;
  int bytecount;
  char tempbuff[60]; //Temporary buffers for charactor concatenating, etc...
  _processingtime = Efergy_pulseIn(_rxpin, HIGH, 5000); //Returns unsigned long - 5 millisecond timeout
  if (_processingtime > 480UL ) {
	//If the High Pulse is greater than 450uS - this is the start of a packet
	_startcom = true;
	_incomingtime[0] = _processingtime;
	p = 1;
	if (_debug) { eflog("Sb",true); }
	//Process individual bits in this loop - store array of packet times in incomingtime[]
	while ( _startcom ) {
	  _processingtime = Efergy_pulseIn(_rxpin, HIGH, 600); //Returns unsigned long - 300uS timeout
	  if ( _processingtime == 0 ) { //With An Active Signal we will basically never timeout
		if (_debug) { eflog("T",false); }
		if (_debug) { prevlength = p; } //Store this packet length in prevlength for debugging
		RESET_PKT();
	  } else if ( _processingtime > 480UL ) {
		//Start of new packet - reset if part of the way through - helps with interference
		_incomingtime[0] = _processingtime;
		p = 1;
	  } else if (_processingtime > 20 ) {
		_incomingtime[p] = _processingtime; //Save time of each subsequent bit received
		p++;
		//If packet has been received (67 bits) - mark it to be processed
		if (_debug) { eflog("b",false); }
		if (p > limit) {
		  // If our length is longer than our buffer - then reset
		  _startcom = false;
		  if (_debug) { prevlength = p; } //Store this packet length in prevlength for debugging
		  yield();  // Yield ASAP as we are receiving a mess of information - give some time back
		  flag = true;  // Flag set for complete packet received
		  if (_debug) { eflog("E",false); }
		}
		//end of limit if
	  }
	  //end of proctime > 30
	}
	//End of _startcom == true
  } else if ( _processingtime == 0 ) { //End of packet RX
	yield();
  }

  //If a complete packet has been receied (flag = 1) then process the individual bits into a byte array
  if ( flag == true ) {
	RXdecodeRAW(_incomingtime,_bytearray);
	//Process Received Packet - 8 bytes long - with a valid checksum
	if ( _bytecount == 8 && RXdecodeCS(_bytearray)) { 
	  flag = false;
	  int TXID = RXdecodeID(_bytearray);
	  int TXbatt = RXdecodeB(_bytearray);
	  _eventjson.clear();
	  _eventjson["ts"] = (millis() / 100);
	  _eventjson["id"] = TXID;
	  _eventjson["type"] = "RX";
	  _eventjson["mA"] = RXdecodeMA(_bytearray);
	  _eventjson["W"] = RXdecodeW(_bytearray, _voltage);
	  _eventjson["Int"] = RXdecodeI(_bytearray);
	  _eventjson["Pair"] = (RXdecodeP(_bytearray) ? "On" : "Off");
	  _eventjson["Batt"] = (TXbatt ? "OK" : "Low");
	  _eventjson["Mon"] = (getMonitoredTX(TXID) ? "Yes" : "No");
	  //Log our Received Data
	  eflog(getcharevent(),true);
	  // TODO: check if mA is valid
	  if (getMonitoredTX(TXID)) {
		  // This transmitter we are logging
		  if ( eventID(TXID,_eventjson["mA"],TXbatt) ){ return true; }
	  }
	} else {
	  //We failed the Checksum test on an 8 byte Packet
	  flag = false;
	  if (_debug) {
		eflog("Received Data failed Checksum - or incomplete packet",true);
		Serial.print("CS");
		Serial_BitTimes(limit);
		Serial_RAW(_bytearray);
	  }
	  RESET_PKT();
	}
  }
  return false;
  // End of FLAG == true Routine
}

StaticJsonDocument<256> efergy::getjsonevent() {
	return _eventjson;
}

char * efergy::getcharevent() {
	static char jsonstr[128];
	serializeJson(_eventjson, jsonstr);
	return jsonstr;
}

// Whitelist TX
// Interval for updates (mAmin,mAavg,mAmax,mAnow, lost packets, battery, jitter?, operating (on/off))
bool efergy::eventID(int id, int valmA, bool battery) {
	//get Listref of TXID
	int arrayid = getdetailTXid(id);
	bool reporttime = false;
	int val_size = _IDinfo["log"][arrayid]["val"].size();
	//update last RX timestamp on TX
	_IDinfo["log"][arrayid]["lastRX"] = millis() / 1000; //Store time in seconds since last Rx
	// ADD new value to end of buffer
	_IDinfo["log"][arrayid]["val"].add(valmA);
	//remove old value if over buffer depth
	if (val_size >= int(_IDinfo["log"][arrayid]["dpth"]) ) {
		_IDinfo["log"][arrayid]["val"].remove(0);
	}
	//If our Json document is getting full - perform a clear to defrag it
	IDinfo_monitor();
	//Report at Interval
	int val_interval = int(_IDinfo["log"][arrayid]["int"]);
	if ( ( int(_IDinfo["log"][arrayid]["lastrep"]) + val_interval ) <= int(millis() / 1000) ) {
		_IDinfo["log"][arrayid]["lastrep"] = int(millis() / 1000); //Store time in seconds since last report created
		// array of values to loop through
		int val_min = 9999999;
		int val_max = 0;
		long val_total = 0;
		for (int i = 0; i < _IDinfo["log"][arrayid]["val"].size(); i++) {
			int arrval = int(_IDinfo["log"][arrayid]["val"][i]);
			//Serial.println(arrval);
			if (arrval < val_min) { val_min = arrval; }
			if (arrval > val_max) { val_max = arrval; }
			val_total = val_total + arrval;
		}
		int val_avg = int(val_total / ( _IDinfo["log"][arrayid]["val"].size() ) );
		_eventjson.clear();
		_eventjson["ts"] = (millis() / 100);
		_eventjson["id"] = id;
		_eventjson["type"] = "report";
		_eventjson["avg"] = val_avg;
		_eventjson["min"] = val_min;
		_eventjson["max"] = val_max;
		_eventjson["batt"] = (battery ? "OK" : "Low");
		if ( val_avg < int(_IDinfo["log"][arrayid]["sw"]) ) {
			_eventjson["status"] = false;
		} else {
			_eventjson["status"] = true;
		}
		if ( _IDinfo["log"][arrayid]["status"] != _eventjson["status"] ) {
			_eventjson["changed"] = true;
		} else {
			_eventjson["changed"] = false;
		}
		_IDinfo["log"][arrayid]["status"] = _eventjson["status"];
		eflog(getcharevent(),true);
		// Ensure we hide the first report - prevent lots of change alerts going off on bootup
		if ( _IDinfo["log"][arrayid]["init"] == true ) { reporttime = true; }
		_IDinfo["log"][arrayid]["init"] = true;
	}
	return reporttime;
}

//Setup parameters for a transmitter - interval can be =<15 (minutes) = 150 rx packets
void efergy::setID(int id, int depth, unsigned long statusmA, int intervalsecs) {
	StaticJsonDocument<512> newTX;
	_IDinfo["TX"].add(id);
	newTX["id"] = id;  // The ID number of the Transmitter
	newTX["dpth"] = depth;  // The depth of value buffer we will keep for min/max/avg stats
	newTX["sw"] = statusmA;  // mA to switch the status from off/on
	newTX["int"] = intervalsecs;  // how many seconds between reporting min/avg/max/status
	_IDinfo["log"].add(newTX);
	char tempbuff[90]; //Temporary buffers for charactor concatenating, etc...
	sprintf(tempbuff,"ADDED TX %d with %d events logged, status will change@ %dmA, report@ %d seconds",id,depth,statusmA,intervalsecs);
	eflog(tempbuff,true);
	//PrintJSON_IDinfo();
}

// Get if the TX is a monitored TX to log values to report on
bool efergy::getMonitoredTX(int id) {
	for (int i = 0; i < _IDinfo["TX"].size(); i++) {
		if ( int(_IDinfo["TX"][i]) == id ) {
			return true;
		}
	}
	return false;	
}

// Get if the TX is a monitored TX to log values to report on
int efergy::getdetailTXid(int id) {
	for (int i = 0; i < _IDinfo["log"].size(); i++) {
		if ( int(_IDinfo["log"][i]["id"]) == id ) {
			return i;
			//TX is in our list
		}
	}
	return 0;
}

// Print out the contents of the IDinfo Json document for logging
void efergy::PrintJSON_IDinfo() {
	serializeJsonPretty(_IDinfo, _printme);
	Serial.println(_printme);
}

//Check the Idinfo Json document - ensure it isn't full, and perform some cleaning
void efergy::IDinfo_monitor() {
	if ( int(_IDinfo.memoryUsage()) > int( BUFFERSIZE * .9) ) {
		eflog("[INFO] Performing Garbage Collection",true);
		_IDinfo.garbageCollect();
		if ( int(_IDinfo.memoryUsage()) > int( BUFFERSIZE * .8) ) {
			eflog("[WARN] Buffer size - reduce depth, increase Tx Interval.",true);
			for (int arri = 0; arri < _IDinfo["log"].size(); arri++) {
				_IDinfo["log"][arri]["val"].remove(0);
				_IDinfo["log"][arri]["val"].remove(1);
			}
		}
	}
}
