/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-iconv.h
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors:
 *   Michael Zucchi <notzed@ximian.com>
 *   Jeffrey Stedfast <fejj@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/* This API has been moved to Camel. */

#ifndef _E_ICONV_H_
#define _E_ICONV_H_

#ifndef EDS_DISABLE_DEPRECATED

#include <iconv.h>

G_BEGIN_DECLS

const gchar *e_iconv_charset_name(const gchar *charset);
iconv_t e_iconv_open(const gchar *oto, const gchar *ofrom);
gsize e_iconv(iconv_t cd, const gchar **inbuf, gsize *inbytesleft, gchar ** outbuf, gsize *outbytesleft);
void e_iconv_close(iconv_t ip);
const gchar *e_iconv_locale_charset(void);

/* languages */
const gchar *e_iconv_locale_language (void);
const gchar *e_iconv_charset_language (const gchar *charset);

G_END_DECLS

#endif /* EDS_DISABLE_DEPRECATED */

#endif /* _E_ICONV_H_ */
