/*!
 * @file
 * @brief AT command parser for WNC modem.
 *
 * Contains functions for reading and writing from
 * the serial port
 *
 * @author Russell Leake
 * @date 2018-01-15
 *
 * @copyright &copy; 2018 Eaton (http://www.eaton.com)
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
#ifndef WNCATPARSER_H
#define WNCATPARSER_H

#include "mbed.h"
#include <stdint.h>
#include <features/netsocket/nsapi_types.h>
#include <BufferedSerial/BufferedSerial.h>

#define WNC_SOCKET_COUNT 5
#define WNC_TCP 1
#define WNC_UDP 2

struct WncIpStats
{
    int  cid;			      //
    int  bearerid;		   	
    char ipaddr[16];
    char mask[16];
    char gateway[16];
    char dnsPrimary[16];
    char dnsSecondary[16];
};

/** WNC AT Parser Interface class.
    This is an interface to a WNC modem.
 */
class WNCATParser {
public:
    /** WNCATParser lifetime
     * @param tx        TX pin
     * @param rx        RX pin
     * @param rstPin    Reset pin
     * @param pwrPin    PowerKey pin
     * @param debug     Enable debugging
     */
    WNCATParser(PinName txPin, PinName rxPin, PinName rstPin, PinName pwrPin);

    /**
    * Startup the WNC
    *
    * @return true only if WNC was started correctly
    */
    bool startup(void);

    /**
    * Reset WNC
    *
    * @return true only if WNC resets successfully
    * play with PWERKEY - (only) to reset the modem, make sure the modem is reset and alive
    */
    bool reset(void);

    /**
    * Hard Reset WNC
    *
    * @return true only if WNC resets successfully
    * play with PWERKEY - (only) to reset the modem, make sure the modem is reset and alive
    */
    bool hard_reset(void);

    /**
    * Check if the Modem is poweredup and running
    *
    * @return true only if WNC OK's to AT cmd
    */
    bool isModemAlive();

    /**
    * Check the modem GPRS status
    *
    * @return 0: GPRS is detached; 1: GPRS is attached
    */
    int checkGPRS();

    /**
    * Power down the modem using AT cmd and bring the power pin to low
    *
    * @return true if AT-powerDown was OK
    */
    bool powerDown(void);

    /**
    * Disconnect WNC from AP
    *
    * @return true only if WNC is disconnected successfully
    */
    bool disconnect(void);

    /**
    * Set up the NTP server and enable the WNC clock functions
    *
    * @return true if AT cmd were sucessful
    */
    bool requestDateTime(void);

    /**
    * Connect WNC to the network
    *
    * @param apn the address of the network APN
    * @param userName the user name
    * @param passPhrase the password
    * @return true only if WNC is connected successfully
    */
    bool connect(const char *apn, const char *userName, const char *passPhrase);

    /**
     * Get the IP address of WNC
     *
     * @return null-teriminated IP address or null if no IP address is assigned
     */
    const char *getIPAddress(void);

    /**
     * Get the IMEI of WNC
     *
     * @return null-teriminated value or null if value is assigned
     */
    bool getIMEI(char *getimei);

    /**
     * Get the ICCID of WNC
     *
     * @return null-teriminated value or null if value is assigned
     */
    bool getICCID(char *geticcid);

    /**
     * Get the Latitude, Longitude, Date and Time of the device
     *
     * @param lat latitude
     * @param lon longitude
     * @param datetime struct contains date and time
     * @return null-teriminated IP address or null if no IP address is assigned
     */
    bool getLocation(char *lon, char *lat, tm *datetime, int *zone = 0);


    /**
     * Get the Battery status, level and voltage of the device
     *
     * @param status battery status
     * @param level battery level
     * @param voltage battery voltage
     * @return return false if
     */
    bool modem_battery(uint8_t *status, int *level, int *voltage);

    /**
    * Check if WNC is connected
    *
    * @return true only if the chip has an IP address
    */
    bool isConnected(void);

    /**
    * Get the IP of the host
    *
    * @return true only if the chip has an IP address
    */
//    bool queryIP(const char *url, const char *theIP);
    bool queryIP(const char *url, char *theIP);

    /**
    * Open a socketed connection
    *
    * @param type the type of socket to open "UDP" or "TCP"
    * @param id id to give the new socket, valid 0-4
    * @param port port to open connection with
    * @param addr the IP address of the destination
    * @return true only if socket opened successfully
    */
    bool open(const char *type, int id, const char *addr, int port);

    /**
    * Sends data to an open socket
    * 1046 Bytes can be sent each time
    *
    * @param id id of socket to send to
    * @param data data to be sent
    * @param amount amount of data to be sent - max 1024
    * @return true only if data sent successfully
    */
    bool send(int id, const void *data, uint32_t amount);

    /**
    * Get the WNC connection status
    *
    * @return status
    */
    int queryConnection();

    /**
    * Receives data from an open socket
    *
    * @param id id to receive from
    * @param data placeholder for returned information
    * @param amount number of bytes to be received
    * @return the number of bytes received
    */
    int32_t recv(int id, void *data, uint32_t amount);

    /**
    * Closes a socket
    *
    * @param id id of socket to close, valid only 0-4
    * @return true only if socket is closed successfully
    */
    bool close(int id);

    /**
    * Allows timeout to be changed between commands
    *
    * @param timeout_ms timeout of the connection
    */
    void setTimeout(uint32_t timeout_ms);

    /**
    * Checks if data is available
    */
    bool readable();

    /**
    * Checks if data can be written
    */
    bool writeable();

    /**
    * Attach a function to call whenever network state has changed
    *
    * @param func A pointer to a void function, or 0 to set as none
    */
    void attach(Callback<void()> func);

    /**
    * Attach a function to call whenever network state has changed
    *
    * @param obj pointer to the object to call the member function on
    * @param method pointer to the member function to call
    */
    template<typename T, typename M>
    void attach(T *obj, M method) {
        attach(Callback<void()>(obj, method));
    }

    /*! send a command */
    bool tx(const char *pattern, ...);
    bool txsimple(const char *pattern, ...); // no newline

    /**
    * @brief Expect a formatted response, blocks until the response is received or timeout.
    * This function will ignore URCs and return when the first non-URC has been received.
    * @param pattern the pattern to match
    * @return the number of matched elements
    */
    int scan(const char *pattern, ...);

    int scancopy(char *buf, int buf_len);

    /*!
    * @brief Expect a certain response, blocks util the response received or timeout.
    * This function will ignore URCs and return when the first non-URC has been received.
    * @param pattern the string to expect
    * @return true if received or false if not
    */
    bool rx(const char *pattern, uint32_t timeout = 5);

    /*!
    * Check if this line is an unsolicited result code.
    * @param response  the pattern to match
    * @return the code index or -1 if it is no known code
    */
    int checkURC(const char *response);

    /*!
    * @brief Read a single line from the WNC
    * @param buffer the character line buffer to read into
    * @param max the number of characters to read
    * @return the number of characters read
    */
    size_t readline(char *buffer, size_t max, uint32_t timeout);

    /*!
    * @brief Read binary data into a buffer
    * @param buffer the buffer to read into
    * @param max the number of bytes to read
    * @return the amount of bytes read
    */
    size_t read(char *buffer, size_t max, uint32_t timeout = 5);

    size_t flushRx(char *buffer, size_t max, uint32_t timeout = 5);

private:
    BufferedSerial _serial;

    DigitalOut _powerPin;
    DigitalOut _resetPin;
    
    struct packet {
        struct packet *next;
        int id;
        uint32_t len;
        char data[0];
        // data follows
    } *_packets, **_packets_end;

    void _packet_handler(const char *response);

    // interal readline
    size_t _readline(char *buffer, size_t max, uint32_t timeout);

    int32_t _check_queue(int id, void *data, uint32_t amount);
    int32_t _enqueue(int id, char *data, uint32_t amount);

    void _debug_dump(const char *prefix, const uint8_t *b, size_t size);

    bool _initialized;
    int _timeout;
    char _ip_buffer[16];
    char _imei[16];
    char _iccid[20];
    struct WncIpStats _ipstats;

};

#endif
