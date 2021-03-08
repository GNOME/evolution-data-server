/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "camel-test.h"
#include "session.h"

static gchar test_msg[] = "Since we need to make sure that\nFrom lines work okay, we should test that"
"as well as test 8bit chars and other fun stuff? 8bit chars: Dražen Kačar\n\nOkay, I guess that covers"
"the basics at least...\n";

#define CAMEL_PGP_SESSION_TYPE     (camel_pgp_session_get_type ())
#define CAMEL_PGP_SESSION(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), CAMEL_PGP_SESSION_TYPE, CamelPgpSession))
#define CAMEL_PGP_SESSION_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), CAMEL_PGP_SESSION_TYPE, CamelPgpSessionClass))
#define CAMEL_PGP_IS_SESSION(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), CAMEL_PGP_SESSION_TYPE))

typedef struct _CamelPgpSession {
	CamelSession parent_object;

} CamelPgpSession;

typedef struct _CamelPgpSessionClass {
	CamelSessionClass parent_class;

} CamelPgpSessionClass;

static gchar *get_password (CamelSession *session, const gchar *prompt,
			   guint32 flags,
			   CamelService *service, const gchar *item,
			   GError **error);

static void
init (CamelPgpSession *session)
{
	;
}

static void
class_init (CamelPgpSessionClass *camel_pgp_session_class)
{
	CamelSessionClass *camel_session_class =
		CAMEL_SESSION_CLASS (camel_pgp_session_class);

	camel_session_class->get_password = get_password;
}

static GType
camel_pgp_session_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = camel_type_register (
			CAMEL_TYPE_TEST_SESSION,
			"CamelPgpSession",
			sizeof (CamelPgpSession),
			sizeof (CamelPgpSessionClass),
			(GClassInitFunc) class_init,
			NULL,
			(GInstanceInitFunc) init,
			NULL);

	return type;
}

static gchar *
get_password (CamelSession *session,
              const gchar *prompt,
              guint32 flags,
              CamelService *service,
              const gchar *item,
              GError **error)
{
	return g_strdup ("no.secret");
}

static CamelSession *
camel_pgp_session_new (const gchar *path)
{
	CamelSession *session;

	session = g_object_new (CAMEL_TYPE_PGP_SESSION, NULL);
	camel_session_construct (session, path);

	return session;
}

gint main (gint argc, gchar **argv)
{
	CamelSession *session;
	CamelCipherContext *ctx;
	GError **error;
	CamelCipherValidity *valid;
	CamelMimePart *mime_part;
	CamelMultipartSigned *mps;
	CamelMultipartEncrypted *mpe;
	GPtrArray *recipients;
	gint ret;

	camel_test_init (argc, argv);

	/* clear out any camel-test data */
	system ("/bin/rm -rf /tmp/camel-test");
	system ("/bin/mkdir /tmp/camel-test");
	setenv ("GNUPGHOME", "/tmp/camel-test/.gnupg", 1);

	/* import the gpg keys */
	if ((ret = system ("gpg < /dev/null > /dev/null 2>&1")) == -1)
		return 77;
	else if (WEXITSTATUS (ret) == 127)
		return 77;

	system ("gpg --import ../data/camel-test.gpg.pub > /dev/null 2>&1");
	system ("gpg --import ../data/camel-test.gpg.sec > /dev/null 2>&1");

	session = camel_pgp_session_new ("/tmp/camel-test");

	ex = camel_exception_new ();

	ctx = camel_gpg_context_new (session);
	camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

	camel_test_start ("Test of PGP/MIME functions");

	mime_part = camel_mime_part_new ();
	camel_mime_part_set_content (mime_part, test_msg, strlen (test_msg), "text/plain");
	camel_mime_part_set_description (mime_part, "Test of PGP/MIME multipart/signed stuff");

	camel_test_push ("PGP/MIME signing");
	mps = camel_multipart_signed_new ();
	camel_multipart_signed_sign (mps, ctx, mime_part, "no.user@no.domain", CAMEL_CIPHER_HASH_SHA256, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	camel_test_pull ();

	g_object_unref (mime_part);
	camel_exception_clear (ex);

	camel_test_push ("PGP/MIME verify");
	valid = camel_multipart_signed_verify (mps, ctx, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	check_msg (camel_cipher_validity_get_valid (valid), "%s", camel_cipher_validity_get_description (valid));
	camel_cipher_validity_free (valid);
	camel_test_pull ();

	g_object_unref (mps);
	camel_exception_clear (ex);

	mime_part = camel_mime_part_new ();
	camel_mime_part_set_content (mime_part, test_msg, strlen (test_msg), "text/plain");
	camel_mime_part_set_description (mime_part, "Test of PGP/MIME multipart/encrypted stuff");

	camel_test_push ("PGP/MIME encrypt");
	recipients = g_ptr_array_new ();
	g_ptr_array_add (recipients, "no.user@no.domain");

	mpe = camel_multipart_encrypted_new ();
	camel_multipart_encrypted_encrypt (mpe, mime_part, ctx, "no.user@no.domain", recipients, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	g_ptr_array_free (recipients, TRUE);
	camel_test_pull ();

	camel_exception_clear (ex);
	g_object_unref (mime_part);

	camel_test_push ("PGP/MIME decrypt");
	mime_part = camel_multipart_encrypted_decrypt (mpe, ctx, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	g_object_unref (mime_part);
	g_object_unref (mpe);
	camel_test_pull ();

	g_object_unref (ctx);
	g_object_unref (session);

	camel_test_end ();

	return 0;
}
