#ifndef __GFX_H__
#define __GFX_H__

#include <stdint.h>
#include <linux/fb.h>
#include <SDL/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	pixelsPa	unused1

extern	int				fd_fb;
extern	struct	fb_fix_screeninfo	finfo;
extern	struct	fb_var_screeninfo	vinfo;

enum { GFX_BLOCKING = 1, GFX_FLIPWAIT = 2 };

void		GFX_Init(void);
void		GFX_Quit(void);
SDL_Surface*	GFX_CreateRGBSurface(uint32_t flags, int width, int height, int depth, uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask);
void		GFX_FreeSurface(SDL_Surface *surface);
void		GFX_ClearSurface(SDL_Surface *surface);
void		GFX_CopySurface(SDL_Surface *src, SDL_Surface *dst);
SDL_Surface*	GFX_DuplicateSurface(SDL_Surface *surface);
void		GFX_Flip(SDL_Surface *surface);
void		GFX_FlipWait(SDL_Surface *surface);
void		GFX_FlipNoWait(SDL_Surface *surface);
void		GFX_FlipForce(SDL_Surface *surface);
void		GFX_UpdateRect(SDL_Surface *screen, int x, int y, int w, int h);
void		GFX_UpdateRectWait(SDL_Surface *screen, int x, int y, int w, int h);
void		GFX_UpdateRectNoWait(SDL_Surface *screen, int x, int y, int w, int h);
void		GFX_UpdateRectForce(SDL_Surface *screen, int x, int y, int w, int h);
void		GFX_SetFlipFlags(uint32_t flags);
uint32_t	GFX_GetFlipFlags(void);
void		GFX_ClearFrameBuffer(void);
void		GFX_FillRectSYS(SDL_Surface* dst, SDL_Rect* dstrect, uint32_t color);
void		GFX_FillRect(SDL_Surface* dst, SDL_Rect* dstrect, uint32_t color);
void		GFX_FillRectNoWait(SDL_Surface* dst, SDL_Rect* dstrect, uint32_t color);
void		GFX_WaitAllDone(void);
void		GFX_BlitSurfaceSYS(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
void		GFX_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
void		GFX_BlitSurfaceRotate(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t rotate);
void		GFX_BlitSurfaceMirror(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t mirror);
void		GFX_BlitSurfaceNoWait(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
void		GFX_BlitSurfaceRotateNoWait(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t rotate);
void		GFX_BlitSurfaceMirrorNoWait(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t mirror);

#ifdef __cplusplus
}
#endif

#endif
