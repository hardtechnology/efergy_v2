/*
   efergy.h
 */
#ifndef efergy_h
#define efergy_h

#include "Arduino.h"
#include <ArduinoJson.h>          // Version 6.x https://github.com/bblanchon/ArduinoJson

#define limit 67      //67 for E2 Classic - bits to Rx from Tx
#define BUFFERSIZE 2048

class efergy {
	public:
		efergy(int inputpin, int DEBUG, int volts);
		void begin(int baudrate);
		void eflog(const char* LOGMSG, bool nl);
		void milliswait(unsigned long wait_ms);
		StaticJsonDocument<256> getjsonevent();
		void setID(int id, int depth, unsigned long statusmA, int intervalsecs);
		char * getcharevent();
		bool mainloop();
		void eventID();
	private:
		unsigned long RXdecodeID(unsigned char bytearray[8]);
		unsigned long RXdecodeMA(unsigned char bytearray[8]);
		unsigned long RXdecodeW(unsigned char bytearray[8], int volts);
		int RXdecodeI(unsigned char bytearray[8]);
		bool RXdecodeP(unsigned char bytearray[8]);
		bool RXdecodeB(unsigned char bytearray[8]);
		unsigned long power2(unsigned char exp1);
		bool RXdecodeCS(unsigned char bytes[]);
		void RXdecodeRAW(unsigned long incomingTime[],unsigned char * bytearray);
		char * timesinceboot();
		void RESET_PKT();
		void Serial_BitTimes(int z);
		void Serial_RAW(unsigned char bytes[]);
		uint32_t get_ccount(void);
		unsigned long Efergy_pulseIn(uint8_t pin, uint8_t state, unsigned long timeout);
		void PrintJSON_IDinfo();
		bool getMonitoredTX(int id);
		int getdetailTXid(int id);
		bool eventID(int id, int valmA, bool battery);
		int charstrlen(char* LOGMSG);
		void IDinfo_monitor();
		int _rxpin;
		int _debug;
		int _voltage;
		unsigned char _bytearray[8];
		StaticJsonDocument<BUFFERSIZE> _IDinfo;  // Store information about received transmitters
		StaticJsonDocument<256> _eventjson;  // Information about most recent RX packet event
		char _printme[BUFFERSIZE];
};

#endif
