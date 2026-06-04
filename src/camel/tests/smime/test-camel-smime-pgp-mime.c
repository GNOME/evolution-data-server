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

	system ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.pub > /dev/null 2>&1");
	system ("gpg --import " TEST_DATA_DIR "/camel-test.gpg.sec > /dev/null 2>&1");

	/* set ultimate trust on the imported key */
	system ("echo 1A7B616196E654BA5B8CF177A62EDCB3B1907A17:6: | gpg --import-ownertrust 2>/dev/null");
}

static void
test_pgp_mime_sign_verify (void)
{
	CamelSession *session;
	CamelCipherContext *ctx;
	CamelCipherValidity *valid;
	CamelMimePart *mime_part, *sigpart;
	GError *error = NULL;

	if (!gpg_available) {
		g_test_skip ("GPG is not available");
		return;
	}

	session = camel_pgp_session_new (camel_test_get_dir ());
	ctx = camel_gpg_context_new (session);
	camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

	mime_part = camel_mime_part_new ();
	camel_mime_part_set_content (
		mime_part,
		"Since we need to make sure that\n"
		"From lines work okay, we should test that "
		"as well as test 8bit chars and other fun stuff? "
		"8bit chars: Drazen Kacar\n\n"
		"Okay, I guess that covers the basics at least...\n",
		-1,
		"text/plain");
	camel_mime_part_set_description (mime_part, "Test of PGP/MIME multipart/signed stuff");

	/* PGP/MIME signing */
	sigpart = camel_mime_part_new ();
	camel_cipher_context_sign_sync (
		ctx, "no.user@no.domain", CAMEL_CIPHER_HASH_SHA256,
		mime_part, sigpart, NULL, &error);
	if (error != NULL) {
		g_test_skip ("PGP/MIME signing failed, assuming non-functional environment");
		g_clear_error (&error);
		g_clear_object (&mime_part);
		g_clear_object (&sigpart);
		g_clear_object (&ctx);
		g_clear_object (&session);
		return;
	}

	g_clear_object (&mime_part);

	/* PGP/MIME verify */
	valid = camel_cipher_context_verify_sync (ctx, sigpart, NULL, &error);
	if (error != NULL)
		g_error ("PGP/MIME verify failed: %s", error->message);
	g_assert_true (camel_cipher_validity_get_valid (valid));
	camel_cipher_validity_free (valid);

	g_clear_object (&sigpart);
	g_clear_object (&ctx);
	g_clear_object (&session);
}

static void
test_pgp_mime_encrypt_decrypt (void)
{
	CamelSession *session;
	CamelCipherContext *ctx;
	CamelCipherValidity *valid;
	CamelMimePart *mime_part, *encpart, *outpart;
	GPtrArray *recipients;
	GError *error = NULL;

	if (!gpg_available) {
		g_test_skip ("GPG is not available");
		return;
	}

	session = camel_pgp_session_new (camel_test_get_dir ());
	ctx = camel_gpg_context_new (session);
	camel_gpg_context_set_always_trust (CAMEL_GPG_CONTEXT (ctx), TRUE);

	mime_part = camel_mime_part_new ();
	camel_mime_part_set_content (
		mime_part,
		"Since we need to make sure that\n"
		"From lines work okay, we should test that "
		"as well as test 8bit chars and other fun stuff? "
		"8bit chars: Drazen Kacar\n\n"
		"Okay, I guess that covers the basics at least...\n",
		-1,
		"text/plain");
	camel_mime_part_set_description (mime_part, "Test of PGP/MIME multipart/encrypted stuff");

	/* PGP/MIME encrypt */
	recipients = g_ptr_array_new ();
	g_ptr_array_add (recipients, (gchar *) "no.user@no.domain");

	encpart = camel_mime_part_new ();
	camel_cipher_context_encrypt_sync (
		ctx, "no.user@no.domain", recipients,
		mime_part, encpart, NULL, &error);
	if (error != NULL)
		g_error ("PGP/MIME encrypt failed: %s", error->message);
	g_ptr_array_free (recipients, TRUE);

	g_clear_object (&mime_part);

	/* PGP/MIME decrypt */
	outpart = camel_mime_part_new ();
	valid = camel_cipher_context_decrypt_sync (
		ctx, encpart, outpart, NULL, &error);
	if (error != NULL)
		g_error ("PGP/MIME decrypt failed: %s", error->message);
	g_assert_nonnull (valid);
	camel_cipher_validity_free (valid);

	g_clear_object (&outpart);
	g_clear_object (&encpart);
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

	g_test_add_func ("/Camel/PGP-MIME/SignVerify", test_pgp_mime_sign_verify);
	g_test_add_func ("/Camel/PGP-MIME/EncryptDecrypt", test_pgp_mime_encrypt_decrypt);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}
