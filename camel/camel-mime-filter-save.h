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

#ifndef __CAMEL_MIME_FILTER_SAVE_H__
#define __CAMEL_MIME_FILTER_SAVE_H__

#include <camel/camel-mime-filter.h>
#include <camel/camel-seekable-stream.h>

#define CAMEL_MIME_FILTER_SAVE_TYPE         (camel_mime_filter_save_get_type ())
#define CAMEL_MIME_FILTER_SAVE(obj)         CAMEL_CHECK_CAST (obj, camel_mime_filter_save_get_type (), CamelMimeFilterSave)
#define CAMEL_MIME_FILTER_SAVE_CLASS(klass) CAMEL_CHECK_CLASS_CAST (klass, camel_mime_filter_save_get_type (), CamelMimeFilterSaveClass)
#define CAMEL_IS_MIME_FILTER_SAVE(obj)      CAMEL_CHECK_TYPE (obj, camel_mime_filter_save_get_type ())

G_BEGIN_DECLS

typedef struct _CamelMimeFilterSaveClass CamelMimeFilterSaveClass;

struct _CamelMimeFilterSave {
	CamelMimeFilter parent;

	CamelStream *stream;
};

struct _CamelMimeFilterSaveClass {
	CamelMimeFilterClass parent_class;
};

CamelType camel_mime_filter_save_get_type (void);

CamelMimeFilter *camel_mime_filter_save_new (void);
CamelMimeFilter *camel_mime_filter_save_new_with_stream (CamelStream *stream);

G_END_DECLS

#endif /* __CAMEL_MIME_FILTER_SAVE_H__ */
