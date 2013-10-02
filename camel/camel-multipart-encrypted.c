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

#include "camel-multipart-encrypted.h"

G_DEFINE_TYPE (
	CamelMultipartEncrypted,
	camel_multipart_encrypted,
	CAMEL_TYPE_MULTIPART)

static void
camel_multipart_encrypted_class_init (CamelMultipartEncryptedClass *class)
{
}

static void
camel_multipart_encrypted_init (CamelMultipartEncrypted *multipart)
{
	camel_data_wrapper_set_mime_type (
		CAMEL_DATA_WRAPPER (multipart), "multipart/encrypted");
}

/**
 * camel_multipart_encrypted_new:
 *
 * Create a new #CamelMultipartEncrypted object.
 *
 * A MultipartEncrypted should be used to store and create parts of
 * type "multipart/encrypted".
 *
 * Returns: a new #CamelMultipartEncrypted object
 **/
CamelMultipartEncrypted *
camel_multipart_encrypted_new (void)
{
	return g_object_new (CAMEL_TYPE_MULTIPART_ENCRYPTED, NULL);
}
