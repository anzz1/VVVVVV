#include "Graphics.h"

#include "gfx.h"

void setRect( SDL_Rect& _r, int x, int y, int w, int h )
{
    _r.x = x;
    _r.y = y;
    _r.w = w;
    _r.h = h;
}

unsigned int endian_swap( unsigned int x )
{
    return (x>>24) |
           ((x<<8) & 0x00FF0000) |
           ((x>>8) & 0x0000FF00) |
           (x<<24);
}


template <class T>
void endian_swap(T *objp)
{
	unsigned char *memp = reinterpret_cast<unsigned char*>(objp);
	std::reverse(memp, memp + sizeof(T));
}

void SDL_BlitSurfaceWithAlpha( SDL_Surface* _src, SDL_Rect* _srcRect, SDL_Surface* _dst, SDL_Rect* _dstRect )
{	// Limited to 32bpp ARGB/ABGR only for better performance
#define	unlikely(x)	__builtin_expect(!!(x), 0)

	Uint32	src_w , src_h , dst_w, dst_h;
	Sint32	srcRect_x, srcRect_y, dstRect_x, dstRect_y;
	Sint32	srcRect_w, srcRect_h;

	src_w = (Uint32)_src->w; src_h = (Uint32)_src->h;
	dst_w = (Uint32)_dst->w; dst_h = (Uint32)_dst->h;
	if (_srcRect) {
		srcRect_x = (Sint32)_srcRect->x; srcRect_y = (Sint32)_srcRect->y;
		srcRect_w = (Sint32)_srcRect->w; srcRect_h = (Sint32)_srcRect->h;
		if (unlikely(srcRect_x<0)) { srcRect_w += srcRect_x; srcRect_x = 0; }
		if (unlikely(srcRect_y<0)) { srcRect_h += srcRect_y; srcRect_y = 0; }
	} else {
		srcRect_x = srcRect_y = 0; srcRect_w = src_w; srcRect_h = src_h;
	}
	if (_dstRect) {
		dstRect_x = (Sint32)_dstRect->x; dstRect_y = (Sint32)_dstRect->y;
		if (unlikely(dstRect_x<0)) { srcRect_x -= dstRect_x; srcRect_w += dstRect_x; dstRect_x = 0; }
		if (unlikely(dstRect_y<0)) { srcRect_y -= dstRect_y; srcRect_h += dstRect_y; dstRect_y = 0; }
	} else {
		dstRect_x = dstRect_y = 0;
	}

	if (unlikely((srcRect_x > (Sint32)src_w)||(srcRect_y > (Sint32)src_h)||(dstRect_x > (Sint32)dst_w)||(dstRect_y > (Sint32)dst_h))) return;
	if (unlikely((dstRect_x + srcRect_w) > (Sint32)dst_w)) { srcRect_w = (Sint32)dst_w - dstRect_x; }
	if (unlikely((dstRect_y + srcRect_h) > (Sint32)dst_h)) { srcRect_h = (Sint32)dst_h - dstRect_y; }
	if (unlikely((srcRect_x + srcRect_w) > (Sint32)src_w)) { srcRect_w = (Sint32)src_w - srcRect_x; }
	if (unlikely((srcRect_y + srcRect_h) > (Sint32)src_h)) { srcRect_h = (Sint32)src_h - srcRect_y; }
	if (unlikely((srcRect_w <= 0)||(srcRect_h <= 0))) return; 

	register Uint32 *src = (Uint32 *)(_src->pixels) + srcRect_y*src_w + srcRect_x;
	register Uint32 *dst = (Uint32 *)(_dst->pixels) + dstRect_y*dst_w + dstRect_x;
	Uint32	srcadd = src_w - srcRect_w;
	Uint32	dstadd = dst_w - srcRect_w;

	if ( _src->format->Rmask == _dst->format->Rmask ) {
		for (Uint32 y=srcRect_h; y>0 ; y--, src+=srcadd, dst+=dstadd) {
			for (Uint32 x=srcRect_w; x>0 ; x--, dst++, src++) {
				if (*(Uint8*)((uintptr_t)src+3)) *dst = *src;	// Write if Alpha is ON
			}
		}
	} else {	// ARGB<>ABGR Blit
		for (Uint32 y=srcRect_h; y>0 ; y--, src+=srcadd, dst+=dstadd) {
			for (Uint32 x=srcRect_w; x>0 ; x--, dst++, src++) {
				if (*(Uint8*)((uintptr_t)src+3)) {
					register Uint32 a = *src;
					*dst = (a & 0xFF00FF00) | ((a&0xFF)<<16) | ((a>>16)&0xFF);
				}
			}
		}
	}
}

SDL_Surface* GetSubSurface( SDL_Surface* metaSurface, int x, int y, int width, int height )
{
    // Create an SDL_Rect with the area of the _surface
    SDL_Rect area;
    area.x = x;
    area.y = y;
    area.w = width;
    area.h = height;

    // Set the RGBA mask values.
    Uint32 r, g, b, a;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
    r = 0xff000000;
    g = 0x00ff0000;
    b = 0x0000ff00;
    a = 0x000000ff;
#else
    r = 0x000000ff;
    g = 0x0000ff00;
    b = 0x00ff0000;
    a = 0xff000000;
#endif

    //Convert to the correct display format after nabbing the new _surface or we will slow things down.
	SDL_Surface* preSurface = SDL_CreateRGBSurface(0, width, height, 32, r, g, b, a);

    // Lastly, apply the area from the meta _surface onto the whole of the sub _surface.
    SDL_BlitSurfaceWithAlpha(metaSurface, &area, preSurface, 0);

    // Return the new Bitmap _surface
    return preSurface;
}

void DrawPixel( SDL_Surface *_surface, int x, int y, Uint32 pixel )
{	// Limited to 32bpp only for better performance
	*(Uint32 *)((Uint32 *)_surface->pixels + (y * _surface->w) + x) = pixel;
}

Uint32 ReadPixel( SDL_Surface *_surface, int x, int y )
{	// Limited to 32bpp only for better performance
	return *(Uint32 *)((Uint32 *)_surface->pixels + (y * _surface->w) + x);
}

SDL_Surface * ScaleSurface( SDL_Surface *_surface, int Width, int Height, SDL_Surface * Dest )
{
    if(!_surface || !Width || !Height)
        return 0;

    SDL_Surface *_ret;
    if(Dest == NULL)
    {
	_ret = GFX_CreateRGBSurface(_surface->flags, Width, Height, _surface->format->BitsPerPixel,
                                    _surface->format->Rmask, _surface->format->Gmask, _surface->format->Bmask, _surface->format->Amask);
        if(_ret == NULL)
        {
            return NULL;
        }

    }
    else
    {
        _ret = Dest;
    }

    float  _stretch_factor_x = (static_cast<double>(Width)  / static_cast<double>(_surface->w)), _stretch_factor_y = (static_cast<double>(Height) / static_cast<double>(_surface->h));

	SDL_Rect gigantoPixel;
    for(Sint32 y = 0; y < _surface->h; y++)
        for(Sint32 x = 0; x < _surface->w; x++)
		{
			setRect(gigantoPixel, static_cast<Sint32>((float(x)*_stretch_factor_x) -1), static_cast<Sint32>((float(y) *_stretch_factor_y)-1), static_cast<Sint32>(_stretch_factor_x +1.0),static_cast<Sint32>( _stretch_factor_y+1.0)) ;
			SDL_FillRect(_ret, &gigantoPixel, ReadPixel(_surface, x, y));
		}
    return _ret;
}

SDL_Surface * ScaleSurfaceSlow( SDL_Surface *_surface, int Width, int Height)
{
	if(!_surface || !Width || !Height)
		return 0;

	SDL_Surface *_ret;

		_ret = GFX_CreateRGBSurface(_surface->flags, Width, Height, _surface->format->BitsPerPixel,
			_surface->format->Rmask, _surface->format->Gmask, _surface->format->Bmask, _surface->format->Amask);
		if(_ret == NULL)
		{
			return NULL;
		}

	float  _stretch_factor_x = (static_cast<double>(Width)  / static_cast<double>(_surface->w)), _stretch_factor_y = (static_cast<double>(Height) / static_cast<double>(_surface->h));

	for(Sint32 y = 0; y < _surface->h; y++) {
		for(Sint32 x = 0; x < _surface->w; x++) {
			for(Sint32 o_y = 0; o_y < _stretch_factor_y; ++o_y) {
				for(Sint32 o_x = 0; o_x < _stretch_factor_x; ++o_x) {
					DrawPixel(_ret, static_cast<Sint32>(_stretch_factor_x * x) + o_x,
					static_cast<Sint32>(_stretch_factor_y * y) + o_y, ReadPixel(_surface, x, y));
				}
			}
		}
	}
	return _ret;
}

SDL_Surface *  FlipSurfaceHorizontal(SDL_Surface* _src)
{
	SDL_Surface * ret = GFX_CreateRGBSurface(_src->flags, _src->w, _src->h, _src->format->BitsPerPixel,
		_src->format->Rmask, _src->format->Gmask, _src->format->Bmask, _src->format->Amask);
	if(ret == NULL)
	{
		return NULL;
	}

	for(Sint32 y = 0; y < _src->h; y++)
	{
		for(Sint32 x = 0; x < _src->w; x++)
		{
			DrawPixel(ret,(_src->w -1) -x,y,ReadPixel(_src, x, y));
		}
	}

	return ret;
}

SDL_Surface *  FlipSurfaceVerticleSDL(SDL_Surface* _src)
{
	SDL_Surface * ret = SDL_CreateRGBSurface(_src->flags, _src->w, _src->h, _src->format->BitsPerPixel,
		_src->format->Rmask, _src->format->Gmask, _src->format->Bmask, _src->format->Amask);
	if(ret == NULL)
	{
		return NULL;
	}

	// Limited to 32bpp only for better performance
	Uint32 pitch = _src->pitch;
	Uint8 *src = (Uint8 *)_src->pixels + (_src->h-1)*pitch;
	Uint8 *dst = (Uint8 *)ret->pixels;
	for (Uint32 y=_src->h; y>0; y--,src-=pitch,dst+=pitch) memcpy(dst,src,pitch);

	return ret;
}

SDL_Surface *  FlipSurfaceVerticle(SDL_Surface* _src)
{
	SDL_Surface * ret = GFX_CreateRGBSurface(_src->flags, _src->w, _src->h, _src->format->BitsPerPixel,
		_src->format->Rmask, _src->format->Gmask, _src->format->Bmask, _src->format->Amask);
	if(ret == NULL)
	{
		return NULL;
	}

	// Limited to 32bpp only for better performance
	Uint32 pitch = _src->pitch;
	Uint8 *src = (Uint8 *)_src->pixels + (_src->h-1)*pitch;
	Uint8 *dst = (Uint8 *)ret->pixels;
	for (Uint32 y=_src->h; y>0; y--,src-=pitch,dst+=pitch) memcpy(dst,src,pitch);

	return ret;
}

void BlitSurfaceStandard( SDL_Surface* _src, SDL_Rect* _srcRect, SDL_Surface* _dest, SDL_Rect* _destRect )
{
	SDL_BlitSurfaceWithAlpha(_src, _srcRect, _dest, _destRect);
}

void BlitSurfaceColoured(
    SDL_Surface* _src,
    SDL_Rect* _srcRect,
    SDL_Surface* _dest,
    SDL_Rect* _destRect,
    colourTransform& ct
) {	// Limited to 32bpp ARGB/ABGR only for better performance
	Uint32	src_w , src_h , dst_w, dst_h;
	Sint32	srcRect_x, srcRect_y, dstRect_x, dstRect_y;
	Sint32	srcRect_w, srcRect_h;

	src_w = (Uint32)_src->w; src_h = (Uint32)_src->h;
	dst_w = (Uint32)_dest->w; dst_h = (Uint32)_dest->h;
	if (_srcRect) {
		srcRect_x = (Sint32)_srcRect->x; srcRect_y = (Sint32)_srcRect->y;
		srcRect_w = (Sint32)_srcRect->w; srcRect_h = (Sint32)_srcRect->h;
		if (unlikely(srcRect_x<0)) { srcRect_w += srcRect_x; srcRect_x = 0; }
		if (unlikely(srcRect_y<0)) { srcRect_h += srcRect_y; srcRect_y = 0; }
	} else {
		srcRect_x = srcRect_y = 0; srcRect_w = src_w; srcRect_h = src_h;
	}
	if (_destRect) {
		dstRect_x = (Sint32)_destRect->x; dstRect_y = (Sint32)_destRect->y;
		if (unlikely(dstRect_x<0)) { srcRect_x -= dstRect_x; srcRect_w += dstRect_x; dstRect_x = 0; }
		if (unlikely(dstRect_y<0)) { srcRect_y -= dstRect_y; srcRect_h += dstRect_y; dstRect_y = 0; }
	} else {
		dstRect_x = dstRect_y = 0;
	}

	if (unlikely((srcRect_x > (Sint32)src_w)||(srcRect_y > (Sint32)src_h)||(dstRect_x > (Sint32)dst_w)||(dstRect_y > (Sint32)dst_h))) return;
	if (unlikely((dstRect_x + srcRect_w) > (Sint32)dst_w)) { srcRect_w = (Sint32)dst_w - dstRect_x; }
	if (unlikely((dstRect_y + srcRect_h) > (Sint32)dst_h)) { srcRect_h = (Sint32)dst_h - dstRect_y; }
	if (unlikely((srcRect_x + srcRect_w) > (Sint32)src_w)) { srcRect_w = (Sint32)src_w - srcRect_x; }
	if (unlikely((srcRect_y + srcRect_h) > (Sint32)src_h)) { srcRect_h = (Sint32)src_h - srcRect_y; }
	if (unlikely((srcRect_w <= 0)||(srcRect_h <= 0))) return; 
	register Uint32 *src = (Uint32 *)(_src->pixels) + srcRect_y*src_w + srcRect_x;
	register Uint32 *dst = (Uint32 *)(_dest->pixels) + dstRect_y*dst_w + dstRect_x;
	register Uint32 pixelcolor = (ct.colour & 0x00FFFFFF) | 0xFF000000;
	if ( _src->format->Rmask != _dest->format->Rmask ) pixelcolor = (pixelcolor & 0xFF00FF00) | ((pixelcolor&0xFF)<<16) | ((pixelcolor>>16)&0xFF);
	Uint32	srcadd = src_w - srcRect_w;
	Uint32	dstadd = dst_w - srcRect_w;

	for (Uint32 y=srcRect_h; y>0 ; y--, src+=srcadd, dst+=dstadd) {
		for (Uint32 x=srcRect_w; x>0 ; x--, dst++, src++) {
			if (*(Uint8*)((uintptr_t)src+3)) *dst = pixelcolor;	// Write if Alpha is ON
		}
	}
}

int scrollamount = 0;
bool isscrolling = 0;
SDL_Surface* ApplyFilter( SDL_Surface* _src )
{
	SDL_Surface* _ret = GFX_CreateRGBSurface(_src->flags, _src->w, _src->h, 32,
		_src->format->Rmask, _src->format->Gmask, _src->format->Bmask, _src->format->Amask);

	if (rand() % 4000 < 8)
	{
		isscrolling = true;
	}

	if(isscrolling == true)
	{
		scrollamount += 20;
		if(scrollamount > 240)
		{
			scrollamount = 0;
			isscrolling = false;
		}
	}

	int redOffset = rand() % 4;

	for(int x = 0; x < _src->w; x++)
	{
		for(int y = 0; y < _src->h; y++)
		{
			int sampley = (y + scrollamount )% 240;

			Uint32 pixel = ReadPixel(_src, x,sampley);

			Uint8 green = (pixel & _src->format->Gmask) >> 8;
			Uint8 blue = (pixel & _src->format->Bmask) >> 0;

			Uint32 pixelOffset = ReadPixel(_src, std::min(x+redOffset, 319), sampley) ;
			Uint8 red = (pixelOffset & _src->format->Rmask) >> 16 ;

			if(isscrolling && sampley > 220 && ((rand() %10) < 4))
			{
				red = std::min(int(red+(fRandom() * 0.6)  * 254) , 255);
				green = std::min(int(green+(fRandom() * 0.6)  * 254) , 255);
				blue = std::min(int(blue+(fRandom() * 0.6)  * 254) , 255);
			}
			else
			{
				red = std::min(int(red+(fRandom() * 0.2)  * 254) , 255);
				green = std::min(int(green+(fRandom() * 0.2)  * 254) , 255);
				blue = std::min(int(blue+(fRandom() * 0.2)  * 254) , 255);
			}


			if(y % 2 == 0)
			{
				red = static_cast<Uint8>(red / 1.2f);
				green = static_cast<Uint8>(green / 1.2f);
				blue =  static_cast<Uint8>(blue / 1.2f);
			}

			int distX =  static_cast<int>((std::abs (160.0f -x ) / 160.0f)*16);
			int distY =  static_cast<int>((std::abs (120.0f -y ) / 120.0f)*32);

			red = std::max(red - ( distX +distY), 0);
			green = std::max(green - ( distX +distY), 0);
			blue = std::max(blue - ( distX +distY), 0);

			Uint32 finalPixel = ((red<<16) + (green<<8) + (blue<<0)) | (pixel &_src->format->Amask);
			DrawPixel(_ret,x,y,  finalPixel);

		}
	}
return _ret;
}

void FillRect( SDL_Surface* _surface, const int _x, const int _y, const int _w, const int _h, const int r, int g, int b )
{
    SDL_Rect rect = {Sint16(_x),Sint16(_y),Uint16(_w),Uint16(_h)};
    Uint32 color;
    color = SDL_MapRGB(_surface->format, r, g, b);
    SDL_FillRect(_surface, &rect, color);
}

void FillRect( SDL_Surface* _surface, const int r, int g, int b )
{
    SDL_Rect rect = {0,0,Uint16(_surface->w) ,Uint16(_surface->h) };
    Uint32 color;
    color = SDL_MapRGB(_surface->format, r, g, b);
    SDL_FillRect(_surface, &rect, color);
}

void FillRect( SDL_Surface* _surface, const int color )
{
    SDL_Rect rect = {0,0,Uint16(_surface->w) ,Uint16(_surface->h) };
    SDL_FillRect(_surface, &rect, color);
}

void FillRect( SDL_Surface* _surface, const int x, const int y, const int w, const int h, int rgba )
{
    SDL_Rect rect = {Sint16(x)  ,Sint16(y) ,Uint16(w) ,Uint16(h) };
    SDL_FillRect(_surface, &rect, rgba);
}

void FillRect( SDL_Surface* _surface, SDL_Rect& _rect, const int r, int g, int b )
{
    Uint32 color;
    color = SDL_MapRGB(_surface->format, r, g, b);
    SDL_FillRect(_surface, &_rect, color);
}

void FillRect( SDL_Surface* _surface, SDL_Rect rect, int rgba )
{
    SDL_FillRect(_surface, &rect, rgba);
}

bool intersectRect( float left1, float right1, float bottom1, float top1, float left2, float right2, float bottom2, float top2 )
{
    return !( left2 > right1 || right2 < left1	|| top2 < bottom1 || bottom2 > top1);
}

void OverlaySurfaceKeyed( SDL_Surface* _src, SDL_Surface* _dest, Uint32 _key )
{	// Limited to 32bpp only for better performance
	Uint32 *src = (Uint32 *)_src->pixels;
	Uint32 *dst = (Uint32 *)_dest->pixels;
	for (Uint32 i=(_src->w*_src->h); i>0; i--,src++,dst++) {
		if (*src != _key) *dst = *src;
	}
}

void ScrollSurface( SDL_Surface* _src, int _pX, int _pY )
{	// Limited to 32bpp only for better performance
	Uint8 *pixels = (Uint8 *)_src->pixels;
	if (_pY<0) {	//	up , _pX is ignored
		Uint32 size = -(_pY) * _src->pitch;
		memcpy(pixels, pixels+size, (_src->pitch*_src->h)-size);
		return;
	}
	if (_pX<0) {	//	left , _pY is ignored
		Uint32 pitch = _src->pitch;
		Uint32 px32 = -(_pX)*4;
		Uint32 size = pitch - px32;
		for (Uint32 y=_src->h; y>0; y--, pixels+=pitch)
			memcpy(pixels, pixels+px32, size);
		return;
	}
	if (_pY>0) {	//	down , _pX is ignored
		Uint32 size = (_pY) * _src->pitch;
		memmove(pixels+size, pixels, (_src->pitch*_src->h)-size);
		return;
	}
	if (_pX>0) {	//	right , _pY is ignored , unused?
		Uint32 pitch = _src->pitch;
		Uint32 px32 = _pX*4;
		Uint32 size = pitch - px32;
		for (Uint32 y=_src->h; y>0; y--, pixels+=pitch)
			memmove(pixels+px32, pixels, size);
		return;
	}
}
