/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 */

#ifndef CAMEL_TCP_STREAM_RAW_H
#define CAMEL_TCP_STREAM_RAW_H

#include <camel/camel-tcp-stream.h>

#define CAMEL_TCP_STREAM_RAW_TYPE     (camel_tcp_stream_raw_get_type ())
#define CAMEL_TCP_STREAM_RAW(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_TCP_STREAM_RAW_TYPE, CamelTcpStreamRaw))
#define CAMEL_TCP_STREAM_RAW_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_TCP_STREAM_RAW_TYPE, CamelTcpStreamRawClass))
#define CAMEL_IS_TCP_STREAM_RAW(o)    (CAMEL_CHECK_TYPE((o), CAMEL_TCP_STREAM_RAW_TYPE))

G_BEGIN_DECLS

struct _CamelTcpStreamRaw
{
	CamelTcpStream parent_object;

	gint sockfd;
#ifdef G_OS_WIN32
	gint is_nonblocking;
#endif
};

typedef struct {
	CamelTcpStreamClass parent_class;

	/* virtual functions */

} CamelTcpStreamRawClass;

/* Standard Camel function */
CamelType camel_tcp_stream_raw_get_type (void);

/* public methods */
CamelStream *camel_tcp_stream_raw_new (void);

G_END_DECLS

#endif /* CAMEL_TCP_STREAM_RAW_H */
