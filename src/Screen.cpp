#include "Screen.h"

#include "FileSystemUtils.h"
#include "GraphicsUtil.h"
#include "gfx.h"

#include <stdlib.h>

Screen::Screen()
{
    m_screen = NULL;
    isWindowed = true;
    stretchMode = 0;
    isFiltered = false;
    filterSubrect.x = 1;
    filterSubrect.y = 1;
    filterSubrect.w = 318;
    filterSubrect.h = 238;

	a_screen = GFX_CreateRGBSurface(0, 640, 480, 32, 0,0,0,0);
	m_screen = GFX_CreateRGBSurface(SDL_SWSURFACE, 320, 240, 32,
	0x00FF0000,
	0x0000FF00,
	0x000000FF,
	0xFF000000);

    badSignalEffect = false;

    glScreen = true;
}

void Screen::ResizeScreen(int x , int y) {}

void Screen::GetWindowSize(int* x, int* y) {}

void Screen::UpdateScreen(SDL_Surface* buffer, SDL_Rect* rect )
{
    if((buffer == NULL) && (m_screen == NULL) )
    {
        return;
    }

    if(badSignalEffect)
    {
        buffer = ApplyFilter(buffer);
    }

    GFX_ClearSurface(m_screen);
    BlitSurfaceStandard(buffer,NULL,m_screen,rect);

    if(badSignalEffect)
    {
        GFX_FreeSurface(buffer);
    }
    //SDL_Flip(screen);
}

const SDL_PixelFormat* Screen::GetFormat()
{
    return m_screen->format;
}

//
//	upscale 2x 32bpp NEON
//		src width must be 16xN
//
void Screen::upscale32NEON(void* src, void* dst) {
	asm volatile (
	".equ	WIDTH,	320		;"
	".equ	HEIGHT,	240		;"
	"	add r2,%0,#(WIDTH*HEIGHT*4);"
	"1:	add r3,%1,#(WIDTH*2*4)	;"
	"	add lr,%1,#(WIDTH*2*2*4);"
	"2:	vldmia %0!,{q8-q11}	;"
	"	vdup.32 d31,d23[1]	;"
	"	vdup.32 d30,d23[0]	;"
	"	vdup.32 d29,d22[1]	;"
	"	vdup.32 d28,d22[0]	;"
	"	vdup.32 d27,d21[1]	;"
	"	vdup.32 d26,d21[0]	;"
	"	vdup.32 d25,d20[1]	;"
	"	vdup.32 d24,d20[0]	;"
	"	vdup.32 d23,d19[1]	;"
	"	vdup.32 d22,d19[0]	;"
	"	vdup.32 d21,d18[1]	;"
	"	vdup.32 d20,d18[0]	;"
	"	vdup.32 d19,d17[1]	;"
	"	vdup.32 d18,d17[0]	;"
	"	vdup.32 d17,d16[1]	;"
	"	vdup.32 d16,d16[0]	;"
	"	vstmia %1!,{q8-q15}	;"
	"	vstmia r3!,{q8-q15}	;"
	"	cmp r3,lr		;"
	"	bne 2b			;"
	"	cmp %0,r2		;"
	"	mov %1,lr		;"
	"	bne 1b			"
	:: "r"(src), "r"(dst)
	: "r2","r3","lr","q8","q9","q10","q11","q12","q13","q14","q15","memory","cc"
	);
}

void Screen::FlipScreen()
{
	if (isFiltered == false) {
		upscale32NEON(m_screen->pixels, a_screen->pixels);
		GFX_Flip(a_screen);
	} else {
		GFX_Flip(m_screen);
	}
//	GFX_ClearSurface(m_screen);
}

void Screen::toggleFullScreen()
{
	isWindowed = !isWindowed;
	ResizeScreen(-1, -1);
}

void Screen::toggleStretchMode()
{
	stretchMode = (stretchMode + 1) % 3;
	ResizeScreen(-1, -1);
}

void Screen::toggleLinearFilter() {
	isFiltered = !isFiltered;
}

void Screen::ClearScreen( int colour )
{
	if (!colour) GFX_ClearSurface(m_screen);
	else FillRect(m_screen, colour) ;
}
