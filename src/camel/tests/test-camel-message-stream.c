/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/*
  test-message-stream.c

  Create a message, save it.

  Retrieve message, compare content.

  Operations:
	writing / loading from different types of streams
	reading / writing different content
	reading / writing different encodings
	reading / writing different charsets

  Just testing streams:
	different stream types
	different file ops
	seek, eof, etc.
*/

#include "camel-test.h"
#include "messages.h"

/* for stat */
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

struct _text {
	gchar *text;
	gint len;
};

#define MAX_TEXTS (14)
static struct _text texts[MAX_TEXTS];

static void
setup (void)
{
	gint i, j;
	gchar *p;

	/* setup various edge and other general cases */
	texts[0].text = g_strdup ("");
	texts[0].len = 0;
	texts[1].text = g_strdup ("");
	texts[1].len = 1;
	texts[2].text = g_strdup ("\n");
	texts[2].len = 1;
	texts[3].text = g_strdup ("A");
	texts[3].len = 1;
	texts[4].text = g_strdup ("This is a test.\n.");
	texts[4].len = strlen (texts[4].text);
	texts[5].text = g_strdup ("This is a test.\n\n.\n");
	texts[5].len = strlen (texts[5].text);
	texts[6].text = g_malloc0 (1024);
	texts[6].len = 1024;
	texts[7].text = g_malloc0 (102400);
	texts[7].len = 102400;
	texts[8].text = g_malloc (1024);
	memset (texts[8].text, '\n', 1024);
	texts[8].len = 1024;
	texts[9].text = g_malloc (102400);
	memset (texts[9].text, '\n', 102400);
	texts[9].len = 102400;
	texts[10].text = g_malloc (1024);
	memset (texts[10].text, ' ', 1024);
	texts[10].len = 1024;
	texts[11].text = g_malloc (102400);
	memset (texts[11].text, ' ', 102400);
	texts[11].len = 102400;

	srand (42);
	p = texts[12].text = g_malloc (1024);
	for (i = 0; i < 1024; i++) {
		j = g_random_int ();
		if (j < G_MAXUINT32 / 120)
			*p++ = '\n';
		else
			*p++ = (j % 95) + 32;
	}
	texts[12].len = 1024;
	p = texts[13].text = g_malloc (102400);
	for (i = 0; i < 102400; i++) {
		j = g_random_int ();
		if (j < G_MAXUINT32 / 120)
			*p++ = '\n';
		else
			*p++ = (j % 95) + 32;
	}
	texts[13].len = 102400;
}

static void
cleanup (void)
{
	gint i;

	for (i = 0; i < MAX_TEXTS; i++)
		g_free (texts[i].text);
}

static void
test_simple_content (void)
{
	CamelMimeMessage *msg, *msg2;
	gint i, j;
	gchar *text;
	gint len;

	/* test all ways of setting simple content for a message (i.e. memory based) */
	for (j = 0; j < MAX_TEXTS; j++) {
		text = texts[j].text;
		len = texts[j].len;
		for (i = 0; i < SET_CONTENT_WAYS; i++) {
			msg = test_message_create_simple ();

			test_message_set_content_simple ((CamelMimePart *) msg, i, "text/plain", text, len);

			test_message_compare_content (camel_medium_get_content ((CamelMedium *) msg), text, len);

			unlink ("test1.msg");
			test_message_write_file (msg, "test1.msg");
			g_assert_cmpuint (G_OBJECT (msg)->ref_count, ==, 1);
			g_clear_object (&msg);

			msg2 = test_message_read_file ("test1.msg");

			test_message_compare_content (camel_medium_get_content ((CamelMedium *) msg2), text, len);
			g_assert_cmpuint (G_OBJECT (msg2)->ref_count, ==, 1);
			g_clear_object (&msg2);

			unlink ("test1.msg");
		}
	}
}

static void
test_different_encodings (void)
{
	CamelMimeMessage *msg, *msg2;
	gint i, j;
	gchar *text;
	gint len;

	for (j = 0; j < MAX_TEXTS; j++) {
		text = texts[j].text;
		len = texts[j].len;
		for (i = 0; i < CAMEL_TRANSFER_NUM_ENCODINGS; i++) {
			msg = test_message_create_simple ();

			test_message_set_content_simple ((CamelMimePart *) msg, 0, "text/plain", text, len);

			camel_mime_part_set_encoding ((CamelMimePart *) msg, i);

			unlink ("test1.msg");
			test_message_write_file (msg, "test1.msg");
			g_assert_cmpuint (G_OBJECT (msg)->ref_count, ==, 1);
			g_clear_object (&msg);

			msg2 = test_message_read_file ("test1.msg");

			test_message_compare_content (camel_medium_get_content ((CamelMedium *) msg2), text, len);
			g_assert_cmpuint (G_OBJECT (msg2)->ref_count, ==, 1);
			g_clear_object (&msg2);

			unlink ("test1.msg");
		}
	}
}

gint
main (gint argc,
      gchar **argv)
{
	gint res;

	camel_test_init (&argc, &argv);

	setup ();

	g_test_add_func ("/Camel/Message/Stream/SimpleContent", test_simple_content);
	g_test_add_func ("/Camel/Message/Stream/DifferentEncodings", test_different_encodings);

	res = g_test_run ();

	cleanup ();
	camel_test_shutdown ();

	return res;
}
