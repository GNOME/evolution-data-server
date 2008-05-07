/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright 2001 Ximian, Inc. (www.ximian.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-mime-utils.h"
#include "camel-sasl-cram-md5.h"
#include "camel-service.h"

CamelServiceAuthType camel_sasl_cram_md5_authtype = {
	N_("CRAM-MD5"),

	N_("This option will connect to the server using a "
	   "secure CRAM-MD5 password, if the server supports it."),

	"CRAM-MD5",
	TRUE
};

static CamelSaslClass *parent_class = NULL;

/* Returns the class for a CamelSaslCramMd5 */
#define CSCM_CLASS(so) CAMEL_SASL_CRAM_MD5_CLASS (CAMEL_OBJECT_GET_CLASS (so))

static GByteArray *cram_md5_challenge (CamelSasl *sasl, GByteArray *token, CamelException *ex);

static void
camel_sasl_cram_md5_class_init (CamelSaslCramMd5Class *camel_sasl_cram_md5_class)
{
	CamelSaslClass *camel_sasl_class = CAMEL_SASL_CLASS (camel_sasl_cram_md5_class);
	
	parent_class = CAMEL_SASL_CLASS (camel_type_get_global_classfuncs (camel_sasl_get_type ()));
	
	/* virtual method overload */
	camel_sasl_class->challenge = cram_md5_challenge;
}

CamelType
camel_sasl_cram_md5_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;
	
	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_sasl_get_type (),
					    "CamelSaslCramMd5",
					    sizeof (CamelSaslCramMd5),
					    sizeof (CamelSaslCramMd5Class),
					    (CamelObjectClassInitFunc) camel_sasl_cram_md5_class_init,
					    NULL,
					    NULL,
					    NULL);
	}
	
	return type;
}

/* CRAM-MD5 algorithm:
 * MD5 ((passwd XOR opad), MD5 ((passwd XOR ipad), timestamp))
 */

static GByteArray *
cram_md5_challenge (CamelSasl *sasl, GByteArray *token, CamelException *ex)
{
	GChecksum *checksum;
	guint8 *digest;
	gsize length;
	char *passwd;
	const gchar *hex;
	GByteArray *ret = NULL;
	guchar ipad[64];
	guchar opad[64];
	int i, pw_len;

	/* Need to wait for the server */
	if (!token)
		return NULL;

	g_return_val_if_fail (sasl->service->url->passwd != NULL, NULL);

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	memset (ipad, 0, sizeof (ipad));
	memset (opad, 0, sizeof (opad));

	passwd = sasl->service->url->passwd;
	pw_len = strlen (passwd);
	if (pw_len <= 64) {
		memcpy (ipad, passwd, pw_len);
		memcpy (opad, passwd, pw_len);
	} else {
		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, (guchar *) passwd, pw_len);
		g_checksum_get_digest (checksum, digest, &length);
		g_checksum_free (checksum);

		memcpy (ipad, digest, length);
		memcpy (opad, digest, length);
	}

	for (i = 0; i < 64; i++) {
		ipad[i] ^= 0x36;
		opad[i] ^= 0x5c;
	}

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *) ipad, sizeof (ipad));
	g_checksum_update (checksum, (guchar *) token->data, token->len);
	g_checksum_get_digest (checksum, digest, &length);
	g_checksum_free (checksum);

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *) opad, sizeof (opad));
	g_checksum_update (checksum, (guchar *) digest, length);

	/* String is owned by the checksum. */
	hex = g_checksum_get_string (checksum);

	ret = g_byte_array_new ();
	g_byte_array_append (ret, (guint8 *) sasl->service->url->user, strlen (sasl->service->url->user));
	g_byte_array_append (ret, (guint8 *) " ", 1);
	g_byte_array_append (ret, (guint8 *) hex, strlen (hex));

	g_checksum_free (checksum);

	sasl->authenticated = TRUE;

	return ret;
}
