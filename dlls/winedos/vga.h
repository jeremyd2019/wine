/*
 * VGA emulation
 * 
 * Copyright 1998 Ove K�ven
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

#ifndef __WINE_VGA_H
#define __WINE_VGA_H

#include "winbase.h"
#include "wingdi.h"

/* graphics mode */
int VGA_SetMode(unsigned Xres,unsigned Yres,unsigned Depth);
int VGA_GetMode(unsigned*Height,unsigned*Width,unsigned*Depth);
void VGA_Exit(void);
void VGA_SetPalette(PALETTEENTRY*pal,int start,int len);
void VGA_SetQuadPalette(RGBQUAD*color,int start,int len);
LPSTR VGA_Lock(unsigned*Pitch,unsigned*Height,unsigned*Width,unsigned*Depth);
void VGA_Unlock(void);

/* text mode */
int VGA_SetAlphaMode(unsigned Xres,unsigned Yres);
void VGA_GetAlphaMode(unsigned*Xres,unsigned*Yres);
void VGA_SetCursorPos(unsigned X,unsigned Y);
void VGA_GetCursorPos(unsigned*X,unsigned*Y);
void VGA_WriteChars(unsigned X,unsigned Y,unsigned ch,int attr,int count);

/* control */
void CALLBACK VGA_Poll(ULONG_PTR arg);
void VGA_ioport_out(WORD port, BYTE val);
BYTE VGA_ioport_in(WORD port);
void VGA_Clean(void);

#endif /* __WINE_VGA_H */
