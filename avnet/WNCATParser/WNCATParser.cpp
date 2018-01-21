/*
 * AVNET WNC 14A2A Modem AT command parser.
 *
 * @author Russell Leake
 * @author Niranjan Rao (original)
 * @date 2018-01-20
 *
 * @copyright &copy; 2018 Eaton (russellleake@eatoncom
 *
 * ```
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 * ```
 */

#include <cctype>
#include <fsl_rtc.h>
#include <string>
#include "WNCATParser.h"
#include "mbed-trace/mbed_trace.h"

#define TRACE_GROUP "wncATP"

#ifdef NCIODEBUG
#  define CIODUMP(buffer, size)
#  define CIODEBUG(...)
#  define CSTDEBUG(...)
#else
#  define CIODUMP(buffer, size) _debug_dump("GSM", buffer, size) /*!< Debug and dump a buffer */
#  define CIODEBUG(...)  printf(__VA_ARGS__)                     /*!< Debug I/O message (AT commands) */
#  define CSTDEBUG(...)  printf(__VA_ARGS__)                     /*!< Standard debug message (info) */
//#  define CSTDEBUG(fmt, ...) printf("%10.10s:%d::" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);
//#  define CIODEBUG(fmt, ...) printf("%10.10s:%d::" fmt, __FUNCTION__, __LINE__, ##__VA_ARGS__);
#endif

#define GSM_UART_BAUD_RATE 115200
#define RXTX_BUFFER_SIZE   1500
#define MAX_SEND_BYTES     1400

DigitalOut  mdm_uart2_rx_boot_mode_sel(PTC17);  // on powerup, 0 = boot mode, 1 = normal boot
DigitalOut  mdm_power_on(PTB9);                 // 0 = modem on, 1 = modem off (hold high for >5 seconds to cycle modem)
DigitalOut  mdm_wakeup_in(PTC2);                // 0 = let modem sleep, 1 = keep modem awake -- Note: pulled high on shi
DigitalOut  mdm_reset(PTC12);                   // active high
DigitalOut  shield_3v3_1v8_sig_trans_ena(PTC4); // 0 = disabled (all signals high impedence, 1 = translation active
DigitalOut  mdm_uart1_cts(PTD0);


WNCATParser::WNCATParser(PinName txPin, PinName rxPin, PinName rstPin, PinName pwrPin)
    : _serial(txPin, rxPin, RXTX_BUFFER_SIZE), _powerPin(pwrPin), _resetPin(rstPin),  _packets(0), _packets_end(&_packets) 
{
    tr_warn("WNC [--] init\r\n");
    _serial.baud(GSM_UART_BAUD_RATE);
    _powerPin = 0;
    _initialized = false;
}

bool WNCATParser::hard_reset(void) {
   // Hard reset the modem (doesn't go through
   // the signal level translator)
   mdm_reset = 0;

   // disable signal level translator (necessary
   // for the modem to boot properly).  All signals
   // except mdm_reset go through the level translator
   // and have internal pull-up/down in the module. While
   // the level translator is disabled, these pins will
   // be in the correct state.
   shield_3v3_1v8_sig_trans_ena = 0;

   // While the level translator is disabled and ouptut pins
   // are tristated, make sure the inputs are in the same state
   // as the WNC Module pins so that when the level translator is
   // enabled, there are no differences.
   mdm_uart2_rx_boot_mode_sel = 1;   // UART2_RX should be high
   mdm_power_on = 0;                 // powr_on should be low
   mdm_wakeup_in = 1;                // wake-up should be high
   mdm_uart1_cts = 0;                // indicate that it is ok to send

   //Now, enable the level translator, the input pins should now be the
   //same as how the M14A module is driving them with internal pull ups/downs.
   //When enabled, there will be no changes in these 4 pins...
   shield_3v3_1v8_sig_trans_ena = 1; 

   return true;
}


bool WNCATParser::startup(void) {
    tr_debug("WNC [--] startup\r\n");

   hard_reset();

   wait_ms(2000);

   bool success = reset();

   _initialized = success;
   return success;
}

bool WNCATParser::powerDown(void) {
   bool normalPowerDown = tx("AT@SHUTDOWN") && rx("OK", 20);
   _powerPin =  0;
   return normalPowerDown;
}

bool WNCATParser::isModemAlive() {
   return (tx("AT") && rx("OK"));
}

int WNCATParser::checkGPRS() {
   int val = -1;
   if (!isModemAlive())
      return false;
   int ret = (tx("AT+CGATT?") && scan("+CGATT: %d", &val) && rx("OK", 10));
   tr_debug("checkGPRS: %s", val ? "ATTACHED":"DETACHED");
   return val && ret;
}

bool WNCATParser::reset(void) {
    //char response[4];
    char response[70];
    //int val = -1;
	 int ret = 0;

    bool modemOn = false;
    for (int tries = 0; !modemOn && tries < 10; tries++) {
        tr_warn("WNC [--] !! reset (%d)\r\n", tries);


        // see if the modem replies health first
        if (isModemAlive()) return true;
			wait_ms(500);

        // TODO check if need delay here to wait for boot
        // Emit AT looking for AT or OK (echo potentially enabled)
        for (int i = 0; !modemOn && i < 1; i++) {
            modemOn = (tx("AT") && scan("%2s", &response)
                       && (!strncmp("AT", response, 2) || !strncmp("OK", response, 2)));

            wait_ms(500);
        }
    }

    if (modemOn) {
        // TODO check if the parser ignores any lines it doesn't expect
        // disable echo
        modemOn = tx("ATE0") && scan("%3s", response)  // echo off
                  && (!strncmp("ATE0", response, 3) || !strncmp("OK", response, 2));

         tx("AT+CMEE=2") && rx("\%CMEEU: 2") && rx("OK"); // 2 - verbose error, 1 - numeric error, 0 - just ERROR

        //ret = tx("AT&V") && rx("OK");
  		  // Get firmware version
  		  //tx("AT+GMR") && scan("MPSS: %60s", response) && rx("OK");
  		  tx("AT+GMR") && readline(response, 60, 5) && rx("OK");
		  tr_debug("%s\n", response);

  		  //tx("AT+QNWINFO") && scan("%60s", response) && rx("OK");
		  //tr_debug("%s\n", response);
  		  //tx("AT%%CCID") && scan("%60s", response) && rx("OK");
		  //tr_debug("%s\n", response);
        
		  //ret |= tx("AT%%CMATT=0") && rx("OK");
		  //ret |= tx("AT+COPS?") && rx("OK");
		  //ret |= tx("AT%%CMATT=1") && rx("OK");

		  ret |= tx("AT+CMGF=1") && rx("OK");
        
        //RDL:  TODO these are broken
        //ret |= tx("AT+CPMS?") && rx("OK");
        //ret |= tx("AT+CPMS=SM,SM,SM") && rx("OK");
        

/*TODO Do we need to save the setting profile
        tx("AT&W");
        rx("OK");*/
    }
    return modemOn;
}


bool WNCATParser::requestDateTime() {

    bool tdStatus = false;

    tdStatus = (tx("AT+QNITZ=1") && rx("OK", 10)
                && tx("AT+CTZU=2") && rx("OK", 10)
                && tx("AT+CFUN=1") && rx("OK", 10)
                && tx("AT+CCLK=\"17/05/19,16:37:54+00\"")&& rx("OK"));

    bool connected = false;
    for (int networkTries = 0; !connected && networkTries < 20; networkTries++) {
        int bearer = -1, status = -1;
        if (tx("AT+CGREG?") && scan("+CGREG: %d,%d", &bearer, &status) && rx("OK", 15)) {
            // TODO add an enum of status codes
            connected = status == 1 || status == 5;
        }
        // TODO check if we need to use thread wait
        wait_ms(1000);
    }
    tdStatus &= (tx("AT+QNTP=\"pool.ntp.org\"") && rx("OK"));

    return tdStatus && connected;
}

bool WNCATParser::connect(const char *apn, const char *userName, const char *passPhrase) {
    // TODO implement setting the pin number, add it to the contructor arguments

    bool connected = false, attached = false;

    //TODO do we need timeout here
    for (int tries = 0; !connected && !attached && tries < 3; tries++) {

        int rawRSSI, ber;
        tx("AT+CSQ") && scan("+CSQ: %d,%d", &rawRSSI, &ber) && rx("OK");
        tr_debug("rawRSSI/ber: %d, %d\n", rawRSSI, ber);

         // check if SIM is locked
        tx("AT+CPIN?") && rx("OK");

        // connect to the mobile network
        //for (int networkTries = 0; !connected && networkTries < 20; networkTries++) {
        for (int networkTries = 0; !connected && networkTries < 5; networkTries++) {
            int bearer = -1, status = -1;
            if (tx("AT+CREG?") && scan("+CREG: %d,%d", &bearer, &status) && rx("OK", 10)) {
            //if (tx("AT+CREG=2") && scan("+CREG: %d,%d", &bearer, &status) && rx("OK", 10)) {
                // TODO add an enum of status codes
                connected = status == 1 || status == 5;
                //if (status == 3)
                //  tx("AT+CFUN=1") && rx("OK");
            }
            // TODO check if we need to use thread wait
            wait_ms(1000);
        }
        if (!connected) continue;

        // Convert WNC RSSI into dBm range:
        //  0 - -113 dBm
        //  1 - -111 dBm
        //  2..30 - -109 to -53 dBm
        //  31 - -51dBm or >
        //  99 - not known or not detectable
       /*
           if (rawRssi == 99)
         *dBm = -199;
         else if (rawRssi == 0)
         *dBm = -113;
         else if (rawRssi == 1)
         *dBm = -111;
         else if (rawRssi == 31)
         *dBm = -51; 
         else if (rawRssi >= 2 && rawRssi <= 30)
         *dBm = -113 + 2 * rawRssi;
         else {
         dbgPuts("Invalid RSSI!");
         return (false);
         }
         */


        // attach GPRS
        //if (!(tx("AT+QIDEACT") && rx("DEACT OK"))) continue;
        // activate cid
        //tx("AT+CGACT?") && rx("OK");
        //if (!(tx("AT+CGACT=1,1") && rx("OK"))) continue;

        /*
        for (int attachTries = 0; !attached && attachTries < 20; attachTries++) {
            attached = tx("AT+CGATT=1") && rx("OK", 10);
            wait_ms(2000);
        }
        if (!attached) continue;
        tr_info("Attached");
        */

        // set APN and finish setup
        //attached =
        //    tx("AT+QIFGCNT=0") && rx("OK") &&
        //    tx("AT+QICSGP=1,\"%s\",\"%s\",\"%s\"", apn, userName, passPhrase) && rx("OK", 10) &&
        //    tx("AT+QIREGAPP") && rx("OK", 10) &&
        //    tx("AT+QIACT") && rx("OK", 10);
        // RDL:  TODO  PDNSET will also take userName and passPhrase
        tx("AT%%PDNSET=1,%s,IP", apn) && rx("OK", 10);
                     //tx("AT+CGACT=1") && rx("OK", 10);

        
  		  tx("AT@INTERNET=1") && rx("OK");
  		  tx("AT@SOCKDIAL=1") && rx("OK");
    }

    // Send request to get the local time
    //attached &= requestDateTime();

    return connected && attached ;
}

bool WNCATParser::disconnect(void) {
    //return (tx("AT+QIDEACT") && rx("DEACT OK"));
    //return (tx("AT+CGACT=0") && rx("DEACT OK"));
    //return (tx("AT+CGACT=1,0") && rx("OK"));
    return true;
}

char *parse_dotstring(char *start, char *dest) {
   char *ptr, *ptr2;
   int size;

   ptr = start;
   ptr2 = strchr(ptr, ',');
   size = ptr2-ptr;
   memcpy(dest, ptr, size);
   dest[size] = '\0';

   return ptr2;
}

void parse_ipstats(char *response, struct WncIpStats *ipstats) {
   char *ptr, *ptr2;
   int size;

   // skip preamble
   //ptr = strchr(response, ' ');
   //printf("%s\n",ptr++);

   ptr = strchr(response, '\"');
   ptr +=1;
   ptr = strchr(ptr, '\"');
   ptr +=2;

   ptr2 = strchr(ptr, '.');
   ptr2 = strchr(ptr2+1, '.');
   ptr2 = strchr(ptr2+1, '.');
   ptr2 = strchr(ptr2+1, '.');
   size = ptr2-ptr;
   memcpy(ipstats->ipaddr, ptr, size);
   ipstats->ipaddr[size] = '\0';

   ptr = parse_dotstring(ptr2+1, ipstats->mask);
   ptr = parse_dotstring(ptr+1, ipstats->gateway);
   ptr = parse_dotstring(ptr+1, ipstats->dnsPrimary);
   ptr = parse_dotstring(ptr+1, ipstats->dnsSecondary);
}

const char *WNCATParser::getIPAddress(void) {
   
    tr_debug("getIPAddress()\n");
    if(!_initialized) {
       tr_error("not initialized\n");
       return NULL;
    }
    //'+CGCONTRDP: 1,5,"m2m.com.attz.mnc170.mcc310.gprs",10.192.234.63.255.255.255.128,10.192.234.1,8.8.8.8,8.8.4.4,,,'
    char buffer[256];
    //int size;
    tx("AT+CGCONTRDP=1");
    if (scan("+CGCONTRDP: %256s", buffer) == 0) {
        tr_error("getIPAddress: not connected\n");
        return NULL;
    }

    if (strlen(buffer) <= 11) {
       tr_error("getIPAddress: not connected\n");
       return NULL;
    }
    tr_debug(buffer);

    rx("OK");

    parse_ipstats(buffer, &_ipstats);
    printf("cid: %d\n",_ipstats.cid);
    printf("bid: %d\n",_ipstats.bearerid);
    printf("ip:  %s\n",_ipstats.ipaddr);
    printf("mask:%s\n",_ipstats.mask);
    printf("gw:  %s\n",_ipstats.gateway);
    printf("dns1:%s\n",_ipstats.dnsPrimary);
    printf("dns2:%s\n",_ipstats.dnsSecondary);

    strcpy(_ip_buffer, _ipstats.ipaddr);
    return _ip_buffer;
}

bool WNCATParser::getIMEI(char *getimei) {
    if (!(tx("AT+GSN") && scan("%s", _imei))) {
        return 0;
    }
    strncpy(getimei, _imei, 16);
    return 1;
}

bool WNCATParser::getICCID(char *geticcid) {
    if (!(tx("AT%%CCID") && scan("%%CCID: %s", _iccid))) {
        return 0;
    }
    strncpy(geticcid, _iccid, 16);
    return 1;
}

bool WNCATParser::getLocation(char *lon, char *lat, tm *datetime, int *zone) {

    char response[32] = "";

    string responseLon;
    string responseLat;

    // get location - +QCELLLOC: Longitude, Latitude
    if (!(tx("AT+QCELLLOC=1") && scan("+QCELLLOC: %s", response) && rx("OK")))
        return false;

    string str(response);
    size_t found = str.find(",");
    if (found <= 0) return false;

    responseLon = str.substr(0, found - 1);
    responseLat = str.substr(found + 1);
    strcpy(lon, responseLon.c_str());
    strcpy(lat, responseLat.c_str());

    // get network time
    if (!((tx("AT+CCLK?")) && (scan("+CCLK: \"%d/%d/%d,%d:%d:%d+%d\"",
                                    &datetime->tm_year, &datetime->tm_mon, &datetime->tm_mday,
                                    &datetime->tm_hour, &datetime->tm_min, &datetime->tm_sec,
                                    &zone))) && rx("OK")){
        CSTDEBUG("WNC [--] !! no time received\r\n");
        return false;
    }
    if (datetime->tm_mon == 05 && datetime->tm_year == 17)
        return false;

    //    int tm_sec;			/* Seconds.	[0-60] (1 leap second) */
    //    int tm_min;			/* Minutes.	[0-59] */
    //    int tm_hour;			/* Hours.	[0-23] */
    //    int tm_mday;			/* Day.		[1-31] */
    //    int tm_mon;			/* Month.	[0-11] */
    //    int tm_year;			/* Year	- 1900.  */
    //    int tm_wday;			/* Day of week.	[0-6] */
    //    int tm_yday;			/* Days in year.[0-365]	*/
    //    int tm_isdst;			/* DST.		[-1/0/1]*/
    /* WNC returns only last 2-digits of the year
     * 'AT+CCLK="17/05/19,16:37:54+00"'
     * to convert time into UTC `mktime(tm)`, we need time number of years since 1900
     * Hence the calculation
     * year + 200 > gives us current year - 1900 = 100
     * So in this case add 100 to the year received from WNC
     */
    datetime->tm_year += 100;
    /* calculate months from 0*/
    datetime->tm_mon -= 1;

    CSTDEBUG("WNC [--] !! %d/%d/%d::%d:%d:%d::%d\r\n",
             datetime->tm_year, datetime->tm_mon, datetime->tm_mday,
             datetime->tm_hour, datetime->tm_min, datetime->tm_sec,
             *zone);
    return true;
}

bool WNCATParser::modem_battery(uint8_t *status, int *level, int *voltage) {
    return (tx("AT+CBC") && scan("+CBC: %d,%d,%d", status, level, voltage));
}

bool WNCATParser::isConnected(void) {
    tr_debug("isConnected()");
    return getIPAddress() != 0;
}

bool WNCATParser::queryIP(const char *url, char *theIP) {

   tr_debug("queryIP(url=%s)\n", url);
    for(int i = 0; i < 3; i++) {
        char *quote;
        char response[64];
        tx("AT@DNSRESVDON=\"%s\"", url);

        while(1) {
            readline(response, 64, 10);
            if (!strncmp("OK", response, 2))
               return true;

            if (!strncmp("ERROR", response, 5))
               return false;
            
            sscanf(response, "@DNSRESVDON:\"%s\"", theIP);
            quote = strchr(theIP, '\"');
            *quote = 0;
            tr_debug("IP: %s\n", theIP);
        }
        
        wait(1);
    }
    return false;
}

bool WNCATParser::open(nsapi_protocol_t type, int id) {
    int id_resp = -1;

    tr_debug("open(type=%s, id=%d\n",type == NSAPI_UDP ? "UDP" : "TCP",id);

    if (id > WNC_SOCKET_COUNT) {
        return false;
    }

    for(int i = 0; i < 3; i++) {

       if (tx("AT@SOCKCREAT=%d,0", type==NSAPI_UDP ? WNC_UDP : WNC_TCP) && 
             scan("@SOCKCREAT:%d",&id_resp) && 
             rx("OK")) {

          if (id != id_resp) return false; //fail

          return true;
       }
    }

    //TODO return a error code to debug the open fail in a better way
    return false;
}

bool WNCATParser::socket_connect(int id, const char *addr, int port) {

    tr_debug("socket_connect(id=%d, addr=%s, port=%d)\n",id,addr,port);
    if (!id) return false;
    if (!addr) return false;
    if (!port) return false;

    if (id > WNC_SOCKET_COUNT) {
        return false;
    }

    for(int i = 0; i < 3; i++) {
       // connect to socket
       if (tx("AT@SOCKCONN=%d,\"%s\",%d,30",id,addr,port) && rx("OK"))
          return true;
    }

    //TODO return a error code to debug the open fail in a better way
    return false;
}


void itohex(char *str, uint8_t *data, unsigned int data_length)
{
	char const hex_chars[16] = { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F' };

	for( unsigned int i = 0; i < data_length; ++i ) {
		char const byte = data[i];

		*str++ = hex_chars[ ( byte & 0xF0 ) >> 4 ];
		*str++ = hex_chars[ ( byte & 0x0F ) >> 0 ];
	}
}

bool WNCATParser::send(int id, const void *data, uint32_t amount) {
   bool ret = false;

   tr_debug("send(id=%d, amount=%d)\n", id, (int)amount);
    //if (!(tx("AT+QISRVC=1") && rx("OK"))) return false;

    uint8_t *tempData = (uint8_t *) data;
    uint32_t remainingAmount = amount;
    int sendDataSize = 0;
    while (remainingAmount > 0) {
        sendDataSize = remainingAmount < MAX_SEND_BYTES ?  remainingAmount : MAX_SEND_BYTES;
        remainingAmount -= sendDataSize;

         tr_debug("send(sendDataSize=%d, remainingAmount=%d)\n", (int)sendDataSize, remainingAmount);

        /* TODO if this retry is required?
         * TODO May take a second try if device is busy
         * TODO use QISACK after you receive SEND OK, to check if whether the data has been sent to the remote
         */
        for (int i = 0; i < 2; i++) {
            // dump binary
				CIODUMP((uint8_t *) tempData, (size_t)sendDataSize);

            // binary to hex string
            char numStr[RXTX_BUFFER_SIZE];
            itohex(numStr, tempData, sendDataSize);
            numStr[sendDataSize * 2] = '\0';

            // write to socket
            int wrote;
            ret = tx("AT@SOCKWRITE=%d,%d,\"%s\"", id, sendDataSize, numStr) && 
                  scan("@SOCKWRITE:%d",&wrote) && rx("OK");

            if (ret && (wrote == sendDataSize)) break;
        } //for:i
        tempData += sendDataSize;
    }//while
    return true;
}

/*TODO Use this commmand to get the IP status before running IP commands(open, send, ..)
 * getIPAddress() can also be used
 * A string parameter to indicate the status of the connection
 */
int WNCATParser::queryConnection() {
    int qstate = -1;

    return qstate;
}

//bool parse_ip_addr(char *str) {
//   return true;
//}

void WNCATParser::_packet_handler(const char *response) {
   tr_error("_packet_handler unsupported");
   return;
}

int32_t WNCATParser::_check_queue(int id, void *data, uint32_t amount) {
   // check if any packets are ready for us
   for (struct packet **p = &_packets; *p; p = &(*p)->next) {
      tr_debug("Inspect packet (id=%d)\n",(*p)->id);
      if ((*p)->id == id) {
            struct packet *q = *p;

            tr_debug("Packet ready: id=%d len=%d\n",(*p)->id, (int)q->len);
            if (q->len <= amount) { // Return and remove full packet
               printf("%p\n",data);
               memcpy(data, q->data, q->len);

               // dump binary data
               //CIODUMP((uint8_t *) data, (size_t)q->len);

               if (_packets_end == &(*p)->next) {
                  _packets_end = p;
               }
               *p = (*p)->next;

               uint32_t len = q->len;
               free(q);
               return len;
            } else { // return only partial packet
               memcpy(data, q->data, amount);

               q->len -= amount;
               memmove(q->data, (uint8_t *) q->data + amount, q->len);

               return amount;
            }
      }
   }
   return 0;
}

int32_t WNCATParser::_enqueue(int id, char *data, uint32_t amount) {
   struct packet *packet = (struct packet *) malloc(sizeof(struct packet) + amount);
   if (!packet) {
      return -1;
   }

   packet->id = id;
   packet->len = (uint32_t) amount;
   packet->next = 0;

   // string to binary
   char tmp[3];
   tmp[2] = 0;
   char *packet_data = packet->data;
   for(unsigned int n=0; n<amount*2; n+=2) {
      tmp[0] = *data++;
      tmp[1] = *data++;
      *packet_data=(char)strtol(tmp,NULL,16);
      //tr_debug("%s %02x\n",tmp, *packet_data);
      packet_data++;
   }

   // dump binary data
   //CIODUMP((uint8_t *) packet->data, (size_t)amount);

   tr_debug("Enqueue packet id=%d len=%u\n",packet->id, (unsigned int)packet->len);

   // append to packet list
   *_packets_end = packet;
   _packets_end = &packet->next;

   return amount;
}

int32_t WNCATParser::recv(int id, void *data, uint32_t amount) {
   char recvBuffer[RXTX_BUFFER_SIZE];
   int32_t ret = 0;
    Timer timer;
    timer.start();

    tr_debug("recv(id=%d, amount=%u)\n",id, (unsigned int)amount);
    while (timer.read_ms() < _timeout) {
        CSTDEBUG("WNC [%02d] !! _timeout=%d, time=%d\r\n", id, (int) _timeout, (int) timer.read() * 1000);

        //ret = _check_queue(id, recvBuffer, amount);
        ret = _check_queue(id, data, amount);
        if (ret) {
           CIODUMP((uint8_t *) data, (size_t)ret);
           return ret;
        }

        tr_debug("RECV:  Waiting . . .\n");
        int id, session_indicator;
        uint32_t amount;
        uint32_t actual_length;
        if (scan("@SOCKDATAIND: %d,%d,%d", &id, &session_indicator, &amount) == 3) {
            tr_debug("@SOCKDATAIND id=%d, session_indicator=%d, amount=%u\n",id,session_indicator,(unsigned int)amount);
            // more data
            if(amount) {
               tx("AT@SOCKREAD=%d,%d",id, MAX_SEND_BYTES);
            }
            // no more data
            else {
               tr_debug("RECV:  no more data indicated id=%d\n",id);
               return -1;
            }
        }
        if (scan("@SOCKREAD: %d,\"%s\"", &actual_length, recvBuffer) == 2) {
            //tr_debug("Got data len=%u data=%s\n", (unsigned int)actual_length, recvBuffer);
            rx("OK");
            _enqueue(id, recvBuffer, actual_length);
        }
    }

    // timeout
    return -1;
}

bool WNCATParser::close(int id) {
    tr_debug("close(id=%d)\n",id);
    if (tx("AT@SOCKCLOSE=%d",id) && rx("OK"))
       return true;

    return false;
}

void WNCATParser::setTimeout(uint32_t timeout_ms) {
    _timeout = timeout_ms;
}

bool WNCATParser::readable() {
    return (bool) _serial.readable();
}

bool WNCATParser::writeable() {
    return (bool) _serial.writeable();
}

void WNCATParser::attach(Callback<void()> func) {
    _serial.attach(func);
}

bool WNCATParser::tx(const char *pattern, ...) {
    char cmd[RXTX_BUFFER_SIZE];

    while (flushRx(cmd, sizeof(cmd), 10)) {
        CIODEBUG("GSM (%02d) !! '%s'\r\n", strlen(cmd), cmd);
        checkURC(cmd);
    }

    // cleanup the input buffer and check for URC messages
    cmd[0] = '\0';

    va_list ap;
    va_start(ap, pattern);
    vsnprintf(cmd, RXTX_BUFFER_SIZE, pattern, ap);
    va_end(ap);

    _serial.puts(cmd);
    _serial.puts("\r\n");
    CIODEBUG("GSM (%02d) <- '%s'\r\n", strlen(cmd), cmd);

    return true;
}

// readline ensuring the reader doesn't get notifications
size_t WNCATParser::readline(char *buffer, size_t max, uint32_t timeout) {
    char response[RXTX_BUFFER_SIZE];

    //TODO use if (readable()) here
    do {
        _readline(response, RXTX_BUFFER_SIZE - 1, timeout);
    } while (checkURC(response) != -1);
   
    CIODEBUG("GSM (%02d) -> '%s'\r\n", strlen(response), response);

    strncpy(buffer, response, max);

    return strlen(buffer);
}

int WNCATParser::scan(const char *pattern, ...) {
    Timer timer;
    timer.start();
    uint32_t timeout = 10;

    char response[RXTX_BUFFER_SIZE];

    //TODO use if (readable()) here
    do {
        _readline(response, RXTX_BUFFER_SIZE - 1, 10);

        if (timer.read() > timeout) {
           tr_error("scan() timeout\n");
           return -1;
        }

    } while (checkURC(response) != -1);

    va_list ap;
    va_start(ap, pattern);
    int matched = vsscanf(response, pattern, ap);
    va_end(ap);

    CIODEBUG("GSM (%02d) -> '%s' (%d)\r\n", strlen(response), response, matched);
    return matched;
}

bool WNCATParser::rx(const char *pattern, uint32_t timeout) {
    Timer timer;
    timer.start();

    char response[RXTX_BUFFER_SIZE];
    size_t length = 0, patternLength = strnlen(pattern, sizeof(response));
    do {
        length = _readline(response, RXTX_BUFFER_SIZE - 1, timeout);
        if (!length) return false;
        if (timer.read() > timeout) {
           tr_error("rx() timeout\n");
           return false;
        }

        CIODEBUG("GSM (%02d) -> '%s'\r\n", strlen(response), response);
    } while (checkURC(response) != -1);

    return strncmp(pattern, (const char *) response, MIN(length, patternLength)) == 0;
}

int WNCATParser::checkURC(const char *response) {
    if (!strncmp("%NOTIFY", response, 7)) {
        tr_debug("GSM -> %s\n", response);
        return 0;
    }
    if (!strncmp("SMS Ready", response, 9)
        || !strncmp("Call Ready", response, 10)
        || !strncmp("+CPIN: READY", response, 12)
        || !strncmp("+QNTP: 0", response, 8)
        || !strncmp("+QNTP: 5", response, 8)
        || !strncmp("+PDP DEACT", response, 10)
        ) {
        return 0;
    }

    // did not consume the response
    return -1;
}

size_t WNCATParser::read(char *buffer, size_t max, uint32_t timeout) {
    Timer timer;
    timer.start();

    size_t idx = 0;
    while (idx < max && timer.read() < timeout) {
        if (!_serial.readable()) {
            __WFI();
            continue;
        }

        if (max - idx) buffer[idx++] = (char) _serial.getc();
    }

    return idx;
}

size_t WNCATParser::_readline(char *buffer, size_t max, uint32_t timeout) {
    Timer timer;
    timer.start();

    size_t idx = 0;

    while (idx < max && timer.read() < timeout) {

        if (!_serial.readable()) {
            // nothing in the buffer, wait for interrupt
            __WFI();
            continue;
        }

        int c = _serial.getc();

        if (c == '\r') continue;

        if (c == '\n') {
            if (!idx) {
                idx = 0;
                continue;
            }
            break;
        }
        if (max - idx && isprint(c)) buffer[idx++] = (char) c;
    }

    buffer[idx] = 0;
    return idx;
}

size_t WNCATParser::flushRx(char *buffer, size_t max, uint32_t timeout) {
    Timer timer;
    timer.start();

    size_t idx = 0;

    do {
        for (int j = 0; j < (int) max && _serial.readable(); j++) {
            int c = _serial.getc();

            if (c == '\n' && idx > 0 && buffer[idx - 1] == '\r') {
                checkURC(buffer);
                idx = 0;
            } else if (max - idx && isprint(c)) {
                buffer[idx++] = (char) c;
            }
        }
        //TODO Do we actually need a timeout here
    } while (idx < max && _serial.readable() && timer.read() < timeout);

    buffer[idx] = 0;
    return idx;
}


void WNCATParser::_debug_dump(const char *prefix, const uint8_t *b, size_t size) {
    for (int i = 0; i < (int) size; i += 16) {
        if (prefix && strlen(prefix) > 0) printf("%s %06x: ", prefix, i);
        for (int j = 0; j < 16; j++) {
            if ((i + j) < (int) size) printf("%02x", b[i + j]); else printf("  ");
            if ((j + 1) % 2 == 0) putchar(' ');
        }
        putchar(' ');
        for (int j = 0; j < 16 && (i + j) < (int) size; j++) {
            putchar(b[i + j] >= 0x20 && b[i + j] <= 0x7E ? b[i + j] : '.');
        }
        printf("\r\n");
    }
}
