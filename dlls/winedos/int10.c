/*
 * BIOS interrupt 10h handler
 *
 * Copyright 1998 Ove K�ven
 * Copyright 1998 Joseph Pranevich
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

#include "config.h"

#include <stdlib.h>

#include "miscemu.h"
#include "vga.h"
#include "wine/debug.h"
#include "console.h"

WINE_DEFAULT_DEBUG_CHANNEL(int);

static void conv_text_mode_attributes(char attribute, int *fg, int *bg,
       int *wattribute);
static void scroll_window(int direction, char lines, char row1, 
   char col1, char row2, char col2, char attribute);

static int color_palette[16];

#define SCROLL_UP 1
#define SCROLL_DOWN 2

/* FIXME: is row or column first? */
static void BIOS_GetCursorPos(BIOSDATA*data,unsigned page,unsigned*X,unsigned*Y)
{
  *X = data->VideoCursorPos[page*2];
  *Y = data->VideoCursorPos[page*2+1];
}

static void BIOS_SetCursorPos(BIOSDATA*data,unsigned page,unsigned X,unsigned Y)
{
  data->VideoCursorPos[page*2] = X;
  data->VideoCursorPos[page*2+1] = Y;
}

/**********************************************************************
 *	    DOSVM_Int10Handler (WPROCS.116)
 *
 * Handler for int 10h (video).
 * 
 * NOTE:
 *    Most INT 10 functions for text-mode, CGA, EGA, and VGA cards
 *    are present in this list. (SVGA and XGA are not) That is not
 *    to say that all these functions should be supported, but if
 *    anyone is brain-damaged enough to want to emulate one of these
 *    beasts then this should get you started. 
 *
 * NOTE:
 *    Several common graphical extensions used by Microsoft hook
 *    off of here. I have *not* added them to this list (yet). They
 *    include:
 *
 *    MSHERC.COM - More functionality for Hercules cards.
 *    EGA.SYS (also MOUSE.COM) - More for EGA cards.
 *    
 *    Yes, MS also added this support into their mouse driver. Don't
 *    ask me, I don't work for them.
 *
 * Joseph Pranevich - 9/98 
 *
 *  Jess Haas 2/99
 *	Added support for Vesa. It is not complete but is a start. 
 *	NOTE: Im not sure if i did all this right or if eny of it works.
 *	Currently i dont have a program that uses Vesa that actually gets far 
 *	enough without crashing to do vesa stuff.
 * 
 *      Added additional vga graphic support - 3/99
 */

void WINAPI DOSVM_Int10Handler( CONTEXT86 *context )
{
    static int registered_colors = FALSE;
    BIOSDATA *data = DOSMEM_BiosData();

    if (!registered_colors)
    {
        /* Colors:
             0000b   black          1000b   dark gray
             0001b   blue           1001b   light blue
             0010b   green          1010b   light green
             0011b   cyan           1011b   light cyan
             0100b   red            1100b   light red
             0101b   magenta        1101b   light magenta
             0110b   brown          1110b   yellow
             0111b   light gray     1111b   white
        */
        
        /* These AllocColor calls have the side-effect of triggering 
           terminal initialization as xx_Init() is no longer called on
           startup. Which is what we want anyway. */

        color_palette[0]  = CONSOLE_AllocColor(WINE_BLACK);
        color_palette[1]  = CONSOLE_AllocColor(WINE_BLUE);
        color_palette[2]  = CONSOLE_AllocColor(WINE_GREEN);
        color_palette[3]  = CONSOLE_AllocColor(WINE_CYAN);
        color_palette[4]  = CONSOLE_AllocColor(WINE_RED);
        color_palette[5]  = CONSOLE_AllocColor(WINE_MAGENTA);
        color_palette[6]  = CONSOLE_AllocColor(WINE_BROWN);
        color_palette[7]  = CONSOLE_AllocColor(WINE_LIGHT_GRAY);
        color_palette[8]  = CONSOLE_AllocColor(WINE_DARK_GRAY);
        color_palette[9]  = CONSOLE_AllocColor(WINE_LIGHT_BLUE);
        color_palette[10] = CONSOLE_AllocColor(WINE_LIGHT_GREEN);
        color_palette[11] = CONSOLE_AllocColor(WINE_LIGHT_CYAN);
        color_palette[12] = CONSOLE_AllocColor(WINE_LIGHT_RED);
        color_palette[13] = CONSOLE_AllocColor(WINE_LIGHT_MAGENTA);
        color_palette[14] = CONSOLE_AllocColor(WINE_YELLOW);
        color_palette[15] = CONSOLE_AllocColor(WINE_WHITE);

        registered_colors = TRUE;
    }

    if(AL_reg(context) == 0x4F) { /* VESA functions */
	switch(AH_reg(context)) {

	case 0x00: /* GET SuperVGA INFORMATION */
	    FIXME("Vesa Get SuperVGA Info STUB!\n");
	    AL_reg(context) = 0x4f;
	    AH_reg(context) = 0x01; /* 0x01=failed 0x00=succesful */
	    break;
	case 0x01: /* GET SuperVGA MODE INFORMATION */
	    FIXME("VESA GET SuperVGA Mode Information - Not supported\n");
	    AL_reg(context) = 0x4f;
	    AH_reg(context) = 0x01; /* 0x00 = successful 0x01 = failed */
	    break;
	case 0x02: /* SET SuperVGA VIDEO MODE */
	    switch(BX_reg(context)) {
	 	/* OEM Video Modes */
		case 0x00: /* 40x25 */
            	case 0x01:
                	TRACE("Set VESA Text Mode - 0x0%x\n",
                	   BX_reg(context));
                	VGA_SetAlphaMode(40, 25);
                        data->VideoColumns = 40;
                	break;                
            	case 0x02:
            	case 0x03:
            	case 0x07:
            	    TRACE("Set VESA Text Mode - 0x0%x\n",
            	       BX_reg(context));
            	    VGA_SetAlphaMode(80, 25);
                    data->VideoColumns = 80;
            	    break;
	        case 0x0D:
                    TRACE("Setting VESA 320x200 16-color mode\n");
                    VGA_SetMode(320,200,4);
                    break;
 	        case 0x0E:
                    TRACE("Setting VESA 640x200 16-color mode\n");
                    VGA_SetMode(640,200,4); 
                    break;
	        case 0x10:
                    TRACE("Setting VESA 640x350 16-color mode\n");
                    VGA_SetMode(640,350,4);
                    break;
	        case 0x12:
                    TRACE("Setting VESA 640x480 16-color mode\n");
                    VGA_SetMode(640,480,4);
                    break;
                case 0x13:
            	    TRACE("Setting VESA 320x200 256-color mode\n");
            	    VGA_SetMode(320,200,8);
            	    break;
		/* VBE Modes */
		case 0x100:
		    TRACE("Setting VESA 640x400 256-color mode\n");
		    VGA_SetMode(640,400,8);
		    break;
		case 0x101:
		    TRACE("Setting VESA 640x480 256-color mode\n");
		    VGA_SetMode(640,480,8);
		    break;
		case 0x102:
		    TRACE("Setting VESA 800x600 16-color mode\n");
		    VGA_SetMode(800,600,4);
		    break;
		case 0x103:
		    TRACE("Setting VESA 800x600 256-color mode\n");
		    VGA_SetMode(800,600,8);
		    break;
		case 0x104:
		    TRACE("Setting VESA 1024x768 16-color mode\n");
		    VGA_SetMode(1024,768,4);
		    break;
		case 0x105:
		    TRACE("Setting VESA 1024x768 256-color mode\n");
		    VGA_SetMode(1024,768,8);
		    break;
		case 0x106:
		    TRACE("Setting VESA 1280x1024 16-color mode\n");
		    VGA_SetMode(1280,1024,4);
		    break;
		case 0x107:
		    TRACE("Setting VESA 1280x1024 256-color mode\n");
		    VGA_SetMode(1280,1024,8);
		    break;
		/* 108h - 10Ch are text modes and im lazy so :p */
		/* VBE v1.2+ */
		case 0x10D:
  		    TRACE("Setting VESA 320x200 15bpp\n");
		    VGA_SetMode(320,200,15);
		    break;
		case 0x10E:
  		    TRACE("Setting VESA 320x200 16bpp\n");
		    VGA_SetMode(320,200,16);
		    break;
		case 0x10F:
  		    TRACE("Setting VESA 320x200 24bpp\n");
		    VGA_SetMode(320,200,24);
		    break;
		case 0x110:
  		    TRACE("Setting VESA 640x480 15bpp\n");
		    VGA_SetMode(640,480,15);
		    break;
		case 0x111:
  		    TRACE("Setting VESA 640x480 16bpp\n");
		    VGA_SetMode(640,480,16);
		    break;
		case 0x112:
  		    TRACE("Setting VESA 640x480 24bpp\n");
		    VGA_SetMode(640,480,24);
		    break;
		case 0x113:
 		    TRACE("Setting VESA 800x600 15bpp\n");
		    VGA_SetMode(800,600,15);
		    break;
		case 0x114:
 		    TRACE("Setting VESA 800x600 16bpp\n");
		    VGA_SetMode(800,600,16);
		    break;
		case 0x115:
 		    TRACE("Setting VESA 800x600 24bpp\n");
		    VGA_SetMode(800,600,24);
		    break;
		case 0x116:
 		    TRACE("Setting VESA 1024x768 15bpp\n");
		    VGA_SetMode(1024,768,15);
		    break;
		case 0x117:
 		    TRACE("Setting VESA 1024x768 16bpp\n");
		    VGA_SetMode(1024,768,16);
		    break;
		case 0x118:
 		    TRACE("Setting VESA 1024x768 24bpp\n");
		    VGA_SetMode(1024,768,24);
		    break;
		case 0x119:
 		    TRACE("Setting VESA 1280x1024 15bpp\n");
		    VGA_SetMode(1280,1024,15);
		    break;
		case 0x11A:
 		    TRACE("Setting VESA 1280x1024 16bpp\n");
		    VGA_SetMode(1280,1024,16);
		    break;
		case 0x11B:
 		    TRACE("Setting VESA 1280x1024 24bpp\n");
		    VGA_SetMode(1280,1024,24);
		    break;
		default:
		    FIXME("VESA Set Video Mode (0x%x) - Not Supported\n", BX_reg(context));
	    }
            data->VideoMode = BX_reg(context);
            AL_reg(context) = 0x4f;
	    AH_reg(context) = 0x00;
	    break;	
	case 0x03: /* VESA SuperVGA BIOS - GET CURRENT VIDEO MODE */
		AL_reg(context) = 0x4f;
		AH_reg(context) = 0x00; /* should probly check if a vesa mode has ben set */
		BX_reg(context) = data->VideoMode;
		break;
	case 0x04: /* VESA SuperVGA BIOS - SAVE/RESTORE SuperVGA VIDEO STATE */
		ERR("VESA SAVE/RESTORE Video State - Not Implemented\n");
		/* AL_reg(context) = 0x4f; = supported so not set since not implemented */	
		/* maybe we should do this instead ? */
		/* AH_reg(context = 0x01;  not implemented so just fail */
		break;
	case 0x05: /* VESA SuperVGA BIOS - CPU VIDEO MEMORY CONTROL */
		ERR("VESA CPU VIDEO MEMORY CONTROL\n");
		/* AL_reg(context) = 0x4f; = supported so not set since not implemented */	
		/* maybe we should do this instead ? */
		/* AH_reg(context = 0x001; not implemented so just fail */
		break;
	case 0x06: /* VESA GET/SET LOGICAL SCAN LINE LENGTH */
		ERR("VESA GET/SET LOGICAL SCAN LINE LENGTH - Not Implemented\n");
		/* AL_reg(context) = 0x4f; = supported so not set since not implemented */	
		/* maybe we should do this instead ? */
		/* AH_reg(context = 0x001; not implemented so just fail */
		break;
	case 0x07: /* GET/SET DISPLAY START */
		ERR("VESA GET/SET DISPLAY START - Not Implemented\n");
		/* AL_reg(context) = 0x4f; = supported so not set since not implemented */	
		/* maybe we should do this instead ? */
		/* AH_reg(context = 0x001; not implemented so just fail */
		break;
	case 0x08: /* GET/SET DAC PALETTE CONTROL */
		ERR("VESA GET/SET DAC PALETTE CONTROL- Not Implemented\n");
		/* AL_reg(context) = 0x4f; = supported so not set since not implemented */	
		/* maybe we should do this instead ? */
		/* AH_reg(context = 0x001; not implemented so just fail */
		break;
	case 0x09: /* SET PALETTE ENTRIES */
		FIXME("VESA Set palette entries - not implemented\n");
		break;
        case 0xef:  /* get video mode for hercules-compatibles   */
                    /* There's no reason to really support this  */
                    /* is there?....................(A.C.)       */
                TRACE("Just report the video not hercules compatible\n");
                DX_reg(context) = 0xffff;
                break; 
	case 0xff: /* Turn VESA ON/OFF */
		/* i dont know what to do */
		break;
	default:
		FIXME("VESA Function (0x%x) - Not Supported\n", AH_reg(context));
		break;		
	}
    }
    else {

    switch(AH_reg(context)) {

    case 0x00: /* SET VIDEO MODE */
        /* Text Modes: */
        /* (mode) (text rows/cols)
            0x00 - 40x25 
            0x01 - 40x25
            0x02 - 80x25
            0x03 - 80x25 or 80x43 or 80x50 (assume 80x25) 
            0x07 - 80x25
        */

        switch (AL_reg(context)) {
            case 0x00: /* 40x25 */
            case 0x01:
                VGA_Exit();
                TRACE("Set Video Mode - Set to Text - 0x0%x\n",
                   AL_reg(context));
                VGA_SetAlphaMode(40, 25);
                data->VideoColumns = 40;
                break;                
            case 0x02:
            case 0x03:
            case 0x07:
                VGA_Exit();
                TRACE("Set Video Mode - Set to Text - 0x0%x\n",
                   AL_reg(context));
                VGA_SetAlphaMode(80, 25);
                data->VideoColumns = 80;
                break;
	    case 0x0D:
                TRACE("Setting VGA 320x200 16-color mode\n");
                VGA_SetMode(320,200,4);
                break;
	    case 0x0E:
                TRACE("Setting VGA 640x200 16-color mode\n");
                VGA_SetMode(640,200,4); 
                break;
	    case 0x10:
                TRACE("Setting VGA 640x350 16-color mode\n");
                VGA_SetMode(640,350,4);
                break;
	    case 0x12:
                TRACE("Setting VGA 640x480 16-color mode\n");
                VGA_SetMode(640,480,4);
                break;
            case 0x13:
                TRACE("Setting VGA 320x200 256-color mode\n");
                VGA_SetMode(320,200,8);
                break;
            default:
                FIXME("Set Video Mode (0x%x) - Not Supported\n", 
                   AL_reg(context));
        }
        data->VideoMode = AL_reg(context);
        break;

    case 0x01: /* SET CURSOR SHAPE */
        FIXME("Set Cursor Shape - Not Supported\n");
        break;
    
    case 0x02: /* SET CURSOR POSITION */
        /* BH = Page Number */ /* Not supported */
        /* DH = Row */ /* 0 is left */
        /* DL = Column */ /* 0 is top */
        BIOS_SetCursorPos(data,BH_reg(context),DL_reg(context),DH_reg(context));
        if (BH_reg(context))
        {
           FIXME("Set Cursor Position: Cannot set to page %d\n",
              BH_reg(context));
        }
        else
        {
           VGA_SetCursorPos(DL_reg(context), DH_reg(context));
           TRACE("Set Cursor Position: %d %d\n", DH_reg(context), 
              DL_reg(context));
        }
        break;

    case 0x03: /* GET CURSOR POSITION AND SIZE */
        {
          unsigned row, col;

          TRACE("Get cursor position and size (page %d)\n", BH_reg(context));
          CX_reg(context) = data->VideoCursorType;
          BIOS_GetCursorPos(data,BH_reg(context),&col,&row);
          DH_reg(context) = row;
          DL_reg(context) = col;
          TRACE("Cursor Position: %d %d\n", DH_reg(context), DL_reg(context));
        }
        break;

    case 0x04: /* READ LIGHT PEN POSITION */
        FIXME("Read Light Pen Position - Not Supported\n");
        AH_reg(context) = 0x00; /* Not down */
        break;

    case 0x05: /* SELECT ACTIVE DISPLAY PAGE */
        FIXME("Select Active Display Page - Not Supported\n");
        data->VideoCurPage = AL_reg(context);
        break;

    case 0x06: /* SCROLL UP WINDOW */
        /* AL = Lines to scroll */
        /* BH = Attribute */
        /* CH,CL = row, col upper-left */
        /* DH,DL = row, col lower-right */
        scroll_window(SCROLL_UP, AL_reg(context), CH_reg(context), 
           CL_reg(context), DH_reg(context), DL_reg(context), 
           BH_reg(context));
        TRACE("Scroll Up Window %d\n", AL_reg(context));
        break;

    case 0x07: /* SCROLL DOWN WINDOW */
        /* AL = Lines to scroll */
        /* BH = Attribute */
        /* CH,CL = row, col upper-left */
        /* DH,DL = row, col lower-right */
        scroll_window(SCROLL_DOWN, AL_reg(context), CH_reg(context), 
           CL_reg(context), DH_reg(context), DL_reg(context), 
           BH_reg(context));
        TRACE("Scroll Down Window %d\n", AL_reg(context));
        break;

    case 0x08: /* READ CHARACTER AND ATTRIBUTE AT CURSOR POSITION */
        {
            /* Note here that color data returned is bogus, will fix later. */
            char ch;
            int bg, fg, attr;
            if (BH_reg(context)) /* Write to different page */
            {
                FIXME("Read character and attribute at cursor position -"
                      " Can't read from non-0 page\n");
                AL_reg(context) = ' '; /* That page is blank */
                AH_reg(context) = 7;
            }
            else
            {
                TRACE(
                      "Read Character and Attribute at Cursor Position\n");
                CONSOLE_GetCharacterAtCursor(&ch, &fg, &bg, &attr);
                AL_reg(context) = ch;
                AH_reg(context) = 7;	/* FIXME: We're assuming wh on bl */ 
            }
        }
        break;

    case 0x09: /* WRITE CHARACTER AND ATTRIBUTE AT CURSOR POSITION */
    case 0x0a: /* WRITE CHARACTER ONLY AT CURSOR POSITION */ 
       /* AL = Character to display. */
       /* BH = Page Number */ /* We can't write to non-0 pages, yet. */
       /* BL = Attribute / Color */
       /* CX = Times to Write Char */
       /* Note here that the cursor is not advanced. */
       {
           unsigned row, col;

           BIOS_GetCursorPos(data,BH_reg(context),&col,&row);
           VGA_WriteChars(col, row,
                          AL_reg(context),
                          (AH_reg(context) == 0x09) ? BL_reg(context) : -1,
                          CX_reg(context));
           if (CX_reg(context) > 1)
              TRACE("Write Character%s at Cursor Position (Rep. %d): %c\n",
                    (AH_reg(context) == 0x09) ? " and Attribute" : "",
                 CX_reg(context), AL_reg(context));
           else
              TRACE("Write Character%s at Cursor Position: %c\n",
                    (AH_reg(context) == 0x09) ? " and Attribute" : "",
                AL_reg(context));
       }
       break;

    case 0x0b: 
        switch BH_reg(context) {
        case 0x00: /* SET BACKGROUND/BORDER COLOR */
            /* In text modes, this sets only the border... */
            /* According to the interrupt list and one of my books. */
            /* Funny though that Beyond Zork seems to indicate that it 
               also sets up the default background attributes for clears
               and scrolls... */
            /* Bear in mind here that we do not want to change,
               apparantly, the foreground or attribute of the background
               with this call, so we should check first to see what the
               foreground already is... FIXME */
            TRACE("Set Background/Border Color: %d\n", 
               BL_reg(context));
            CONSOLE_SetBackgroundColor(color_palette[0],
               color_palette[BL_reg(context)]);   
            break;
        case 0x01: /* SET PALETTE */
            FIXME("Set Palette - Not Supported\n");
            break;
        default:
            FIXME("INT 10 AH = 0x0b BH = 0x%x - Unknown\n", 
               BH_reg(context));
            break;
        }
        break;

    case 0x0c: /* WRITE GRAPHICS PIXEL */
        /* Not in graphics mode, can ignore w/o error */
        FIXME("Write Graphics Pixel - Not Supported\n");
        break;
        
    case 0x0d: /* READ GRAPHICS PIXEL */
        /* Not in graphics mode, can ignore w/o error */
        FIXME("Read Graphics Pixel - Not Supported\n");
        break;
              
    case 0x0e: /* TELETYPE OUTPUT */
        TRACE("Teletype Output\n");
        CONSOLE_Write(AL_reg(context), 0, 0, 0);
        break;

    case 0x0f: /* GET CURRENT VIDEO MODE */
        TRACE("Get current video mode\n");
        /* Note: This should not be a constant value. */
        AL_reg(context) = data->VideoMode;
        AH_reg(context) = data->VideoColumns;
        BH_reg(context) = 0; /* Display page 0 */
        break;

    case 0x10: 
        switch AL_reg(context) {
        case 0x00: /* SET SINGLE PALETTE REGISTER */
            FIXME("Set Single Palette Register - Not Supported\n");
            break;
        case 0x01: /* SET BORDER (OVERSCAN) */
            /* Text terminals have no overscan */
            TRACE("Set Border (Overscan) - Ignored\n");
            break;
        case 0x02: /* SET ALL PALETTE REGISTERS */
            FIXME("Set all palette registers - Not Supported\n");
            break;
        case 0x03: /* TOGGLE INTENSITY/BLINKING BIT */
            FIXME("Toggle Intensity/Blinking Bit - Not Supported\n");
            break;
        case 0x07: /* GET INDIVIDUAL PALETTE REGISTER */
            FIXME("Get Individual Palette Register - Not Supported\n");
            break;
        case 0x08: /* READ OVERSCAN (BORDER COLOR) REGISTER */
            FIXME(
               "Read Overscan (Border Color) Register - Not Supported\n");
            break;
        case 0x09: /* READ ALL PALETTE REGISTERS AND OVERSCAN REGISTER */
            FIXME(
               "Read All Palette Registers and Overscan Register "
               " - Not Supported\n");
            break;
        case 0x10: /* SET INDIVIDUAL DAC REGISTER */
            FIXME("Set Individual DAC register - Not Supported\n");
            break;
        case 0x12: /* SET BLOCK OF DAC REGISTERS */
            FIXME("Set Block of DAC registers - Not Supported\n");
            break;
        case 0x13: /* SELECT VIDEO DAC COLOR PAGE */
            FIXME("Select video DAC color page - Not Supported\n");
            break;
        case 0x15: /* READ INDIVIDUAL DAC REGISTER */
            FIXME("Read individual DAC register - Not Supported\n");
            break;
        case 0x17: /* READ BLOCK OF DAC REGISTERS */
            FIXME("Read block of DAC registers - Not Supported\n");
            break;
        case 0x18: /* SET PEL MASK */
            FIXME("Set PEL mask - Not Supported\n");
            break;
        case 0x19: /* READ PEL MASK */
            FIXME("Read PEL mask - Not Supported\n");
            break;
        case 0x1a: /* GET VIDEO DAC COLOR PAGE STATE */
            FIXME("Get video DAC color page state - Not Supported\n");
            break;
        case 0x1b: /* PERFORM GRAY-SCALE SUMMING */
            FIXME("Perform Gray-scale summing - Not Supported\n");
            break;
        default:
            FIXME("INT 10 AH = 0x10 AL = 0x%x - Unknown\n", 
               AL_reg(context));
            break;
        }
        break;

    case 0x11: /* TEXT MODE CHARGEN */
        /* Note that second subfunction is *almost* identical. */
        /* See INTERRUPT.A for details. */
        switch AL_reg(context) {
        case 0x00: /* LOAD USER SPECIFIED PATTERNS */
        case 0x10:
            FIXME("Load User Specified Patterns - Not Supported\n");
            break;
        case 0x01: /* LOAD ROM MONOCHROME PATTERNS */
        case 0x11:
            FIXME("Load ROM Monochrome Patterns - Not Supported\n");
            break;
        case 0x02: /* LOAD ROM 8x8 DOUBLE-DOT PATTERNS */
        case 0x12:
            FIXME(
                "Load ROM 8x8 Double Dot Patterns - Not Supported\n");       
            break;
        case 0x03: /* SET BLOCK SPECIFIER */
            FIXME("Set Block Specifier - Not Supported\n");
            break;
        case 0x04: /* LOAD ROM 8x16 CHARACTER SET */
        case 0x14:
            FIXME("Load ROM 8x16 Character Set - Not Supported\n");
            break;
        case 0x20: /* SET USER 8x16 GRAPHICS CHARS */
            FIXME("Set User 8x16 Graphics Chars - Not Supported\n");
            break;
        case 0x21: /* SET USER GRAPICS CHARACTERS */
            FIXME("Set User Graphics Characters - Not Supported\n");
            break;
        case 0x22: /* SET ROM 8x14 GRAPHICS CHARS */
            FIXME("Set ROM 8x14 Graphics Chars - Not Supported\n");
            break;
        case 0x23: /* SET ROM 8x8 DBL DOT CHARS */
            FIXME(
                "Set ROM 8x8 Dbl Dot Chars (Graphics) - Not Supported\n");
            break;
        case 0x24: /* LOAD 8x16 GRAPHIC CHARS */
            FIXME("Load 8x16 Graphic Chars - Not Supported\n");
            break;
        case 0x30: /* GET FONT INFORMATION */
            FIXME("Get Font Information - Not Supported\n");
            break;
        default:
            FIXME("INT 10 AH = 0x11 AL = 0x%x - Unknown\n", 
               AL_reg(context));
            break;
        }
        break;
        
    case 0x12: /* ALTERNATE FUNCTION SELECT */
        switch BL_reg(context) {
        case 0x10: /* GET EGA INFO */
            TRACE("EGA info requested\n");
            BH_reg(context) = 0x00;   /* Color screen */
            BL_reg(context) =
                data->ModeOptions >> 5; /* EGA memory size */
            CX_reg(context) =
                data->FeatureBitsSwitches;
            break;
        case 0x20: /* ALTERNATE PRTSC */
            FIXME("Install Alternate Print Screen - Not Supported\n");
            break;
        case 0x30: /* SELECT VERTICAL RESOULTION */
            FIXME("Select vertical resolution - not supported\n");
            break;
        case 0x31: /* ENABLE/DISABLE DEFAULT PALETTE LOADING */
            FIXME("Default palette loading - not supported\n");
            data->VGASettings =
                (data->VGASettings & 0xf7) |
                ((AL_reg(context) == 1) << 3);
            break;
        case 0x32: /* ENABLE/DISABLE VIDEO ADDRERSSING */
            FIXME("Video Addressing - Not Supported\n");
            break;
        case 0x33: /* ENABLE/DISABLE GRAY SCALE SUMMING */
            FIXME("Gray Scale Summing - Not Supported\n");
            break;
        case 0x34: /* ENABLE/DISABLE CURSOR EMULATION */
            TRACE("Set cursor emulation to %d\n", AL_reg(context));
            data->ModeOptions =
                (data->ModeOptions & 0xfe)|(AL_reg(context) == 1);
            break;
        case 0x36: /* VIDEO ADDRESS CONTROL */
            FIXME("Video Address Control - Not Supported\n");
            break;
        default:
            FIXME("INT 10 AH = 0x11 AL = 0x%x - Unknown\n", 
               AL_reg(context));
            break;
        }
        break;

    case 0x13: /* WRITE STRING */
        /* This one does not imply that string be at cursor. */
        FIXME("Write String - Not Supported\n");
        break;
                             
    case 0x1a: 
        switch AL_reg(context) {
        case 0x00: /* GET DISPLAY COMBINATION CODE */
            TRACE("Get Display Combination Code\n");
            AX_reg(context) = 0x001a;
            BL_reg(context) = 0x08; /* VGA w/ color analog display */
            BH_reg(context) = 0x00; /* No secondary hardware */
            break;
        case 0x01: /* SET DISPLAY COMBINATION CODE */
            FIXME("Set Display Combination Code - Not Supported\n");
            break;
        default:
            FIXME("INT 10 AH = 0x1a AL = 0x%x - Unknown\n", 
               AL_reg(context));
            break;
        }
    break;

    case 0x1b: /* FUNCTIONALITY/STATE INFORMATION */
        FIXME("Get functionality/state information - partially implemented\n");
        if (BX_reg(context) == 0x0)
        {
          AL_reg(context) = 0x1b;
          if (ISV86(context)) /* real */
            context->SegEs = 0xf000;
          else
            context->SegEs = DOSMEM_BiosSysSeg;
          BX_reg(context) = 0xe000;
        }
        break;

    case 0x1c: /* SAVE/RESTORE VIDEO STATE */
        FIXME("Save/Restore Video State - Not Supported\n");
        break;

    case 0x4f: /* Get SuperVGA INFORMATION */
        {
          BYTE *p =
               CTX_SEG_OFF_TO_LIN(context, context->SegEs, context->Edi);
          /* BOOL16 vesa20 = (*(DWORD *)p == *(DWORD *)"VBE2"); */
  
          TRACE("Get SuperVGA information\n");
          AH_reg(context) = 0;
          *(DWORD *)p = *(DWORD *)"VESA";
          *(WORD *)(p+0x04) = 0x0200; /* VESA 2.0 */
          *(DWORD *)(p+0x06) = 0x00000000; /* pointer to OEM name */
          *(DWORD *)(p+0x0a) = 0xfffffffd; /* capabilities flags :-) */
        }
        break;
        case 0xef:  /* get video mode for hercules-compatibles   */
                    /* There's no reason to really support this  */
                    /* is there?....................(A.C.)       */
                TRACE("Just report the video not hercules compatible\n");
                DX_reg(context) = 0xffff;
                break; 
    default:
        FIXME("Unknown - 0x%x\n", AH_reg(context));
        INT_BARF( context, 0x10 );
    }
    }
}

static void conv_text_mode_attributes(char attribute, int *fg, int *bg,
   int *wattribute)
{
    /* This is a local function to convert the text-mode attributes
       to Wine's color and attribute scheme */

    /* Foreground Color is stored in bits 3 through 0 */
    /* Background Color is stored in bits 6 through 4 */
    /* If this has bit 7 set, then we need to blink */

    *fg = color_palette[attribute & 15];
    *bg = color_palette[(attribute & 112) / 16];
    *wattribute = attribute & 128;

}

static void scroll_window(int direction, char lines, char row1, 
   char col1, char row2, char col2, char attribute)
{
   int wattribute, bg_color, fg_color;

   conv_text_mode_attributes(attribute, &fg_color, &bg_color,
      &wattribute);

   if (!lines) /* Actually, clear the window */
   {
      CONSOLE_ClearWindow(row1, col1, row2, col2, bg_color, wattribute);
   }
   else if (direction == SCROLL_UP)
   {
      CONSOLE_ScrollUpWindow(row1, col1, row2, col2, lines, bg_color,
         wattribute);
   }
   else
   {
      CONSOLE_ScrollDownWindow(row1, col1, row2, col2, lines, bg_color,
         wattribute);
   }
}
   
