/*
 * X11 graphics driver graphics functions
 *
 * Copyright 1993,1994 Alexandre Julliard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * FIXME: only some of these functions obey the GM_ADVANCED
 * graphics mode
 */

#include "config.h"

#include <X11/Intrinsic.h>

#include "ts_xlib.h"
#include "ts_xutil.h"

#include <math.h>
#ifdef HAVE_FLOAT_H
# include <float.h>
#endif
#include <stdlib.h>
#ifndef PI
#define PI M_PI
#endif
#include <string.h>

#include "x11drv.h"
#include "x11font.h"
#include "bitmap.h"
#include "gdi.h"
#include "palette.h"
#include "region.h"
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL(graphics);

#define ABS(x)    ((x)<0?(-(x)):(x))

  /* ROP code to GC function conversion */
const int X11DRV_XROPfunction[16] =
{
    GXclear,        /* R2_BLACK */
    GXnor,          /* R2_NOTMERGEPEN */
    GXandInverted,  /* R2_MASKNOTPEN */
    GXcopyInverted, /* R2_NOTCOPYPEN */
    GXandReverse,   /* R2_MASKPENNOT */
    GXinvert,       /* R2_NOT */
    GXxor,          /* R2_XORPEN */
    GXnand,         /* R2_NOTMASKPEN */
    GXand,          /* R2_MASKPEN */
    GXequiv,        /* R2_NOTXORPEN */
    GXnoop,         /* R2_NOP */
    GXorInverted,   /* R2_MERGENOTPEN */
    GXcopy,         /* R2_COPYPEN */
    GXorReverse,    /* R2_MERGEPENNOT */
    GXor,           /* R2_MERGEPEN */
    GXset           /* R2_WHITE */
};


/***********************************************************************
 *           X11DRV_SetupGCForPatBlt
 *
 * Setup the GC for a PatBlt operation using current brush.
 * If fMapColors is TRUE, X pixels are mapped to Windows colors.
 * Return FALSE if brush is BS_NULL, TRUE otherwise.
 */
BOOL X11DRV_SetupGCForPatBlt( DC * dc, GC gc, BOOL fMapColors )
{
    XGCValues val;
    unsigned long mask;
    Pixmap pixmap = 0;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;

    if (physDev->brush.style == BS_NULL) return FALSE;
    if (physDev->brush.pixel == -1)
    {
	/* Special case used for monochrome pattern brushes.
	 * We need to swap foreground and background because
	 * Windows does it the wrong way...
	 */
	val.foreground = physDev->backgroundPixel;
	val.background = physDev->textPixel;
    }
    else
    {
	val.foreground = physDev->brush.pixel;
	val.background = physDev->backgroundPixel;
    }
    if (fMapColors && X11DRV_PALETTE_XPixelToPalette)
    {
        val.foreground = X11DRV_PALETTE_XPixelToPalette[val.foreground];
        val.background = X11DRV_PALETTE_XPixelToPalette[val.background];
    }

    val.function = X11DRV_XROPfunction[dc->ROPmode-1];
    /*
    ** Let's replace GXinvert by GXxor with (black xor white)
    ** This solves the selection color and leak problems in excel
    ** FIXME : Let's do that only if we work with X-pixels, not with Win-pixels
    */
    if (val.function == GXinvert)
    {
        val.foreground = (WhitePixel( gdi_display, DefaultScreen(gdi_display) ) ^
                          BlackPixel( gdi_display, DefaultScreen(gdi_display) ));
	val.function = GXxor;
    }
    val.fill_style = physDev->brush.fillStyle;
    switch(val.fill_style)
    {
    case FillStippled:
    case FillOpaqueStippled:
	if (dc->backgroundMode==OPAQUE) val.fill_style = FillOpaqueStippled;
	val.stipple = physDev->brush.pixmap;
	mask = GCStipple;
        break;

    case FillTiled:
        if (fMapColors && X11DRV_PALETTE_XPixelToPalette)
        {
            register int x, y;
            XImage *image;
            wine_tsx11_lock();
            pixmap = XCreatePixmap( gdi_display, root_window, 8, 8, screen_depth );
            image = XGetImage( gdi_display, physDev->brush.pixmap, 0, 0, 8, 8,
                               AllPlanes, ZPixmap );
            for (y = 0; y < 8; y++)
                for (x = 0; x < 8; x++)
                    XPutPixel( image, x, y,
                               X11DRV_PALETTE_XPixelToPalette[XGetPixel( image, x, y)] );
            XPutImage( gdi_display, pixmap, gc, image, 0, 0, 0, 0, 8, 8 );
            XDestroyImage( image );
            wine_tsx11_unlock();
            val.tile = pixmap;
        }
        else val.tile = physDev->brush.pixmap;
	mask = GCTile;
        break;

    default:
        mask = 0;
        break;
    }
    val.ts_x_origin = dc->DCOrgX + dc->brushOrgX;
    val.ts_y_origin = dc->DCOrgY + dc->brushOrgY;
    val.fill_rule = (dc->polyFillMode==WINDING) ? WindingRule : EvenOddRule;
    TSXChangeGC( gdi_display, gc,
	       GCFunction | GCForeground | GCBackground | GCFillStyle |
	       GCFillRule | GCTileStipXOrigin | GCTileStipYOrigin | mask,
	       &val );
    if (pixmap) TSXFreePixmap( gdi_display, pixmap );
    return TRUE;
}


/***********************************************************************
 *           X11DRV_SetupGCForBrush
 *
 * Setup physDev->gc for drawing operations using current brush.
 * Return FALSE if brush is BS_NULL, TRUE otherwise.
 */
BOOL X11DRV_SetupGCForBrush( DC * dc )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    return X11DRV_SetupGCForPatBlt( dc, physDev->gc, FALSE );
}


/***********************************************************************
 *           X11DRV_SetupGCForPen
 *
 * Setup physDev->gc for drawing operations using current pen.
 * Return FALSE if pen is PS_NULL, TRUE otherwise.
 */
BOOL X11DRV_SetupGCForPen( DC * dc )
{
    XGCValues val;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;

    if (physDev->pen.style == PS_NULL) return FALSE;

    switch (dc->ROPmode)
    {
    case R2_BLACK :
        val.foreground = BlackPixel( gdi_display, DefaultScreen(gdi_display) );
	val.function = GXcopy;
	break;
    case R2_WHITE :
        val.foreground = WhitePixel( gdi_display, DefaultScreen(gdi_display) );
	val.function = GXcopy;
	break;
    case R2_XORPEN :
	val.foreground = physDev->pen.pixel;
	/* It is very unlikely someone wants to XOR with 0 */
	/* This fixes the rubber-drawings in paintbrush */
	if (val.foreground == 0)
            val.foreground = (WhitePixel( gdi_display, DefaultScreen(gdi_display) ) ^
                              BlackPixel( gdi_display, DefaultScreen(gdi_display) ));
	val.function = GXxor;
	break;
    default :
	val.foreground = physDev->pen.pixel;
	val.function   = X11DRV_XROPfunction[dc->ROPmode-1];
    }
    val.background = physDev->backgroundPixel;
    val.fill_style = FillSolid;
    if ((physDev->pen.width <= 1) &&
        (physDev->pen.style != PS_SOLID) &&
        (physDev->pen.style != PS_INSIDEFRAME))
    {
        TSXSetDashes( gdi_display, physDev->gc, 0, physDev->pen.dashes, physDev->pen.dash_len );
	val.line_style = (dc->backgroundMode == OPAQUE) ?
	                      LineDoubleDash : LineOnOffDash;
    }
    else val.line_style = LineSolid;
    val.line_width = physDev->pen.width;
    if (val.line_width <= 1) {
	val.cap_style = CapNotLast;
    } else {
	switch (physDev->pen.endcap)
	{
	case PS_ENDCAP_SQUARE:
	    val.cap_style = CapProjecting;
	    break;
	case PS_ENDCAP_FLAT:
	    val.cap_style = CapButt;
	    break;
	case PS_ENDCAP_ROUND:
	default:
	    val.cap_style = CapRound;
	}
    }
    switch (physDev->pen.linejoin)
    {
    case PS_JOIN_BEVEL:
	val.join_style = JoinBevel;
        break;
    case PS_JOIN_MITER:
	val.join_style = JoinMiter;
        break;
    case PS_JOIN_ROUND:
    default:
	val.join_style = JoinRound;
    }
    TSXChangeGC( gdi_display, physDev->gc,
	       GCFunction | GCForeground | GCBackground | GCLineWidth |
	       GCLineStyle | GCCapStyle | GCJoinStyle | GCFillStyle, &val );
    return TRUE;
}


/***********************************************************************
 *           X11DRV_SetupGCForText
 *
 * Setup physDev->gc for text drawing operations.
 * Return FALSE if the font is null, TRUE otherwise.
 */
BOOL X11DRV_SetupGCForText( DC * dc )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    XFontStruct* xfs = XFONT_GetFontStruct( physDev->font );

    if( xfs )
    {
	XGCValues val;

	val.function   = GXcopy;  /* Text is always GXcopy */
	val.foreground = physDev->textPixel;
	val.background = physDev->backgroundPixel;
	val.fill_style = FillSolid;
	val.font       = xfs->fid;

	TSXChangeGC( gdi_display, physDev->gc,
		   GCFunction | GCForeground | GCBackground | GCFillStyle |
		   GCFont, &val );
	return TRUE;
    } 
    WARN("Physical font failure\n" );
    return FALSE;
}

/***********************************************************************
 *           X11DRV_LineTo
 */
BOOL
X11DRV_LineTo( DC *dc, INT x, INT y )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    POINT start;
    POINT end;

    if (X11DRV_SetupGCForPen( dc )) {
	/* Update the pixmap from the DIB section */
	X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

	start.x = dc->CursPosX;
	start.y = dc->CursPosY;
	end.x = x;
	end.y = y;
	INTERNAL_LPTODP(dc,&start);
	INTERNAL_LPTODP(dc,&end);

	TSXDrawLine(gdi_display, physDev->drawable, physDev->gc,
		  dc->DCOrgX + start.x,
		  dc->DCOrgY + start.y,
		  dc->DCOrgX + end.x,
		  dc->DCOrgY + end.y);

	/* Update the DIBSection from the pixmap */
	X11DRV_UnlockDIBSection(dc, TRUE); 
    }
    return TRUE;
}



/***********************************************************************
 *           X11DRV_DrawArc
 *
 * Helper functions for Arc(), Chord() and Pie().
 * 'lines' is the number of lines to draw: 0 for Arc, 1 for Chord, 2 for Pie.
 *
 */
static BOOL
X11DRV_DrawArc( DC *dc, INT left, INT top, INT right,
                INT bottom, INT xstart, INT ystart,
                INT xend, INT yend, INT lines )
{
    INT xcenter, ycenter, istart_angle, idiff_angle;
    INT width, oldwidth, oldendcap;
    double start_angle, end_angle;
    XPoint points[4];
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    BOOL update = FALSE;

    left   = XLPTODP( dc, left );
    top    = YLPTODP( dc, top );
    right  = XLPTODP( dc, right );
    bottom = YLPTODP( dc, bottom );
    xstart = XLPTODP( dc, xstart );
    ystart = YLPTODP( dc, ystart );
    xend   = XLPTODP( dc, xend );
    yend   = YLPTODP( dc, yend );

    if (right < left) { INT tmp = right; right = left; left = tmp; }
    if (bottom < top) { INT tmp = bottom; bottom = top; top = tmp; }
    if ((left == right) || (top == bottom)
            ||(lines && ((right-left==1)||(bottom-top==1)))) return TRUE;

    if( dc->ArcDirection == AD_CLOCKWISE )
        { INT tmp = xstart; xstart = xend; xend = tmp;
	  tmp = ystart; ystart = yend; yend = tmp; } 
	
    oldwidth = width = physDev->pen.width;
    oldendcap = physDev->pen.endcap;
    if (!width) width = 1;
    if(physDev->pen.style == PS_NULL) width = 0;

    if ((physDev->pen.style == PS_INSIDEFRAME))
    {
        if (2*width > (right-left)) width=(right-left + 1)/2;
        if (2*width > (bottom-top)) width=(bottom-top + 1)/2;
        left   += width / 2;
        right  -= (width - 1) / 2;
        top    += width / 2;
        bottom -= (width - 1) / 2;
    }
    if(width == 0) width = 1; /* more accurate */
    physDev->pen.width = width;
    physDev->pen.endcap = PS_ENDCAP_SQUARE;

    xcenter = (right + left) / 2;
    ycenter = (bottom + top) / 2;
    start_angle = atan2( (double)(ycenter-ystart)*(right-left),
			 (double)(xstart-xcenter)*(bottom-top) );
    end_angle   = atan2( (double)(ycenter-yend)*(right-left),
			 (double)(xend-xcenter)*(bottom-top) );
    if ((xstart==xend)&&(ystart==yend))
      { /* A lazy program delivers xstart=xend=ystart=yend=0) */
	start_angle = 0;
	end_angle = 2* PI;
      }
    else /* notorious cases */
      if ((start_angle == PI)&&( end_angle <0))
	start_angle = - PI;
    else
      if ((end_angle == PI)&&( start_angle <0))
	end_angle = - PI;
    istart_angle = (INT)(start_angle * 180 * 64 / PI + 0.5);
    idiff_angle  = (INT)((end_angle - start_angle) * 180 * 64 / PI + 0.5);
    if (idiff_angle <= 0) idiff_angle += 360 * 64;

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

      /* Fill arc with brush if Chord() or Pie() */

    if ((lines > 0) && X11DRV_SetupGCForBrush( dc )) {
        TSXSetArcMode( gdi_display, physDev->gc, (lines==1) ? ArcChord : ArcPieSlice);
        TSXFillArc( gdi_display, physDev->drawable, physDev->gc,
                 dc->DCOrgX + left, dc->DCOrgY + top,
                 right-left-1, bottom-top-1, istart_angle, idiff_angle );
	update = TRUE;
    }

      /* Draw arc and lines */

    if (X11DRV_SetupGCForPen( dc )){
        TSXDrawArc( gdi_display, physDev->drawable, physDev->gc,
                    dc->DCOrgX + left, dc->DCOrgY + top,
                    right-left-1, bottom-top-1, istart_angle, idiff_angle );
        if (lines) {
            /* use the truncated values */
            start_angle=(double)istart_angle*PI/64./180.;
            end_angle=(double)(istart_angle+idiff_angle)*PI/64./180.;
            /* calculate the endpoints and round correctly */
            points[0].x = (int) floor(dc->DCOrgX + (right+left)/2.0 +
                    cos(start_angle) * (right-left-width*2+2) / 2. + 0.5);
            points[0].y = (int) floor(dc->DCOrgY + (top+bottom)/2.0 -
                    sin(start_angle) * (bottom-top-width*2+2) / 2. + 0.5);
            points[1].x = (int) floor(dc->DCOrgX + (right+left)/2.0 +
                    cos(end_angle) * (right-left-width*2+2) / 2. + 0.5);
            points[1].y = (int) floor(dc->DCOrgY + (top+bottom)/2.0 -
                    sin(end_angle) * (bottom-top-width*2+2) / 2. + 0.5);
                    
            /* OK, this stuff is optimized for Xfree86 
             * which is probably the server most used by
             * wine users. Other X servers will not 
             * display correctly. (eXceed for instance)
             * so if you feel you must make changes, make sure that
             * you either use Xfree86 or separate your changes 
             * from these (compile switch or whatever)
             */
            if (lines == 2) {
                INT dx1,dy1;
                points[3] = points[1];
                points[1].x = dc->DCOrgX + xcenter;
                points[1].y = dc->DCOrgY + ycenter;
                points[2] = points[1];
                dx1=points[1].x-points[0].x;
                dy1=points[1].y-points[0].y;
                if(((top-bottom) | -2) == -2)
                    if(dy1>0) points[1].y--;
                if(dx1<0) {
                    if (((-dx1)*64)<=ABS(dy1)*37) points[0].x--;
                    if(((-dx1*9))<(dy1*16)) points[0].y--;
                    if( dy1<0 && ((dx1*9)) < (dy1*16)) points[0].y--;
                } else {
                    if(dy1 < 0)  points[0].y--;
                    if(((right-left) | -2) == -2) points[1].x--;
                }
                dx1=points[3].x-points[2].x;
                dy1=points[3].y-points[2].y;
                if(((top-bottom) | -2 ) == -2)
                    if(dy1 < 0) points[2].y--;
                if( dx1<0){ 
                    if( dy1>0) points[3].y--;
                    if(((right-left) | -2) == -2 ) points[2].x--;
                }else {
                    points[3].y--;
                    if( dx1 * 64 < dy1 * -37 ) points[3].x--;
                }
                lines++;
	    }
            TSXDrawLines( gdi_display, physDev->drawable, physDev->gc,
                          points, lines+1, CoordModeOrigin );
        }
	update = TRUE;
    }

    /* Update the DIBSection of the pixmap */
    X11DRV_UnlockDIBSection(dc, update);

    physDev->pen.width = oldwidth;
    physDev->pen.endcap = oldendcap;
    return TRUE;
}


/***********************************************************************
 *           X11DRV_Arc
 */
BOOL
X11DRV_Arc( DC *dc, INT left, INT top, INT right, INT bottom,
            INT xstart, INT ystart, INT xend, INT yend )
{
    return X11DRV_DrawArc( dc, left, top, right, bottom,
			   xstart, ystart, xend, yend, 0 );
}


/***********************************************************************
 *           X11DRV_Pie
 */
BOOL
X11DRV_Pie( DC *dc, INT left, INT top, INT right, INT bottom,
            INT xstart, INT ystart, INT xend, INT yend )
{
    return X11DRV_DrawArc( dc, left, top, right, bottom,
			   xstart, ystart, xend, yend, 2 );
}

/***********************************************************************
 *           X11DRV_Chord
 */
BOOL
X11DRV_Chord( DC *dc, INT left, INT top, INT right, INT bottom,
              INT xstart, INT ystart, INT xend, INT yend )
{
    return X11DRV_DrawArc( dc, left, top, right, bottom,
		  	   xstart, ystart, xend, yend, 1 );
}


/***********************************************************************
 *           X11DRV_Ellipse
 */
BOOL
X11DRV_Ellipse( DC *dc, INT left, INT top, INT right, INT bottom )
{
    INT width, oldwidth;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    BOOL update = FALSE;

    left   = XLPTODP( dc, left );
    top    = YLPTODP( dc, top );
    right  = XLPTODP( dc, right );
    bottom = YLPTODP( dc, bottom );
    if ((left == right) || (top == bottom)) return TRUE;

    if (right < left) { INT tmp = right; right = left; left = tmp; }
    if (bottom < top) { INT tmp = bottom; bottom = top; top = tmp; }
    
    oldwidth = width = physDev->pen.width;
    if (!width) width = 1;
    if(physDev->pen.style == PS_NULL) width = 0;

    if ((physDev->pen.style == PS_INSIDEFRAME))
    {
        if (2*width > (right-left)) width=(right-left + 1)/2;
        if (2*width > (bottom-top)) width=(bottom-top + 1)/2;
        left   += width / 2;
        right  -= (width - 1) / 2;
        top    += width / 2;
        bottom -= (width - 1) / 2;
    }
    if(width == 0) width = 1; /* more accurate */
    physDev->pen.width = width;

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

    if (X11DRV_SetupGCForBrush( dc ))
    {
        TSXFillArc( gdi_display, physDev->drawable, physDev->gc,
                    dc->DCOrgX + left, dc->DCOrgY + top,
                    right-left-1, bottom-top-1, 0, 360*64 );
	update = TRUE;
    }
    if (X11DRV_SetupGCForPen( dc ))
    {
        TSXDrawArc( gdi_display, physDev->drawable, physDev->gc,
                    dc->DCOrgX + left, dc->DCOrgY + top,
                    right-left-1, bottom-top-1, 0, 360*64 );
	update = TRUE;
    }

    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(dc, update);
    
    physDev->pen.width = oldwidth;
    return TRUE;
}


/***********************************************************************
 *           X11DRV_Rectangle
 */
BOOL
X11DRV_Rectangle(DC *dc, INT left, INT top, INT right, INT bottom)
{
    INT width, oldwidth, oldjoinstyle;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    BOOL update = FALSE;

    TRACE("(%d %d %d %d)\n", 
    	left, top, right, bottom);

    left   = INTERNAL_XWPTODP( dc, left, top );
    top    = INTERNAL_YWPTODP( dc, left, top );
    right  = INTERNAL_XWPTODP( dc, right, bottom );
    bottom = INTERNAL_YWPTODP( dc, right, bottom );

    if ((left == right) || (top == bottom)) return TRUE;

    if (right < left) { INT tmp = right; right = left; left = tmp; }
    if (bottom < top) { INT tmp = bottom; bottom = top; top = tmp; }

    oldwidth = width = physDev->pen.width;
    if (!width) width = 1;
    if(physDev->pen.style == PS_NULL) width = 0;

    if ((physDev->pen.style == PS_INSIDEFRAME))
    {
        if (2*width > (right-left)) width=(right-left + 1)/2;
        if (2*width > (bottom-top)) width=(bottom-top + 1)/2;
        left   += width / 2;
        right  -= (width - 1) / 2;
        top    += width / 2;
        bottom -= (width - 1) / 2;
    }
    if(width == 1) width = 0;
    physDev->pen.width = width;
    oldjoinstyle = physDev->pen.linejoin;
    if(physDev->pen.type != PS_GEOMETRIC)
        physDev->pen.linejoin = PS_JOIN_MITER;

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);
    
    if ((right > left + width) && (bottom > top + width))
        if (X11DRV_SetupGCForBrush( dc ))
	{
            TSXFillRectangle( gdi_display, physDev->drawable, physDev->gc,
                              dc->DCOrgX + left + (width + 1) / 2,
                              dc->DCOrgY + top + (width + 1) / 2,
                              right-left-width-1, bottom-top-width-1);
	    update = TRUE;
	}
    if (X11DRV_SetupGCForPen( dc ))
    {
        TSXDrawRectangle( gdi_display, physDev->drawable, physDev->gc,
                          dc->DCOrgX + left, dc->DCOrgY + top,
                          right-left-1, bottom-top-1 );
	update = TRUE;
    }

    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(dc, update);
   
    physDev->pen.width = oldwidth;
    physDev->pen.linejoin = oldjoinstyle;
    return TRUE;
}

/***********************************************************************
 *           X11DRV_RoundRect
 */
BOOL
X11DRV_RoundRect( DC *dc, INT left, INT top, INT right,
                  INT bottom, INT ell_width, INT ell_height )
{
    INT width, oldwidth, oldendcap;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    BOOL update = FALSE;

    TRACE("(%d %d %d %d  %d %d\n", 
    	left, top, right, bottom, ell_width, ell_height);

    left   = XLPTODP( dc, left );
    top    = YLPTODP( dc, top );
    right  = XLPTODP( dc, right );
    bottom = YLPTODP( dc, bottom );

    if ((left == right) || (top == bottom))
	return TRUE;

    /* Make sure ell_width and ell_height are >= 1 otherwise XDrawArc gets
       called with width/height < 0 */
    ell_width  = max(abs( ell_width * dc->vportExtX / dc->wndExtX ), 1);
    ell_height = max(abs( ell_height * dc->vportExtY / dc->wndExtY ), 1);

    /* Fix the coordinates */

    if (right < left) { INT tmp = right; right = left; left = tmp; }
    if (bottom < top) { INT tmp = bottom; bottom = top; top = tmp; }

    oldwidth = width = physDev->pen.width;
    oldendcap = physDev->pen.endcap;
    if (!width) width = 1;
    if(physDev->pen.style == PS_NULL) width = 0;

    if ((physDev->pen.style == PS_INSIDEFRAME))
    {
        if (2*width > (right-left)) width=(right-left + 1)/2;
        if (2*width > (bottom-top)) width=(bottom-top + 1)/2;
        left   += width / 2;
        right  -= (width - 1) / 2;
        top    += width / 2;
        bottom -= (width - 1) / 2;
    }
    if(width == 0) width = 1;
    physDev->pen.width = width;
    physDev->pen.endcap = PS_ENDCAP_SQUARE;

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

    wine_tsx11_lock();
    if (X11DRV_SetupGCForBrush( dc ))
    {
        if (ell_width > (right-left) )
            if (ell_height > (bottom-top) )
                XFillArc( gdi_display, physDev->drawable, physDev->gc,
                          dc->DCOrgX + left, dc->DCOrgY + top,
                          right - left - 1, bottom - top - 1,
                          0, 360 * 64 );
            else{
                XFillArc( gdi_display, physDev->drawable, physDev->gc,
                          dc->DCOrgX + left, dc->DCOrgY + top,
                          right - left - 1, ell_height, 0, 180 * 64 );
                XFillArc( gdi_display, physDev->drawable, physDev->gc,
                          dc->DCOrgX + left,
                          dc->DCOrgY + bottom - ell_height - 1,
                          right - left - 1, ell_height, 180 * 64,
                          180 * 64 );
            }
	else if (ell_height > (bottom-top) ){
            XFillArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + left, dc->DCOrgY + top,
                      ell_width, bottom - top - 1, 90 * 64, 180 * 64 );
            XFillArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + right - ell_width -1, dc->DCOrgY + top,
                      ell_width, bottom - top - 1, 270 * 64, 180 * 64 );
        }else{
            XFillArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + left, dc->DCOrgY + top,
                      ell_width, ell_height, 90 * 64, 90 * 64 );
            XFillArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + left,
                      dc->DCOrgY + bottom - ell_height - 1,
                      ell_width, ell_height, 180 * 64, 90 * 64 );
            XFillArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + right - ell_width - 1,
                      dc->DCOrgY + bottom - ell_height - 1,
                      ell_width, ell_height, 270 * 64, 90 * 64 );
            XFillArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + right - ell_width - 1,
                      dc->DCOrgY + top,
                      ell_width, ell_height, 0, 90 * 64 );
        }
        if (ell_width < right - left)
        {
            XFillRectangle( gdi_display, physDev->drawable, physDev->gc,
                            dc->DCOrgX + left + (ell_width + 1) / 2,
                            dc->DCOrgY + top + 1,
                            right - left - ell_width - 1,
                            (ell_height + 1) / 2 - 1);
            XFillRectangle( gdi_display, physDev->drawable, physDev->gc,
                            dc->DCOrgX + left + (ell_width + 1) / 2,
                            dc->DCOrgY + bottom - (ell_height) / 2 - 1,
                            right - left - ell_width - 1,
                            (ell_height) / 2 );
        }
        if  (ell_height < bottom - top)
        {
            XFillRectangle( gdi_display, physDev->drawable, physDev->gc,
                            dc->DCOrgX + left + 1,
                            dc->DCOrgY + top + (ell_height + 1) / 2,
                            right - left - 2,
                            bottom - top - ell_height - 1);
        }
	update = TRUE;
    }
    /* FIXME: this could be done with on X call
     * more efficient and probably more correct
     * on any X server: XDrawArcs will draw
     * straight horizontal and vertical lines
     * if width or height are zero.
     *
     * BTW this stuff is optimized for an Xfree86 server
     * read the comments inside the X11DRV_DrawArc function
     */
    if (X11DRV_SetupGCForPen(dc)) {
        if (ell_width > (right-left) )
            if (ell_height > (bottom-top) )
                XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                          dc->DCOrgX + left, dc->DCOrgY + top,
                          right - left - 1, bottom -top - 1, 0 , 360 * 64 );
            else{
                XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                          dc->DCOrgX + left, dc->DCOrgY + top,
                          right - left - 1, ell_height - 1, 0 , 180 * 64 );
                XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                          dc->DCOrgX + left,
                          dc->DCOrgY + bottom - ell_height,
                          right - left - 1, ell_height - 1, 180 * 64 , 180 * 64 );
            }
	else if (ell_height > (bottom-top) ){
            XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + left, dc->DCOrgY + top,
                      ell_width - 1 , bottom - top - 1, 90 * 64 , 180 * 64 );
            XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + right - ell_width,
                      dc->DCOrgY + top,
                      ell_width - 1 , bottom - top - 1, 270 * 64 , 180 * 64 );
	}else{
            XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + left, dc->DCOrgY + top,
                      ell_width - 1, ell_height - 1, 90 * 64, 90 * 64 );
            XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + left, dc->DCOrgY + bottom - ell_height,
                      ell_width - 1, ell_height - 1, 180 * 64, 90 * 64 );
            XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + right - ell_width,
                      dc->DCOrgY + bottom - ell_height,
                      ell_width - 1, ell_height - 1, 270 * 64, 90 * 64 );
            XDrawArc( gdi_display, physDev->drawable, physDev->gc,
                      dc->DCOrgX + right - ell_width, dc->DCOrgY + top,
                      ell_width - 1, ell_height - 1, 0, 90 * 64 );
	}
	if (ell_width < right - left)
	{
            XDrawLine( gdi_display, physDev->drawable, physDev->gc,
                       dc->DCOrgX + left + ell_width / 2,
                       dc->DCOrgY + top,
                       dc->DCOrgX + right - (ell_width+1) / 2,
                       dc->DCOrgY + top);
            XDrawLine( gdi_display, physDev->drawable, physDev->gc,
                       dc->DCOrgX + left + ell_width / 2 ,
                       dc->DCOrgY + bottom - 1,
                       dc->DCOrgX + right - (ell_width+1)/ 2,
                       dc->DCOrgY + bottom - 1);
	}
	if (ell_height < bottom - top)
	{
            XDrawLine( gdi_display, physDev->drawable, physDev->gc,
                       dc->DCOrgX + right - 1,
                       dc->DCOrgY + top + ell_height / 2,
                       dc->DCOrgX + right - 1,
                       dc->DCOrgY + bottom - (ell_height+1) / 2);
            XDrawLine( gdi_display, physDev->drawable, physDev->gc,
                       dc->DCOrgX + left,
                       dc->DCOrgY + top + ell_height / 2,
                       dc->DCOrgX + left,
                       dc->DCOrgY + bottom - (ell_height+1) / 2);
	}
	update = TRUE;
    }
    wine_tsx11_unlock();
    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(dc, update);

    physDev->pen.width = oldwidth;
    physDev->pen.endcap = oldendcap;
    return TRUE;
}


/***********************************************************************
 *           X11DRV_SetPixel
 */
COLORREF
X11DRV_SetPixel( DC *dc, INT x, INT y, COLORREF color )
{
    Pixel pixel;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    
    x = dc->DCOrgX + INTERNAL_XWPTODP( dc, x, y );
    y = dc->DCOrgY + INTERNAL_YWPTODP( dc, x, y );
    pixel = X11DRV_PALETTE_ToPhysical( dc, color );

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

    /* inefficient but simple... */
    wine_tsx11_lock();
    XSetForeground( gdi_display, physDev->gc, pixel );
    XSetFunction( gdi_display, physDev->gc, GXcopy );
    XDrawPoint( gdi_display, physDev->drawable, physDev->gc, x, y );
    wine_tsx11_unlock();

    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(dc, TRUE);

    return X11DRV_PALETTE_ToLogical(pixel);
}


/***********************************************************************
 *           X11DRV_GetPixel
 */
COLORREF
X11DRV_GetPixel( DC *dc, INT x, INT y )
{
    static Pixmap pixmap = 0;
    XImage * image;
    int pixel;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

    x = dc->DCOrgX + INTERNAL_XWPTODP( dc, x, y );
    y = dc->DCOrgY + INTERNAL_YWPTODP( dc, x, y );
    wine_tsx11_lock();
    if (dc->flags & DC_MEMORY)
    {
        image = XGetImage( gdi_display, physDev->drawable, x, y, 1, 1,
                           AllPlanes, ZPixmap );
    }
    else
    {
        /* If we are reading from the screen, use a temporary copy */
        /* to avoid a BadMatch error */
        if (!pixmap) pixmap = XCreatePixmap( gdi_display, root_window,
                                             1, 1, dc->bitsPerPixel );
        XCopyArea( gdi_display, physDev->drawable, pixmap, BITMAP_colorGC,
                   x, y, 1, 1, 0, 0 );
        image = XGetImage( gdi_display, pixmap, 0, 0, 1, 1, AllPlanes, ZPixmap );
    }
    pixel = XGetPixel( image, 0, 0 );
    XDestroyImage( image );
    wine_tsx11_unlock();

    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(dc, FALSE);

    return X11DRV_PALETTE_ToLogical(pixel);
}


/***********************************************************************
 *           X11DRV_PaintRgn
 */
BOOL
X11DRV_PaintRgn( DC *dc, HRGN hrgn )
{
    RECT box;
    HRGN tmpVisRgn, prevVisRgn;
    HDC  hdc = dc->hSelf; /* FIXME: should not mix dc/hdc this way */
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;

    if (!(tmpVisRgn = CreateRectRgn( 0, 0, 0, 0 ))) return FALSE;

      /* Transform region into device co-ords */
    if (  !REGION_LPTODP( hdc, tmpVisRgn, hrgn )
        || OffsetRgn( tmpVisRgn, dc->DCOrgX, dc->DCOrgY ) == ERROR) {
        DeleteObject( tmpVisRgn );
	return FALSE;
    }

      /* Modify visible region */
    if (!(prevVisRgn = SaveVisRgn16( hdc ))) {
        DeleteObject( tmpVisRgn );
	return FALSE;
    }
    CombineRgn( tmpVisRgn, prevVisRgn, tmpVisRgn, RGN_AND );
    SelectVisRgn16( hdc, tmpVisRgn );
    DeleteObject( tmpVisRgn );

      /* Fill the region */

    GetRgnBox( dc->hGCClipRgn, &box );
    if (X11DRV_SetupGCForBrush( dc ))
    {
	/* Update the pixmap from the DIB section */
    	X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

        TSXFillRectangle( gdi_display, physDev->drawable, physDev->gc,
		          box.left, box.top,
		          box.right-box.left, box.bottom-box.top );
    
	/* Update the DIBSection from the pixmap */
    	X11DRV_UnlockDIBSection(dc, TRUE);
    }

      /* Restore the visible region */

    RestoreVisRgn16( hdc );
    return TRUE;
}

/**********************************************************************
 *          X11DRV_Polyline
 */
BOOL
X11DRV_Polyline( DC *dc, const POINT* pt, INT count )
{
    INT oldwidth;
    register int i;
    XPoint *points;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;

    if((oldwidth = physDev->pen.width) == 0) physDev->pen.width = 1;

    if (!(points = HeapAlloc( GetProcessHeap(), 0, sizeof(XPoint) * count )))
    {
        WARN("No memory to convert POINTs to XPoints!\n");
        return FALSE;
    }
    for (i = 0; i < count; i++)
    {
	points[i].x = dc->DCOrgX + INTERNAL_XWPTODP( dc, pt[i].x, pt[i].y );
	points[i].y = dc->DCOrgY + INTERNAL_YWPTODP( dc, pt[i].x, pt[i].y );
    }

    if (X11DRV_SetupGCForPen ( dc ))
    {
	/* Update the pixmap from the DIB section */
	X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);
 
        TSXDrawLines( gdi_display, physDev->drawable, physDev->gc,
                      points, count, CoordModeOrigin );

	/* Update the DIBSection from the pixmap */
    	X11DRV_UnlockDIBSection(dc, TRUE);
    }

    HeapFree( GetProcessHeap(), 0, points );
    physDev->pen.width = oldwidth;
    return TRUE;
}


/**********************************************************************
 *          X11DRV_Polygon
 */
BOOL
X11DRV_Polygon( DC *dc, const POINT* pt, INT count )
{
    register int i;
    XPoint *points;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    BOOL update = FALSE;

    if (!(points = HeapAlloc( GetProcessHeap(), 0, sizeof(XPoint) * (count+1) )))
    {
        WARN("No memory to convert POINTs to XPoints!\n");
        return FALSE;
    }
    for (i = 0; i < count; i++)
    {
	points[i].x = dc->DCOrgX + INTERNAL_XWPTODP( dc, pt[i].x, pt[i].y );
	points[i].y = dc->DCOrgY + INTERNAL_YWPTODP( dc, pt[i].x, pt[i].y );
    }
    points[count] = points[0];

    /* Update the pixmap from the DIB section */
    X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);
 
    if (X11DRV_SetupGCForBrush( dc ))
    {
        TSXFillPolygon( gdi_display, physDev->drawable, physDev->gc,
                        points, count+1, Complex, CoordModeOrigin);
	update = TRUE;
    }
    if (X11DRV_SetupGCForPen ( dc ))
    {
        TSXDrawLines( gdi_display, physDev->drawable, physDev->gc,
                      points, count+1, CoordModeOrigin );
	update = TRUE;
    }
   
    /* Update the DIBSection from the pixmap */
    X11DRV_UnlockDIBSection(dc, update);

    HeapFree( GetProcessHeap(), 0, points );
    return TRUE;
}


/**********************************************************************
 *          X11DRV_PolyPolygon
 */
BOOL 
X11DRV_PolyPolygon( DC *dc, const POINT* pt, const INT* counts, UINT polygons)
{
    HRGN hrgn;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;

    /* FIXME: The points should be converted to device coords before */
    /* creating the region. */

    hrgn = CreatePolyPolygonRgn( pt, counts, polygons, dc->polyFillMode );
    X11DRV_PaintRgn( dc, hrgn );
    DeleteObject( hrgn );

      /* Draw the outline of the polygons */

    if (X11DRV_SetupGCForPen ( dc ))
    {
	int i, j, max = 0;
	XPoint *points;

	/* Update the pixmap from the DIB section */
	X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);
 
	for (i = 0; i < polygons; i++) if (counts[i] > max) max = counts[i];
        if (!(points = HeapAlloc( GetProcessHeap(), 0, sizeof(XPoint) * (max+1) )))
        {
            WARN("No memory to convert POINTs to XPoints!\n");
            return FALSE;
        }
	for (i = 0; i < polygons; i++)
	{
	    for (j = 0; j < counts[i]; j++)
	    {
		points[j].x = dc->DCOrgX + INTERNAL_XWPTODP( dc, pt->x, pt->y );
		points[j].y = dc->DCOrgY + INTERNAL_YWPTODP( dc, pt->x, pt->y );
		pt++;
	    }
	    points[j] = points[0];
            TSXDrawLines( gdi_display, physDev->drawable, physDev->gc,
                          points, j + 1, CoordModeOrigin );
	}
	
	/* Update the DIBSection of the dc's bitmap */
	X11DRV_UnlockDIBSection(dc, TRUE);

	HeapFree( GetProcessHeap(), 0, points );
    }
    return TRUE;
}


/**********************************************************************
 *          X11DRV_PolyPolyline
 */
BOOL 
X11DRV_PolyPolyline( DC *dc, const POINT* pt, const DWORD* counts, DWORD polylines )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    
    if (X11DRV_SetupGCForPen ( dc ))
    {
        int i, j, max = 0;
        XPoint *points;

	/* Update the pixmap from the DIB section */
    	X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);
 
        for (i = 0; i < polylines; i++) if (counts[i] > max) max = counts[i];
        if (!(points = HeapAlloc( GetProcessHeap(), 0, sizeof(XPoint) * max )))
        {
            WARN("No memory to convert POINTs to XPoints!\n");
            return FALSE;
        }
        for (i = 0; i < polylines; i++)
        {
            for (j = 0; j < counts[i]; j++)
            {
		points[j].x = dc->DCOrgX + INTERNAL_XWPTODP( dc, pt->x, pt->y );
		points[j].y = dc->DCOrgY + INTERNAL_YWPTODP( dc, pt->x, pt->y );
                pt++;
            }
            TSXDrawLines( gdi_display, physDev->drawable, physDev->gc,
			  points, j, CoordModeOrigin );
        }
	
	/* Update the DIBSection of the dc's bitmap */
    	X11DRV_UnlockDIBSection(dc, TRUE);

	HeapFree( GetProcessHeap(), 0, points );
    }
    return TRUE;
}


/**********************************************************************
 *          X11DRV_InternalFloodFill
 *
 * Internal helper function for flood fill.
 * (xorg,yorg) is the origin of the X image relative to the drawable.
 * (x,y) is relative to the origin of the X image.
 */
static void X11DRV_InternalFloodFill(XImage *image, DC *dc,
                                     int x, int y,
                                     int xOrg, int yOrg,
                                     Pixel pixel, WORD fillType )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    int left, right;

#define TO_FLOOD(x,y)  ((fillType == FLOODFILLBORDER) ? \
                        (XGetPixel(image,x,y) != pixel) : \
                        (XGetPixel(image,x,y) == pixel))

    if (!TO_FLOOD(x,y)) return;

      /* Find left and right boundaries */

    left = right = x;
    while ((left > 0) && TO_FLOOD( left-1, y )) left--;
    while ((right < image->width) && TO_FLOOD( right, y )) right++;
    XFillRectangle( gdi_display, physDev->drawable, physDev->gc,
                    xOrg + left, yOrg + y, right-left, 1 );

      /* Set the pixels of this line so we don't fill it again */

    for (x = left; x < right; x++)
    {
        if (fillType == FLOODFILLBORDER) XPutPixel( image, x, y, pixel );
        else XPutPixel( image, x, y, ~pixel );
    }

      /* Fill the line above */

    if (--y >= 0)
    {
        x = left;
        while (x < right)
        {
            while ((x < right) && !TO_FLOOD(x,y)) x++;
            if (x >= right) break;
            while ((x < right) && TO_FLOOD(x,y)) x++;
            X11DRV_InternalFloodFill(image, dc, x-1, y,
                                     xOrg, yOrg, pixel, fillType );
        }
    }

      /* Fill the line below */

    if ((y += 2) < image->height)
    {
        x = left;
        while (x < right)
        {
            while ((x < right) && !TO_FLOOD(x,y)) x++;
            if (x >= right) break;
            while ((x < right) && TO_FLOOD(x,y)) x++;
            X11DRV_InternalFloodFill(image, dc, x-1, y,
                                     xOrg, yOrg, pixel, fillType );
        }
    }
#undef TO_FLOOD    
}


/**********************************************************************
 *          X11DRV_ExtFloodFill
 */
BOOL
X11DRV_ExtFloodFill( DC *dc, INT x, INT y, COLORREF color,
                     UINT fillType )
{
    XImage *image;
    RECT rect;
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;

    TRACE("X11DRV_ExtFloodFill %d,%d %06lx %d\n", x, y, color, fillType );

    if (!PtVisible( dc->hSelf, x, y )) return FALSE;
    if (GetRgnBox( dc->hGCClipRgn, &rect ) == ERROR) return FALSE;

    if (!(image = TSXGetImage( gdi_display, physDev->drawable,
                               rect.left,
                               rect.top,
                               rect.right - rect.left,
                               rect.bottom - rect.top,
                               AllPlanes, ZPixmap ))) return FALSE;

    if (X11DRV_SetupGCForBrush( dc ))
    {
	/* Update the pixmap from the DIB section */
	X11DRV_LockDIBSection(dc, DIB_Status_GdiMod, FALSE);

          /* ROP mode is always GXcopy for flood-fill */
        wine_tsx11_lock();
        XSetFunction( gdi_display, physDev->gc, GXcopy );
        X11DRV_InternalFloodFill(image, dc,
                                 XLPTODP(dc,x) + dc->DCOrgX - rect.left,
                                 YLPTODP(dc,y) + dc->DCOrgY - rect.top,
                                 rect.left, rect.top,
                                 X11DRV_PALETTE_ToPhysical( dc, color ),
                                 fillType );
        wine_tsx11_unlock();
        /* Update the DIBSection of the dc's bitmap */
        X11DRV_UnlockDIBSection(dc, TRUE);
    }

    TSXDestroyImage( image );
    return TRUE;
}

/**********************************************************************
 *          X11DRV_SetBkColor
 */
COLORREF
X11DRV_SetBkColor( DC *dc, COLORREF color )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    COLORREF oldColor;

    oldColor = dc->backgroundColor;
    dc->backgroundColor = color;

    physDev->backgroundPixel = X11DRV_PALETTE_ToPhysical( dc, color );

    return oldColor;
}

/**********************************************************************
 *          X11DRV_SetTextColor
 */
COLORREF
X11DRV_SetTextColor( DC *dc, COLORREF color )
{
    X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *)dc->physDev;
    COLORREF oldColor;

    oldColor = dc->textColor;
    dc->textColor = color;

    physDev->textPixel = X11DRV_PALETTE_ToPhysical( dc, color );

    return oldColor;
}

/***********************************************************************
 *           X11DRV_GetDCOrgEx
 */
BOOL X11DRV_GetDCOrgEx( DC *dc, LPPOINT lpp )
{
    if (!(dc->flags & DC_MEMORY))
    {
       X11DRV_PDEVICE *physDev = (X11DRV_PDEVICE *) dc->physDev;
       Window root;
       int x, y, w, h, border, depth;

       /* FIXME: this is not correct for managed windows */
       TSXGetGeometry( gdi_display, physDev->drawable, &root,
                       &x, &y, &w, &h, &border, &depth );
       lpp->x = x;
       lpp->y = y;
    }
    else lpp->x = lpp->y = 0;
    return TRUE;
}

