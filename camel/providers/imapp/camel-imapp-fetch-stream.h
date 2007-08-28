/*
 *  Copyright (C) 2000 Ximian Inc.
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _CAMEL_IMAPP_FETCH_STREAM_H
#define _CAMEL_IMAPP_FETCH_STREAM_H

#include <camel/camel-stream.h>

#define CAMEL_IMAPP_FETCH_STREAM(obj)         CAMEL_CHECK_CAST (obj, camel_imapp_fetch_stream_get_type (), CamelIMAPPFetchStream)
#define CAMEL_IMAPP_FETCH_STREAM_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_imapp_fetch_stream_get_type (), CamelIMAPPFetchStreamClass)
#define CAMEL_IS_IMAP_FETCH_STREAM(obj)      CAMEL_CHECK_TYPE (obj, camel_imapp_fetch_stream_get_type ())

G_BEGIN_DECLS

typedef struct _CamelIMAPPFetchStreamClass CamelIMAPPFetchStreamClass;
typedef struct _CamelIMAPPFetchStream CamelIMAPPFetchStream;

struct _CamelIMAPPFetchStream {
	CamelStream parent;

	struct _CamelIMAPPEngine *engine;
};

struct _CamelIMAPPFetchStreamClass {
	CamelStreamClass parent_class;
};

CamelType	 camel_imapp_fetch_stream_get_type	(void);

CamelStream     *camel_imapp_fetch_stream_new		(struct _CamelIMAPPEngine *src, const char *uid, const char *spec);

G_END_DECLS

#endif /* ! _CAMEL_IMAPP_FETCH_STREAM_H */
