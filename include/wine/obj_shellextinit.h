/*
 *    IShellExtInit
 *
 * Copyright (C) 1999 Juergen Schmied
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

#ifndef __WINE_WINE_OBJ_SHELLEXTINIT_H
#define __WINE_WINE_OBJ_SHELLEXTINIT_H

#ifdef __cplusplus
extern "C" {
#endif /* defined(__cplusplus) */

typedef struct 	IShellExtInit IShellExtInit, *LPSHELLEXTINIT;

#define ICOM_INTERFACE IShellExtInit
#define IShellExtInit_METHODS \
	ICOM_METHOD3(HRESULT, Initialize, LPCITEMIDLIST, pidlFolder, LPDATAOBJECT, lpdobj, HKEY, hkeyProgID)
#define IShellExtInit_IMETHODS \
	IUnknown_IMETHODS \
	IShellExtInit_METHODS
ICOM_DEFINE(IShellExtInit,IUnknown)
#undef ICOM_INTERFACE

/*** IUnknown methods ***/
#define IShellExtInit_QueryInterface(p,a,b)	ICOM_CALL2(QueryInterface,p,a,b)
#define IShellExtInit_AddRef(p)			ICOM_CALL(AddRef,p)
#define IShellExtInit_Release(p)		ICOM_CALL(Release,p)
/*** IShellExtInit methods ***/
#define IShellExtInit_Initialize(p,a,b,c)	ICOM_CALL3(Initialize,p,a,b,c)

#ifdef __cplusplus
} /* extern "C" */
#endif /* defined(__cplusplus) */

#endif /* __WINE_WINE_OBJ_SHELLEXTINIT_H */
