/*
 * Wine internal Unicode definitions
 *
 * Copyright 2000 Alexandre Julliard
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

#ifndef __WINE_UNICODE_H
#define __WINE_UNICODE_H

#include "windef.h"
#include "winnls.h"

/* code page info common to SBCS and DBCS */
struct cp_info
{
    unsigned int          codepage;          /* codepage id */
    unsigned int          char_size;         /* char size (1 or 2 bytes) */
    WCHAR                 def_char;          /* default char value (can be double-byte) */
    WCHAR                 def_unicode_char;  /* default Unicode char value */
    const char           *name;              /* code page name */
};

struct sbcs_table
{
    struct cp_info        info;
    const WCHAR          *cp2uni;            /* code page -> Unicode map */
    const unsigned char  *uni2cp_low;        /* Unicode -> code page map */
    const unsigned short *uni2cp_high;
};

struct dbcs_table
{
    struct cp_info        info;
    const WCHAR          *cp2uni;            /* code page -> Unicode map */
    const unsigned char  *cp2uni_leadbytes;
    const unsigned short *uni2cp_low;        /* Unicode -> code page map */
    const unsigned short *uni2cp_high;
    unsigned char         lead_bytes[12];    /* lead bytes ranges */
};

union cptable
{
    struct cp_info    info;
    struct sbcs_table sbcs;
    struct dbcs_table dbcs;
};

extern const union cptable *cp_get_table( unsigned int codepage );
extern const union cptable *cp_enum_table( unsigned int index );

extern int cp_mbstowcs( const union cptable *table, int flags,
                        const char *src, int srclen,
                        WCHAR *dst, int dstlen );
extern int cp_wcstombs( const union cptable *table, int flags,
                        const WCHAR *src, int srclen,
                        char *dst, int dstlen, const char *defchar, int *used );
extern int utf8_wcstombs( const WCHAR *src, int srclen, char *dst, int dstlen );
extern int utf8_mbstowcs( int flags, const char *src, int srclen, WCHAR *dst, int dstlen );

static inline int is_dbcs_leadbyte( const union cptable *table, unsigned char ch )
{
    return (table->info.char_size == 2) && (table->dbcs.cp2uni_leadbytes[ch]);
}

static inline WCHAR tolowerW( WCHAR ch )
{
    extern const WCHAR casemap_lower[];
    return ch + casemap_lower[casemap_lower[ch >> 8] + (ch & 0xff)];
}

static inline WCHAR toupperW( WCHAR ch )
{
    extern const WCHAR casemap_upper[];
    return ch + casemap_upper[casemap_upper[ch >> 8] + (ch & 0xff)];
}

/* the character type contains the C1_* flags in the low 12 bits */
/* and the C2_* type in the high 4 bits */
static inline unsigned short get_char_typeW( WCHAR ch )
{
    extern const unsigned short wctype_table[];
    return wctype_table[wctype_table[ch >> 8] + (ch & 0xff)];
}

inline static int iscntrlW( WCHAR wc )
{
    return get_char_typeW(wc) & C1_CNTRL;
}

inline static int ispunctW( WCHAR wc )
{
    return get_char_typeW(wc) & C1_PUNCT;
}

inline static int isspaceW( WCHAR wc )
{
    return get_char_typeW(wc) & C1_SPACE;
}

inline static int isdigitW( WCHAR wc )
{
    return get_char_typeW(wc) & C1_DIGIT;
}

inline static int isxdigitW( WCHAR wc )
{
    return get_char_typeW(wc) & C1_XDIGIT;
}

inline static int islowerW( WCHAR wc )
{
    return get_char_typeW(wc) & C1_LOWER;
}

inline static int isupperW( WCHAR wc )
{
    return get_char_typeW(wc) & C1_UPPER;
}

inline static int isalnumW( WCHAR wc )
{
    return get_char_typeW(wc) & (C1_ALPHA|C1_DIGIT|C1_LOWER|C1_UPPER);
}

inline static int isalphaW( WCHAR wc )
{
    return get_char_typeW(wc) & (C1_ALPHA|C1_LOWER|C1_UPPER);
}

inline static int isgraphW( WCHAR wc )
{
    return get_char_typeW(wc) & (C1_ALPHA|C1_PUNCT|C1_DIGIT|C1_LOWER|C1_UPPER);
}

inline static int isprintW( WCHAR wc )
{
    return get_char_typeW(wc) & (C1_ALPHA|C1_BLANK|C1_PUNCT|C1_DIGIT|C1_LOWER|C1_UPPER);
}

/* some useful string manipulation routines */

static inline unsigned int strlenW( const WCHAR *str )
{
#if defined(__i386__) && defined(__GNUC__)
    int dummy, res;
    __asm__ __volatile__( "cld\n\t"
                          "repnz\n\t"
                          "scasw\n\t"
                          "notl %0"
                          : "=c" (res), "=&D" (dummy)
                          : "0" (0xffffffff), "1" (str), "a" (0) );
    return res - 1;
#else
    const WCHAR *s = str;
    while (*s) s++;
    return s - str;
#endif
}

static inline WCHAR *strcpyW( WCHAR *dst, const WCHAR *src ) 
{
#if defined(__i386__) && defined(__GNUC__)
    int dummy1, dummy2, dummy3;
    __asm__ __volatile__( "cld\n"
                          "1:\tlodsw\n\t"
                          "stosw\n\t"
                          "testw %%ax,%%ax\n\t"
                          "jne 1b"
                          : "=&S" (dummy1), "=&D" (dummy2), "=&a" (dummy3)
                          : "0" (src), "1" (dst)
                          : "memory" );
#else
    WCHAR *p = dst;
    while ((*p++ = *src++));
#endif
    return dst;
}

static inline int strcmpW( const WCHAR *str1, const WCHAR *str2 ) 
{
    while (*str1 && (*str1 == *str2)) { str1++; str2++; }
    return *str1 - *str2;
}

static inline int strncmpW( const WCHAR *str1, const WCHAR *str2, int n )
{
    if (n <= 0) return 0;
    while ((--n > 0) && *str1 && (*str1 == *str2)) { str1++; str2++; }
    return *str1 - *str2;
}

static inline WCHAR *strncpyW( WCHAR *str1, const WCHAR *str2, int n )
{
    WCHAR *ret = str1;
    while (n-- > 0) if (!(*str1++ = *str2++)) break;
    while (n-- > 0) *str1++ = 0;
    return ret;
}

static inline WCHAR *strcatW( WCHAR *dst, const WCHAR *src )
{
    strcpyW( dst + strlenW(dst), src );
    return dst;
}

static inline WCHAR *strchrW( const WCHAR *str, WCHAR ch )
{
    for ( ; *str; str++) if (*str == ch) return (WCHAR *)str;
    return NULL;
}
 
static inline WCHAR *strrchrW( const WCHAR *str, WCHAR ch )
{
    WCHAR *ret = NULL;
    for ( ; *str; str++) if (*str == ch) ret = (WCHAR *)str;
    return ret;
}               

static inline WCHAR *strlwrW( WCHAR *str )
{
    WCHAR *ret = str;
    while ((*str = tolowerW(*str))) str++;
    return ret;
}

static inline WCHAR *struprW( WCHAR *str )
{
    WCHAR *ret = str;
    while ((*str = toupperW(*str))) str++;
    return ret;
}

extern int strcmpiW( const WCHAR *str1, const WCHAR *str2 );
extern int strncmpiW( const WCHAR *str1, const WCHAR *str2, int n );
extern WCHAR *strstrW( const WCHAR *str, const WCHAR *sub );

#endif  /* __WINE_UNICODE_H */
