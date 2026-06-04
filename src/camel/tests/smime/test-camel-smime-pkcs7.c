/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"
#include "session.h"

static void
test_pkcs7_sign_verify (void)
{
	CamelSession *session;
	CamelCipherContext *ctx;
	CamelCipherValidity *valid;
	CamelStream *stream1;
	CamelMimePart *conpart, *sigpart;
	CamelDataWrapper *dw;
	GError *error = NULL;

	session = camel_test_session_new (camel_test_get_dir ());
	ctx = camel_smime_context_new (session);

	stream1 = camel_stream_mem_new ();
	camel_stream_write (stream1, "Hello, I am a test stream.", 25, NULL, NULL);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	conpart = camel_mime_part_new ();
	dw = camel_data_wrapper_new ();
	camel_data_wrapper_construct_from_stream_sync (
		dw, stream1, NULL, NULL);
	camel_medium_set_content ((CamelMedium *) conpart, dw);
	g_clear_object (&stream1);
	g_clear_object (&dw);

	sigpart = camel_mime_part_new ();

	/* PKCS7 signing */
	camel_cipher_context_sign_sync (
		ctx, "smime@xtorshun.org", CAMEL_CIPHER_HASH_SHA256,
		conpart, sigpart, NULL, &error);
	if (error != NULL)
		g_error ("PKCS7 sign failed: %s", error->message);

	/* PKCS7 verify */
	valid = camel_cipher_context_verify_sync (ctx, sigpart, NULL, &error);
	if (error != NULL)
		g_error ("PKCS7 verify failed: %s", error->message);
	g_assert_true (camel_cipher_validity_get_valid (valid));
	camel_cipher_validity_free (valid);

	g_clear_object (&conpart);
	g_clear_object (&sigpart);
	g_clear_object (&ctx);
	g_clear_object (&session);
}

static void
test_pkcs7_encrypt_decrypt (void)
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

	session = camel_test_session_new (camel_test_get_dir ());
	ctx = camel_smime_context_new (session);

	stream1 = camel_stream_mem_new ();
	camel_stream_write (
		stream1, "Hello, I am a test of encryption/decryption.",
		44, NULL, NULL);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	conpart = camel_mime_part_new ();
	dw = camel_data_wrapper_new ();
	camel_data_wrapper_construct_from_stream_sync (
		dw, stream1, NULL, NULL);
	camel_medium_set_content ((CamelMedium *) conpart, dw);
	g_clear_object (&stream1);
	g_clear_object (&dw);

	encpart = camel_mime_part_new ();

	/* PKCS7 encrypt */
	recipients = g_ptr_array_new ();
	g_ptr_array_add (recipients, (gchar *) "smime@xtorshun.org");
	camel_cipher_context_encrypt_sync (
		ctx, "smime@xtorshun.org", recipients,
		conpart, encpart, NULL, &error);
	if (error != NULL)
		g_error ("PKCS7 encrypt failed: %s", error->message);
	g_ptr_array_free (recipients, TRUE);

	/* PKCS7 decrypt */
	outpart = camel_mime_part_new ();
	valid = camel_cipher_context_decrypt_sync (
		ctx, encpart, outpart, NULL, &error);
	if (error != NULL)
		g_error ("PKCS7 decrypt failed: %s", error->message);
	g_assert_nonnull (valid);

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

	/* clear out any camel-test data */

	g_test_add_func ("/Camel/PKCS7/SignVerify", test_pkcs7_sign_verify);
	g_test_add_func ("/Camel/PKCS7/EncryptDecrypt", test_pkcs7_encrypt_decrypt);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}
