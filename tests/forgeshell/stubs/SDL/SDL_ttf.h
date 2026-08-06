#ifndef SDL_TTF_H_STUB
#define SDL_TTF_H_STUB
#include "SDL.h"
typedef struct _TTF_Font TTF_Font;
typedef struct SDL_Color { Uint8 r, g, b, unused; } SDL_Color;
int TTF_Init(void);
void TTF_Quit(void);
int TTF_WasInit(void);
TTF_Font *TTF_OpenFont(const char *file, int ptsize);
void TTF_CloseFont(TTF_Font *font);
SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *font, const char *text, SDL_Color fg);
int TTF_SizeUTF8(TTF_Font *font, const char *text, int *w, int *h);
#endif
