/*
 * SPDX-FileCopyrightText: (C) 2026 Mike C. Ward <mward5@tinfoil-fedora.com>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include "camel-test.h"

#define BOUNDARY "=-TestBoundary"

static CamelMultipartSigned *
build_multipart_signed (const gchar *preface)
{
	CamelMultipartSigned *mps;
	CamelMimePart *sigpart;
	CamelStream *content;
	const gchar *text = "Content-Type: text/plain\n\nsigned text\n";

	sigpart = camel_mime_part_new ();
	camel_mime_part_set_content (sigpart, "signature", 9, "application/pkcs7-signature");

	content = camel_stream_mem_new_with_buffer (text, strlen (text));

	mps = camel_multipart_signed_new ();
	camel_data_wrapper_set_mime_type (CAMEL_DATA_WRAPPER (mps),
		"multipart/signed; protocol=\"application/pkcs7-signature\"");
	camel_multipart_set_boundary (CAMEL_MULTIPART (mps), BOUNDARY);
	camel_multipart_set_preface (CAMEL_MULTIPART (mps), preface);
	camel_multipart_signed_set_signature (mps, sigpart);
	camel_multipart_signed_set_content_stream (mps, content);

	g_object_unref (content);
	g_object_unref (sigpart);

	return mps;
}

static gchar *
write_to_stream (CamelDataWrapper *data_wrapper)
{
	CamelStream *stream;
	GByteArray *byte_array;
	gchar *text;

	stream = camel_stream_mem_new ();
	g_assert_cmpint (camel_data_wrapper_write_to_stream_sync (data_wrapper, stream, NULL, NULL), >, 0);

	byte_array = camel_stream_mem_get_byte_array (CAMEL_STREAM_MEM (stream));
	text = g_strndup ((const gchar *) byte_array->data, byte_array->len);

	g_object_unref (stream);

	return text;
}

static gchar *
write_to_output_stream (CamelDataWrapper *data_wrapper)
{
	GOutputStream *stream;
	gchar *text;

	stream = g_memory_output_stream_new_resizable ();
	g_assert_cmpint (camel_data_wrapper_write_to_output_stream_sync (data_wrapper, stream, NULL, NULL), >, 0);
	g_assert_true (g_output_stream_write_all (stream, "", 1, NULL, NULL, NULL));
	g_assert_true (g_output_stream_close (stream, NULL, NULL));

	text = g_memory_output_stream_steal_data (G_MEMORY_OUTPUT_STREAM (stream));

	g_object_unref (stream);

	return text;
}

static void
test_multipart_signed_no_preface (void)
{
	CamelMultipartSigned *mps;
	gchar *text;

	mps = build_multipart_signed (NULL);

	text = write_to_stream (CAMEL_DATA_WRAPPER (mps));
	g_assert_true (g_str_has_prefix (text, "--" BOUNDARY "\nContent-Type: text/plain\n"));
	g_free (text);

	text = write_to_output_stream (CAMEL_DATA_WRAPPER (mps));
	g_assert_true (g_str_has_prefix (text, "--" BOUNDARY "\nContent-Type: text/plain\n"));
	g_free (text);

	g_object_unref (mps);
}

static void
test_multipart_signed_preface (void)
{
	CamelMultipartSigned *mps;
	gchar *text;

	mps = build_multipart_signed ("preface");

	text = write_to_stream (CAMEL_DATA_WRAPPER (mps));
	g_assert_true (g_str_has_prefix (text, "preface\n--" BOUNDARY "\n"));
	g_free (text);

	text = write_to_output_stream (CAMEL_DATA_WRAPPER (mps));
	g_assert_true (g_str_has_prefix (text, "preface\n--" BOUNDARY "\n"));
	g_free (text);

	g_object_unref (mps);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/MultipartSigned/no-preface", test_multipart_signed_no_preface);
	g_test_add_func ("/Camel/MultipartSigned/preface", test_multipart_signed_preface);

	ret = g_test_run ();
	camel_test_shutdown ();

	return ret;
}
