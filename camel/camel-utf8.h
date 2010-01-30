/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#ifndef _CAMEL_UTF8_H
#define _CAMEL_UTF8_H

G_BEGIN_DECLS

void camel_utf8_putc(guchar **ptr, guint32 c);
guint32 camel_utf8_getc(const guchar **ptr);
guint32 camel_utf8_getc_limit (const guchar **ptr, const guchar *end);

/* utility func for utf8 gstrings */
void g_string_append_u(GString *out, guint32 c);

/* convert utf7 to/from utf8, actually this is modified IMAP utf7 */
gchar *camel_utf7_utf8(const gchar *ptr);
gchar *camel_utf8_utf7(const gchar *ptr);

/* convert ucs2 to/from utf8 */
gchar *camel_utf8_ucs2(const gchar *ptr);
gchar *camel_ucs2_utf8(const gchar *ptr);

/* make valid utf8 string */
gchar *camel_utf8_make_valid (const gchar *text);

G_END_DECLS

#endif /* _CAMEL_UTF8_H */
