/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

#include "camel-test.h"
#include "session.h"

#define CAMEL_TYPE_PGP_SESSION     (camel_pgp_session_get_type ())
#define CAMEL_PGP_SESSION(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), CAMEL_TYPE_PGP_SESSION, CamelPgpSession))
#define CAMEL_PGP_SESSION_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), CAMEL_TYPE_PGP_SESSION, CamelPgpSessionClass))
#define CAMEL_PGP_IS_SESSION(o)    (G_TYPE_CHECK_INSTANCE_TYPE ((o), CAMEL_TYPE_PGP_SESSION))

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

static gboolean gpg_available = FALSE;

static void
setup_gpg_environment (void)
{
	gchar *gnupg_home;
	gchar *private_keys_dir;
	gint status;

	/* clear out any camel-test data */
	/*
	 * You need to add the private-keys-v1.d folder for this
	 * to work on newer versions of gnupg
	 */
	private_keys_dir = g_build_filename (camel_test_get_dir (), ".gnupg", "private-keys-v1.d", NULL);
	g_mkdir_with_parents (private_keys_dir, 0700);
	g_free (private_keys_dir);
	gnupg_home = g_build_filename (camel_test_get_dir (), ".gnupg", NULL);
	setenv ("GNUPGHOME", gnupg_home, 1);
	g_free (gnupg_home);

	/* import the gpg keys */
	if ((status = system ("gpg < /dev/null > /dev/null 2>&1")) == -1) {
		gpg_available = FALSE;
		return;
	} else if (WEXITSTATUS (status) == 127) {
		gpg_available = FALSE;
		return;
	}

	gpg_available = TRUE;

	g_message ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.pub > /dev/null 2>&1");
	system ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.pub > /dev/null 2>&1");
	g_message ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.sec > /dev/null 2>&1");
	system ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.sec > /dev/null 2>&1");
}

static void
test_pgp_sign_verify (void)
{
	CamelSession *session;
	CamelCipherContext *ctx;
	CamelCipherValidity *valid;
	CamelStream *stream1;
	CamelMimePart *sigpart, *conpart;
	CamelDataWrapper *dw;
	GError *error = NULL;

	if (!gpg_available) {
		g_test_skip ("GPG is not available");
		return;
	}

	session = camel_pgp_session_new (camel_test_get_dir ());
	ctx = camel_gpg_context_new (session);
	camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

	stream1 = camel_stream_mem_new ();
	camel_stream_write (
		stream1, "Hello, I am a test stream.\n", 27, NULL, NULL);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	conpart = camel_mime_part_new ();
	dw = camel_data_wrapper_new ();
	camel_data_wrapper_construct_from_stream_sync (
		dw, stream1, NULL, NULL);
	camel_medium_set_content ((CamelMedium *) conpart, dw);
	g_clear_object (&stream1);
	g_clear_object (&dw);

	sigpart = camel_mime_part_new ();

	/* PGP signing */
	camel_cipher_context_sign_sync (
		ctx, "no.user@no.domain", CAMEL_CIPHER_HASH_SHA256,
		conpart, sigpart, NULL, &error);
	if (error != NULL) {
		g_test_skip ("PGP signing failed, assuming non-functional environment");
		g_clear_error (&error);
		g_clear_object (&conpart);
		g_clear_object (&sigpart);
		g_clear_object (&ctx);
		g_clear_object (&session);
		return;
	}

	/* PGP verify untrusted */
	valid = camel_cipher_context_verify_sync (ctx, sigpart, NULL, &error);
	if (error != NULL)
		g_error ("PGP verify failed: %s", error->message);
	/* We do not trust the keys yet, so camel_cipher_validity_get_valid () will return false */
	g_assert_false (camel_cipher_validity_get_valid (valid));
	camel_cipher_validity_free (valid);

	/* PGP verify trusted - set the ultimate trust on the imported key */
	system ("echo 1A7B616196E654BA5B8CF177A62EDCB3B1907A17:6: | gpg --import-ownertrust 2>/dev/null");
	valid = camel_cipher_context_verify_sync (ctx, sigpart, NULL, &error);
	if (error != NULL)
		g_error ("PGP verify (trusted) failed: %s", error->message);
	g_assert_true (camel_cipher_validity_get_valid (valid));
	camel_cipher_validity_free (valid);

	g_clear_object (&conpart);
	g_clear_object (&sigpart);
	g_clear_object (&ctx);
	g_clear_object (&session);
}

static void
test_pgp_encrypt_decrypt (void)
{
	CamelSession *session;
	CamelCipherContext *ctx;
	CamelCipherValidity *valid;
	CamelStream *stream1, *stream2;
	GByteArray *buffer1, *buffer2;
	CamelMimePart *conpart, *encpart, *outpart;
	CamelDataWrapper *dw;
	GPtrArray *recipients;
	gchar *before, *after;
	GError *error = NULL;

	if (!gpg_available) {
		g_test_skip ("GPG is not available");
		return;
	}

	session = camel_pgp_session_new (camel_test_get_dir ());
	ctx = camel_gpg_context_new (session);
	camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

	/* Set up trust for the key */
	system ("echo 1A7B616196E654BA5B8CF177A62EDCB3B1907A17:6: | gpg --import-ownertrust 2>/dev/null");

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
	g_clear_object (&stream1);
	g_clear_object (&dw);

	encpart = camel_mime_part_new ();

	/* PGP encrypt */
	recipients = g_ptr_array_new ();
	g_ptr_array_add (recipients, (guint8 *) "no.user@no.domain");
	camel_cipher_context_encrypt_sync (
		ctx, "no.user@no.domain", recipients,
		conpart, encpart, NULL, &error);
	if (error != NULL)
		g_error ("PGP encrypt failed: %s", error->message);
	g_ptr_array_free (recipients, TRUE);

	/* PGP decrypt */
	outpart = camel_mime_part_new ();
	valid = camel_cipher_context_decrypt_sync (
		ctx, encpart, outpart, NULL, &error);
	if (error != NULL)
		g_error ("PGP decrypt failed: %s", error->message);
	g_assert_cmpuint (valid->encrypt.status, ==, CAMEL_CIPHER_VALIDITY_ENCRYPT_ENCRYPTED);

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
	if (!string_equal (before, after))
		g_error ("before = '%s', after = '%s'", before, after);
	g_free (before);
	g_free (after);

	camel_cipher_validity_free (valid);
	g_clear_object (&stream1);
	g_clear_object (&stream2);
	g_clear_object (&conpart);
	g_clear_object (&encpart);
	g_clear_object (&outpart);
	g_clear_object (&ctx);
	g_clear_object (&session);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	g_test_init (&argc, &argv, NULL);
	camel_test_init ();

	setup_gpg_environment ();

	g_test_add_func ("/Camel/PGP/SignVerify", test_pgp_sign_verify);
	g_test_add_func ("/Camel/PGP/EncryptDecrypt", test_pgp_encrypt_decrypt);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}
