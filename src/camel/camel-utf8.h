/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_UTF8_H
#define CAMEL_UTF8_H

#include <glib.h>

G_BEGIN_DECLS

void camel_utf8_putc (guchar **ptr, guint32 c);
guint32 camel_utf8_getc (const guchar **ptr);
guint32 camel_utf8_getc_limit (const guchar **ptr, const guchar *end);

/* convert utf7 to/from utf8, actually this is modified IMAP utf7 */
gchar *camel_utf7_utf8 (const gchar *ptr);
gchar *camel_utf8_utf7 (const gchar *ptr);

/* convert ucs2 to/from utf8 */
gchar *camel_utf8_ucs2 (const gchar *ptr);
gchar *camel_ucs2_utf8 (const gchar *ptr);

/* make valid utf8 string */
gchar *camel_utf8_make_valid (const gchar *text);
gchar *camel_utf8_make_valid_len (const gchar *text,
				  gssize text_len);

G_END_DECLS

#endif /* CAMEL_UTF8_H */
