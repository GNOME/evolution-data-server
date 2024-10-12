/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors: Jeffrey Stedfast <fejj@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "camel-multipart-encrypted.h"

G_DEFINE_TYPE (
	CamelMultipartEncrypted,
	camel_multipart_encrypted,
	CAMEL_TYPE_MULTIPART)

static gchar *
multipart_encrypted_generate_preview (CamelMultipart *multipart,
				      CamelGeneratePreviewFunc func,
				      gpointer user_data)
{
	return g_strdup (_("Encrypted content"));
}

static void
camel_multipart_encrypted_class_init (CamelMultipartEncryptedClass *klass)
{
	CamelMultipartClass *multipart_class;

	multipart_class = CAMEL_MULTIPART_CLASS (klass);
	multipart_class->generate_preview = multipart_encrypted_generate_preview;
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
