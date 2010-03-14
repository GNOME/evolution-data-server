/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 *
 * Authors:
 *		Jeffrey Stedfast <fejj@ximian.com>
 *		Veerapuram Varadhan <vvaradhan@novell.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

/**
 * EProxy:
 *
 * Since: 2.24
 **/
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
SoupURI* e_proxy_peek_uri_for (EProxy* proxy, const gchar *uri);
void e_proxy_setup_proxy (EProxy* proxy);
GType e_proxy_get_type (void);
gboolean e_proxy_require_proxy_for_uri (EProxy *proxy,
					const gchar * uri);

G_END_DECLS

#endif
