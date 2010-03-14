/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */

#ifndef __CAMEL_MIME_FILTER_PROGRESS_H__
#define __CAMEL_MIME_FILTER_PROGRESS_H__

#include <camel/camel-operation.h>
#include <camel/camel-mime-filter.h>

#define CAMEL_MIME_FILTER_PROGRESS(obj)         CAMEL_CHECK_CAST (obj, camel_mime_filter_progress_get_type (), CamelMimeFilterProgress)
#define CAMEL_MIME_FILTER_PROGRESS_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mime_filter_progress_get_type (), CamelMimeFilterProgressClass)
#define CAMEL_IS_MIME_FILTER_PROGRESS(obj)      CAMEL_CHECK_TYPE (obj, camel_mime_filter_progress_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMimeFilterProgressClass CamelMimeFilterProgressClass;
typedef struct _CamelMimeFilterProgress CamelMimeFilterProgress;

/**
 * CamelMimeFilterProgress:
 *
 * Since: 2.24
 **/
struct _CamelMimeFilterProgress {
	CamelMimeFilter parent;

	CamelOperation *operation;
	gsize total;
        gsize count;
};

struct _CamelMimeFilterProgressClass {
	CamelMimeFilterClass parent_class;

};

CamelType camel_mime_filter_progress_get_type (void);

CamelMimeFilter *camel_mime_filter_progress_new (CamelOperation *operation, gsize total);

G_END_DECLS

#endif /* __CAMEL_MIME_FILTER_PROGRESS_H__ */
