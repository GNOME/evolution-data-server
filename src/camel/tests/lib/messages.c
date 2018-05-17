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
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "messages.h"
#include "camel-test.h"

CamelMimeMessage *
test_message_create_simple (void)
{
	CamelMimeMessage *msg;
	CamelInternetAddress *addr;

	msg = camel_mime_message_new ();

	addr = camel_internet_address_new ();
	camel_internet_address_add (addr, "Michael Zucchi", "zed@nowhere.com");
	camel_mime_message_set_from (msg, addr);
	camel_address_remove ((CamelAddress *) addr, -1);
	camel_internet_address_add (addr, "POSTMASTER", "POSTMASTER@somewhere.net");
	camel_mime_message_set_recipients (msg, CAMEL_RECIPIENT_TYPE_TO, addr);
	camel_address_remove ((CamelAddress *) addr, -1);
	camel_internet_address_add (addr, "Michael Zucchi", "zed@nowhere.com");
	camel_mime_message_set_recipients (msg, CAMEL_RECIPIENT_TYPE_CC, addr);

	check_unref (addr, 1);

	camel_mime_message_set_subject (msg, "Simple message subject");
	camel_mime_message_set_date (msg, time (0), 930);

	return msg;
}

static void
content_weak_notify (GByteArray *ba,
                     GObject *where_the_object_was)
{
	g_byte_array_free (ba, TRUE);
}

void
test_message_set_content_simple (CamelMimePart *part,
                                 gint how,
                                 const gchar *type,
                                 const gchar *text,
                                 gint len)
{
	CamelStreamMem *content = NULL;
	CamelDataWrapper *dw;
	static GByteArray *ba;

	switch (how) {
	case 0:
		camel_mime_part_set_content (part, text, len, type);
		break;
	case 1:
		content = (CamelStreamMem *) camel_stream_mem_new_with_buffer (text, len);
		break;
	case 2:
		content = (CamelStreamMem *) camel_stream_mem_new ();
		camel_stream_mem_set_buffer (content, text, len);
		break;
	case 3:
		ba = g_byte_array_new ();
		g_byte_array_append (ba, (guint8 *) text, len);

		content = (CamelStreamMem *) camel_stream_mem_new_with_byte_array (ba);
		ba = NULL;
		break;
	case 4:
		ba = g_byte_array_new ();
		g_byte_array_append (ba, (guint8 *) text, len);

		content = (CamelStreamMem *) camel_stream_mem_new ();
		camel_stream_mem_set_byte_array (content, ba);

		g_object_weak_ref (
			G_OBJECT (content), (GWeakNotify)
			content_weak_notify, ba);
		break;
	}

	if (content != 0) {
		dw = camel_data_wrapper_new ();
		camel_data_wrapper_set_mime_type (dw, type);

		camel_data_wrapper_construct_from_stream_sync (
			dw, (CamelStream *) content, NULL, NULL);
		camel_medium_set_content ((CamelMedium *) part, dw);

		check_unref (content, 2);
		check_unref (dw, 2);
	}
}

gint
test_message_write_file (CamelMimeMessage *msg,
                         const gchar *name)
{
	CamelStream *stream;
	gint ret;

	stream = camel_stream_fs_new_with_name (
		name, O_CREAT | O_WRONLY, 0600, NULL);
	camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (msg), stream, NULL, NULL);
	ret = camel_stream_close (stream, NULL, NULL);

	check (G_OBJECT (stream)->ref_count == 1);
	g_object_unref (stream);

	return ret;
}

CamelMimeMessage *
test_message_read_file (const gchar *name)
{
	CamelStream *stream;
	CamelMimeMessage *msg2;

	stream = camel_stream_fs_new_with_name (name, O_RDONLY, 0, NULL);
	msg2 = camel_mime_message_new ();

	camel_data_wrapper_construct_from_stream_sync (
		CAMEL_DATA_WRAPPER (msg2), stream, NULL, NULL);
	/* stream's refcount may be > 1 if the message is real big */
	check (G_OBJECT (stream)->ref_count >=1);
	g_object_unref (stream);

	return msg2;
}

static void
hexdump (const guchar *in,
         gint inlen)
{
	const guchar *inptr = in, *start = inptr;
	const guchar *inend = in + inlen;
	gint octets;

	while (inptr < inend) {
		octets = 0;
		while (inptr < inend && octets < 16) {
			printf ("%.2X ", *inptr++);
			octets++;
		}

		while (octets < 16) {
			printf ("   ");
			octets++;
		}

		printf ("       ");

		while (start < inptr) {
			fputc (isprint ((gint) *start) ? *start : '.', stdout);
			start++;
		}

		fputc ('\n', stdout);
	}
}

gint
test_message_compare_content (CamelDataWrapper *dw,
                              const gchar *text,
                              gint len)
{
	GByteArray *byte_array;
	CamelStream *stream;

	/* sigh, ok, so i len == 0, dw will probably be 0 too
	 * camel_mime_part_set_content is weird like that */
	if (dw == 0 && len == 0)
		return 0;

	byte_array = g_byte_array_new ();
	stream = camel_stream_mem_new_with_byte_array (byte_array);
	camel_data_wrapper_decode_to_stream_sync (dw, stream, NULL, NULL);

	if (byte_array->len != len) {
		printf ("original text:\n");
		hexdump ((guchar *) text, len);

		printf ("new text:\n");
		hexdump (byte_array->data, byte_array->len);
	}

	check_msg (byte_array->len == len, "buffer->len = %d, len = %d", byte_array->len, len);
	check_msg (memcmp (byte_array->data, text, byte_array->len) == 0, "len = %d", len);

	check_unref (stream, 1);

	return 0;
}

gint
test_message_compare (CamelMimeMessage *msg)
{
	CamelMimeMessage *msg2;
	CamelStream *stream1;
	CamelStream *stream2;
	GByteArray *byte_array1;
	GByteArray *byte_array2;

	byte_array1 = g_byte_array_new ();
	stream1 = camel_stream_mem_new_with_byte_array (byte_array1);
	check_msg (camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (msg), stream1, NULL, NULL) != -1,
		"write_to_stream 1 failed", NULL);
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	msg2 = camel_mime_message_new ();
	check_msg (camel_data_wrapper_construct_from_stream_sync (
		CAMEL_DATA_WRAPPER (msg2), stream1, NULL, NULL) != -1,
		"construct_from_stream 1 failed");
	g_seekable_seek (G_SEEKABLE (stream1), 0, G_SEEK_SET, NULL, NULL);

	byte_array2 = g_byte_array_new ();
	stream2 = camel_stream_mem_new_with_byte_array (byte_array2);
	check_msg (camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (msg2), stream2, NULL, NULL) != -1,
		"write_to_stream 2 failed");
	g_seekable_seek (G_SEEKABLE (stream2), 0, G_SEEK_SET, NULL, NULL);

	if (byte_array1->len != byte_array2->len) {
		printf ("stream1 stream:\n%.*s\n", byte_array1->len, byte_array1->data);
		printf ("stream2 stream:\n%.*s\n\n", byte_array2->len, byte_array2->data);

		printf ("msg1:\n");
		test_message_dump_structure (msg);
		printf ("msg2:\n");
		test_message_dump_structure (msg2);
	}

	check_unref (msg2, 1);

	check_msg (
		byte_array1->len == byte_array2->len,
		"byte_array1->len = %d, byte_array2->len = %d",
		byte_array1->len, byte_array2->len);

	check_msg (memcmp (byte_array1->data, byte_array2->data, byte_array1->len) == 0, "msg/stream compare");

	g_object_unref (stream1);
	g_object_unref (stream2);

	return 0;
}

gint
test_message_compare_header (CamelMimeMessage *m1,
                             CamelMimeMessage *m2)
{
	return 0;
}

gint
test_message_compare_messages (CamelMimeMessage *m1,
                               CamelMimeMessage *m2)
{
	return 0;
}

static void
message_dump_rec (CamelMimeMessage *msg,
                  CamelMimePart *part,
                  gint depth)
{
	CamelDataWrapper *containee;
	gint parts, i;
	gchar *s;
	gchar *mime_type;

	s = alloca (depth + 1);
	memset (s, ' ', depth);
	s[depth] = 0;

	mime_type = camel_data_wrapper_get_mime_type ((CamelDataWrapper *) part);
	printf ("%sPart <%s>\n", s, G_OBJECT_TYPE_NAME (part));
	printf ("%sContent-Type: %s\n", s, mime_type);
	g_free (mime_type);
	printf ("%s encoding: %s\n", s, camel_transfer_encoding_to_string (camel_data_wrapper_get_encoding ((CamelDataWrapper *) part)));
	printf ("%s part encoding: %s\n", s, camel_transfer_encoding_to_string (camel_mime_part_get_encoding (part)));

	containee = camel_medium_get_content (CAMEL_MEDIUM (part));

	if (containee == NULL)
		return;

	mime_type = camel_data_wrapper_get_mime_type (containee);
	printf ("%sContent <%s>\n", s, G_OBJECT_TYPE_NAME (containee));
	printf ("%sContent-Type: %s\n", s, mime_type);
	g_free (mime_type);
	printf ("%s encoding: %s\n", s, camel_transfer_encoding_to_string (camel_data_wrapper_get_encoding ((CamelDataWrapper *) containee)));

	/* using the object types is more accurate than using the mime/types */
	if (CAMEL_IS_MULTIPART (containee)) {
		parts = camel_multipart_get_number (CAMEL_MULTIPART (containee));
		for (i = 0; i < parts; i++) {
			CamelMimePart *subpart = camel_multipart_get_part (CAMEL_MULTIPART (containee), i);

			message_dump_rec (msg, subpart, depth + 1);
		}
	} else if (CAMEL_IS_MIME_MESSAGE (containee)) {
		message_dump_rec (msg, (CamelMimePart *) containee, depth + 1);
	}
}

void
test_message_dump_structure (CamelMimeMessage *m)
{
	message_dump_rec (m, (CamelMimePart *) m, 0);
}
