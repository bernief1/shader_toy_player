uniform vec3 iResolution;
uniform float iTime;
uniform float iTimeDelta;
uniform int iFrame;
uniform float iFrameRate;
uniform float iChannelTime[SHADERTOY_MAX_INPUT_CHANNELS]; // TODO
uniform vec4 iMouse;
uniform vec4 iDate = vec4(0); // TODO
uniform float iSampleRate = 44100.0;
uniform vec3 iChannelResolution[SHADERTOY_MAX_INPUT_CHANNELS];
uniform vec3 iOutputResolution;

#if defined(_KEYBOARD2_)
#define IS_KEY_DOWN(key)        bool(texelFetch(_KEYBOARD2_, ivec2(key, KEYBOARD2_ROW_STATE), 0).x & KEYBOARD2_STATE_DOWN)
#define IS_KEY_PRESSED(key)         (texelFetch(_KEYBOARD2_, ivec2(key, KEYBOARD2_ROW_STATE), 0).x == KEYBOARD2_STATE_PRESSED)
#define IS_KEY_RELEASED(key)        (texelFetch(_KEYBOARD2_, ivec2(key, KEYBOARD2_ROW_STATE), 0).x == KEYBOARD2_STATE_RELEASED)
#define IS_KEY_TOGGLED(key)     bool(texelFetch(_KEYBOARD2_, ivec2(key, KEYBOARD2_ROW_COUNTER_NO_MODIFIERS), 0).x & 1U)
#define GET_KEY_MODE(key,n)         (texelFetch(_KEYBOARD2_, ivec2(key, KEYBOARD2_ROW_COUNTER_NO_MODIFIERS), 0).x % (n))
#define IS_KEY_NOT_TOGGLED(key)     (!IS_KEY_TOGGLED(key))
#elif defined(_KEYBOARD_)
#define IS_KEY_DOWN(key)        (texelFetch(_KEYBOARD_, ivec2(key, 0), 0).x > 0.0)
#define IS_KEY_PRESSED(key)     (texelFetch(_KEYBOARD_, ivec2(key, 1), 0).x > 0.0)
#define IS_KEY_TOGGLED(key)     (texelFetch(_KEYBOARD_, ivec2(key, 2), 0).x > 0.0)
#define IS_KEY_NOT_TOGGLED(key) (texelFetch(_KEYBOARD_, ivec2(key, 2), 0).x == 0.0)
#endif

#define KEY_SHIFT 16
#define KEY_CNTRL 17
#define KEY_ALT   18
#define KEY_SPACE 32
#define KEY_LEFT  37
#define KEY_UP    38
#define KEY_RIGHT 39
#define KEY_DOWN  40
#define KEY_0     48
#define KEY_1     49
#define KEY_2     50
#define KEY_A     65
#define KEY_B     66
#define KEY_C     67
#define KEY_D     68
#define KEY_E     69
#define KEY_F     70
#define KEY_G     71
#define KEY_H     72
#define KEY_I     73
#define KEY_J     74
#define KEY_K     75
#define KEY_L     76
#define KEY_M     77
#define KEY_N     78
#define KEY_O     79
#define KEY_P     80
#define KEY_Q     81
#define KEY_R     82
#define KEY_S     83
#define KEY_T     84
#define KEY_U     85
#define KEY_V     86
#define KEY_W     87
#define KEY_X     88
#define KEY_Y     89
#define KEY_Z     90

#define NO_UNROLL_(x, int_which_cannot_be_negative) ((x) + min(0, (int_which_cannot_be_negative)))
#define NO_UNROLL(x) NO_UNROLL_(x, iFrame)