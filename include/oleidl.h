/*
 * Copyright (C) 1999 Francis Beaudet
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

#ifndef __WINE__
#include "rpc.h"
#include "rpcndr.h"
#ifndef COM_NO_WINDOWS_H
#include "windows.h"
#include "ole2.h"
#endif
#endif

#ifndef __WINE_OLEIDL_H
#define __WINE_OLEIDL_H


#include "wine/obj_base.h"
#include "wine/obj_misc.h"

/* the following depend only on obj_base.h */
#include "wine/obj_storage.h"

/* the following depend on obj_storage.h */
#include "wine/obj_moniker.h"

/* the following depend on obj_moniker */
#include "wine/obj_dataobject.h"

/* the following depend on obj_dataobject.h */
#include "wine/obj_dragdrop.h"

#include "wine/obj_inplace.h"
#include "wine/obj_cache.h"
#include "wine/obj_oleobj.h"
#include "wine/obj_oleview.h"
#include "wine/obj_errorinfo.h"

#endif /* __WINE_OLEIDL_H */

