/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- *
 *
 * Author: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_DEBUG_H
#define CAMEL_DEBUG_H

#include <glib.h>

/* This is how the basic debug checking strings should be done */
#define CAMEL_DEBUG_IMAP "imap"
#define CAMEL_DEBUG_IMAP_FOLDER "imap:folder"

G_BEGIN_DECLS

void camel_debug_init(void);
gboolean camel_debug(const gchar *mode);

gboolean camel_debug_start(const gchar *mode);
void camel_debug_end(void);

/**
 * CAMEL_CHECK_GERROR:
 *
 * This sanity checks return values and #GErrors.  If returning
 * failure, make sure the #GError is set.  If returning success,
 * make sure the #GError is NOT set.
 *
 * Example:
 *
 *     success = class->foo (object, some_data, error);
 *     CAMEL_CHECK_GERROR (object, foo, success, error);
 *     return success;
 *
 * Since: 2.32
 */
#define CAMEL_CHECK_GERROR(object, method, expr, error) \
	G_STMT_START { \
	if (expr) { \
		if ((error) != NULL && *(error) != NULL) { \
			g_warning ( \
				"%s::%s() set its GError " \
				"but then reported success", \
				G_OBJECT_TYPE_NAME (object), \
				G_STRINGIFY (method)); \
			g_warning ( \
				"Error message was: %s", \
				(*(error))->message); \
		} \
	} else { \
		if ((error) != NULL && *(error) == NULL) { \
			g_warning ( \
				"%s::%s() reported failure " \
				"without setting its GError", \
				G_OBJECT_TYPE_NAME (object), \
				G_STRINGIFY (method)); \
		} \
	} \
	} G_STMT_END

G_END_DECLS

#endif /* CAMEL_DEBUG_H */
