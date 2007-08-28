/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2003 Ximian, Inc. (www.ximian.com)
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


#ifndef __CAMEL_ICONV_H__
#define __CAMEL_ICONV_H__

#include <sys/types.h>
#include <iconv.h>

G_BEGIN_DECLS

const char *camel_iconv_locale_charset (void);
const char *camel_iconv_locale_language (void);

const char *camel_iconv_charset_name (const char *charset);

const char *camel_iconv_charset_language (const char *charset);

iconv_t camel_iconv_open (const char *to, const char *from);
size_t camel_iconv (iconv_t cd, const char **inbuf, size_t *inleft, char **outbuf, size_t *outleft);
void camel_iconv_close (iconv_t cd);

G_END_DECLS

#endif /* __CAMEL_ICONV_H__ */
