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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "camel-test.h"
#include "messages.h"

#if 0
static void
dump_mime_struct (CamelMimePart *mime_part,
                  gint depth)
{
	CamelDataWrapper *content;
	gchar *mime_type;
	gint i = 0;

	while (i < depth) {
		printf ("   ");
		i++;
	}

	content = camel_medium_get_content ((CamelMedium *) mime_part);

	mime_type = camel_data_wrapper_get_mime_type (content);
	printf ("Content-Type: %s\n", mime_type);
	g_free (mime_type);

	if (CAMEL_IS_MULTIPART (content)) {
		guint num, index = 0;

		num = camel_multipart_get_number ((CamelMultipart *) content);
		while (index < num) {
			mime_part = camel_multipart_get_part ((CamelMultipart *) content, index);
			dump_mime_struct (mime_part, depth + 1);
			index++;
		}
	} else if (CAMEL_IS_MIME_MESSAGE (content)) {
		dump_mime_struct ((CamelMimePart *) content, depth + 1);
	}
}
#endif

static void
test_message_parser (void)
{
	const gchar *msg1 =
		"MIME-Version: 1.0\r\n"
		"Message-ID: <1@example.com>\r\n"
		"Date: Wed, 01 Dec 1980 00:00:00 -0800 (PST)\r\n"
		"From: 1stfrom@example.com\r\n"
		"To: 1stto@example.com\r\n"
		"Subject: 1st subject\r\n"
		"Date: Wed, 01 Dec 2022 00:00:00 -0800 (PST)\r\n"
		"From: 2ndfrom@example.com\r\n"
		"To: 2ndto@example.com\r\n"
		"Subject: 2nd subject\r\n"
		"Content-type: text/plain\r\n"
		"\r\n";
	const gchar *msg2 =
		"MIME-Version: 1.0\r\n"
		"Message-ID: <2@example.com>\r\n"
		"Date: Wed, 01 Dec 1980 00:00:01 -0800 (PST)\r\n"
		"From: 1stfrom2@example.com\r\n"
		"To: 1stto2@example.com\r\n"
		"Subject: 1st subject2\r\n"
		"Date: Wed, 01 Dec 2022 00:00:01 -0800 (PST)\r\n"
		"From: 2ndfrom2@example.com\r\n"
		"To: 2ndto2@example.com\r\n"
		"Subject: 2nd subject2\r\n"
		"Content-type: text/plain\r\n"
		"\r\n";
	CamelMimeMessage *message;
	CamelStream *stream;
	CamelInternetAddress *addr;
	const gchar *email;

	stream = camel_stream_mem_new_with_buffer (msg1, strlen (msg1));
	message = camel_mime_message_new ();

	g_assert_true (camel_data_wrapper_construct_from_stream_sync (CAMEL_DATA_WRAPPER (message), stream, NULL, NULL));
	g_assert_cmpstr (camel_mime_message_get_message_id (message), ==, "1@example.com");
	g_assert_cmpstr (camel_mime_message_get_subject (message), ==, "1st subject");
	g_assert_cmpint (camel_mime_message_get_date (message, NULL), ==, 344505600);

	addr = camel_mime_message_get_from (message);
	g_assert_nonnull (addr);
	g_assert_cmpint (camel_address_length (CAMEL_ADDRESS (addr)), ==, 1);
	g_assert_true (camel_internet_address_get (addr, 0, NULL, &email));
	g_assert_cmpstr (email, ==, "1stfrom@example.com");

	addr = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	g_assert_nonnull (addr);
	g_assert_cmpint (camel_address_length (CAMEL_ADDRESS (addr)), ==, 1);
	g_assert_true (camel_internet_address_get (addr, 0, NULL, &email));
	g_assert_cmpstr (email, ==, "1stto@example.com");

	/* Should be able to change it now, when not parsing anymore */
	camel_mime_message_set_subject (message, "changed subject");
	g_assert_cmpstr (camel_mime_message_get_subject (message), ==, "changed subject");

	g_clear_object (&stream);
	stream = camel_stream_mem_new_with_buffer (msg2, strlen (msg2));

	/* Just in case, constructing from stream again should not preserve previous values */
	g_assert_true (camel_data_wrapper_construct_from_stream_sync (CAMEL_DATA_WRAPPER (message), stream, NULL, NULL));
	g_assert_cmpstr (camel_mime_message_get_message_id (message), ==, "2@example.com");
	g_assert_cmpstr (camel_mime_message_get_subject (message), ==, "1st subject2");
	g_assert_cmpint (camel_mime_message_get_date (message, NULL), ==, 344505601);

	addr = camel_mime_message_get_from (message);
	g_assert_nonnull (addr);
	g_assert_cmpint (camel_address_length (CAMEL_ADDRESS (addr)), ==, 1);
	g_assert_true (camel_internet_address_get (addr, 0, NULL, &email));
	g_assert_cmpstr (email, ==, "1stfrom2@example.com");

	addr = camel_mime_message_get_recipients (message, CAMEL_RECIPIENT_TYPE_TO);
	g_assert_nonnull (addr);
	g_assert_cmpint (camel_address_length (CAMEL_ADDRESS (addr)), ==, 1);
	g_assert_true (camel_internet_address_get (addr, 0, NULL, &email));
	g_assert_cmpstr (email, ==, "1stto2@example.com");

	/* Should be able to change it now, when not parsing anymore */
	camel_mime_message_set_subject (message, "changed subject2");
	g_assert_cmpstr (camel_mime_message_get_subject (message), ==, "changed subject2");

	g_clear_object (&stream);
	g_clear_object (&message);
}

static gboolean
test_data_messages (void)
{
	struct dirent *dent;
	DIR *dir;
	gint fd;

	camel_test_start ("Message Test Suite");

	if (!(dir = opendir ("../data/messages")))
		return FALSE;

	while ((dent = readdir (dir)) != NULL) {
		CamelMimeMessage *message;
		CamelStream *stream;
		gchar *filename;
		struct stat st;

		if (dent->d_name[0] == '.')
			continue;

		filename = g_strdup_printf ("../data/messages/%s", dent->d_name);
		if (g_stat (filename, &st) == -1 || !S_ISREG (st.st_mode)) {
			g_free (filename);
			continue;
		}

		if ((fd = open (filename, O_RDONLY)) == -1) {
			g_free (filename);
			continue;
		}

		push ("testing message '%s'", filename);
		g_free (filename);

		stream = camel_stream_fs_new_with_fd (fd);
		message = camel_mime_message_new ();
		camel_data_wrapper_construct_from_stream_sync (
			CAMEL_DATA_WRAPPER (message), stream, NULL, NULL);
		g_seekable_seek (
			G_SEEKABLE (stream), 0, G_SEEK_SET, NULL, NULL);

		/*dump_mime_struct ((CamelMimePart *) message, 0);*/
		test_message_compare (message);

		g_object_unref (message);
		g_object_unref (stream);

		pull ();
	}

	closedir (dir);

	camel_test_end ();

	return TRUE;
}

static void
test_message_preview_data (const gchar *data,
			   const gchar *expected)
{
	CamelMimePart *part;
	CamelMimeParser *parser;
	GBytes *bytes;
	gchar *preview;

	parser = camel_mime_parser_new ();
	camel_mime_parser_scan_from (parser, FALSE);
	camel_mime_parser_scan_pre_from (parser, FALSE);

	bytes = g_bytes_new_static (data, strlen (data));
	camel_mime_parser_init_with_bytes (parser, bytes);
	g_bytes_unref (bytes);

	part = camel_mime_part_new ();
	camel_mime_part_construct_from_parser_sync (part, parser, NULL, NULL);

	g_clear_object (&parser);

	preview = camel_mime_part_generate_preview (part, NULL, NULL);

	check_msg ((g_strcmp0 (preview, expected) == 0), "expected '%s' received '%s'", expected, preview);

	g_clear_object (&part);
	g_free (preview);
}

static void
test_message_preview (void)
{
	struct _cases {
		const gchar *test;
		const gchar *expected;
		const gchar *data;
	} cases[] = {
		{ "Preview text/plain UTF-8", "ěščřžýáíé ĚŠČŘŽÝÁÍÉ ",
		  "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
		  "Content-Transfer-Encoding: base64\r\n"
		  "\r\n"
		  "xJvFocSNxZnFvsO9w6HDrcOpCsSaxaDEjMWYxb3DncOBw43DiQo=\r\n" },
		{ "Preview text/html UTF-8", "ěščřžýáíéĚŠČŘŽÝÁÍÉ ",
		  "Content-Type: text/html; charset=\"utf-8\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "\r\n"
		  "<html><head></head><body><div>=C4=9B=C5=A1=C4=8D=C5=99=C5=BE=C3=BD=C3=A1=C3=AD=\r\n"
		  "=C3=A9</div><div>=C4=9A=C5=A0=C4=8C=C5=98=C5=BD=C3=9D=C3=81=C3=8D=C3=89</di=\r\n"
		  "v></body></html>\r\n" },
		{ "Preview text/plain ISO-8859-2", "ěščřžýáíé ĚŠČŘŽÝÁÍÉ ",
		  "Content-Type: text/plain; charset=\"ISO-8859-2\"\r\n"
		  "Content-Transfer-Encoding: base64\r\n"
		  "\r\n"
		  "7Lno+L794e3pCsypyNiu3cHNyQoK\r\n" },
		{ "Preview text/html ISO-8859-2", "ěščřžýáíéĚŠČŘŽÝÁÍÉ ",
		  "Content-Type: text/html; charset=\"iso-8859-2\"\r\n"
		  "Content-Transfer-Encoding: quoted-printable\r\n"
		  "\r\n"
		  "<html><head></head><body><div>=EC=B9=E8=F8=BE=FD=E1=ED=E9=\r\n"
		  "</div><div>=CC=A9=C8=D8=AE=DD=C1=CD=C9</di=\r\n"
		  "v></body></html>\r\n" },
	};
	guint ii;

	for (ii = 0; ii < G_N_ELEMENTS (cases); ii++) {
		camel_test_start (cases[ii].test);
		test_message_preview_data (cases[ii].data, cases[ii].expected);
		camel_test_end ();
	}
}

gint main (gint argc, gchar **argv)
{
	camel_test_init (argc, argv);

	test_message_parser ();
	test_message_preview ();

	if (!test_data_messages ())
		return 77;

	return 0;
}
