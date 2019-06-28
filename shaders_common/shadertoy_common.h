#define SHADERTOY_MAX_INPUT_CHANNELS 32 // could do more with bindless textures ..

#define USE_GUI (1)

#define KEYBOARD2_ROW_COUNTER_NO_MODIFIERS      0 // 32 bit integer counter incremented each time key is pressed [---]
#define KEYBOARD2_ROW_COUNTER_SHIFT             1 // 32 bit integer counter incremented each time key is pressed [S--]
#define KEYBOARD2_ROW_COUNTER_CONTROL           2 // 32 bit integer counter incremented each time key is pressed [-C-]
#define KEYBOARD2_ROW_COUNTER_SHIFT_CONTROL     3 // 32 bit integer counter incremented each time key is pressed [SC-]
#define KEYBOARD2_ROW_COUNTER_ALT               4 // 32 bit integer counter incremented each time key is pressed [--A]
#define KEYBOARD2_ROW_COUNTER_SHIFT_ALT         5 // 32 bit integer counter incremented each time key is pressed [S-A]
#define KEYBOARD2_ROW_COUNTER_CONTROL_ALT       6 // 32 bit integer counter incremented each time key is pressed [-CA]
#define KEYBOARD2_ROW_COUNTER_SHIFT_CONTROL_ALT 7 // 32 bit integer counter incremented each time key is pressed [SCA]
#define KEYBOARD2_ROW_COUNTER_ANY_MODIFIERS     8 // 32 bit integer counter incremented each time key is pressed [???]
#define KEYBOARD2_ROW_STATE                     9 // 0=up, 1=down, 2=released, 3=pressed
#define KEYBOARD2_STATE_UP       0U
#define KEYBOARD2_STATE_DOWN     1U
#define KEYBOARD2_STATE_RELEASED 2U
#define KEYBOARD2_STATE_PRESSED  3U