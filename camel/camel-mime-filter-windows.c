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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "camel-charset-map.h"
#include "camel-mime-filter-windows.h"

#define d(x)
#define w(x)

static CamelMimeFilterClass *parent_class = NULL;

struct _CamelMimeFilterWindowsPrivate {
	gboolean is_windows;
	gchar *claimed_charset;
};

static void
mime_filter_windows_finalize (CamelMimeFilterWindows *mime_filter)
{
	g_free (mime_filter->priv->claimed_charset);
	g_free (mime_filter->priv);
}

static void
mime_filter_windows_filter (CamelMimeFilter *mime_filter,
                            const gchar *in,
                            gsize len,
                            gsize prespace,
                            gchar **out,
                            gsize *outlen,
                            gsize *outprespace)
{
	CamelMimeFilterWindowsPrivate *priv;
	register guchar *inptr;
	guchar *inend;

	priv = CAMEL_MIME_FILTER_WINDOWS (mime_filter)->priv;

	if (!priv->is_windows) {
		inptr = (guchar *) in;
		inend = inptr + len;

		while (inptr < inend) {
			register guchar c = *inptr++;

			if (c >= 128 && c <= 159) {
				w(g_warning ("Encountered Windows charset masquerading as %s",
					     priv->claimed_charset));
				priv->is_windows = TRUE;
				break;
			}
		}
	}

	*out = (gchar *) in;
	*outlen = len;
	*outprespace = prespace;
}

static void
mime_filter_windows_complete (CamelMimeFilter *mime_filter,
                              const gchar *in,
                              gsize len,
                              gsize prespace,
                              gchar **out,
                              gsize *outlen,
                              gsize *outprespace)
{
	mime_filter_windows_filter (
		mime_filter, in, len, prespace,
		out, outlen, outprespace);
}

static void
mime_filter_windows_reset (CamelMimeFilter *mime_filter)
{
	CamelMimeFilterWindowsPrivate *priv;

	priv = CAMEL_MIME_FILTER_WINDOWS (mime_filter)->priv;

	priv->is_windows = FALSE;
}

static void
camel_mime_filter_windows_class_init (CamelMimeFilterWindowsClass *class)
{
	CamelMimeFilterClass *mime_filter_class;

	parent_class = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_windows_filter;
	mime_filter_class->complete = mime_filter_windows_complete;
	mime_filter_class->reset = mime_filter_windows_reset;
}

static void
camel_mime_filter_windows_init (CamelMimeFilterWindows *mime_filter)
{
	mime_filter->priv = g_new0 (CamelMimeFilterWindowsPrivate, 1);
}

CamelType
camel_mime_filter_windows_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type (),
					    "CamelMimeFilterWindows",
					    sizeof (CamelMimeFilterWindows),
					    sizeof (CamelMimeFilterWindowsClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_windows_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_windows_init,
					    (CamelObjectFinalizeFunc) mime_filter_windows_finalize);
	}

	return type;
}

/**
 * camel_mime_filter_windows_new:
 * @claimed_charset: ISO charset name
 *
 * Create a new #CamelMimeFilterWindows object that will analyse
 * whether or not the text is really encoded in @claimed_charset.
 *
 * Returns: a new #CamelMimeFilter object
 **/
CamelMimeFilter *
camel_mime_filter_windows_new (const gchar *claimed_charset)
{
	CamelMimeFilter *filter;
	CamelMimeFilterWindowsPrivate *priv;

	g_return_val_if_fail (claimed_charset != NULL, NULL);

	filter = CAMEL_MIME_FILTER (camel_object_new (camel_mime_filter_windows_get_type ()));
	priv = CAMEL_MIME_FILTER_WINDOWS (filter)->priv;

	priv->claimed_charset = g_strdup (claimed_charset);

	return filter;
}

/**
 * camel_mime_filter_windows_is_windows_charset:
 * @filter: a #CamelMimeFilterWindows object
 *
 * Get whether or not the textual content filtered by @filter is
 * really in a Microsoft Windows charset rather than the claimed ISO
 * charset.
 *
 * Returns: %TRUE if the text was found to be in a Microsoft Windows
 * CP125x charset or %FALSE otherwise.
 **/
gboolean
camel_mime_filter_windows_is_windows_charset (CamelMimeFilterWindows *filter)
{
	g_return_val_if_fail (CAMEL_IS_MIME_FILTER_WINDOWS (filter), FALSE);

	return filter->priv->is_windows;
}

/**
 * camel_mime_filter_windows_real_charset:
 * @filter: a #CamelMimeFilterWindows object
 *
 * Get the name of the actual charset used to encode the textual
 * content filtered by @filter (it will either be the original
 * claimed_charset passed in at creation time or the Windows-CP125x
 * equivalent).
 *
 * Returns: the name of the actual charset
 **/
const gchar *
camel_mime_filter_windows_real_charset (CamelMimeFilterWindows *filter)
{
	const gchar *charset;

	g_return_val_if_fail (CAMEL_IS_MIME_FILTER_WINDOWS (filter), NULL);

	charset = filter->priv->claimed_charset;

	if (filter->priv->is_windows)
		charset = camel_charset_iso_to_windows (charset);

	return charset;
}
