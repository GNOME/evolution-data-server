/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Matt Brown <matt@mattb.net.nz>
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

#ifndef _CAMEL_MIME_FILTER_PGP_H
#define _CAMEL_MIME_FILTER_PGP_H

#include <camel/camel-mime-filter.h>

#define CAMEL_MIME_FILTER_PGP_TYPE         (camel_mime_filter_canon_get_type ())
#define CAMEL_MIME_FILTER_PGP(obj)         CAMEL_CHECK_CAST (obj, CAMEL_MIME_FILTER_PGP_TYPE, CamelMimeFilterPgp)
#define CAMEL_MIME_FILTER_PGP_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, CAMEL_MIME_FILTER_PGP_TYPE, CamelMimeFilterPgpClass)
#define CAMEL_IS_MIME_FILTER_PGP(obj)      CAMEL_CHECK_TYPE (obj, CAMEL_MIME_FILTER_PGP_TYPE)

G_BEGIN_DECLS

typedef struct _CamelMimeFilterPgp {
	CamelMimeFilter filter;
	gint state;
} CamelMimeFilterPgp;

typedef struct _CamelMimeFilterPgpClass {
	CamelMimeFilterClass parent_class;
} CamelMimeFilterPgpClass;

CamelType camel_mime_filter_pgp_get_type (void);

CamelMimeFilter *camel_mime_filter_pgp_new(void);

G_END_DECLS

#endif /* _CAMEL_MIME_FILTER_PGP_H */
