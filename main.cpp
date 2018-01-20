/*
 * Copyright (c) 2017 ARM Limited. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "mbed.h"
#include "common_functions.h"
#include "UDPSocket.h"
#include "avnet/WNC14A2AInterface.h"
#include "mbed-trace/mbed_trace.h"

#define TRACE_GROUP "main"

#define UDP 0
#define TCP 1

// SIM pin code goes here
#ifndef MBED_CONF_APP_SIM_PIN_CODE
# define MBED_CONF_APP_SIM_PIN_CODE    "1234"
#endif

#ifndef MBED_CONF_APP_APN
# define MBED_CONF_APP_APN         "internet"
#endif
#ifndef MBED_CONF_APP_USERNAME
# define MBED_CONF_APP_USERNAME    NULL
#endif
#ifndef MBED_CONF_APP_PASSWORD
# define MBED_CONF_APP_PASSWORD    NULL
#endif

// Number of retries /
#define RETRY_COUNT 3

// Instantiate our modem
//M66Interface modem(GSM_UART_TX, GSM_UART_RX, GSM_PWRKEY, GSM_POWER, true);
//    MDMPWRON  = PE_14,   // Power (active high)
//    MDMRST    = PB_5,    // Reset (active low)
//    MDMTXD    = PD_5,    // Transmit Data
//    MDMRXD    = PD_6,    // Receive Data


// Echo server hostname
const char *host_name = "echo.u-blox.com";

// Echo server port (same for TCP and UDP)
const int port = 7;

Mutex PrintMutex;
Thread dot_thread;

#define PRINT_TEXT_LENGTH 128
char print_text[PRINT_TEXT_LENGTH];
void print_function(const char *input_string)
{
    PrintMutex.lock();
    printf("%s", input_string);
    fflush(NULL);
    PrintMutex.unlock();
}

void dot_event(void const *args)
{
   WNC14A2AInterface *iface = (WNC14A2AInterface *)args;

    while (true) {
        wait(4);
        if (!iface->is_connected()) {
            print_function(".");
        } else {
            print_function("?");
            break;
        }
    }

}

/**
 * Connects to the Cellular Network
 */
nsapi_error_t do_connect(WNC14A2AInterface *iface)
{
    nsapi_error_t retcode;
    uint8_t retry_counter = 0;

    tr_debug("do_connect\n");
    while (!iface->is_connected()) {

        tr_debug("Connecting\n");
        retcode = iface->connect();
        if (retcode == NSAPI_ERROR_AUTH_FAILURE) {
            tr_error("Authentication Failure. Exiting application\n");
            return retcode;
        } else if (retcode != NSAPI_ERROR_OK) {
            snprintf(print_text, PRINT_TEXT_LENGTH, "Couldn't connect: %d, will retry\n", retcode);
            tr_error(print_text);
            retry_counter++;
            continue;
        } else if (retcode != NSAPI_ERROR_OK && retry_counter > RETRY_COUNT) {
            snprintf(print_text, PRINT_TEXT_LENGTH, "Fatal connection failure: %d\n", retcode);
            tr_error(print_text);
            return retcode;
        }

        break;
    }

    tr_info("Connection Established.\n");
    tr_info("~do_connect()\n");

    return NSAPI_ERROR_OK;
}

/**
 * Opens a UDP or a TCP socket with the given echo server and performs an echo
 * transaction retrieving current.
 */
nsapi_error_t test_send_recv(WNC14A2AInterface *iface)
{
    nsapi_size_or_error_t retcode;
#if MBED_CONF_APP_SOCK_TYPE == TCP
    TCPSocket sock;
#else
    UDPSocket sock;
#endif
    tr_debug("test_send_recv()\n");

    tr_debug("open()\n");
    retcode = sock.open(iface);
    if (retcode != NSAPI_ERROR_OK) {
        snprintf(print_text, PRINT_TEXT_LENGTH, "UDPSocket.open() fails, code: %d\n", retcode);
        tr_error(print_text);
        return -1;
    }

    SocketAddress sock_addr;
    tr_debug("gethostbyename()\n");
    retcode = iface->gethostbyname(host_name, &sock_addr, NSAPI_IPv4);
    if (retcode != NSAPI_ERROR_OK) {
        snprintf(print_text, PRINT_TEXT_LENGTH, "Couldn't resolve remote host: %s, code: %d\n", host_name,
               retcode);
        tr_error(print_text);
        return -1;
    }

    sock_addr.set_port(port);

    sock.set_timeout(15000);
    int n = 0;
    const char *echo_string = "TEST";
    char recv_buf[512];
#if MBED_CONF_APP_SOCK_TYPE == TCP
    retcode = sock.connect(sock_addr);
    if (retcode < 0) {
        snprintf(print_text, PRINT_TEXT_LENGTH, "TCPSocket.connect() fails, code: %d\n", retcode);
        tr_info(print_text);
        return -1;
    } else {
        snprintf(print_text, PRINT_TEXT_LENGTH, "TCP: connected with %s server\n", host_name);
        tr_info(print_text);
    }
    retcode = sock.send((void*) echo_string, sizeof(echo_string));
    if (retcode < 0) {
        snprintf(print_text, PRINT_TEXT_LENGTH, "TCPSocket.send() fails, code: %d\n", retcode);
        tr_info(print_text);
        return -1;
    } else {
        snprintf(print_text, PRINT_TEXT_LENGTH, "TCP: Sent %d Bytes to %s\n", retcode, host_name);
        tr_info(print_text);
    }

    // get the first message
    n = sock.recv((void*) recv_buf, 512);

    if (n > 0) {
        snprintf(print_text, PRINT_TEXT_LENGTH, "Received from echo server %d Bytes\n", n);
        tr_info(print_text);
    }

    // get the second message
    n = sock.recv((void*) recv_buf, 4);
#else

    retcode = sock.sendto(sock_addr, (void*) echo_string, sizeof(echo_string));
    if (retcode < 0) {
        snprintf(print_text, PRINT_TEXT_LENGTH, "UDPSocket.sendto() fails, code: %d\n", retcode);
        tr_info(print_text);
        return -1;
    } else {
        snprintf(print_text, PRINT_TEXT_LENGTH, "UDP: Sent %d Bytes to %s\n", retcode, host_name);
        tr_info(print_text);
    }

    n = sock.recvfrom(&sock_addr, (void*) recv_buf, 4);
#endif

    sock.close();

    if (n > 0) {
        snprintf(print_text, PRINT_TEXT_LENGTH, "Received from echo server %d Bytes\n", n);
        tr_info(print_text);
        return 0;
    }

    return -1;
}

// debug printf function
void trace_printer(const char* str) {
    printf("%s", str);
}

int main()
{
    mbed_trace_init();
    mbed_trace_print_function_set(trace_printer);
    mbed_trace_config_set(TRACE_MODE_COLOR | TRACE_ACTIVE_LEVEL_DEBUG | TRACE_CARRIAGE_RETURN);

    WNC14A2AInterface iface(PTD3, PTD2, PTC12, PTB9, true);

    //bool res = iface.powerUpModem();
    //iface.modem_debug_on(MBED_CONF_APP_MODEM_TRACE);
    /* Set Pin code for SIM card */
    //iface.set_sim_pin(MBED_CONF_APP_SIM_PIN_CODE);

    /* Set network credentials here, e.g., APN*/
    iface.set_credentials(MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD);

    tr_info("mbed-os-example-cellular\n");
    tr_info("Establishing connection \n");
    //dot_thread.start(callback(dot_event, (void *)&iface));

    /* Attempt to connect to a cellular network */
    if (do_connect(&iface) == NSAPI_ERROR_OK) {
        tr_info("test_send_recv\n");
        nsapi_error_t retcode = test_send_recv(&iface);
        if (retcode != NSAPI_ERROR_OK) {
            tr_error("Failure. Exiting \n\n");
            return -1;
        }
    }

    tr_info("\nSuccess!  Exiting \n\n");
    return 0;
}

#if 0
int main()
{
    mbed_trace_init();
    mbed_trace_print_function_set(trace_printer);
    mbed_trace_config_set(TRACE_MODE_COLOR | TRACE_ACTIVE_LEVEL_DEBUG | TRACE_CARRIAGE_RETURN);

    tr_error("Hello World\n");
    WNC14A2AInterface modem(PTD3, PTD2, PTC12, PTB9, true);
    //modem.modem_debug_on(MBED_CONF_APP_MODEM_TRACE);

    /* Set Pin code for SIM card */
    //modem.set_sim_pin(MBED_CONF_APP_SIM_PIN_CODE);

    /* Set network credentials here, e.g., APN*/
    modem.set_credentials(MBED_CONF_APP_APN, MBED_CONF_APP_USERNAME, MBED_CONF_APP_PASSWORD);
    tr_error("Set Credentials\n");
    while(1) {
      bool res = modem.powerUpModem();
      if (!res) {
         tr_debug("power up failed!\n");
         wait_ms(500);
      }
      else
         break;
    }
    tr_debug("connecting ...\n");
    modem.connect();
    return 0;
}
#endif
// EOF
