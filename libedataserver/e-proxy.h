/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *	     Veerapuram Varadhan <vvaradhan@novell.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */


#ifndef __E_PROXY_H__
#define __E_PROXY_H__

#include <libsoup/soup-uri.h>

G_BEGIN_DECLS

#define E_TYPE_PROXY            (e_proxy_get_type ())
#define E_PROXY(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_PROXY, EProxy))
#define E_PROXY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_PROXY, EProxyClass))
#define E_IS_PROXY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_PROXY))
#define E_IS_PROXY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_PROXY))

typedef struct _EProxy        EProxy;
typedef struct _EProxyClass   EProxyClass;
typedef struct _EProxyPrivate EProxyPrivate;

struct _EProxy {
	GObject parent;
	EProxyPrivate *priv;
};

struct _EProxyClass {
	GObjectClass parent_class;
	/* Signals.  */

	void (*changed) (EProxy *proxy);
};

EProxy* e_proxy_new (void);
SoupURI* e_proxy_peek_uri (EProxy* proxy);
void e_proxy_setup_proxy (EProxy* proxy);
GType e_proxy_get_type (void);
gboolean e_proxy_require_proxy_for_uri (EProxy *proxy, 
					const char* uri);

G_END_DECLS

#endif
