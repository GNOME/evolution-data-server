/*
 * SPDX-FileCopyrightText: (C) 2026 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <string.h>

#include "camel-test.h"

#include "camel-imapx-input-stream.h"
#include "camel-imapx-utils.h"

static CamelIMAPXInputStream *
test_imapx_body_stream_new (const gchar *data)
{
	CamelIMAPXInputStream *stream;
	GInputStream *base_stream;

	base_stream = g_memory_input_stream_new_from_data (
		g_strdup (data), strlen (data), g_free);

	stream = (CamelIMAPXInputStream *) camel_imapx_input_stream_new (base_stream);
	g_object_unref (base_stream);

	return stream;
}

static gchar *
test_imapx_body_build_deep (guint levels)
{
	GString *str;
	guint ii;

	str = g_string_new (NULL);

	for (ii = 0; ii < levels; ii++)
		g_string_append_c (str, '(');

	g_string_append (str, "(\"TEXT\" \"PLAIN\" (\"CHARSET\" \"UTF-8\") NIL NIL \"7BIT\" 0 0)");

	for (ii = 0; ii < levels; ii++)
		g_string_append (str, " \"MIXED\")");

	return g_string_free (str, FALSE);
}

static void
test_bodystructure_valid (void)
{
	CamelIMAPXInputStream *stream;
	CamelMessageContentInfo *cinfo, *child;
	GError *error = NULL;

	stream = test_imapx_body_stream_new (
		"(("
		"\"TEXT\" \"PLAIN\" (\"CHARSET\" \"UTF-8\") NIL NIL \"7BIT\" 12 1"
		")("
		"\"TEXT\" \"HTML\" (\"CHARSET\" \"UTF-8\") NIL NIL \"7BIT\" 34 2"
		") \"ALTERNATIVE\")");

	cinfo = imapx_parse_body (stream, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (cinfo);
	g_assert_true (camel_content_type_is (cinfo->type, "multipart", "alternative"));

	child = cinfo->childs;
	g_assert_nonnull (child);
	g_assert_true (child->parent == cinfo);
	g_assert_true (camel_content_type_is (child->type, "text", "plain"));
	g_assert_cmpuint (child->size, ==, 12);

	child = child->next;
	g_assert_nonnull (child);
	g_assert_true (child->parent == cinfo);
	g_assert_true (camel_content_type_is (child->type, "text", "html"));
	g_assert_cmpuint (child->size, ==, 34);
	g_assert_null (child->next);

	camel_message_content_info_free (cinfo);
	g_object_unref (stream);
}

static void
test_bodystructure_too_deep (void)
{
	CamelIMAPXInputStream *stream;
	CamelMessageContentInfo *cinfo;
	GError *error = NULL;
	gchar *body, *input;
	guchar *token;
	gint tok;
	guint len;

	body = test_imapx_body_build_deep (100);
	input = g_strconcat (body, " 42 X", NULL);
	stream = test_imapx_body_stream_new (input);

	cinfo = imapx_parse_body (stream, NULL, &error);
	g_assert_null (cinfo);
	g_assert_error (error, CAMEL_IMAPX_ERROR, CAMEL_IMAPX_ERROR_IGNORE);
	g_clear_error (&error);

	tok = camel_imapx_input_stream_token (stream, &token, &len, NULL, &error);
	g_assert_no_error (error);
	g_assert_cmpint (tok, ==, IMAPX_TOK_INT);
	g_assert_cmpuint (len, ==, 2);
	g_assert_cmpmem (token, len, "42", 2);

	g_object_unref (stream);
	g_free (input);
	g_free (body);
}

static void
test_fetch_bodystructure_too_deep (void)
{
	CamelIMAPXInputStream *stream;
	struct _fetch_info *finfo;
	GError *error = NULL;
	gchar *body, *input;

	body = test_imapx_body_build_deep (100);
	input = g_strdup_printf ("(UID 123 BODYSTRUCTURE %s FLAGS (\\Seen))", body);
	stream = test_imapx_body_stream_new (input);

	finfo = imapx_parse_fetch (stream, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (finfo);
	g_assert_cmpuint (finfo->got & FETCH_UID, ==, FETCH_UID);
	g_assert_cmpuint (finfo->got & FETCH_FLAGS, ==, FETCH_FLAGS);
	g_assert_cmpuint (finfo->got & FETCH_CINFO, ==, 0);
	g_assert_cmpstr (finfo->uid, ==, "123");
	g_assert_null (finfo->cinfo);

	imapx_free_fetch (finfo);
	g_object_unref (stream);
	g_free (input);
	g_free (body);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	imapx_utils_init ();

	g_test_add_func ("/Camel/IMAPx/BodyStructure/valid", test_bodystructure_valid);
	g_test_add_func ("/Camel/IMAPx/BodyStructure/too-deep", test_bodystructure_too_deep);
	g_test_add_func ("/Camel/IMAPx/BodyStructure/too-deep-fetch", test_fetch_bodystructure_too_deep);

	ret = g_test_run ();
	camel_test_shutdown ();
	return ret;
}
