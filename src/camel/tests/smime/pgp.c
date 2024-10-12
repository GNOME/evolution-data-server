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

#define CAMEL_TYPE_PGP_SESSION     (camel_pgp_session_get_type ())
#define CAMEL_PGP_SESSION (obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), CAMEL_TYPE_PGP_SESSION, CamelPgpSession))
#define CAMEL_PGP_SESSION_CLASS (k) (G_TYPE_CHECK_CLASS_CAST ((k), CAMEL_TYPE_PGP_SESSION, CamelPgpSessionClass))
#define CAMEL_PGP_IS_SESSION (o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), CAMEL_TYPE_PGP_SESSION))

typedef struct _CamelPgpSession {
	CamelSession parent_object;

} CamelPgpSession;

typedef struct _CamelPgpSessionClass {
	CamelSessionClass parent_class;

} CamelPgpSessionClass;

GType camel_pgp_session_get_type (void);

G_DEFINE_TYPE (CamelPgpSession, camel_pgp_session, camel_test_session_get_type ())

static void
camel_pgp_session_class_init (CamelPgpSessionClass *class)
{
}

static void
camel_pgp_session_init (CamelPgpSession *session)
{
}

static CamelSession *
camel_pgp_session_new (const gchar *path)
{
	return g_object_new (
		CAMEL_TYPE_PGP_SESSION,
		"user-data-dir", path,
		"user-cache-dir", path,
		NULL);
}

gint main (gint argc, gchar **argv)
{
	CamelSession *session;
	CamelCipherContext *ctx;
	CamelCipherValidity *valid;
	CamelStream *stream1, *stream2;
	GByteArray *buffer1, *buffer2;
	struct _CamelMimePart *sigpart, *conpart, *encpart, *outpart;
	CamelDataWrapper *dw;
	GPtrArray *recipients;
	gchar *before, *after;
	gint ret;
	GError *error = NULL;

	camel_test_init (argc, argv);

	/* clear out any camel-test data */
	system ("/bin/rm -rf /tmp/camel-test");
	/*
	 * You need to add the private-keys-v1.d folder for this
	 * to work on newer versions of gnupg
	 */
	g_mkdir_with_parents ("/tmp/camel-test/.gnupg/private-keys-v1.d", 0700);
	setenv ("GNUPGHOME", "/tmp/camel-test/.gnupg/", 1);

	/* import the gpg keys */
	if ((ret = system ("gpg < /dev/null > /dev/null 2>&1")) == -1)
		return 77;
	else if (WEXITSTATUS (ret) == 127)
		return 77;

	g_message ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.pub > /dev/null 2>&1");
	system ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.pub > /dev/null 2>&1");
	g_message ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.sec > /dev/null 2>&1");
	system ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.sec > /dev/null 2>&1");

	session = camel_pgp_session_new ("/tmp/camel-test");

	ctx = camel_gpg_context_new (session);
	camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

	camel_test_start ("Test of PGP functions");

	stream1 = camel_stream_mem_new ();
	camel_stream_write (
		stream1, "Hello, I am a test stream.\n", 27, NULL, NULL);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	conpart = camel_mime_part_new ();
	dw = camel_data_wrapper_new ();
	camel_data_wrapper_construct_from_stream_sync (
		dw, stream1, NULL, NULL);
	camel_medium_set_content ((CamelMedium *) conpart, dw);
	g_object_unref (stream1);
	g_object_unref (dw);

	sigpart = camel_mime_part_new ();

	camel_test_push ("PGP signing");
	camel_cipher_context_sign_sync (
		ctx, "no.user@no.domain", CAMEL_CIPHER_HASH_SHA256,
		conpart, sigpart, NULL, &error);
	if (error != NULL) {
		printf ("PGP signing failed assuming non-functional environment\n%s", error->message);
		camel_test_pull ();
		return 77;
	}
	camel_test_pull ();

	g_clear_error (&error);

	camel_test_push ("PGP verify untrusted");
	valid = camel_cipher_context_verify_sync (ctx, sigpart, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	/* We do not trust the keys yet, so camel_cipher_validity_get_valid () will return false */
	check_msg (!camel_cipher_validity_get_valid (valid), "%s", camel_cipher_validity_get_description (valid));
	camel_cipher_validity_free (valid);
	camel_test_pull ();

	camel_test_push ("PGP verify trusted");
	/* this sets the ultimate trust on the imported key */
	system ("echo 1A7B616196E654BA5B8CF177A62EDCB3B1907A17:6: | gpg --import-ownertrust 2>/dev/null");
	valid = camel_cipher_context_verify_sync (ctx, sigpart, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	check_msg (camel_cipher_validity_get_valid (valid), "%s", camel_cipher_validity_get_description (valid));
	camel_cipher_validity_free (valid);
	camel_test_pull ();

	g_object_unref (conpart);
	g_object_unref (sigpart);

	stream1 = camel_stream_mem_new ();
	camel_stream_write (
		stream1, "Hello, I am a test of encryption/decryption.",
		44, NULL, NULL);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	conpart = camel_mime_part_new ();
	dw = camel_data_wrapper_new ();
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);
	camel_data_wrapper_construct_from_stream_sync (
		dw, stream1, NULL, NULL);
	camel_medium_set_content ((CamelMedium *) conpart, dw);
	g_object_unref (stream1);
	g_object_unref (dw);

	encpart = camel_mime_part_new ();

	g_clear_error (&error);

	camel_test_push ("PGP encrypt");
	recipients = g_ptr_array_new ();
	g_ptr_array_add (recipients, (guint8 *) "no.user@no.domain");
	camel_cipher_context_encrypt_sync (
		ctx, "no.user@no.domain", recipients,
		conpart, encpart, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	g_ptr_array_free (recipients, TRUE);
	camel_test_pull ();

	g_clear_error (&error);

	camel_test_push ("PGP decrypt");
	outpart = camel_mime_part_new ();
	valid = camel_cipher_context_decrypt_sync (
		ctx, encpart, outpart, NULL, &error);
	check_msg (error == NULL, "%s", error->message);
	check_msg (valid->encrypt.status == CAMEL_CIPHER_VALIDITY_ENCRYPT_ENCRYPTED, "%s", valid->encrypt.description);

	buffer1 = g_byte_array_new ();
	stream1 = camel_stream_mem_new_with_byte_array (buffer1);
	buffer2 = g_byte_array_new ();
	stream2 = camel_stream_mem_new_with_byte_array (buffer2);

	camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (conpart), stream1, NULL, NULL);
	camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (outpart), stream2, NULL, NULL);

	before = g_strndup ((gchar *) buffer1->data, buffer1->len);
	after = g_strndup ((gchar *) buffer2->data, buffer2->len);
	check_msg (string_equal (before, after), "before = '%s', after = '%s'", before, after);
	g_free (before);
	g_free (after);

	g_object_unref (stream1);
	g_object_unref (stream2);
	g_object_unref (conpart);
	g_object_unref (encpart);
	g_object_unref (outpart);

	camel_test_pull ();

	g_object_unref (ctx);
	g_object_unref (session);

	camel_test_end ();

	return 0;
}
