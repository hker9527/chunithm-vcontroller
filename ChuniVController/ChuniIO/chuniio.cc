#define _WINSOCK_DEPRECATED_NO_WARNINGS true

#include <winsock2.h>
#include <windows.h>
#include <process.h>
#include <stdio.h>
#include <stdbool.h>
#include <fcntl.h>
#include <io.h>
#include "chuniio.h"
#include "log.h"
#include <bitset>
#pragma comment(lib,"ws2_32.lib")

static unsigned int __stdcall chuni_io_slider_thread_proc(void* ctx);
static unsigned int __stdcall chuni_io_network_thread_proc(void* ctx);

static bool chuni_coin_pending = false;
static bool chuni_service_pending = false;
static bool chuni_test_pending = false;
static uint16_t chuni_coins = 0;
static uint8_t chuni_ir_sensor_map = 0;
static HANDLE chuni_io_slider_thread;
static bool chuni_io_slider_stop_flag;
static SOCKET chuni_socket;
static USHORT chuni_port = 24864; // CHUNI on dialpad
static struct sockaddr_in remote;
static bool remote_exist = false;
static uint8_t chuni_sliders[32];

using namespace std;

HRESULT chuni_io_jvs_init(void)
{
    // alloc console for debug output
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);

    log_info("allocated debug console.");

    struct sockaddr_in local;
    memset(&local, 0, sizeof(struct sockaddr_in));

    WSAData wsad;
    if (WSAStartup(MAKEWORD(2, 2), &wsad) != 0) {
        log_error("WSAStartup failed.");
        return S_FALSE;
    }

    chuni_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (chuni_socket == INVALID_SOCKET) {
        log_error("socket() failed.");
        return S_FALSE;
    }

    local.sin_addr.s_addr = INADDR_ANY; // TODO: make configurable
    local.sin_family = AF_INET;
    local.sin_port = htons(chuni_port);

    if (bind(chuni_socket, (struct sockaddr*) & local, sizeof(struct sockaddr_in)) == SOCKET_ERROR) {
        log_error("bind() failed.");
        return S_FALSE;
    }

    (HANDLE)_beginthreadex(
        NULL,
        0,
        chuni_io_network_thread_proc,
        NULL,
        0,
        NULL);

    log_info("server socket initialization completed.");

    return S_OK;
}

void chuni_io_jvs_read_coin_counter(uint16_t* out)
{
    if (out == NULL) {
        return;
    }

    if (chuni_coin_pending) chuni_coins++;
    chuni_coin_pending = false;

    *out = chuni_coins;
}

void chuni_io_jvs_poll(uint8_t* opbtn, uint8_t* beams)
{
    if (chuni_test_pending) {
        *opbtn |= 0x01;
        chuni_test_pending = false;
    }

    if (chuni_service_pending) {
        *opbtn |= 0x02;
        chuni_service_pending = false;

    }

    *beams = chuni_ir_sensor_map;
}

void chuni_io_jvs_set_coin_blocker(bool open)
{
    if (open) log_info("coin blocker disabled");
    else log_info("coin blocker enabled.");
    
}

HRESULT chuni_io_slider_init(void)
{
    log_info("init slider...");
    return S_OK;
}

void chuni_io_slider_start(chuni_io_slider_callback_t callback)
{
    log_info("starting slider...");
    if (chuni_io_slider_thread != NULL) {
        return;
    }

    chuni_io_slider_thread = (HANDLE)_beginthreadex(
        NULL,
        0,
        chuni_io_slider_thread_proc,
        callback,
        0,
        NULL);
}

void chuni_io_slider_stop(void)
{
    log_info("stopping slider...");
    if (chuni_io_slider_thread == NULL) {
        return;
    }

    chuni_io_slider_stop_flag = true;

    WaitForSingleObject(chuni_io_slider_thread, INFINITE);
    CloseHandle(chuni_io_slider_thread);
    chuni_io_slider_thread = NULL;
    chuni_io_slider_stop_flag = false;
}

void chuni_io_slider_set_leds(const uint8_t* rgb)
{
    static uint8_t prev_rgb_status[96];
    static chuni_msg_t message;
    message.src = SRC_GAME;
    message.type = LED_SET;

    // ignore odd, since no 1/32 color strip exist.
    for (uint8_t i = 0; i < 96; i += 6) {
        if (rgb[i] != prev_rgb_status[i] || rgb[i + 1] != prev_rgb_status[i + 1] || rgb[i + 2] != prev_rgb_status[i + 2]) {
            uint8_t n = i / 6;
            log_debug("SET_LED[%d]: rgb(%d, %d, %d)", n, rgb[i + 1], rgb[i + 2], rgb[i]);
            if (!remote_exist) log_warn("remote does not exist.");
            else {
                message.data[0] = n;
                message.data[1] = rgb[i + 1];
                message.data[2] = rgb[i + 2];
                message.data[3] = rgb[i];
                sendto(chuni_socket, (const char*)&message, sizeof(chuni_msg_t), 0, (const sockaddr*)&remote, sizeof(struct sockaddr_in));
            }
        }
            
        prev_rgb_status[i] = rgb[i];
        prev_rgb_status[i + 1] = rgb[i + 1];
        prev_rgb_status[i + 2] = rgb[i + 2];
    }
}

bool checkBit(uint8_t num, uint8_t index) {
    return (num & (int)pow(2, 7 - index)) > 0;
}

static void chuni_io_ir(uint8_t sensor, bool set) {
    if (sensor % 2 == 0) sensor++;
    else sensor--;
    if (set) chuni_ir_sensor_map |= 1 << sensor;
    else chuni_ir_sensor_map &= ~(1 << sensor);
}

void chuni_io_slider(uint8_t sensor, bool set) {
    chuni_sliders[sensor * 2] = set * 128;
    chuni_sliders[sensor * 2 + 1] = set * 128;
}


// https://tangentsoft.net/wskfaq/examples/ipaddr.html
void display_ip() {
    char ac[80];
    if (gethostname(ac, sizeof(ac)) == SOCKET_ERROR) {
        log_error("Error %d when getting local host name.", WSAGetLastError());
        return;
    }

    log_info("Host name is %s.", ac);

    struct hostent* phe = gethostbyname(ac);
    if (phe == 0) {
        log_error("Yow! Bad host lookup.");
        return;
    }

    for (int i = 0; phe->h_addr_list[i] != 0; ++i) {
        struct in_addr addr;
        memcpy(&addr, phe->h_addr_list[i], sizeof(struct in_addr));
        log_debug("Address %d: %s", i, inet_ntoa(addr));
    }
}

static unsigned int __stdcall chuni_io_network_thread_proc(void* ctx) {
    try {
        display_ip();
        log_info("spinning up network event handler...");

        static char recv_buf[32];
        int addr_sz = sizeof(struct sockaddr_in);
        while (true) {
            int len = recvfrom(chuni_socket, recv_buf, 32, 0, (sockaddr*)&remote, &addr_sz);
            remote_exist = true;

            if (len == (int)sizeof(chuni_msg_t)) {
                const chuni_msg_t* msg = (const chuni_msg_t*)recv_buf;
                
                if (msg->src != SRC_CONTROLLER) {
                    log_warn("got non-controller message.");
                    continue;
                }
                switch (msg->type) {
                case COIN_INSERT:
                    log_info("adding coin.");
                    chuni_coin_pending = true;
                    break;
                case SLIDER_PRESS:
                    //log_debug("slider_press at %d.", msg->target);
                    if (msg->data[0] >= 16) log_error("invalid slider value %d in SLIDER_PRESS.", msg->data[0]);
                    else {
                        chuni_io_slider((msg->data[0]) * 2    , true);
                        chuni_io_slider((msg->data[0]) * 2 + 1, true);
                    }
                    break;
                case SLIDER_RELEASE:
                    //log_debug("slider released on %d.", msg->target);
                    if (msg->data[0] >= 16) log_error("invalid slider value %d in SLIDER_RELEASE.", msg->data[0]);
                    else {
                        chuni_io_slider((msg->data[0]) * 2    , false);
                        chuni_io_slider((msg->data[0]) * 2 + 1, false);
                    }
                    break;
                case CABINET_TEST:
                    log_info("setting cabinet_test.");
                    chuni_test_pending = true;
                    break;
                case CABINET_SERVICE:
                    log_info("setting cabinet_service.");
                    chuni_service_pending = true;
                    break;
                case IR_BLOCKED:
                    //log_debug("ir %d blokced.", msg->target);
                    if (msg->data[0] >= 6) log_error("invalid slider value %d in IR_BLOCKED.", msg->data[0]);
                    else chuni_io_ir(msg->data[0], true);
                    break;
                case IR_UNBLOCKED:
                    //log_debug("ir %d unblokced.", msg->target);
                    if (msg->data[0] >= 6) log_error("invalid slider value %d in IR_UNBLOCKED.", msg->data[0]);
                    else chuni_io_ir(msg->data[0], false);
                    break;
                case BITMASK: {
                    // 16 Sliders + 6 Air + 8 Unused = 32bit Int.
                    try {
                        for (uint8_t i = 0; i < 2; i++) {
                            for (uint8_t j = 0; j < 8; j++) {
                                uint8_t index = 8 * i + j;
                                bool set = checkBit(msg->data[i], j);
                                // log_debug("Slider %d: %d", index, set);
                                chuni_io_slider(index, set);
                            }
                        }

                        for (uint8_t i = 0; i < 6; i++) {
                            bool set = checkBit(msg->data[2], i);
                            // log_debug("Air %d: %d", i, set);
                            chuni_io_ir(i, set);
                        }
                    }
                    catch (exception e) {
                        log_fatal("ERROR: %s", e.what());
                    }
                }
                    break;
                case SHUTDOWN:
                    exit(0);
                    break;
                case PING: {
                    static chuni_msg_t message;
                    message.src = SRC_GAME;
                    message.type = PONG;

                    sendto(chuni_socket, (const char*)&message, sizeof(chuni_msg_t), 0, (const sockaddr*)&remote, sizeof(struct sockaddr_in));
                    // log_debug("Client ping");
                }
                    break;
                default:
                    log_error("bad message type %d.", msg->type);
                }
            }
            else if (len > 0) {
                log_warn("got invalid packet of length %d.", len);
            }
        }
    }
    catch (const exception& e) {
        log_fatal("Error: %s", e.what());
    }
}

static unsigned int __stdcall chuni_io_slider_thread_proc(void* ctx) {
    chuni_io_slider_callback_t callback;
   
    size_t i;

    for (i = 0; i < _countof(chuni_sliders); i++) chuni_sliders[i] = 0;

    callback = (chuni_io_slider_callback_t) ctx;

    while (!chuni_io_slider_stop_flag) {
        callback(chuni_sliders);
        Sleep(1);
    }

    return 0;
}
