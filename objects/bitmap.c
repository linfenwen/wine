/*
 * GDI bitmap objects
 *
 * Copyright 1993 Alexandre Julliard
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include "gdi.h"
#include "callback.h"
#include "dc.h"
#include "bitmap.h"
#include "heap.h"
#include "stddebug.h"
#include "debug.h"

#ifdef PRELIMINARY_WING16_SUPPORT
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

  /* GCs used for B&W and color bitmap operations */
GC BITMAP_monoGC = 0, BITMAP_colorGC = 0;

extern void CLIPPING_UpdateGCRegion( DC * dc );  /* objects/clipping.c */


/***********************************************************************
 *           CreateBitmap16    (GDI.48)
 */
HBITMAP16 CreateBitmap16( INT16 width, INT16 height, UINT16 planes,
                          UINT16 bpp, LPCVOID bits )
{
    return CreateBitmap32( width, height, planes, bpp, bits );
}


/***********************************************************************
 *           CreateBitmap32    (GDI32.25)
 */
HBITMAP32 CreateBitmap32( INT32 width, INT32 height, UINT32 planes,
                          UINT32 bpp, LPCVOID bits )
{
    BITMAPOBJ * bmpObjPtr;
    HBITMAP32 hbitmap;

    planes = (BYTE)planes;
    bpp    = (BYTE)bpp;

    dprintf_gdi( stddeb, "CreateBitmap: %dx%d, %d colors\n", 
                 width, height, 1 << (planes*bpp) );

      /* Check parameters */
    if (!height || !width || planes != 1) return 0;
    if ((bpp != 1) && (bpp != screenDepth)) return 0;
    if (height < 0) height = -height;
    if (width < 0) width = -width;

      /* Create the BITMAPOBJ */
    hbitmap = GDI_AllocObject( sizeof(BITMAPOBJ), BITMAP_MAGIC );
    if (!hbitmap) return 0;
    bmpObjPtr = (BITMAPOBJ *) GDI_HEAP_LIN_ADDR( hbitmap );

    bmpObjPtr->size.cx = 0;
    bmpObjPtr->size.cy = 0;
    bmpObjPtr->bitmap.bmType = 0;
    bmpObjPtr->bitmap.bmWidth = (INT16)width;
    bmpObjPtr->bitmap.bmHeight = (INT16)height;
    bmpObjPtr->bitmap.bmPlanes = (BYTE)planes;
    bmpObjPtr->bitmap.bmBitsPixel = (BYTE)bpp;
    bmpObjPtr->bitmap.bmWidthBytes = (INT16)BITMAP_WIDTH_BYTES( width, bpp );
    bmpObjPtr->bitmap.bmBits = NULL;

      /* Create the pixmap */
    bmpObjPtr->pixmap = XCreatePixmap(display, rootWindow, width, height, bpp);
    if (!bmpObjPtr->pixmap)
    {
	GDI_HEAP_FREE( hbitmap );
	hbitmap = 0;
    }
    else if (bits)  /* Set bitmap bits */
	SetBitmapBits32( hbitmap, height * bmpObjPtr->bitmap.bmWidthBytes,
                         bits );
    return hbitmap;
}


/***********************************************************************
 *           CreateCompatibleBitmap16    (GDI.51)
 */
HBITMAP16 CreateCompatibleBitmap16( HDC16 hdc, INT16 width, INT16 height )
{
    return CreateCompatibleBitmap32( hdc, width, height );
}


/***********************************************************************
 *           CreateCompatibleBitmap32    (GDI32.30)
 */
HBITMAP32 CreateCompatibleBitmap32( HDC32 hdc, INT32 width, INT32 height )
{
    HBITMAP32 hbmpRet = 0;
    DC *dc;

    dprintf_gdi( stddeb, "CreateCompatibleBitmap(%04x,%d,%d) = \n", 
                 hdc, width, height );
    if (!(dc = DC_GetDCPtr( hdc ))) return 0;
    hbmpRet = CreateBitmap32( width, height, 1, dc->w.bitsPerPixel, NULL );
    dprintf_gdi(stddeb,"\t\t%04x\n", hbmpRet);
    return hbmpRet;
}


/***********************************************************************
 *           CreateBitmapIndirect16    (GDI.49)
 */
HBITMAP16 CreateBitmapIndirect16( const BITMAP16 * bmp )
{
    return CreateBitmap16( bmp->bmWidth, bmp->bmHeight, bmp->bmPlanes,
                           bmp->bmBitsPixel, PTR_SEG_TO_LIN( bmp->bmBits ) );
}


/***********************************************************************
 *           CreateBitmapIndirect32    (GDI32.26)
 */
HBITMAP32 CreateBitmapIndirect32( const BITMAP32 * bmp )
{
    return CreateBitmap32( bmp->bmWidth, bmp->bmHeight, bmp->bmPlanes,
                           bmp->bmBitsPixel, bmp->bmBits );
}


/***********************************************************************
 *           GetBitmapBits16    (GDI.74)
 */
LONG GetBitmapBits16( HBITMAP16 hbitmap, LONG count, LPVOID buffer )
{
    return GetBitmapBits32( hbitmap, count, buffer );
}


/***********************************************************************
 *           GetBitmapBits32    (GDI32.143)
 */
LONG GetBitmapBits32( HBITMAP32 hbitmap, LONG count, LPVOID buffer )
{
    BITMAPOBJ * bmp;
    LONG height,widthbytes;
    XImage * image;
    LPBYTE tmpbuffer,tbuf;
    int	h,w,pad;
    
    /* KLUDGE! */
    if (count < 0) {
	fprintf(stderr, "Negative number of bytes (%ld) passed to GetBitmapBits???\n", count );
	count = -count;
    }
    bmp = (BITMAPOBJ *) GDI_GetObjPtr( hbitmap, BITMAP_MAGIC );
    if (!bmp) return 0;

      /* Only get entire lines */
    height = count / bmp->bitmap.bmWidthBytes;
    if (height > bmp->bitmap.bmHeight) height = bmp->bitmap.bmHeight;
    dprintf_bitmap(stddeb, "GetBitmapBits: %dx%d %d colors %p fetched height: %ld\n",
	    bmp->bitmap.bmWidth, bmp->bitmap.bmHeight,
	    1 << bmp->bitmap.bmBitsPixel, buffer, height );
    if (!height) return 0;

    switch (bmp->bitmap.bmBitsPixel) {
    case 1:
	if (!(bmp->bitmap.bmWidth & 15))
		pad = 0;
	else
		pad = ((16 - (bmp->bitmap.bmWidth & 15)) + 7) / 8;
    	break;
    case 4:
	if (!(bmp->bitmap.bmWidth & 3))
	    pad = 0;
	else
	    pad = ((4 - (bmp->bitmap.bmWidth & 3)) + 1) / 2;
	break;
    case 8:
    	pad = (2 - (bmp->bitmap.bmWidth & 1)) & 1;
    	break;
    case 15:
    case 16:
    	pad = 0; /* we have 16bit alignment already */
	break;
    case 24:
    	pad = (bmp->bitmap.bmWidth*3) & 1;
    	break;
    default:
	fprintf(stderr,"GetBitMapBits32: unknown depth %d, please report.\n",
		bmp->bitmap.bmBitsPixel
	);
	return 0;
    }

    widthbytes	= DIB_GetXImageWidthBytes(bmp->bitmap.bmWidth,bmp->bitmap.bmBitsPixel);
    tmpbuffer	= (LPBYTE)xmalloc(widthbytes*height);
    image = XCreateImage( display, DefaultVisualOfScreen(screen),
		  bmp->bitmap.bmBitsPixel, ZPixmap, 0, tmpbuffer,
		  bmp->bitmap.bmWidth,height,32,widthbytes
    );
    
    CallTo32_LargeStack( (int(*)())XGetSubImage, 11,
                         display, bmp->pixmap, 0, 0, bmp->bitmap.bmWidth,
                         height, AllPlanes, ZPixmap, image, 0, 0 );

    /* copy XImage to 16 bit padded image buffer with real bitsperpixel */

    tbuf = buffer;
    switch (bmp->bitmap.bmBitsPixel)
    {
    case 1:
        for (h=0;h<height;h++)
        {
            *tbuf = 0;
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
                if ((w%8) == 0)
                    *tbuf = 0;
                *tbuf |= XGetPixel(image,w,h)<<(7-(w&7));
                if ((w&7) == 7) ++tbuf;
            }
            tbuf += pad;
        }
        break;
    case 4:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
                if (!(w & 1)) *tbuf = XGetPixel( image, w, h) << 4;
	    	else *tbuf++ |= XGetPixel( image, w, h) & 0x0f;
            }
            tbuf += pad;
        }
        break;
    case 8:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
                *tbuf++ = XGetPixel(image,w,h);
            tbuf += pad;
        }
        break;
    case 15:
    case 16:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
	    	long pixel = XGetPixel(image,w,h);

		*tbuf++ = pixel & 0xff;
		*tbuf++ = (pixel>>8) & 0xff;
            }
        }
        break;
    case 24:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
	    	long pixel = XGetPixel(image,w,h);

		*tbuf++ = pixel & 0xff;
		*tbuf++ = (pixel>> 8) & 0xff;
		*tbuf++ = (pixel>>16) & 0xff;
	    }
            tbuf += pad;
	}
    }
    XDestroyImage( image ); /* frees tbuffer too */
    return height * bmp->bitmap.bmWidthBytes;
}


/***********************************************************************
 *           SetBitmapBits16    (GDI.106)
 */
LONG SetBitmapBits16( HBITMAP16 hbitmap, LONG count, LPCVOID buffer )
{
    return SetBitmapBits32( hbitmap, count, buffer );
}


/***********************************************************************
 *           SetBitmapBits32    (GDI32.303)
 */
LONG SetBitmapBits32( HBITMAP32 hbitmap, LONG count, LPCVOID buffer )
{
    BITMAPOBJ * bmp;
    LONG height;
    XImage * image;
    LPBYTE sbuf,tmpbuffer;
    int	w,h,pad,widthbytes;
    
    /* KLUDGE! */
    if (count < 0) {
	fprintf(stderr, "Negative number of bytes (%ld) passed to SetBitmapBits???\n", count );
	count = -count;
    }
    bmp = (BITMAPOBJ *) GDI_GetObjPtr( hbitmap, BITMAP_MAGIC );
    if (!bmp) return 0;

    dprintf_bitmap(stddeb, "SetBitmapBits: %dx%d %d colors %p\n",
	    bmp->bitmap.bmWidth, bmp->bitmap.bmHeight,
	    1 << bmp->bitmap.bmBitsPixel, buffer );

      /* Only set entire lines */
    height = count / bmp->bitmap.bmWidthBytes;
    if (height > bmp->bitmap.bmHeight) height = bmp->bitmap.bmHeight;
    if (!height) return 0;
    	
    switch (bmp->bitmap.bmBitsPixel) {
    case 1:
	if (!(bmp->bitmap.bmWidth & 15))
		pad = 0;
	else
		pad = ((16 - (bmp->bitmap.bmWidth & 15)) + 7) / 8;
    	break;
    case 4:
	if (!(bmp->bitmap.bmWidth & 3))
	    pad = 0;
	else
	    pad = ((4 - (bmp->bitmap.bmWidth & 3)) + 1) / 2;
	break;
    case 8:
    	pad = (2 - (bmp->bitmap.bmWidth & 1)) & 1;
    	break;
    case 15:
    case 16:
    	pad = 0; /* we have 16bit alignment already */
	break;
    case 24:
    	pad = (bmp->bitmap.bmWidth*3) & 1;
    	break;
    default:
	fprintf(stderr,"SetBitMapBits32: unknown depth %d, please report.\n",
		bmp->bitmap.bmBitsPixel
	);
	return 0;
    }
    sbuf = (LPBYTE)buffer;

    widthbytes	= DIB_GetXImageWidthBytes(bmp->bitmap.bmWidth,bmp->bitmap.bmBitsPixel);
    tmpbuffer	= (LPBYTE)xmalloc(widthbytes*height);
    image = XCreateImage( display, DefaultVisualOfScreen(screen),
		  bmp->bitmap.bmBitsPixel, ZPixmap, 0, tmpbuffer,
		  bmp->bitmap.bmWidth,height,32,widthbytes
    );
    
    /* copy 16 bit padded image buffer with real bitsperpixel to XImage */
    sbuf = (LPBYTE)buffer;
    switch (bmp->bitmap.bmBitsPixel)
    {
    case 1:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
                XPutPixel(image,w,h,(sbuf[0]>>(7-(w&7))) & 1);
                if ((w&7) == 7)
                    sbuf++;
            }
            sbuf += pad;
        }
        break;
    case 4:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
                if (!(w & 1)) XPutPixel( image, w, h, *sbuf >> 4 );
                else XPutPixel( image, w, h, *sbuf++ & 0xf );
            }
            sbuf += pad;
        }
        break;
    case 8:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
                XPutPixel(image,w,h,*sbuf++);
            sbuf += pad;
        }
        break;
    case 15:
    case 16:
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
                XPutPixel(image,w,h,sbuf[1]*256+sbuf[0]);
                sbuf+=2;
            }
        }
        break;
    case 24: 
        for (h=0;h<height;h++)
        {
            for (w=0;w<bmp->bitmap.bmWidth;w++)
            {
                XPutPixel(image,w,h,(sbuf[2]<<16)+(sbuf[1]<<8)+sbuf[0]);
                sbuf += 3;
            }
            sbuf += pad;
        }
        break;
    }

    CallTo32_LargeStack( XPutImage, 10,
                         display, bmp->pixmap, BITMAP_GC(bmp), image, 0, 0,
                         0, 0, bmp->bitmap.bmWidth, height );
    XDestroyImage( image ); /* frees tmpbuffer too */
    return height * bmp->bitmap.bmWidthBytes;
}

/**********************************************************************
 *	    LoadImageA    (USER32.364)
 * FIXME: implementation still lacks nearly all features, see LR_*
 * defines in windows.h
 */

HANDLE32 LoadImage32A(
	HINSTANCE32 hinst,LPCSTR name,UINT32 type,INT32 desiredx,
	INT32 desiredy,UINT32 loadflags
) {
	if (HIWORD(name)) {
		dprintf_resource(stddeb,"LoadImage32A(0x%04x,%s,%d,%d,%d,0x%08x)\n",
			hinst,name,type,desiredx,desiredy,loadflags
		);
	} else {
		dprintf_resource(stddeb,"LoadImage32A(0x%04x,%p,%d,%d,%d,0x%08x)\n",
			hinst,name,type,desiredx,desiredy,loadflags
		);
	}
	switch (type) {
	case IMAGE_BITMAP:
		return LoadBitmap32A(hinst,name);
	case IMAGE_ICON:
		return LoadIcon32A(hinst,name);
	case IMAGE_CURSOR:
		return LoadCursor32A(hinst,name);
	}
	return 0;
}

/**********************************************************************
 *	    CopyImage32    (USER32.60)
 *
 * FIXME: implementation still lacks nearly all features, see LR_*
 * defines in windows.h
 */
HANDLE32 CopyImage32( HANDLE32 hnd, UINT32 type, INT32 desiredx,
                      INT32 desiredy, UINT32 flags )
{
    switch (type)
    {
	case IMAGE_BITMAP:
		return hnd;	/* FIXME ... need to copy here */
	case IMAGE_ICON:
		return CopyIcon32(hnd);
	case IMAGE_CURSOR:
		return CopyCursor32(hnd);
    }
    return 0;
}


/**********************************************************************
 *	    LoadBitmap16    (USER.175)
 */
HBITMAP16 LoadBitmap16( HINSTANCE16 instance, SEGPTR name )
{
    HBITMAP32 hbitmap = 0;
    HDC32 hdc;
    HRSRC16 hRsrc;
    HGLOBAL16 handle;
    BITMAPINFO *info;

    if (HIWORD(name))
    {
        char *str = (char *)PTR_SEG_TO_LIN( name );
        dprintf_bitmap( stddeb, "LoadBitmap16(%04x,'%s')\n", instance, str );
        if (str[0] == '#') name = (SEGPTR)(DWORD)(WORD)atoi( str + 1 );
    }
    else
        dprintf_bitmap( stddeb, "LoadBitmap16(%04x,%04x)\n",
                        instance, LOWORD(name) );

    if (!instance)  /* OEM bitmap */
    {
        if (HIWORD((int)name)) return 0;
        return OBM_LoadBitmap( LOWORD((int)name) );
    }

    if (!(hRsrc = FindResource16( instance, name, RT_BITMAP ))) return 0;
    if (!(handle = LoadResource16( instance, hRsrc ))) return 0;

    info = (BITMAPINFO *)LockResource16( handle );
    if ((hdc = GetDC32(0)) != 0)
    {
        char *bits = (char *)info + DIB_BitmapInfoSize( info, DIB_RGB_COLORS );
        hbitmap = CreateDIBitmap32( hdc, &info->bmiHeader, CBM_INIT,
                                    bits, info, DIB_RGB_COLORS );
        ReleaseDC32( 0, hdc );
    }
    FreeResource16( handle );
    return hbitmap;
}

/**********************************************************************
 *	    LoadBitmap32W   (USER32.357)
 */
HBITMAP32 LoadBitmap32W( HINSTANCE32 instance, LPCWSTR name )
{
    HBITMAP32 hbitmap = 0;
    HDC32 hdc;
    HRSRC32 hRsrc;
    HGLOBAL32 handle;
    BITMAPINFO *info;

    if (!instance)  /* OEM bitmap */
    {
        if (HIWORD((int)name)) return 0;
        return OBM_LoadBitmap( LOWORD((int)name) );
    }

    if (!(hRsrc = FindResource32W( instance, name,
		(LPWSTR)RT_BITMAP ))) return 0;
    if (!(handle = LoadResource32( instance, hRsrc ))) return 0;

    info = (BITMAPINFO *)LockResource32( handle );
    if ((hdc = GetDC32(0)) != 0)
    {
        char *bits = (char *)info + DIB_BitmapInfoSize( info, DIB_RGB_COLORS );
        hbitmap = CreateDIBitmap32( hdc, &info->bmiHeader, CBM_INIT,
                                    bits, info, DIB_RGB_COLORS );
        ReleaseDC32( 0, hdc );
    }
    return hbitmap;
}


/**********************************************************************
 *	    LoadBitmap32A   (USER32.356)
 */
HBITMAP32 LoadBitmap32A( HINSTANCE32 instance, LPCSTR name )
{
    HBITMAP32 res;
    if (!HIWORD(name)) res = LoadBitmap32W( instance, (LPWSTR)name );
    else
    {
        LPWSTR uni = HEAP_strdupAtoW( GetProcessHeap(), 0, name );
        res = LoadBitmap32W( instance, uni );
        HeapFree( GetProcessHeap(), 0, uni );
    }
    return res;
}


/***********************************************************************
 *           BITMAP_DeleteObject
 */
BOOL32 BITMAP_DeleteObject( HBITMAP16 hbitmap, BITMAPOBJ * bmp )
{
#ifdef PRELIMINARY_WING16_SUPPORT
    if( bmp->bitmap.bmBits )
 	XShmDetach( display, (XShmSegmentInfo*)bmp->bitmap.bmBits );
#endif

    XFreePixmap( display, bmp->pixmap );
#ifdef PRELIMINARY_WING16_SUPPORT
    if( bmp->bitmap.bmBits )
    {
    __ShmBitmapCtl* p = (__ShmBitmapCtl*)bmp->bitmap.bmBits;
      WORD          sel = HIWORD(p->bits);
      unsigned long l, limit = GetSelectorLimit(sel);

      for( l = 0; l < limit; l += 0x10000, sel += __AHINCR )
	   FreeSelector(sel);
      shmctl(p->si.shmid, IPC_RMID, NULL); 
      shmdt(p->si.shmaddr);  /* already marked for destruction */
    }
#endif
    return GDI_FreeObject( hbitmap );
}

	
/***********************************************************************
 *           BITMAP_GetObject16
 */
INT16 BITMAP_GetObject16( BITMAPOBJ * bmp, INT16 count, LPVOID buffer )
{
    if (count > sizeof(bmp->bitmap)) count = sizeof(bmp->bitmap);
    memcpy( buffer, &bmp->bitmap, count );
    return count;
}
    

/***********************************************************************
 *           BITMAP_GetObject32
 */
INT32 BITMAP_GetObject32( BITMAPOBJ * bmp, INT32 count, LPVOID buffer )
{
    BITMAP32 bmp32;
    bmp32.bmType       = bmp->bitmap.bmType;
    bmp32.bmWidth      = bmp->bitmap.bmWidth;
    bmp32.bmHeight     = bmp->bitmap.bmHeight;
    bmp32.bmWidthBytes = bmp->bitmap.bmWidthBytes;
    bmp32.bmPlanes     = bmp->bitmap.bmPlanes;
    bmp32.bmBitsPixel  = bmp->bitmap.bmBitsPixel;
    bmp32.bmBits       = NULL;
    if (count > sizeof(bmp32)) count = sizeof(bmp32);
    memcpy( buffer, &bmp32, count );
    return count;
}
    


/***********************************************************************
 *           CreateDiscardableBitmap16    (GDI.156)
 */
HBITMAP16 CreateDiscardableBitmap16( HDC16 hdc, INT16 width, INT16 height )
{
    return CreateCompatibleBitmap16( hdc, width, height );
}


/***********************************************************************
 *           CreateDiscardableBitmap32    (GDI32.38)
 */
HBITMAP32 CreateDiscardableBitmap32( HDC32 hdc, INT32 width, INT32 height )
{
    return CreateCompatibleBitmap32( hdc, width, height );
}


/***********************************************************************
 *           GetBitmapDimensionEx16    (GDI.468)
 */
BOOL16 GetBitmapDimensionEx16( HBITMAP16 hbitmap, LPSIZE16 size )
{
    BITMAPOBJ * bmp = (BITMAPOBJ *) GDI_GetObjPtr( hbitmap, BITMAP_MAGIC );
    if (!bmp) return FALSE;
    *size = bmp->size;
    return TRUE;
}


/***********************************************************************
 *           GetBitmapDimensionEx32    (GDI32.144)
 */
BOOL32 GetBitmapDimensionEx32( HBITMAP32 hbitmap, LPSIZE32 size )
{
    BITMAPOBJ * bmp = (BITMAPOBJ *) GDI_GetObjPtr( hbitmap, BITMAP_MAGIC );
    if (!bmp) return FALSE;
    size->cx = (INT32)bmp->size.cx;
    size->cy = (INT32)bmp->size.cy;
    return TRUE;
}


/***********************************************************************
 *           GetBitmapDimension    (GDI.162)
 */
DWORD GetBitmapDimension( HBITMAP16 hbitmap )
{
    SIZE16 size;
    if (!GetBitmapDimensionEx16( hbitmap, &size )) return 0;
    return MAKELONG( size.cx, size.cy );
}


/***********************************************************************
 *           SetBitmapDimensionEx16    (GDI.478)
 */
BOOL16 SetBitmapDimensionEx16( HBITMAP16 hbitmap, INT16 x, INT16 y,
                               LPSIZE16 prevSize )
{
    BITMAPOBJ * bmp = (BITMAPOBJ *) GDI_GetObjPtr( hbitmap, BITMAP_MAGIC );
    if (!bmp) return FALSE;
    if (prevSize) *prevSize = bmp->size;
    bmp->size.cx = x;
    bmp->size.cy = y;
    return TRUE;
}


/***********************************************************************
 *           SetBitmapDimensionEx32    (GDI32.304)
 */
BOOL32 SetBitmapDimensionEx32( HBITMAP32 hbitmap, INT32 x, INT32 y,
                               LPSIZE32 prevSize )
{
    BITMAPOBJ * bmp = (BITMAPOBJ *) GDI_GetObjPtr( hbitmap, BITMAP_MAGIC );
    if (!bmp) return FALSE;
    if (prevSize) CONV_SIZE16TO32( &bmp->size, prevSize );
    bmp->size.cx = (INT16)x;
    bmp->size.cy = (INT16)y;
    return TRUE;
}


/***********************************************************************
 *           SetBitmapDimension    (GDI.163)
 */
DWORD SetBitmapDimension( HBITMAP16 hbitmap, INT16 x, INT16 y )
{
    SIZE16 size;
    if (!SetBitmapDimensionEx16( hbitmap, x, y, &size )) return 0;
    return MAKELONG( size.cx, size.cy );
}
