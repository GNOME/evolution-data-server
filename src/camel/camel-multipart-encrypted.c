/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

/**
 * CamelMultipartEncrypted:
 *
 * A multipart/encrypted MIME container.
 *
 * #CamelMultipartEncrypted is a #CamelMultipart subclass for handling
 * multipart/encrypted MIME parts as defined in RFC 1847. It holds the
 * encryption protocol part and the encrypted body together, and integrates
 * with #CamelCipherContext to decrypt or encrypt message content.
 **/

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
