#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <pthread.h>
#include <errno.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <SDL/SDL.h>
#include <mi_sys.h>
#include <mi_gfx.h>

#define	pixelsPa	unused1
#define ALIGN4K(val)	((val+4095)&(~4095))
//	FREEMMA		: force free all allocated MMAs when init & quit
#define FREEMMA
//	GFX_BLOCKING	: limit to 60fps but never skips frames
//			:  in case of clearing all buffers by GFX_Flip()x3, needs to use BLOCKING (or GFX_FlipForce())
//	GFX_FLIPWAIT	: wait until Blit is done when flip
//			:  when NOWAIT, do not clear/write source surface immediately after Flip
//			:  if absolutely necessary, use GFX_WaitAllDone() before write (or GFX_FlipWait())
enum { GFX_BLOCKING = 1, GFX_FLIPWAIT = 2 };
//#define	DEFAULTFLIPFLAGS	(GFX_BLOCKING | GFX_FLIPWAIT)		// versatile but slower
#define	DEFAULTFLIPFLAGS	0					// high performance

int			fd_fb = 0;
struct			fb_fix_screeninfo finfo;
struct			fb_var_screeninfo vinfo;
MI_GFX_Surface_t	stSrc;
MI_GFX_Rect_t		stSrcRect;
MI_GFX_Surface_t	stDst;
MI_GFX_Rect_t		stDstRect;
MI_GFX_Opt_t		stOpt;
volatile uint32_t	now_flipping;
MI_PHY			shadowPa;
uint32_t		shadowsize;
pthread_t		flip_pt;
pthread_mutex_t		flip_mx;
pthread_cond_t		flip_req;
pthread_cond_t		flip_start;
MI_U16			flipFence;
uint32_t		flipFlags;
#ifndef	FREEMMA
#define			MMADBMAX	100
uint32_t		mma_db[MMADBMAX];
#endif

//
//	Actual Flip thread
//
static void* GFX_FlipThread(void* param) {
	uint32_t	target_offset;
	MI_U16		Fence;
	pthread_mutex_lock(&flip_mx);
	while(1) {
		while (!now_flipping) pthread_cond_wait(&flip_req, &flip_mx);
		Fence = flipFence;
		do {	target_offset = vinfo.yoffset + 480;
			if ( target_offset == 1440 ) target_offset = 0;
			vinfo.yoffset = target_offset;
			pthread_cond_signal(&flip_start);
			pthread_mutex_unlock(&flip_mx);
			if (Fence) { MI_GFX_WaitAllDone(FALSE, Fence); Fence = 0; }
			ioctl(fd_fb, FBIOPAN_DISPLAY, &vinfo);
			pthread_mutex_lock(&flip_mx);
		} while(--now_flipping);
	}
	return 0;
}

//
//	Get GFX_ColorFmt from SDL_Surface
//
MI_GFX_ColorFmt_e	GFX_ColorFmt(SDL_Surface *surface) {
	if (surface != NULL) {
		if (surface->format->BytesPerPixel == 2) {
			if (surface->format->Amask == 0) return E_MI_GFX_FMT_RGB565;
			if (surface->format->Amask == 0x8000) return E_MI_GFX_FMT_ARGB1555;
			if (surface->format->Amask == 0xF000) {
				if (surface->format->Bmask == 0x000F) return E_MI_GFX_FMT_ARGB4444;
				return E_MI_GFX_FMT_ABGR4444;
			}
			if (surface->format->Amask == 0x000F) {
				if (surface->format->Bmask == 0x00F0) return E_MI_GFX_FMT_RGBA4444;
				return E_MI_GFX_FMT_BGRA4444;
			}
			return E_MI_GFX_FMT_RGB565;
		}
		if (surface->format->Bmask == 0x000000FF) return E_MI_GFX_FMT_ARGB8888;
		if (surface->format->Amask == 0x000000FF) {
			if (surface->format->Bmask == 0x0000FF00) return E_MI_GFX_FMT_RGBA8888;
			return E_MI_GFX_FMT_BGRA8888;
		}
		if (surface->format->Rmask == 0x000000FF) return E_MI_GFX_FMT_ABGR8888;
	}
	return E_MI_GFX_FMT_ARGB8888;
}

//
//	Get SYS_PixelFormat from SDL_Surface
//
MI_SYS_PixelFormat_e	SYS_PixelFormat(SDL_Surface *surface) {
	if (surface != NULL) {
		if (surface->format->BytesPerPixel == 2) {
			if (surface->format->Amask == 0) return E_MI_SYS_PIXEL_FRAME_RGB565;
			if (surface->format->Amask == 0x8000) return E_MI_SYS_PIXEL_FRAME_ARGB1555;
			if (surface->format->Amask == 0xF000) return E_MI_SYS_PIXEL_FRAME_ARGB4444;
			return E_MI_SYS_PIXEL_FRAME_RGB565;
		}
		if (surface->format->Bmask == 0x000000FF) return E_MI_SYS_PIXEL_FRAME_ARGB8888;
		if (surface->format->Amask == 0x000000FF) return E_MI_SYS_PIXEL_FRAME_BGRA8888;
		if (surface->format->Rmask == 0x000000FF) return E_MI_SYS_PIXEL_FRAME_ABGR8888;
	}
	return E_MI_SYS_PIXEL_FRAME_ARGB8888;
}

//
//	GFX Flip / in place of SDL_Flip
//		HW Blit : surface -> FB(backbuffer) with Rotate180/bppConvert/Scaling
//			and Request Flip
//		rev9 : Use an intermediate buffer to improve FlipWait
//
void	GFX_FlipExec(SDL_Surface *surface, uint32_t flags) {
	uint32_t	target_offset, surfacesize;

	if ((fd_fb)&&(surface != NULL)&&(surface->pixelsPa)) {
		surfacesize = surface->pitch * surface->h;
		stSrc.eColorFmt = GFX_ColorFmt(surface);
		stSrc.u32Width = surface->w;
		stSrc.u32Height = surface->h;
		stSrc.u32Stride = surface->pitch;
		stSrcRect.u32Width = stSrc.u32Width;
		stSrcRect.u32Height = stSrc.u32Height;

		if (flags & GFX_FLIPWAIT) {
			if (flipFence) MI_GFX_WaitAllDone(FALSE, flipFence);
			if (shadowsize < surfacesize) {
				if (shadowPa) MI_SYS_MMA_Free(shadowPa);
				if (MI_SYS_MMA_Alloc(NULL, ALIGN4K(surfacesize), &shadowPa)) {
					shadowPa = shadowsize = 0; goto NOWAIT;
				}
				shadowsize = surfacesize;
			}
			MI_SYS_FlushInvCache(surface->pixels, ALIGN4K(surfacesize));
			MI_SYS_MemcpyPa(shadowPa, surface->pixelsPa, surfacesize);
			stSrc.phyAddr = shadowPa;
		} else {
		NOWAIT:	MI_SYS_FlushInvCache(surface->pixels, ALIGN4K(surfacesize));
			stSrc.phyAddr = surface->pixelsPa;
		}

		pthread_mutex_lock(&flip_mx);
		if (flags & GFX_BLOCKING) {
			while (now_flipping == 2) pthread_cond_wait(&flip_start, &flip_mx);
		}
		target_offset = vinfo.yoffset + 480;
		if ( target_offset == 1440 ) target_offset = 0;
		stDst.phyAddr = finfo.smem_start + (640*target_offset*4);
		MI_GFX_BitBlit(&stSrc, &stSrcRect, &stDst, &stDstRect, &stOpt, &flipFence);

		// Request Flip
		if (!now_flipping) {
			now_flipping = 1;
			pthread_cond_signal(&flip_req);
			pthread_cond_wait(&flip_start, &flip_mx);
		} else {
			now_flipping = 2;
		}
		pthread_mutex_unlock(&flip_mx);
	}
}
void		GFX_Flip(SDL_Surface *surface) { GFX_FlipExec(surface, flipFlags); }
void		GFX_FlipNoWait(SDL_Surface *surface) { GFX_FlipExec(surface, flipFlags & ~GFX_FLIPWAIT); }
void		GFX_FlipWait(SDL_Surface *surface) { GFX_FlipExec(surface, flipFlags | GFX_FLIPWAIT); }
void		GFX_FlipForce(SDL_Surface *surface) { GFX_FlipExec(surface, flipFlags | GFX_BLOCKING); }
void		GFX_SetFlipFlags(uint32_t flags) { flipFlags = flags; }
uint32_t	GFX_GetFlipFlags(void) { return flipFlags; }

#ifdef	FREEMMA
//
//	Free all allocated MMAs (except "daemon")
//
void	freemma(void) {
	FILE		*fp;
	const char	*heapinfoname = "/proc/mi_modules/mi_sys_mma/mma_heap_name0";
	char		str[256];
	uint32_t	offset, length, usedflag;
	uint32_t	baseaddr = finfo.smem_start - 0x021000;	// default baseaddr (tmp)

	// open heap information file
	fp = fopen(heapinfoname, "r");
	if (fp) {
		// skip reading until chunk information
		do { if (fscanf(fp, "%255s", str) == EOF) { fclose(fp); return; } } while (strcmp(str,"sys-logConfig"));
		// get MMA each chunk information and release
		while(fscanf(fp, "%x %x %x %255s", &offset, &length, &usedflag, str) != EOF) {
			if (!usedflag) continue; // NA
			if (!strcmp(str,"fb_device")) { // FB .. fix baseaddr
				baseaddr = finfo.smem_start - offset; continue;
			}
			if (!strcmp(str,"ao-Dev0-tmp")) continue; // ao .. Audio buffer, skip
			// For daemon program authors, MMA allocated as "daemon" will not be released
			if (strncmp(str,"daemon",6)) { // others except "daemon" .. release
				if (!MI_SYS_MMA_Free(baseaddr + offset)) {
					fprintf(stdout, "MMA_Released %s offset : %08X length : %08X\n", str, offset, length);
				}
			}
		}
		fclose(fp);
	}
}
#endif

//
//	Clear entire FrameBuffer
//
void GFX_ClearFrameBuffer(void) { MI_SYS_MemsetPa(finfo.smem_start, 0, finfo.smem_len); }

//
//	GFX Init
//		Prepare for HW Blit to FB
//
void	GFX_Init(void) {
	if (fd_fb == 0) {
		MI_SYS_Init();
		MI_GFX_Open();
#ifdef	FREEMMA
		freemma();
#else
		memset(mma_db, 0, sizeof(mma_db));
#endif
		fd_fb = open("/dev/fb0", O_RDWR);

		// 640 x 480 x 32bpp x 3screen init
		SDL_SetVideoMode(640, 480, 32, SDL_SWSURFACE);
		ioctl(fd_fb, FBIOGET_VSCREENINFO, &vinfo);
		vinfo.yres_virtual = 1440; vinfo.yoffset = 0;
		/* vinfo.xres = vinfo.xres_virtual = 640; vinfo.yres = 480;
		vinfo.xoffset = vinfo.yoffset = vinfo.red.msb_right = vinfo.green.msb_right = 
		vinfo.blue.msb_right = vinfo.transp.msb_right = vinfo.blue.offset = 0;
		vinfo.red.length = vinfo.green.length = vinfo.blue.length = vinfo.transp.length = vinfo.green.offset = 8;
		vinfo.red.offset = 16; vinfo.transp.offset = 24; vinfo.bits_per_pixel = 32; */
		ioctl(fd_fb, FBIOPUT_VSCREENINFO, &vinfo);

		// get physical address of FB
		ioctl(fd_fb, FBIOGET_FSCREENINFO, &finfo);
		// clear entire FB
		GFX_ClearFrameBuffer();

		// prepare for Flip
		stDst.phyAddr = finfo.smem_start;
		stDst.eColorFmt = E_MI_GFX_FMT_ARGB8888;
		stDst.u32Width = 640;
		stDst.u32Height = 480;
		stDst.u32Stride = 640*4;
		stDstRect.s32Xpos = 0;
		stDstRect.s32Ypos = 0;
		stDstRect.u32Width = 640;
		stDstRect.u32Height = 480;
		stSrcRect.s32Xpos = 0;
		stSrcRect.s32Ypos = 0;

		memset(&stOpt, 0, sizeof(stOpt));
		stOpt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
		stOpt.eRotate = E_MI_GFX_ROTATE_180;

		flip_mx = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
		flip_req = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
		flip_start = (pthread_cond_t)PTHREAD_COND_INITIALIZER;
		now_flipping = shadowPa = shadowsize = flipFence = 0;
		flipFlags = DEFAULTFLIPFLAGS;
		pthread_create(&flip_pt, NULL, GFX_FlipThread, NULL);
	}
}

//
//	GFX Quit
//
void	GFX_Quit(void) {
	if (fd_fb) {
		pthread_cancel(flip_pt);
		pthread_join(flip_pt,NULL);

		if (shadowPa) MI_SYS_MMA_Free(shadowPa);
		// clear entire FB
		GFX_ClearFrameBuffer();
		// reset yoffset
		vinfo.yoffset = 0;
		ioctl(fd_fb, FBIOPUT_VSCREENINFO, &vinfo);
		close(fd_fb);
		fd_fb = 0;
#ifdef	FREEMMA
		freemma();
#else
		for (uint32_t i=0; i<MMADBMAX; i++) {
			if (mma_db[i]) {
				if (!MI_SYS_MMA_Free(mma_db[i])) {
					fprintf(stdout, "MMA_Released offset : %08X\n", mma_db[i]);
					mma_db[i] = 0;
				}
			}
		}
#endif
		MI_GFX_Close();
		MI_SYS_Exit();
	}
}

//
//	Create GFX Surface / in place of SDL_CreateRGBSurface
//		flags has no meaning, fixed to SWSURFACE
//		Additional return value : surface->unused1 = Physical address of surface
//
SDL_Surface*	GFX_CreateRGBSurface(uint32_t flags, int width, int height, int depth, uint32_t Rmask, uint32_t Gmask, uint32_t Bmask, uint32_t Amask) {
	SDL_Surface*	surface;
	MI_PHY		phyAddr;
	void*		virAddr;
	if (!width) width = 640;
	if (!height) height = 480;
	if (!depth) depth = 32;
	int		pitch = width * (uint32_t)(depth/8);
	uint32_t	size = pitch * height;

	if (MI_SYS_MMA_Alloc(NULL, ALIGN4K(size), &phyAddr)) return NULL;
#ifndef	FREEMMA
	uint32_t i;
	for (i=0; i<MMADBMAX; i++) {
		if (!mma_db[i]) {
			mma_db[i] = phyAddr; break;
		}
	} if (i==MMADBMAX) { MI_SYS_MMA_Free(phyAddr); return NULL; }
#endif
	MI_SYS_MemsetPa(phyAddr, 0, size);
	MI_SYS_Mmap(phyAddr, ALIGN4K(size), &virAddr, TRUE);	// write cache ON needs Flush when r/w Pa directly

	surface = SDL_CreateRGBSurfaceFrom(virAddr,width,height,depth,pitch,Rmask,Gmask,Bmask,Amask);
	if (surface != NULL) surface->pixelsPa = phyAddr;
	return surface;
}

//
//	Free GFX Surface / in place of SDL_FreeSurface
//
void	GFX_FreeSurface(SDL_Surface *surface) {
	if (surface != NULL) {
		MI_PHY		phyAddr = surface->pixelsPa;
		void*		virAddr = surface->pixels;
		uint32_t	size = surface->pitch * surface->h;

		SDL_FreeSurface(surface);
		if (phyAddr) {
			MI_SYS_Munmap(virAddr, ALIGN4K(size));
			MI_SYS_MMA_Free(phyAddr);
#ifndef	FREEMMA
			for (uint32_t i=0; i<MMADBMAX; i++) {
				if (mma_db[i] == phyAddr) {
					mma_db[i] = 0; break;
				}
			}
#endif
		}
	}
}

//
//	Clear GFX/SDL Surface (entire)
//
void	GFX_ClearSurface(SDL_Surface *surface) {
	if (surface != NULL) {
		uint32_t size = surface->pitch * surface->h;
		if (surface->pixelsPa) {
			MI_SYS_FlushInvCache(surface->pixels, ALIGN4K(size));
			MI_SYS_MemsetPa(surface->pixelsPa, 0, size);
		} else {
			memset(surface->pixels, 0, size);
		}
	}
}

//
//	Copy GFX/SDL Surface (entire)
//		src/dst surfaces must be the same size
//
void	GFX_CopySurface(SDL_Surface *src, SDL_Surface *dst) {
	if ((src != NULL)&&(dst != NULL)) {
		uint32_t size = src->pitch * src->h;
		if (size == (uint32_t)(dst->pitch * dst->h)) {
			if ((src->pixelsPa)&&(dst->pixelsPa)) {
				MI_SYS_FlushInvCache(src->pixels, ALIGN4K(size));
				MI_SYS_FlushInvCache(dst->pixels, ALIGN4K(size));
				MI_SYS_MemcpyPa(dst->pixelsPa, src->pixelsPa, size);
			} else {
				memcpy(dst->pixels, src->pixels, size);
			}
		}
	}
}

//
//	Duplicate GFX Surface from SDL_Surface
//
SDL_Surface*	GFX_DuplicateSurface(SDL_Surface *src) {
	if (src == NULL) return NULL;
	SDL_Surface	*dst;
	dst = GFX_CreateRGBSurface(0, src->w, src->h, src->format->BitsPerPixel,
		src->format->Rmask, src->format->Gmask, src->format->Bmask, src->format->Amask);
	if (dst != NULL) GFX_CopySurface(src, dst);
	return dst;
}

//
//	GFX UpdateRect
//		Flip after setting the update area
//		*Note* blit from entire screen to framebuffer rect
//
void	GFX_UpdateRectExec(SDL_Surface *screen, int x, int y, int w, int h, uint32_t flags) {
	if ((screen != NULL)&&(screen->pixelsPa)) {
		if (x|y|w|h) {
			MI_GFX_Rect_t DstRectPush = stDstRect;
			stDstRect.s32Xpos = 640-(x+w);	// for rotate180
			stDstRect.s32Ypos = 480-(y+h);	// 
			stDstRect.u32Width = w;
			stDstRect.u32Height = h;
			GFX_FlipExec(screen, flags);
			stDstRect = DstRectPush;
		} else {
			GFX_FlipExec(screen, flags);
		}
	}
}
void	GFX_UpdateRect(SDL_Surface *screen, int x, int y, int w, int h) {
	GFX_UpdateRectExec(screen, x, y, w, h, flipFlags); }
void	GFX_UpdateRectNoWait(SDL_Surface *screen, int x, int y, int w, int h) {
	GFX_UpdateRectExec(screen, x, y, w, h, flipFlags & ~GFX_FLIPWAIT); }
void	GFX_UpdateRectWait(SDL_Surface *screen, int x, int y, int w, int h) {
	GFX_UpdateRectExec(screen, x, y, w, h, flipFlags | GFX_FLIPWAIT); }
void	GFX_UpdateRectForce(SDL_Surface *screen, int x, int y, int w, int h) {
	GFX_UpdateRectExec(screen, x, y, w, h, flipFlags | GFX_BLOCKING); }

//
//	Check Rect Overflow
//
SDL_Rect* CheckRect(SDL_Surface* dst, SDL_Rect* dstrect) {
	if ((dst == NULL)||(dstrect == NULL)) return NULL;
	int w = dstrect->w; int h = dstrect->h;
	if (dstrect->x < 0) { w += dstrect->x; dstrect->x = 0; }
	if (dstrect->y < 0) { h += dstrect->y; dstrect->y = 0; }
	if ((dstrect->x + w) > dst->w) { w = dst->w - dstrect->x; }
	if ((dstrect->y + h) > dst->h) { h = dst->h - dstrect->y; }
	if ((w <= 0)||(h <= 0)||(dstrect->x >= dst->w)||(dstrect->y >= dst->h)) return NULL;
	dstrect->w = w; dstrect->h = h;
	return dstrect;
}

//
//	GFX FillRect (MI_SYS ver) / in place of SDL_FillRect
//		*Note* color : in case of RGB565 : 2 pixel color values used alternately
//
void	GFX_FillRectSYS(SDL_Surface* dst, SDL_Rect* dstrect, uint32_t color) {
	if ((dst != NULL)&&(dst->pixelsPa)) {
		if (dstrect != NULL) { if ((dstrect = CheckRect(dst, dstrect)) == NULL) return; }

		MI_SYS_FrameData_t Buf;
		MI_SYS_WindowRect_t Rect;

		Buf.phyAddr[0] = dst->pixelsPa;
		Buf.u16Width = dst->w;
		Buf.u16Height = dst->h;
		Buf.u32Stride[0] = dst->pitch;
		Buf.ePixelFormat = SYS_PixelFormat(dst);
		if (dstrect != NULL) {
			Rect.u16X = dstrect->x;
			Rect.u16Y = dstrect->y;
			Rect.u16Width = dstrect->w;
			Rect.u16Height = dstrect->h;
		} else {
			Rect.u16X = 0;
			Rect.u16Y = 0;
			Rect.u16Width = Buf.u16Width;
			Rect.u16Height = Buf.u16Height;
		}

		MI_SYS_FlushInvCache(dst->pixels, ALIGN4K(dst->pitch * dst->h));
		MI_SYS_BufFillPa(&Buf, color, &Rect);
	} else 	SDL_FillRect(dst, dstrect, color);
}

//
//	GFX FillRect (MI_GFX ver) / in place of SDL_FillRect
//		*Note* color : in case of RGB565 : ARGB8888 color value
//		nowait : 0 = wait until done / 1 = no wait
//
void	GFX_FillRectExec(SDL_Surface* dst, SDL_Rect* dstrect, uint32_t color, uint32_t nowait) {
	if ((dst != NULL)&&(dst->pixelsPa)) {
		if (dstrect != NULL) { if ((dstrect = CheckRect(dst, dstrect)) == NULL) return; }

		MI_GFX_Surface_t Dst;
		MI_GFX_Rect_t DstRect;
		MI_U16 Fence;

		Dst.phyAddr = dst->pixelsPa;
		Dst.eColorFmt = GFX_ColorFmt(dst);
		Dst.u32Width = dst->w;
		Dst.u32Height = dst->h;
		Dst.u32Stride = dst->pitch;
		if (dstrect != NULL) {
			DstRect.s32Xpos = dstrect->x;
			DstRect.s32Ypos = dstrect->y;
			DstRect.u32Width = dstrect->w;
			DstRect.u32Height = dstrect->h;
		} else {
			DstRect.s32Xpos = 0;
			DstRect.s32Ypos = 0;
			DstRect.u32Width = Dst.u32Width;
			DstRect.u32Height = Dst.u32Height;
		}

		MI_SYS_FlushInvCache(dst->pixels, ALIGN4K(dst->pitch * dst->h));
		MI_GFX_QuickFill(&Dst, &DstRect, color, &Fence);
		if (!nowait) MI_GFX_WaitAllDone(FALSE, Fence);
	} else SDL_FillRect(dst, dstrect, color);
}
void	GFX_FillRect(SDL_Surface* dst, SDL_Rect* dstrect, uint32_t color) {
	GFX_FillRectExec(dst, dstrect, color, 0);
}
void	GFX_FillRectNoWait(SDL_Surface* dst, SDL_Rect* dstrect, uint32_t color) {
	GFX_FillRectExec(dst, dstrect, color, 1);
}

//
//	GFX_WaitAllDone / wait all done for No Wait functions
//
void	GFX_WaitAllDone(void) {
	MI_GFX_WaitAllDone(TRUE, 0);
}

//
//	GFX BlitSurface (MI_SYS ver) / in place of SDL_BlitSurface
//		*Note* Just a copy, no convert scale/bpp
//		** rect.h is not working properly for some reason, use GFX_BlitSurface instead **
//
void GFX_BlitSurfaceSYS(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
	if ((src != NULL)&&(dst != NULL)&&(src->pixelsPa)&&(dst->pixelsPa)) {
		MI_SYS_FrameData_t SrcBuf;
		MI_SYS_FrameData_t DstBuf;
		MI_SYS_WindowRect_t SrcRect;
		MI_SYS_WindowRect_t DstRect;

		memset(&SrcBuf, 0, sizeof(SrcBuf));
		SrcBuf.phyAddr[0] = src->pixelsPa;
		SrcBuf.u16Width = src->w;
		SrcBuf.u16Height = src->h;
		SrcBuf.u32Stride[0] = src->pitch;
		SrcBuf.ePixelFormat = SYS_PixelFormat(src);
		if (srcrect != NULL) {
			SrcRect.u16X = srcrect->x;
			SrcRect.u16Y = srcrect->y;
			SrcRect.u16Width = srcrect->w;
			SrcRect.u16Height = srcrect->h;
		} else {
			SrcRect.u16X = 0;
			SrcRect.u16Y = 0;
			SrcRect.u16Width = SrcBuf.u16Width;
			SrcRect.u16Height = SrcBuf.u16Height;
		}

		memset(&DstBuf, 0, sizeof(DstBuf));
		DstBuf.phyAddr[0] = dst->pixelsPa;
		DstBuf.u16Width = dst->w;
		DstBuf.u16Height = dst->h;
		DstBuf.u32Stride[0] = dst->pitch;
		DstBuf.ePixelFormat = SYS_PixelFormat(dst);
		if (dstrect != NULL) {
			DstRect.u16X = dstrect->x;
			DstRect.u16Y = dstrect->y;
			if ((dstrect->w==0)||(dstrect->h==0)) {
				DstRect.u16Width = SrcRect.u16Width;
				DstRect.u16Height = SrcRect.u16Height;
			} else {
				DstRect.u16Width = dstrect->w;
				DstRect.u16Height = dstrect->h;
			}
		} else {
			DstRect.u16X = 0;
			DstRect.u16Y = 0;
			DstRect.u16Width = DstBuf.u16Width;
			DstRect.u16Height = DstBuf.u16Height;
		}

		MI_SYS_FlushInvCache(src->pixels, ALIGN4K(src->pitch * src->h));
		MI_SYS_FlushInvCache(dst->pixels, ALIGN4K(dst->pitch * dst->h));
		MI_SYS_BufBlitPa(&DstBuf, &DstRect, &SrcBuf, &SrcRect);
	} else SDL_BlitSurface(src, srcrect, dst, dstrect);
}

//
//	GFX BlitSurface (MI_GFX ver) / in place of SDL_BlitSurface
//		with scale/bpp convert and rotate/mirror
//		rotate : 1 = 90 / 2 = 180 / 3 = 270
//		mirror : 1 = Horizontal / 2 = Vertical / 3 = Both
//		nowait : 0 = wait until done / 1 = no wait
//
void GFX_BlitSurfaceExec(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect,
			 uint32_t rotate, uint32_t mirror, uint32_t nowait) {
	if ((src != NULL)&&(dst != NULL)&&(src->pixelsPa)&&(dst->pixelsPa)) {
		MI_GFX_Surface_t Src;
		MI_GFX_Surface_t Dst;
		MI_GFX_Rect_t SrcRect;
		MI_GFX_Rect_t DstRect;
		MI_GFX_Opt_t Opt;
		MI_U16 Fence;

		memset(&Opt, 0, sizeof(Opt));
		Opt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
		Opt.eRotate = (MI_GFX_Rotate_e)rotate;
		Opt.eMirror = (MI_GFX_Mirror_e)mirror;

		Src.phyAddr = src->pixelsPa;
		Src.u32Width = src->w;
		Src.u32Height = src->h;
		Src.u32Stride = src->pitch;
		Src.eColorFmt = GFX_ColorFmt(src);
		if (srcrect != NULL) {
			SrcRect.s32Xpos = srcrect->x;
			SrcRect.s32Ypos = srcrect->y;
			SrcRect.u32Width = srcrect->w;
			SrcRect.u32Height = srcrect->h;
		} else {
			SrcRect.s32Xpos = 0;
			SrcRect.s32Ypos = 0;
			SrcRect.u32Width = Src.u32Width;
			SrcRect.u32Height = Src.u32Height;
		}

		Dst.phyAddr = dst->pixelsPa;
		Dst.u32Width = dst->w;
		Dst.u32Height = dst->h;
		Dst.u32Stride = dst->pitch;
		Dst.eColorFmt = GFX_ColorFmt(dst);
		if (dstrect != NULL) {
			DstRect.s32Xpos = dstrect->x;
			DstRect.s32Ypos = dstrect->y;
			if ((dstrect->w==0)||(dstrect->h==0)) {
				DstRect.u32Width = SrcRect.u32Width;
				DstRect.u32Height = SrcRect.u32Height;
			} else {
				DstRect.u32Width = dstrect->w;
				DstRect.u32Height = dstrect->h;
			}
		} else {
			DstRect.s32Xpos = 0;
			DstRect.s32Ypos = 0;
			DstRect.u32Width = Dst.u32Width;
			DstRect.u32Height = Dst.u32Height;
		}

		MI_SYS_FlushInvCache(src->pixels, ALIGN4K(src->pitch * src->h));
		MI_SYS_FlushInvCache(dst->pixels, ALIGN4K(dst->pitch * dst->h));
		MI_GFX_BitBlit(&Src, &SrcRect, &Dst, &DstRect, &Opt, &Fence);
		if (!nowait) MI_GFX_WaitAllDone(FALSE, Fence);
	} else SDL_BlitSurface(src, srcrect, dst, dstrect);
}
void GFX_BlitSurface(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
	GFX_BlitSurfaceExec(src, srcrect, dst, dstrect, 0, 0, 0);
}
void GFX_BlitSurfaceNoWait(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect) {
	GFX_BlitSurfaceExec(src, srcrect, dst, dstrect, 0, 0, 1);
}
void GFX_BlitSurfaceRotate(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t rotate) {
	GFX_BlitSurfaceExec(src, srcrect, dst, dstrect, rotate, 0, 0);
}
void GFX_BlitSurfaceRotateNoWait(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t rotate) {
	GFX_BlitSurfaceExec(src, srcrect, dst, dstrect, rotate, 0, 1);
}
void GFX_BlitSurfaceMirror(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t mirror) {
	GFX_BlitSurfaceExec(src, srcrect, dst, dstrect, 0, mirror, 0);
}
void GFX_BlitSurfaceMirrorNoWait(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect, uint32_t mirror) {
	GFX_BlitSurfaceExec(src, srcrect, dst, dstrect, 0, mirror, 1);
}
// TODO: add Alpha blend / Colorkey blit
