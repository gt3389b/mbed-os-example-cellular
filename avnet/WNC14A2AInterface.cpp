/*
 * AVNET WNC14A2A Modem core functionality interface.
 *
 * @author Russell Leake
 * @author Niranjan Rao (original)
 * @date 2018-01-20
 *
 * @copyright &copy; 2018 (russellleake@eaton.com)
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

#include <string.h>
#include <fsl_rtc.h>
#include "WNC14A2AInterface.h"
#include "mbed-trace/mbed_trace.h"

#define TRACE_GROUP "wncIfc"

// Various timeouts for different operations
#define WNC_CONNECT_TIMEOUT 15000
#define WNC_SEND_TIMEOUT    15000
#define WNC_RECV_TIMEOUT    40000
#define WNC_MISC_TIMEOUT    40000

// WNC14A2AInterface implementation
WNC14A2AInterface::WNC14A2AInterface(PinName tx, PinName rx, PinName rstPin, PinName pwrPin, bool debug)
    : _wnc(tx, rx, rstPin, pwrPin), _sockets(), _apn(), _userName(), _passPhrase(), _imei()
{

    tr_debug("init()\n");
    memset(_sockets, 0, sizeof(_sockets));
    memset(_cbs, 0, sizeof(_cbs));

    _wnc.attach(this, &WNC14A2AInterface::event);
}

bool WNC14A2AInterface::powerUpModem(){
    return _wnc.startup();
}

bool WNC14A2AInterface::reset() {
    return _wnc.reset();
}

bool WNC14A2AInterface::powerDown(){
    return _wnc.powerDown();
}

bool WNC14A2AInterface::isModemAlive() {
    return _wnc.isModemAlive();
}

int WNC14A2AInterface::checkGPRS() {
    return _wnc.checkGPRS();
}

int WNC14A2AInterface::set_imei(){
    if(!_wnc.getIMEI(_imei)){
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    return NSAPI_ERROR_OK;
}

bool WNC14A2AInterface::is_connected(){
   tr_debug("is_connected()\n");
   return _wnc.isConnected();
}

const char *WNC14A2AInterface::get_imei(){
    return _imei;
}

int WNC14A2AInterface::connect(const char *apn, const char *userName, const char *passPhrase)
{
    tr_debug("connect(...)\n");
    set_credentials(apn, userName, passPhrase);
    return connect();
}

int WNC14A2AInterface::connect()
{
    tr_debug("connect()\n");
    _wnc.setTimeout(WNC_CONNECT_TIMEOUT);

    if (!_wnc.startup()) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    if (!_wnc.connect(_apn, _userName, _passPhrase)) {
        return NSAPI_ERROR_NO_CONNECTION;
    }

    if (!_wnc.getIPAddress()) {
        return NSAPI_ERROR_NO_ADDRESS;
    }

    if(set_imei()){
        return NSAPI_ERROR_DEVICE_ERROR;
    }
    return NSAPI_ERROR_OK;
}

int WNC14A2AInterface::set_credentials(const char *apn, const char *userName, const char *passPhrase)
{
    memset(_apn, 0, sizeof(_apn));
    strncpy(_apn, apn, sizeof(_apn));

    memset(_userName, 0, sizeof(_userName));
    strncpy(_userName, userName, sizeof(_userName));

    memset(_passPhrase, 0, sizeof(_passPhrase));
    strncpy(_passPhrase, passPhrase, sizeof(_passPhrase));

    return 0;
}

int WNC14A2AInterface::disconnect()
{
    _wnc.setTimeout(WNC_MISC_TIMEOUT);

    if (!_wnc.disconnect()) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    return NSAPI_ERROR_OK;
}

const char *WNC14A2AInterface::get_ip_address()
{
    return _wnc.getIPAddress();
}

bool WNC14A2AInterface::get_location_date(char *lon, char *lat, tm *datetime, int *zone) {
    return _wnc.getLocation(lon, lat, datetime, zone);
}

bool WNC14A2AInterface::queryIP(const char *url, const char *theIP){
    tr_debug("queryIP(url=%s)\n", url);
    return _wnc.queryIP(url, (char *)theIP);
}

bool WNC14A2AInterface::getModemBattery(uint8_t *status, int *level, int *voltage){
    return _wnc.modem_battery(status, level, voltage);
}

struct wnc_socket {
    int id;
    nsapi_protocol_t proto;
    bool connected;
    SocketAddress addr;
};


nsapi_error_t WNC14A2AInterface::gethostbyname(const char* name, SocketAddress *address)
{
   nsapi_error_t ret = NSAPI_ERROR_OK;
   char ipAddr[16];
   memset(ipAddr,0,16);
   this->queryIP(name, ipAddr);
   address->set_ip_address(ipAddr);
   tr_debug("~gethostbyname(url=%s) = %s\n",name, ipAddr);
   return ret;
}

int WNC14A2AInterface::socket_open(void **handle, nsapi_protocol_t proto)
{
    // Look for an unused socket
    int id = -1;

//    for (int i = 0; i < WNC_SOCKET_COUNT; i++) {
    for (int i = 1; i < WNC_SOCKET_COUNT; i++) {
        if (!_sockets[i]) {
            id = i;
            _sockets[i] = true;
            break;
        }
    }

    if (id == -1) {
        return NSAPI_ERROR_NO_SOCKET;
    }

    struct wnc_socket *socket = new struct wnc_socket;
    if (!socket) {
        return NSAPI_ERROR_NO_SOCKET;
    }

    socket->id = id;
    socket->proto = proto;
    socket->connected = false;
    *handle = socket;
    tr_debug("socket_open() = %d\n",id);
    return 0;
}

int WNC14A2AInterface::socket_close(void *handle)
{
    struct wnc_socket *socket = (struct wnc_socket *)handle;
    int err = 0;
    _wnc.setTimeout(WNC_MISC_TIMEOUT);

    if (!_wnc.close(socket->id)) {
        err = NSAPI_ERROR_DEVICE_ERROR;
    }

    tr_debug("socket_close(%d)\n",socket->id);
    _sockets[socket->id] = false;
    delete socket;
    return err;
}

int WNC14A2AInterface::socket_bind(void *handle, const SocketAddress &address)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int WNC14A2AInterface::socket_listen(void *handle, int backlog)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int WNC14A2AInterface::socket_connect(void *handle, const SocketAddress &addr)
{
    struct wnc_socket *socket = (struct wnc_socket *)handle;
    _wnc.setTimeout(WNC_MISC_TIMEOUT);

    const char *proto = (socket->proto == NSAPI_UDP) ? "UDP" : "TCP";
    tr_debug("socket_connect(%s)\n",socket->proto == NSAPI_UDP ? "UDP" : "TCP");
    if (!_wnc.open(proto, socket->id, addr.get_ip_address(), addr.get_port())) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    socket->connected = true;
    return 0;
}

int WNC14A2AInterface::socket_accept(void *server, void **socket, SocketAddress *addr)
{
    return NSAPI_ERROR_UNSUPPORTED;
}

int WNC14A2AInterface::socket_send(void *handle, const void *data, unsigned size)
{
    struct wnc_socket *socket = (struct wnc_socket *)handle;
    _wnc.setTimeout(WNC_SEND_TIMEOUT);

    if (!_wnc.send(socket->id, data, size)) {
        return NSAPI_ERROR_DEVICE_ERROR;
    }

    return size;
}

int WNC14A2AInterface::socket_recv(void *handle, void *data, unsigned size)
{
    struct wnc_socket *socket = (struct wnc_socket *)handle;
    _wnc.setTimeout(WNC_RECV_TIMEOUT);

    int32_t recv = _wnc.recv(socket->id, data, size);
    if (recv < 0) {
        return NSAPI_ERROR_WOULD_BLOCK;
    }

    return recv;
}

int WNC14A2AInterface::socket_sendto(void *handle, const SocketAddress &addr, const void *data, unsigned size)
{
    struct wnc_socket *socket = (struct wnc_socket *)handle;

    if (socket->connected && socket->addr != addr) {
        _wnc.setTimeout(WNC_MISC_TIMEOUT);
        if (!_wnc.close(socket->id)) {
            return NSAPI_ERROR_DEVICE_ERROR;
        }
        socket->connected = false;
    }

    if (!socket->connected) {
        int err = socket_connect(socket, addr);
        if (err < 0) {
            return err;
        }
        socket->addr = addr;
    }

    return socket_send(socket, data, size);
}

int WNC14A2AInterface::socket_recvfrom(void *handle, SocketAddress *addr, void *data, unsigned size)
{
    struct wnc_socket *socket = (struct wnc_socket *)handle;
    int ret = socket_recv(socket, data, size);
    if (ret >= 0 && addr) {
        *addr = socket->addr;
    }

    return ret;
}

void WNC14A2AInterface::socket_attach(void *handle, void (*callback)(void *), void *data)
{
    struct wnc_socket *socket = (struct wnc_socket *)handle;
    _cbs[socket->id].callback = callback;
    _cbs[socket->id].data = data;
}

void WNC14A2AInterface::event() {
    for (int i = 0; i < WNC_SOCKET_COUNT; i++) {
        if (_cbs[i].callback) {
            _cbs[i].callback(_cbs[i].data);
        }
    }
}
