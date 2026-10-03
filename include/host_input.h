#pragma once

#include <3ds.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t buttons;
    uint8_t left_trigger;
    uint8_t right_trigger;
    int16_t left_x;
    int16_t left_y;
    int16_t right_x;
    int16_t right_y;
} HostGamepadState;

typedef enum {
    /* Match physical positions: the bottom face button is Cross / Xbox A. */
    HOST_LAYOUT_POSITION = 0,
    /* Match printed letters: 3DS A sends Xbox A. */
    HOST_LAYOUT_LABEL = 1
} HostButtonLayout;

typedef enum {
    HOST_GYRO_OFF = 0,
    HOST_GYRO_ALWAYS = 1,
    /* Only while the left trigger (aim) is held, like most shooters. */
    HOST_GYRO_WHILE_AIMING = 2,
    HOST_GYRO_MODE_COUNT
} HostGyroMode;

typedef struct {
    HostButtonLayout layout;
    unsigned deadzone_percent;
    bool swap_shoulders;
    HostGyroMode gyro_mode;
    /* 0 low, 1 medium, 2 high. */
    unsigned gyro_speed;
} HostInputConfig;

enum {
    HOST_PAD_DPAD_UP = 0x0001, HOST_PAD_DPAD_DOWN = 0x0002,
    HOST_PAD_DPAD_LEFT = 0x0004, HOST_PAD_DPAD_RIGHT = 0x0008,
    HOST_PAD_START = 0x0010, HOST_PAD_BACK = 0x0020,
    HOST_PAD_LEFT_THUMB = 0x0040, HOST_PAD_RIGHT_THUMB = 0x0080,
    HOST_PAD_LEFT_SHOULDER = 0x0100, HOST_PAD_RIGHT_SHOULDER = 0x0200,
    HOST_PAD_GUIDE = 0x0400,
    HOST_PAD_A = 0x1000, HOST_PAD_B = 0x2000, HOST_PAD_X = 0x4000, HOST_PAD_Y = 0x8000
};

/* Custom button mapping: every remappable 3DS input sends one output. */
enum {
    HOST_IN_A, HOST_IN_B, HOST_IN_X, HOST_IN_Y, HOST_IN_L, HOST_IN_R, HOST_IN_ZL, HOST_IN_ZR,
    HOST_IN_START, HOST_IN_SELECT, HOST_IN_UP, HOST_IN_DOWN, HOST_IN_LEFT, HOST_IN_RIGHT,
    HOST_INPUT_COUNT
};
enum {
    HOST_OUT_NONE, HOST_OUT_CROSS, HOST_OUT_CIRCLE, HOST_OUT_SQUARE, HOST_OUT_TRIANGLE,
    HOST_OUT_L1, HOST_OUT_R1, HOST_OUT_L2, HOST_OUT_R2, HOST_OUT_L3, HOST_OUT_R3,
    HOST_OUT_OPTIONS, HOST_OUT_SHARE, HOST_OUT_PS,
    HOST_OUT_UP, HOST_OUT_DOWN, HOST_OUT_LEFT, HOST_OUT_RIGHT,
    HOST_OUTPUT_COUNT
};
/* The 3DS key of an input, and the name of an output. */
u32 host_input_key(unsigned input);
const char *host_input_name(unsigned input);
const char *host_output_name(unsigned output);
/* What each input sends under a layout and trigger choice (no custom map). */
void host_input_default_map(HostButtonLayout layout, bool swap_shoulders, unsigned char map[HOST_INPUT_COUNT]);
/* Apply a custom map (NULL returns to the layout's own). */
void host_input_set_custom_map(const unsigned char *map);
bool host_input_custom_map_active(void);

void host_input_configure(const HostInputConfig *config);
/* Buttons the 3DS lacks (L3, R3, Guide), held from the touch screen. */
void host_input_set_virtual_buttons(uint16_t buttons);
/* While suppressed, reads return a neutral pad (menus and overlays). */
void host_input_set_suppressed(bool suppressed);
uint16_t host_input_buttons_for_keys(u32 keys);
void host_input_read_3ds(HostGamepadState *state);
size_t host_input_encode_gamepad(uint8_t output[38], const HostGamepadState *state,
                                uint64_t timestamp_us);
size_t host_input_encode_gamepad_wire(uint8_t output[50], const HostGamepadState *state,
                                     uint64_t timestamp_us, int protocol_version);
/* Partially reliable gamepad framing (0x26): unordered delivery with a short
 * lifetime, so one lost packet never delays newer controller states. */
size_t host_input_encode_gamepad_partial(uint8_t output[54], const HostGamepadState *state,
                                        uint64_t timestamp_us, uint16_t sequence);
size_t host_input_encode_mouse_move(uint8_t output[34], int16_t dx, int16_t dy,
                                   uint64_t timestamp_us, int protocol_version);
size_t host_input_encode_mouse_button(uint8_t output[28], bool pressed,
                                     uint64_t timestamp_us, int protocol_version);
size_t host_input_encode_key(uint8_t output[28], uint16_t keycode, uint16_t scancode,
                            uint16_t modifiers, bool pressed, uint64_t timestamp_us,
                            int protocol_version);
bool host_input_key_for_char(char character, uint16_t *keycode, uint16_t *scancode,
                            uint16_t *modifiers);
bool host_input_self_test(void);
/* Gyro aim adds console rotation to the right stick (or the pointer). */
bool host_input_gyro_active(void);
