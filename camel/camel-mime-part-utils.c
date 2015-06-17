/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-mime-part-utils : Utility for mime parsing and so on
 *
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
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *          Michael Zucchi <notzed@ximian.com>
 *          Jeffrey Stedfast <fejj@ximian.com>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "camel-charset-map.h"
#include "camel-html-parser.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-charset.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-message.h"
#include "camel-mime-part-utils.h"
#include "camel-multipart-encrypted.h"
#include "camel-multipart-signed.h"
#include "camel-multipart.h"
#include "camel-stream-filter.h"
#include "camel-stream-fs.h"
#include "camel-stream-mem.h"
#include "camel-stream-buffer.h"
#include "camel-utf8.h"

#define d(x) /* (printf("%s(%d): ", __FILE__, __LINE__),(x)) */

/* simple data wrapper */
static gboolean
simple_data_wrapper_construct_from_parser (CamelDataWrapper *dw,
                                           CamelMimeParser *mp,
                                           GCancellable *cancellable,
                                           GError **error)
{
	gchar *buf;
	GByteArray *buffer;
	CamelStream *mem;
	gsize len;
	gboolean success;

	d (printf ("simple_data_wrapper_construct_from_parser()\n"));

	/* read in the entire content */
	buffer = g_byte_array_new ();
	while (camel_mime_parser_step (mp, &buf, &len) != CAMEL_MIME_PARSER_STATE_BODY_END) {
		d (printf ("appending o/p data: %d: %.*s\n", len, len, buf));
		g_byte_array_append (buffer, (guint8 *) buf, len);
	}

	d (printf ("message part kept in memory!\n"));

	mem = camel_stream_mem_new_with_byte_array (buffer);
	success = camel_data_wrapper_construct_from_stream_sync (
		dw, mem, cancellable, error);
	g_object_unref (mem);

	return success;
}

/**
 * camel_mime_part_construct_content_from_parser:
 *
 * Since: 2.24
 **/
gboolean
camel_mime_part_construct_content_from_parser (CamelMimePart *dw,
                                               CamelMimeParser *mp,
                                               GCancellable *cancellable,
                                               GError **error)
{
	CamelDataWrapper *content = NULL;
	CamelContentType *ct;
	gchar *encoding;
	gboolean success = TRUE;

	g_return_val_if_fail (CAMEL_IS_MIME_PART (dw), FALSE);

	ct = camel_mime_parser_content_type (mp);

	encoding = camel_content_transfer_encoding_decode (camel_mime_parser_header (mp, "Content-Transfer-Encoding", NULL));

	switch (camel_mime_parser_state (mp)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
		d (printf ("Creating body part\n"));
		/* multipart/signed is some type that we must treat as binary data. */
		if (camel_content_type_is (ct, "multipart", "signed")) {
			content = (CamelDataWrapper *) camel_multipart_signed_new ();
			camel_multipart_construct_from_parser ((CamelMultipart *) content, mp);
		} else {
			content = camel_data_wrapper_new ();
			success = simple_data_wrapper_construct_from_parser (
				content, mp, cancellable, error);
		}
		break;
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		d (printf ("Creating message part\n"));
		content = (CamelDataWrapper *) camel_mime_message_new ();
		success = camel_mime_part_construct_from_parser_sync (
			(CamelMimePart *) content, mp, cancellable, error);
		break;
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		d (printf ("Creating multi-part\n"));
		if (camel_content_type_is (ct, "multipart", "encrypted"))
			content = (CamelDataWrapper *) camel_multipart_encrypted_new ();
		else if (camel_content_type_is (ct, "multipart", "signed"))
			content = (CamelDataWrapper *) camel_multipart_signed_new ();
		else
			content = (CamelDataWrapper *) camel_multipart_new ();

		camel_multipart_construct_from_parser ((CamelMultipart *) content, mp);
		d (printf ("Created multi-part\n"));
		break;
	default:
		g_warning ("Invalid state encountered???: %u", camel_mime_parser_state (mp));
	}

	if (content) {
		if (encoding)
			content->encoding = camel_transfer_encoding_from_string (encoding);

		/* would you believe you have to set this BEFORE you set the content object???  oh my god !!!! */
		camel_data_wrapper_set_mime_type_field (content, camel_mime_part_get_content_type (dw));
		camel_medium_set_content ((CamelMedium *) dw, content);
		g_object_unref (content);
	}

	g_free (encoding);

	return success;
}

/**
 * camel_mime_message_build_preview:
 *
 * <note>
 *   <para>
 *     This function blocks like crazy.
 *   </para>
 * </note>
 *
 * Since: 2.28
 **/
gboolean
camel_mime_message_build_preview (CamelMimePart *msg,
                                  CamelMessageInfo *info)
{
	CamelDataWrapper *dw;
	gboolean got_plain = FALSE;

	dw = camel_medium_get_content ((CamelMedium *) msg);
	if (camel_content_type_is (dw->mime_type, "multipart", "*")) {
		gint i, nparts;
		CamelMultipart *mp = (CamelMultipart *) camel_medium_get_content ((CamelMedium *) msg);

		g_warn_if_fail (CAMEL_IS_MULTIPART (mp));

		nparts = camel_multipart_get_number (mp);
		for (i = 0; i < nparts && !got_plain; i++) {
			CamelMimePart *part = camel_multipart_get_part (mp, i);
			got_plain = camel_mime_message_build_preview (part, info);
		}

	} else if (camel_content_type_is (dw->mime_type, "text", "*") &&
		/*    !camel_content_type_is (dw->mime_type, "text", "html") && */
		    !camel_content_type_is (dw->mime_type, "text", "calendar")) {
		CamelStream *mstream, *bstream;

		/* FIXME Pass a GCancellable and GError here. */
		mstream = camel_stream_mem_new ();
		if (camel_data_wrapper_decode_to_stream_sync (dw, mstream, NULL, NULL) > 0) {
			gchar *line = NULL;
			GString *str = g_string_new (NULL);

			g_seekable_seek (
				G_SEEKABLE (mstream), 0,
				G_SEEK_SET, NULL, NULL);

			bstream = camel_stream_buffer_new (mstream, CAMEL_STREAM_BUFFER_READ | CAMEL_STREAM_BUFFER_BUFFER);

			/* We should fetch just 200 unquoted lines. */
			while ((line = camel_stream_buffer_read_line ((CamelStreamBuffer *) bstream, NULL, NULL)) && str->len < 200) {
				gchar *tmp = line;

				if (*line == '>' || strstr (line, "wrote:")) {
					g_free (tmp);
					continue;
				}
				if (g_str_has_prefix (line, "--")) {
					g_free (tmp);
					line = NULL;
					break;
				}
				while (*line && ((*line == ' ') || *line == '\t'))
					line++;
				if (*line == '\0' || *line == '\n') {
					g_free (tmp);
					continue;
				}

				g_string_append (str, " ");
				g_string_append (str, line);
				g_free (tmp);
				line = NULL;
			}
			if (str->len > 100) {
				g_string_insert (str, 100, "\n");
			}
			/* We don't mark dirty, as we don't store these */
			((CamelMessageInfoBase *) info)->preview = camel_utf8_make_valid (str->str);
			g_string_free (str, TRUE);

			g_object_unref (bstream);
		}
		g_object_unref (mstream);
		return TRUE;
	}

	return got_plain;
}
