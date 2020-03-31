#include <stdint.h>
#include <stdbool.h>
#ifdef CHUNIIO_EXPORTS
#define CHUNIIO_API __declspec(dllexport)
#else
#define CHUNIIO_API __declspec(dllimport)
#endif

typedef enum {
	SRC_GAME = 0 ,
	SRC_CONTROLLER = 1
} chuni_msg_src_t;

typedef enum {
	COIN_INSERT = 0,
	SLIDER_PRESS = 1,
	SLIDER_RELEASE = 2,
	LED_SET = 3,
	CABINET_TEST = 4,
	CABINET_SERVICE = 5,
	IR_BLOCKED = 6,
	IR_UNBLOCKED = 7,

	BITMASK = 8,
	SHUTDOWN = 9,
	PING = 10,
	PONG = 11,
} chuni_msg_type_t;

typedef struct {
	uint8_t src;
	uint8_t type;

	uint8_t data[4];
} chuni_msg_t;

typedef void (*chuni_io_slider_callback_t)(const uint8_t* state);

extern "C" {
	CHUNIIO_API long chuni_io_jvs_init(void);
	CHUNIIO_API void chuni_io_jvs_poll(uint8_t* opbtn, uint8_t* beams);
	CHUNIIO_API void chuni_io_jvs_read_coin_counter(uint16_t* total);
	CHUNIIO_API void chuni_io_jvs_set_coin_blocker(bool open);
	CHUNIIO_API long chuni_io_slider_init(void);
	CHUNIIO_API void chuni_io_slider_start(chuni_io_slider_callback_t callback);
	CHUNIIO_API void chuni_io_slider_stop(void);
	CHUNIIO_API void chuni_io_slider_set_leds(const uint8_t* rgb);
}