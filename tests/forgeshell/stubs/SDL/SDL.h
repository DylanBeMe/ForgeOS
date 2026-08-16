#ifndef SDL_H_STUB
#define SDL_H_STUB
#include <stdint.h>
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef void *SDL_TimerID;
typedef Uint32 (*SDL_NewTimerCallback)(Uint32 interval, void *param);
typedef struct SDL_PixelFormat SDL_PixelFormat;
typedef struct _SDL_Joystick SDL_Joystick;
typedef struct SDL_Surface { SDL_PixelFormat *format; } SDL_Surface;
typedef struct SDL_Rect { Sint16 x; Sint16 y; Uint16 w; Uint16 h; } SDL_Rect;
typedef enum SDLKey {
SDLK_UNKNOWN=0, SDLK_BACKSPACE=8, SDLK_TAB=9, SDLK_RETURN=13, SDLK_ESCAPE=27,
SDLK_SPACE=32, SDLK_UP=273, SDLK_DOWN=274, SDLK_RIGHT=275, SDLK_LEFT=276,
SDLK_LSHIFT=304, SDLK_LCTRL=306, SDLK_LALT=308, SDLK_RCTRL=305
} SDLKey;
typedef struct SDL_keysym { SDLKey sym; } SDL_keysym;
typedef struct SDL_KeyboardEvent { Uint8 type; SDL_keysym keysym; } SDL_KeyboardEvent;
typedef struct SDL_JoyAxisEvent { Uint8 type; Uint8 which; Uint8 axis; Sint16 value; } SDL_JoyAxisEvent;
typedef struct SDL_JoyButtonEvent { Uint8 type; Uint8 which; Uint8 button; Uint8 state; } SDL_JoyButtonEvent;
typedef struct SDL_JoyHatEvent { Uint8 type; Uint8 which; Uint8 hat; Uint8 value; } SDL_JoyHatEvent;
typedef union SDL_Event { Uint8 type; SDL_KeyboardEvent key; SDL_JoyAxisEvent jaxis; SDL_JoyButtonEvent jbutton; SDL_JoyHatEvent jhat; } SDL_Event;
#define SDL_INIT_TIMER 0x00000001U
#define SDL_INIT_VIDEO 0x00000020U
#define SDL_INIT_JOYSTICK 0x00000200U
#define SDL_SWSURFACE 0x00000000U
#define SDL_FULLSCREEN 0x80000000U
#define SDL_DISABLE 0
#define SDL_QUIT 12
#define SDL_KEYDOWN 2
#define SDL_KEYUP 3
#define SDL_JOYAXISMOTION 7
#define SDL_JOYHATMOTION 9
#define SDL_JOYBUTTONDOWN 10
#define SDL_JOYBUTTONUP 11
#define SDL_USEREVENT 24
#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP 0x01
#define SDL_HAT_RIGHT 0x02
#define SDL_HAT_DOWN 0x04
#define SDL_HAT_LEFT 0x08
int SDL_Init(Uint32 flags);
void SDL_Quit(void);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
                                  Uint32 rmask, Uint32 gmask, Uint32 bmask, Uint32 amask);
int SDL_SoftStretch(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_ShowCursor(int toggle);
int SDL_EnableKeyRepeat(int delay, int interval);
int SDL_NumJoysticks(void);
SDL_Joystick *SDL_JoystickOpen(int device_index);
void SDL_JoystickClose(SDL_Joystick *joystick);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
int SDL_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
SDL_Surface *SDL_LoadBMP(const char *file);
void SDL_FreeSurface(SDL_Surface *surface);
int SDL_Flip(SDL_Surface *screen);
Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);
int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
SDL_TimerID SDL_AddTimer(Uint32 interval, SDL_NewTimerCallback callback, void *param);
int SDL_RemoveTimer(SDL_TimerID timer);
#endif
