/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_ICONV_H
#define CAMEL_ICONV_H

#include <sys/types.h>
#include <glib.h>

G_BEGIN_DECLS

const gchar *	camel_iconv_locale_charset	(void);
const gchar *	camel_iconv_locale_language	(void);

const gchar *	camel_iconv_charset_name	(const gchar *charset);
const gchar *	camel_iconv_charset_language	(const gchar *charset);

GIConv		camel_iconv_open		(const gchar *to,
						 const gchar *from);
gsize		camel_iconv			(GIConv cd,
						 const gchar **inbuf,
						 gsize *inleft,
						 gchar **outbuf,
						 gsize *outleft);
void		camel_iconv_close		(GIConv cd);

G_END_DECLS

#endif /* CAMEL_ICONV_H */
