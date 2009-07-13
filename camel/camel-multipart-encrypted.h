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

#ifndef __CAMEL_MULTIPART_ENCRYPTED_H__
#define __CAMEL_MULTIPART_ENCRYPTED_H__

#include <camel/camel-multipart.h>

#define CAMEL_MULTIPART_ENCRYPTED_TYPE     (camel_multipart_encrypted_get_type ())
#define CAMEL_MULTIPART_ENCRYPTED(obj)     (CAMEL_CHECK_CAST((obj), CAMEL_MULTIPART_ENCRYPTED_TYPE, CamelMultipartEncrypted))
#define CAMEL_MULTIPART_ENCRYPTED_CLASS(k) (CAMEL_CHECK_CLASS_CAST ((k), CAMEL_MULTIPART_ENCRYPTED_TYPE, CamelMultipartEncryptedClass))
#define CAMEL_IS_MULTIPART_ENCRYPTED(o)    (CAMEL_CHECK_TYPE((o), CAMEL_MULTIPART_ENCRYPTED_TYPE))

G_BEGIN_DECLS

typedef struct _CamelMultipartEncrypted CamelMultipartEncrypted;
typedef struct _CamelMultipartEncryptedClass CamelMultipartEncryptedClass;

/* 'handy' enums for getting the internal parts of the multipart */
enum {
	CAMEL_MULTIPART_ENCRYPTED_VERSION,
	CAMEL_MULTIPART_ENCRYPTED_CONTENT
};

struct _CamelMultipartEncrypted {
	CamelMultipart parent_object;

	CamelMimePart *version;
	CamelMimePart *content;
	CamelMimePart *decrypted;

	gchar *protocol;
};

struct _CamelMultipartEncryptedClass {
	CamelMultipartClass parent_class;

};

CamelType camel_multipart_encrypted_get_type (void);

CamelMultipartEncrypted *camel_multipart_encrypted_new (void);

G_END_DECLS

#endif /* __CAMEL_MULTIPART_ENCRYPTED_H__ */
