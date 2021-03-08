/*
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
 */

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "camel-test.h"

#define CAMEL_TEST_SESSION_TYPE     (camel_test_session_get_type ())
#define CAMEL_TEST_SESSION(obj)     (G_TYPE_CHECK_INSTANCE_CAST((obj), CAMEL_TEST_SESSION_TYPE, CamelTestSession))
#define CAMEL_TEST_SESSION_CLASS(k) (G_TYPE_CHECK_CLASS_CAST ((k), CAMEL_TEST_SESSION_TYPE, CamelTestSessionClass))
#define CAMEL_TEST_IS_SESSION(o)    (G_TYPE_CHECK_INSTANCE_TYPE((o), CAMEL_TEST_SESSION_TYPE))

typedef struct _CamelTestSession {
	CamelSession parent_object;

} CamelTestSession;

typedef struct _CamelTestSessionClass {
	CamelSessionClass parent_class;

} CamelTestSessionClass;

static gchar *get_password (CamelSession *session, const gchar *prompt,
			   guint32 flags, CamelService *service,
			   const gchar *item, GError **error);

static void
init (CamelTestSession *session)
{
	;
}

static void
class_init (CamelTestSessionClass *camel_test_session_class)
{
	CamelSessionClass *camel_session_class =
		CAMEL_SESSION_CLASS (camel_test_session_class);

	camel_session_class->get_password = get_password;
}

static GType
camel_test_session_get_type (void)
{
	static GType type = G_TYPE_INVALID;

	if (G_UNLIKELY (type == G_TYPE_INVALID))
		type = camel_type_register (
			CAMEL_TEST_SESSION_TYPE,
			"CamelTestSession",
			sizeof (CamelTestSession),
			sizeof (CamelTestSessionClass),
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
	return g_strdup ("S/MIME v3 is rfc263x, now go and read them.");
}

static CamelSession *
camel_test_session_new (const gchar *path)
{
	CamelSession *session;

	session = g_object_new (CAMEL_TYPE_TEST_SESSION, NULL);
	camel_session_construct (session, path);

	return session;
}

gint main (gint argc, gchar **argv)
{
	CamelSession *session;
	CamelSMimeContext *ctx;
	GError **error;
	CamelCipherValidity *valid;
	CamelStream *stream1, *stream2, *stream3;
	GPtrArray *recipients;
	GByteArray *buf;
	gchar *before, *after;

	camel_test_init (argc, argv);

	ex = camel_exception_new ();

	/* clear out any camel-test data */
	system ("/bin/rm -rf /tmp/camel-test");

	session = camel_test_session_new ("/tmp/camel-test");

	ctx = camel_smime_context_new (session);

	camel_test_start ("Test of S/MIME PKCS7 functions");

	stream1 = camel_stream_mem_new ();
	camel_stream_write (stream1, "Hello, I am a test stream.", 25);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	stream2 = camel_stream_mem_new ();

	camel_test_push ("PKCS7 signing");
	camel_smime_sign (
		ctx, "smime@xtorshun.org", CAMEL_CIPHER_HASH_SHA256,
		stream1, stream2, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	camel_test_pull ();

	camel_exception_clear (ex);

	camel_test_push ("PKCS7 verify");
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);
	g_seekable_seek (G_SEEKABLE (stream2), 0, G_SEEK_SET, NULL, NULL);
	valid = camel_smime_verify (ctx, CAMEL_CIPHER_HASH_SHA256, stream1, stream2, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	check_msg (camel_cipher_validity_get_valid (valid), "%s", camel_cipher_validity_get_description (valid));
	camel_cipher_validity_free (valid);
	camel_test_pull ();

	g_object_unref (stream1);
	g_object_unref (stream2);

	stream1 = camel_stream_mem_new ();
	stream2 = camel_stream_mem_new ();
	stream3 = camel_stream_mem_new ();

	camel_stream_write (stream1, "Hello, I am a test of encryption/decryption.", 44);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	camel_exception_clear (ex);

	camel_test_push ("PKCS7 encrypt");
	recipients = g_ptr_array_new ();
	g_ptr_array_add (recipients, "smime@xtorshun.org");
	camel_smime_encrypt (
		ctx, FALSE, "smime@xtorshun.org", recipients,
		stream1, stream2, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	g_ptr_array_free (recipients, TRUE);
	camel_test_pull ();

	g_seekable_seek (G_SEEKABLE (stream2), 0, G_SEEK_SET, NULL, NULL);
	camel_exception_clear (ex);

	camel_test_push ("PKCS7 decrypt");
	camel_smime_decrypt (ctx, stream2, stream3, ex);
	check_msg (!camel_exception_is_set (ex), "%s", camel_exception_get_description (ex));
	buf = CAMEL_STREAM_MEM (stream1)->buffer;
	before = g_strndup (buf->data, buf->len);
	buf = CAMEL_STREAM_MEM (stream3)->buffer;
	after = g_strndup (buf->data, buf->len);
	check_msg (string_equal (before, after), "before = '%s', after = '%s'", before, after);
	g_free (before);
	g_free (after);
	camel_test_pull ();

	g_object_unref (ctx);
	g_object_unref (session);

	camel_test_end ();

	return 0;
}
