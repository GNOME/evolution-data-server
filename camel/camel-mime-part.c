/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camelMimePart.c : Abstract class for a mime_part */

/*
 * Authors: Bertrand Guiheneuf <bertrand@helixcode.com>
 *	    Michael Zucchi <notzed@ximian.com>
 *          Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "camel-charset-map.h"
#include "camel-debug.h"
#include "camel-iconv.h"
#include "camel-mime-filter-basic.h"
#include "camel-mime-filter-charset.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-parser.h"
#include "camel-mime-part-utils.h"
#include "camel-mime-part.h"
#include "camel-mime-utils.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"
#include "camel-stream-null.h"
#include "camel-string-utils.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))*/

#define CAMEL_MIME_PART_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_MIME_PART, CamelMimePartPrivate))

typedef struct _AsyncContext AsyncContext;

struct _CamelMimePartPrivate {
	gchar *description;
	CamelContentDisposition *disposition;
	gchar *content_id;
	gchar *content_md5;
	gchar *content_location;
	GList *content_languages;
	CamelTransferEncoding encoding;
};

struct _AsyncContext {
	/* arguments */
	CamelMimeParser *parser;
};

enum {
	PROP_0,
	PROP_CONTENT_ID,
	PROP_CONTENT_LOCATION,
	PROP_CONTENT_MD5,
	PROP_DESCRIPTION,
	PROP_DISPOSITION,
	PROP_FILENAME
};

typedef enum {
	HEADER_UNKNOWN,
	HEADER_DESCRIPTION,
	HEADER_DISPOSITION,
	HEADER_CONTENT_ID,
	HEADER_ENCODING,
	HEADER_CONTENT_MD5,
	HEADER_CONTENT_LOCATION,
	HEADER_CONTENT_LANGUAGES,
	HEADER_CONTENT_TYPE
} CamelHeaderType;

static GHashTable *header_name_table;
static GHashTable *header_formatted_table;

G_DEFINE_TYPE (CamelMimePart, camel_mime_part, CAMEL_TYPE_MEDIUM)

static void
async_context_free (AsyncContext *async_context)
{
	if (async_context->parser != NULL)
		g_object_unref (async_context->parser);

	g_slice_free (AsyncContext, async_context);
}

static gssize
write_header (CamelStream *stream,
              const gchar *name,
              const gchar *value,
              GCancellable *cancellable,
              GError **error)
{
	GString *buffer;
	gssize n_written;

	buffer = g_string_new (name);
	g_string_append_c (buffer, ':');
	if (!isspace (value[0]))
		g_string_append_c (buffer, ' ');
	g_string_append (buffer, value);
	g_string_append_c (buffer, '\n');

	n_written = camel_stream_write (
		stream, buffer->str, buffer->len, cancellable, error);

	g_string_free (buffer, TRUE);

	return n_written;
}

static gssize
write_references (CamelStream *stream,
                  const gchar *name,
                  const gchar *value,
                  GCancellable *cancellable,
                  GError **error)
{
	GString *buffer;
	const gchar *ids, *ide;
	gssize n_written;
	gsize len;

	/* this is only approximate, based on the next >, this way it retains
	 * any content from the original which may not be properly formatted,
	 * etc.  It also doesn't handle the case where an individual messageid
	 * is too long, however thats a bad mail to start with ... */

	buffer = g_string_new (name);
	g_string_append_c (buffer, ':');
	if (!isspace (value[0]))
		g_string_append_c (buffer, ' ');

	len = buffer->len;

	while (*value) {
		ids = value;
		ide = strchr (ids + 1, '>');
		if (ide)
			value = ++ide;
		else
			ide = value = strlen (ids) + ids;

		if (len > 0 && len + (ide - ids) >= CAMEL_FOLD_SIZE) {
			g_string_append_len (buffer, "\n\t", 2);
			len = 0;
		}

		g_string_append_len (buffer, ids, ide - ids);
		len += (ide - ids);
	}

	g_string_append_c (buffer, '\n');

	n_written = camel_stream_write (
		stream, buffer->str, buffer->len, cancellable, error);

	g_string_free (buffer, TRUE);

	return n_written;
}

/* loads in a hash table the set of header names we */
/* recognize and associate them with a unique enum  */
/* identifier (see CamelHeaderType above)           */
static void
init_header_name_table (void)
{
	if (header_name_table)
		return;

	header_name_table = g_hash_table_new (
		camel_strcase_hash, camel_strcase_equal);
	g_hash_table_insert (
		header_name_table,
		(gpointer) "Content-Description",
		(gpointer) HEADER_DESCRIPTION);
	g_hash_table_insert (
		header_name_table,
		(gpointer) "Content-Disposition",
		(gpointer) HEADER_DISPOSITION);
	g_hash_table_insert (
		header_name_table,
		(gpointer) "Content-id",
		(gpointer) HEADER_CONTENT_ID);
	g_hash_table_insert (
		header_name_table,
		(gpointer) "Content-Transfer-Encoding",
		(gpointer) HEADER_ENCODING);
	g_hash_table_insert (
		header_name_table,
		(gpointer) "Content-MD5",
		(gpointer) HEADER_CONTENT_MD5);
	g_hash_table_insert (
		header_name_table,
		(gpointer) "Content-Location",
		(gpointer) HEADER_CONTENT_LOCATION);
	g_hash_table_insert (
		header_name_table,
		(gpointer) "Content-Type",
		(gpointer) HEADER_CONTENT_TYPE);

	header_formatted_table = g_hash_table_new (
		camel_strcase_hash, camel_strcase_equal);
	g_hash_table_insert (
		header_formatted_table,
		(gpointer) "Content-Type", write_header);
	g_hash_table_insert (
		header_formatted_table,
		(gpointer) "Content-Disposition", write_header);
	g_hash_table_insert (
		header_formatted_table,
		(gpointer) "From", write_header);
	g_hash_table_insert (
		header_formatted_table,
		(gpointer) "Reply-To", write_header);
	g_hash_table_insert (
		header_formatted_table,
		(gpointer) "Message-ID", write_header);
	g_hash_table_insert (
		header_formatted_table,
		(gpointer) "In-Reply-To", write_header);
	g_hash_table_insert (
		header_formatted_table,
		(gpointer) "References", write_references);
}

static void
mime_part_set_disposition (CamelMimePart *mime_part,
                           const gchar *disposition)
{
	camel_content_disposition_unref (mime_part->priv->disposition);
	if (disposition)
		mime_part->priv->disposition =
			camel_content_disposition_decode (disposition);
	else
		mime_part->priv->disposition = NULL;
}

static gboolean
mime_part_process_header (CamelMedium *medium,
                          const gchar *name,
                          const gchar *value)
{
	CamelMimePart *mime_part = CAMEL_MIME_PART (medium);
	CamelHeaderType header_type;
	const gchar *charset;
	gchar *text;

	/* Try to parse the header pair. If it corresponds to something   */
	/* known, the job is done in the parsing routine. If not,         */
	/* we simply add the header in a raw fashion                      */

	header_type = (CamelHeaderType) g_hash_table_lookup (header_name_table, name);
	switch (header_type) {
	case HEADER_DESCRIPTION: /* raw header->utf8 conversion */
		g_free (mime_part->priv->description);
		if (((CamelDataWrapper *) mime_part)->mime_type) {
			charset = camel_content_type_param (((CamelDataWrapper *) mime_part)->mime_type, "charset");
			charset = camel_iconv_charset_name (charset);
		} else
			charset = NULL;
		mime_part->priv->description = g_strstrip (camel_header_decode_string (value, charset));
		break;
	case HEADER_DISPOSITION:
		mime_part_set_disposition (mime_part, value);
		break;
	case HEADER_CONTENT_ID:
		g_free (mime_part->priv->content_id);
		mime_part->priv->content_id = camel_header_contentid_decode (value);
		break;
	case HEADER_ENCODING:
		text = camel_header_token_decode (value);
		mime_part->priv->encoding = camel_transfer_encoding_from_string (text);
		g_free (text);
		break;
	case HEADER_CONTENT_MD5:
		g_free (mime_part->priv->content_md5);
		mime_part->priv->content_md5 = g_strdup (value);
		break;
	case HEADER_CONTENT_LOCATION:
		g_free (mime_part->priv->content_location);
		mime_part->priv->content_location = camel_header_location_decode (value);
		break;
	case HEADER_CONTENT_TYPE:
		if (((CamelDataWrapper *) mime_part)->mime_type)
			camel_content_type_unref (((CamelDataWrapper *) mime_part)->mime_type);
		((CamelDataWrapper *) mime_part)->mime_type = camel_content_type_decode (value);
		break;
	default:
		return FALSE;
	}
	return TRUE;
}

static void
mime_part_set_property (GObject *object,
                        guint property_id,
                        const GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTENT_ID:
			camel_mime_part_set_content_id (
				CAMEL_MIME_PART (object),
				g_value_get_string (value));
			return;

		case PROP_CONTENT_MD5:
			camel_mime_part_set_content_md5 (
				CAMEL_MIME_PART (object),
				g_value_get_string (value));
			return;

		case PROP_CONTENT_LOCATION:
			camel_mime_part_set_content_location (
				CAMEL_MIME_PART (object),
				g_value_get_string (value));
			return;

		case PROP_DESCRIPTION:
			camel_mime_part_set_description (
				CAMEL_MIME_PART (object),
				g_value_get_string (value));
			return;

		case PROP_DISPOSITION:
			camel_mime_part_set_disposition (
				CAMEL_MIME_PART (object),
				g_value_get_string (value));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mime_part_get_property (GObject *object,
                        guint property_id,
                        GValue *value,
                        GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_CONTENT_ID:
			g_value_set_string (
				value, camel_mime_part_get_content_id (
				CAMEL_MIME_PART (object)));
			return;

		case PROP_CONTENT_MD5:
			g_value_set_string (
				value, camel_mime_part_get_content_md5 (
				CAMEL_MIME_PART (object)));
			return;

		case PROP_CONTENT_LOCATION:
			g_value_set_string (
				value, camel_mime_part_get_content_location (
				CAMEL_MIME_PART (object)));
			return;

		case PROP_DESCRIPTION:
			g_value_set_string (
				value, camel_mime_part_get_description (
				CAMEL_MIME_PART (object)));
			return;

		case PROP_DISPOSITION:
			g_value_set_string (
				value, camel_mime_part_get_disposition (
				CAMEL_MIME_PART (object)));
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
mime_part_finalize (GObject *object)
{
	CamelMimePartPrivate *priv;

	priv = CAMEL_MIME_PART_GET_PRIVATE (object);

	g_free (priv->description);
	g_free (priv->content_id);
	g_free (priv->content_md5);
	g_free (priv->content_location);

	camel_string_list_free (priv->content_languages);
	camel_content_disposition_unref (priv->disposition);

	camel_header_raw_clear (&CAMEL_MIME_PART (object)->headers);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_mime_part_parent_class)->finalize (object);
}

static void
mime_part_add_header (CamelMedium *medium,
                      const gchar *name,
                      gconstpointer value)
{
	CamelMimePart *part = CAMEL_MIME_PART (medium);

	/* Try to parse the header pair. If it corresponds to something   */
	/* known, the job is done in the parsing routine. If not,         */
	/* we simply add the header in a raw fashion                      */

	/* If it was one of the headers we handled, it must be unique, set it instead of add */
	if (mime_part_process_header (medium, name, value))
		camel_header_raw_replace (&part->headers, name, value, -1);
	else
		camel_header_raw_append (&part->headers, name, value, -1);
}

static void
mime_part_set_header (CamelMedium *medium,
                      const gchar *name,
                      gconstpointer value)
{
	CamelMimePart *part = CAMEL_MIME_PART (medium);

	mime_part_process_header (medium, name, value);
	camel_header_raw_replace (&part->headers, name, value, -1);
}

static void
mime_part_remove_header (CamelMedium *medium,
                         const gchar *name)
{
	CamelMimePart *part = (CamelMimePart *) medium;

	mime_part_process_header (medium, name, NULL);
	camel_header_raw_remove (&part->headers, name);
}

static gconstpointer
mime_part_get_header (CamelMedium *medium,
                      const gchar *name)
{
	CamelMimePart *part = (CamelMimePart *) medium;
	const gchar *value;

	value = camel_header_raw_find (&part->headers, name, NULL);

	/* Skip leading whitespace. */
	while (value != NULL && g_ascii_isspace (*value))
		value++;

	return value;
}

static GArray *
mime_part_get_headers (CamelMedium *medium)
{
	CamelMimePart *part = (CamelMimePart *) medium;
	GArray *headers;
	CamelMediumHeader header;
	struct _camel_header_raw *h;

	headers = g_array_new (FALSE, FALSE, sizeof (CamelMediumHeader));
	for (h = part->headers; h; h = h->next) {
		header.name = h->name;
		header.value = h->value;
		g_array_append_val (headers, header);
	}

	return headers;
}

static void
mime_part_free_headers (CamelMedium *medium,
                        GArray *headers)
{
	g_array_free (headers, TRUE);
}

static void
mime_part_set_content (CamelMedium *medium,
                       CamelDataWrapper *content)
{
	CamelDataWrapper *mime_part = CAMEL_DATA_WRAPPER (medium);
	CamelMediumClass *medium_class;
	CamelContentType *content_type;

	/* Chain up to parent's set_content() method. */
	medium_class = CAMEL_MEDIUM_CLASS (camel_mime_part_parent_class);
	medium_class->set_content (medium, content);

	content_type = camel_data_wrapper_get_mime_type_field (content);
	if (mime_part->mime_type != content_type) {
		gchar *txt;

		txt = camel_content_type_format (content_type);
		camel_medium_set_header (medium, "Content-Type", txt);
		g_free (txt);
	}
}

static gssize
mime_part_write_to_stream_sync (CamelDataWrapper *dw,
                                CamelStream *stream,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelMimePart *mp = CAMEL_MIME_PART (dw);
	CamelMedium *medium = CAMEL_MEDIUM (dw);
	CamelStream *ostream = stream;
	CamelDataWrapper *content;
	gssize total = 0;
	gssize count;
	gint errnosav;

	d (printf ("mime_part::write_to_stream\n"));

	/* FIXME: something needs to be done about this ... */
	/* TODO: content-languages header? */

	if (mp->headers) {
		struct _camel_header_raw *h = mp->headers;
		gchar *val;
		gssize (*writefn) (
			CamelStream *stream,
			const gchar *name,
			const gchar *value,
			GCancellable *cancellable,
			GError **error);

		/* fold/write the headers.   But dont fold headers that are already formatted
		 * (e.g. ones with parameter-lists, that we know about, and have created) */
		while (h) {
			val = h->value;
			if (val == NULL) {
				g_warning ("h->value is NULL here for %s", h->name);
				count = 0;
			} else if ((writefn = g_hash_table_lookup (header_formatted_table, h->name)) == NULL) {
				val = camel_header_fold (val, strlen (h->name));
				count = write_header (
					stream, h->name, val,
					cancellable, error);
				g_free (val);
			} else {
				count = writefn (
					stream, h->name, h->value,
					cancellable, error);
			}
			if (count == -1)
				return -1;
			total += count;
			h = h->next;
		}
	}

	count = camel_stream_write (stream, "\n", 1, cancellable, error);
	if (count == -1)
		return -1;
	total += count;

	content = camel_medium_get_content (medium);
	if (content) {
		CamelMimeFilter *filter = NULL;
		CamelStream *filter_stream = NULL;
		CamelMimeFilter *charenc = NULL;
		const gchar *content_charset = NULL;
		const gchar *part_charset = NULL;
		gboolean reencode = FALSE;
		const gchar *filename;

		if (camel_content_type_is (dw->mime_type, "text", "*")) {
			content_charset = camel_content_type_param (content->mime_type, "charset");
			part_charset = camel_content_type_param (dw->mime_type, "charset");

			if (content_charset && part_charset) {
				content_charset = camel_iconv_charset_name (content_charset);
				part_charset = camel_iconv_charset_name (part_charset);
			}
		}

		if (mp->priv->encoding != content->encoding) {
			gchar *content;

			switch (mp->priv->encoding) {
			case CAMEL_TRANSFER_ENCODING_BASE64:
				filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_BASE64_ENC);
				break;
			case CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE:
				filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_QP_ENC);
				break;
			case CAMEL_TRANSFER_ENCODING_UUENCODE:
				filename = camel_mime_part_get_filename (mp);
				if (filename == NULL)
					filename = "untitled";

				content = g_strdup_printf (
					"begin 644 %s\n", filename);
				count = camel_stream_write_string (
					ostream, content, cancellable, error);
				g_free (content);

				if (count == -1)
					return -1;

				total += count;
				filter = camel_mime_filter_basic_new (CAMEL_MIME_FILTER_BASIC_UU_ENC);
				break;
			default:
				/* content is encoded but the part doesn't want to be... */
				reencode = TRUE;
				break;
			}
		}

		if (content_charset && part_charset && part_charset != content_charset)
			charenc = camel_mime_filter_charset_new (content_charset, part_charset);

		if (filter || charenc) {
			filter_stream = camel_stream_filter_new (stream);

			/* if we have a character encoder, add that always */
			if (charenc) {
				camel_stream_filter_add (
					CAMEL_STREAM_FILTER (filter_stream), charenc);
				g_object_unref (charenc);
			}

			/* we only re-do crlf on encoded blocks */
			if (filter && camel_content_type_is (dw->mime_type, "text", "*")) {
				CamelMimeFilter *crlf = camel_mime_filter_crlf_new (
					CAMEL_MIME_FILTER_CRLF_ENCODE,
					CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);

				camel_stream_filter_add (
					CAMEL_STREAM_FILTER (filter_stream), crlf);
				g_object_unref (crlf);
			}

			if (filter) {
				camel_stream_filter_add (
					CAMEL_STREAM_FILTER (filter_stream), filter);
				g_object_unref (filter);
			}

			stream = filter_stream;

			reencode = TRUE;
		}

		if (reencode)
			count = camel_data_wrapper_decode_to_stream_sync (
				content, stream, cancellable, error);
		else
			count = camel_data_wrapper_write_to_stream_sync (
				content, stream, cancellable, error);

		if (filter_stream) {
			errnosav = errno;
			camel_stream_flush (stream, NULL, NULL);
			g_object_unref (filter_stream);
			errno = errnosav;
		}

		if (count == -1)
			return -1;

		total += count;

		if (reencode && mp->priv->encoding == CAMEL_TRANSFER_ENCODING_UUENCODE) {
			count = camel_stream_write (
				ostream, "end\n", 4, cancellable, error);
			if (count == -1)
				return -1;
			total += count;
		}
	} else {
		g_warning ("No content for medium, nothing to write");
	}

	return total;
}

static gboolean
mime_part_construct_from_stream_sync (CamelDataWrapper *dw,
                                      CamelStream *stream,
                                      GCancellable *cancellable,
                                      GError **error)
{
	CamelMimeParser *parser;
	gboolean success;

	d (printf ("mime_part::construct_from_stream()\n"));

	parser = camel_mime_parser_new ();
	if (camel_mime_parser_init_with_stream (parser, stream, error) == -1) {
		success = FALSE;
	} else {
		success = camel_mime_part_construct_from_parser_sync (
			CAMEL_MIME_PART (dw), parser, cancellable, error);
	}
	g_object_unref (parser);

	return success;
}

static gboolean
mime_part_construct_from_parser_sync (CamelMimePart *mime_part,
                                      CamelMimeParser *parser,
                                      GCancellable *cancellable,
                                      GError **error)
{
	CamelDataWrapper *dw = (CamelDataWrapper *) mime_part;
	struct _camel_header_raw *headers;
	const gchar *content;
	gchar *buf;
	gsize len;
	gint err;
	gboolean success = TRUE;

	switch (camel_mime_parser_step (parser, &buf, &len)) {
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		/* set the default type of a message always */
		if (dw->mime_type)
			camel_content_type_unref (dw->mime_type);
		dw->mime_type = camel_content_type_decode ("message/rfc822");
	case CAMEL_MIME_PARSER_STATE_HEADER:
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		/* we have the headers, build them into 'us' */
		headers = camel_mime_parser_headers_raw (parser);

		/* if content-type exists, process it first, set for fallback charset in headers */
		content = camel_header_raw_find (&headers, "content-type", NULL);
		if (content)
			mime_part_process_header ((CamelMedium *) dw, "content-type", content);

		while (headers) {
			if (g_ascii_strcasecmp (headers->name, "content-type") == 0
			    && headers->value != content)
				camel_medium_add_header ((CamelMedium *) dw, "X-Invalid-Content-Type", headers->value);
			else
				camel_medium_add_header ((CamelMedium *) dw, headers->name, headers->value);
			headers = headers->next;
		}

		success = camel_mime_part_construct_content_from_parser (
			mime_part, parser, cancellable, error);
		break;
	default:
		g_warning ("Invalid state encountered???: %u", camel_mime_parser_state (parser));
	}

	err = camel_mime_parser_errno (parser);
	if (err != 0) {
		errno = err;
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			"%s", g_strerror (errno));
		success = FALSE;
	}

	return success;
}

static void
mime_part_construct_from_parser_thread (GSimpleAsyncResult *simple,
                                        GObject *object,
                                        GCancellable *cancellable)
{
	AsyncContext *async_context;
	GError *error = NULL;

	async_context = g_simple_async_result_get_op_res_gpointer (simple);

	camel_mime_part_construct_from_parser_sync (
		CAMEL_MIME_PART (object), async_context->parser,
		cancellable, &error);

	if (error != NULL)
		g_simple_async_result_take_error (simple, error);
}

static void
mime_part_construct_from_parser (CamelMimePart *mime_part,
                                 CamelMimeParser *parser,
                                 gint io_priority,
                                 GCancellable *cancellable,
                                 GAsyncReadyCallback callback,
                                 gpointer user_data)
{
	GSimpleAsyncResult *simple;
	AsyncContext *async_context;

	async_context = g_slice_new0 (AsyncContext);
	async_context->parser = g_object_ref (parser);

	simple = g_simple_async_result_new (
		G_OBJECT (mime_part), callback, user_data,
		mime_part_construct_from_parser);

	g_simple_async_result_set_check_cancellable (simple, cancellable);

	g_simple_async_result_set_op_res_gpointer (
		simple, async_context, (GDestroyNotify) async_context_free);

	g_simple_async_result_run_in_thread (
		simple, mime_part_construct_from_parser_thread,
		io_priority, cancellable);

	g_object_unref (simple);
}

static gboolean
mime_part_construct_from_parser_finish (CamelMimePart *mime_part,
                                        GAsyncResult *result,
                                        GError **error)
{
	GSimpleAsyncResult *simple;

	g_return_val_if_fail (
		g_simple_async_result_is_valid (
		result, G_OBJECT (mime_part),
		mime_part_construct_from_parser), FALSE);

	simple = G_SIMPLE_ASYNC_RESULT (result);

	/* Assume success unless a GError is set. */
	return !g_simple_async_result_propagate_error (simple, error);
}

static void
camel_mime_part_class_init (CamelMimePartClass *class)
{
	GObjectClass *object_class;
	CamelMediumClass *medium_class;
	CamelDataWrapperClass *data_wrapper_class;

	g_type_class_add_private (class, sizeof (CamelMimePartPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = mime_part_set_property;
	object_class->get_property = mime_part_get_property;
	object_class->finalize = mime_part_finalize;

	medium_class = CAMEL_MEDIUM_CLASS (class);
	medium_class->add_header = mime_part_add_header;
	medium_class->set_header = mime_part_set_header;
	medium_class->remove_header = mime_part_remove_header;
	medium_class->get_header = mime_part_get_header;
	medium_class->get_headers = mime_part_get_headers;
	medium_class->free_headers = mime_part_free_headers;
	medium_class->set_content = mime_part_set_content;

	data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (class);
	data_wrapper_class->write_to_stream_sync = mime_part_write_to_stream_sync;
	data_wrapper_class->construct_from_stream_sync = mime_part_construct_from_stream_sync;

	class->construct_from_parser_sync = mime_part_construct_from_parser_sync;
	class->construct_from_parser = mime_part_construct_from_parser;
	class->construct_from_parser_finish = mime_part_construct_from_parser_finish;

	g_object_class_install_property (
		object_class,
		PROP_CONTENT_ID,
		g_param_spec_string (
			"content-id",
			"Content ID",
			NULL,
			NULL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_CONTENT_MD5,
		g_param_spec_string (
			"content-md5",
			"Content MD5",
			NULL,
			NULL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_DESCRIPTION,
		g_param_spec_string (
			"description",
			"Description",
			NULL,
			NULL,
			G_PARAM_READWRITE));

	g_object_class_install_property (
		object_class,
		PROP_DISPOSITION,
		g_param_spec_string (
			"disposition",
			"Disposition",
			NULL,
			NULL,
			G_PARAM_READWRITE));

	init_header_name_table ();
}

static void
camel_mime_part_init (CamelMimePart *mime_part)
{
	CamelDataWrapper *data_wrapper;

	mime_part->priv = CAMEL_MIME_PART_GET_PRIVATE (mime_part);
	mime_part->priv->encoding = CAMEL_TRANSFER_ENCODING_DEFAULT;

	data_wrapper = CAMEL_DATA_WRAPPER (mime_part);

	if (data_wrapper->mime_type != NULL)
		camel_content_type_unref (data_wrapper->mime_type);

	data_wrapper->mime_type = camel_content_type_new ("text", "plain");
}

/**
 * camel_mime_part_new:
 *
 * Create a new MIME part.
 *
 * Returns: a new #CamelMimePart
 **/
CamelMimePart *
camel_mime_part_new (void)
{
	return g_object_new (CAMEL_TYPE_MIME_PART, NULL);
}

/**
 * camel_mime_part_set_content:
 * @mime_part: a #CamelMimePart
 * @data: data to put into the part
 * @length: length of @data
 * @type: Content-Type of the data
 *
 * Utility function used to set the content of a mime part object to
 * be the provided data. If @length is 0, this routine can be used as
 * a way to remove old content (in which case @data and @type are
 * ignored and may be %NULL).
 **/
void
camel_mime_part_set_content (CamelMimePart *mime_part,
                             const gchar *data,
                             gint length,
                             const gchar *type) /* why on earth is the type last? */
{
	CamelMedium *medium = CAMEL_MEDIUM (mime_part);

	if (length) {
		CamelDataWrapper *dw;
		CamelStream *stream;

		dw = camel_data_wrapper_new ();
		camel_data_wrapper_set_mime_type (dw, type);
		stream = camel_stream_mem_new_with_buffer (data, length);
		camel_data_wrapper_construct_from_stream_sync (
			dw, stream, NULL, NULL);
		g_object_unref (stream);
		camel_medium_set_content (medium, dw);
		g_object_unref (dw);
	} else
		camel_medium_set_content (medium, NULL);
}

/**
 * camel_mime_part_get_content_disposition:
 * @mime_part: a #CamelMimePart
 *
 * Get the disposition of the MIME part as a structure.
 * Returned pointer is owned by @mime_part.
 *
 * Returns: the disposition structure
 *
 * Since: 2.30
 **/
const CamelContentDisposition *
camel_mime_part_get_content_disposition (CamelMimePart *mime_part)
{
	g_return_val_if_fail (mime_part != NULL, NULL);

	return mime_part->priv->disposition;
}

/**
 * camel_mime_part_get_content_id:
 * @mime_part: a #CamelMimePart
 *
 * Get the content-id field of a MIME part.
 *
 * Returns: the content-id field of the MIME part
 **/
const gchar *
camel_mime_part_get_content_id (CamelMimePart *mime_part)
{
	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), NULL);

	return mime_part->priv->content_id;
}

/**
 * camel_mime_part_set_content_id:
 * @mime_part: a #CamelMimePart
 * @contentid: content id
 *
 * Set the content-id field on a MIME part.
 **/
void
camel_mime_part_set_content_id (CamelMimePart *mime_part,
                                const gchar *contentid)
{
	CamelMedium *medium;
	gchar *cid, *id;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));

	medium = CAMEL_MEDIUM (mime_part);

	if (contentid)
		id = g_strstrip (g_strdup (contentid));
	else
		id = camel_header_msgid_generate ();

	cid = g_strdup_printf ("<%s>", id);
	camel_medium_set_header (medium, "Content-ID", cid);
	g_free (cid);

	g_free (id);

	g_object_notify (G_OBJECT (mime_part), "content-id");
}

/**
 * camel_mime_part_get_content_location:
 * @mime_part: a #CamelMimePart
 *
 * Get the content-location field of a MIME part.
 *
 * Returns: the content-location field of a MIME part
 **/
const gchar *
camel_mime_part_get_content_location (CamelMimePart *mime_part)
{
	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), NULL);

	return mime_part->priv->content_location;
}

/**
 * camel_mime_part_set_content_location:
 * @mime_part: a #CamelMimePart
 * @location: the content-location value of the MIME part
 *
 * Set the content-location field of the MIME part.
 **/
void
camel_mime_part_set_content_location (CamelMimePart *mime_part,
                                      const gchar *location)
{
	CamelMedium *medium;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));

	medium = CAMEL_MEDIUM (mime_part);

	/* FIXME: this should perform content-location folding */
	camel_medium_set_header (medium, "Content-Location", location);

	g_object_notify (G_OBJECT (mime_part), "content-location");
}

/**
 * camel_mime_part_get_content_md5:
 * @mime_part: a #CamelMimePart
 *
 * Get the content-md5 field of the MIME part.
 *
 * Returns: the content-md5 field of the MIME part
 **/
const gchar *
camel_mime_part_get_content_md5 (CamelMimePart *mime_part)
{
	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), NULL);

	return mime_part->priv->content_md5;
}

/**
 * camel_mime_part_set_content_md5:
 * @mime_part: a #CamelMimePart
 * @md5sum: the md5sum of the MIME part
 *
 * Set the content-md5 field of the MIME part.
 **/
void
camel_mime_part_set_content_md5 (CamelMimePart *mime_part,
                                 const gchar *content_md5)
{
	CamelMedium *medium;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));

	medium = CAMEL_MEDIUM (mime_part);

	camel_medium_set_header (medium, "Content-MD5", content_md5);
}

/**
 * camel_mime_part_get_content_languages:
 * @mime_part: a #CamelMimePart
 *
 * Get the Content-Languages set on the MIME part.
 *
 * Returns: a #GList of languages
 **/
const GList *
camel_mime_part_get_content_languages (CamelMimePart *mime_part)
{
	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), NULL);

	return mime_part->priv->content_languages;
}

/**
 * camel_mime_part_set_content_languages:
 * @mime_part: a #CamelMimePart
 * @content_languages: list of languages
 *
 * Set the Content-Languages field of a MIME part.
 **/
void
camel_mime_part_set_content_languages (CamelMimePart *mime_part,
                                       GList *content_languages)
{
	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));

	if (mime_part->priv->content_languages)
		camel_string_list_free (mime_part->priv->content_languages);

	mime_part->priv->content_languages = content_languages;

	/* FIXME: translate to a header and set it */
}

/**
 * camel_mime_part_get_content_type:
 * @mime_part: a #CamelMimePart
 *
 * Get the Content-Type of a MIME part.
 *
 * Returns: the parsed #CamelContentType of the MIME part
 **/
CamelContentType *
camel_mime_part_get_content_type (CamelMimePart *mime_part)
{
	CamelDataWrapper *data_wrapper;

	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), NULL);

	data_wrapper = CAMEL_DATA_WRAPPER (mime_part);

	return camel_data_wrapper_get_mime_type_field (data_wrapper);
}

/**
 * camel_mime_part_set_content_type:
 * @mime_part: a #CamelMimePart
 * @content_type: content-type string
 *
 * Set the content-type on a MIME part.
 **/
void
camel_mime_part_set_content_type (CamelMimePart *mime_part,
                                  const gchar *content_type)
{
	CamelMedium *medium;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));

	medium = CAMEL_MEDIUM (mime_part);

	camel_medium_set_header (medium, "Content-Type", content_type);
}

/**
 * camel_mime_part_get_description:
 * @mime_part: a #CamelMimePart
 *
 * Get the description of the MIME part.
 *
 * Returns: the description
 **/
const gchar *
camel_mime_part_get_description (CamelMimePart *mime_part)
{
	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), NULL);

	return mime_part->priv->description;
}

/**
 * camel_mime_part_set_description:
 * @mime_part: a #CamelMimePart
 * @description: description of the MIME part
 *
 * Set a description on the MIME part.
 **/
void
camel_mime_part_set_description (CamelMimePart *mime_part,
                                 const gchar *description)
{
	CamelMedium *medium;
	gchar *text;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));
	g_return_if_fail (description != NULL);

	medium = CAMEL_MEDIUM (mime_part);

	text = camel_header_encode_string ((guchar *) description);
	camel_medium_set_header (medium, "Content-Description", text);
	g_free (text);

	g_object_notify (G_OBJECT (mime_part), "description");
}

/**
 * camel_mime_part_get_disposition:
 * @mime_part: a #CamelMimePart
 *
 * Get the disposition of the MIME part.
 *
 * Returns: the disposition
 **/
const gchar *
camel_mime_part_get_disposition (CamelMimePart *mime_part)
{
	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), NULL);

	if (mime_part->priv->disposition)
		return mime_part->priv->disposition->disposition;
	else
		return NULL;
}

/**
 * camel_mime_part_set_disposition:
 * @mime_part: a #CamelMimePart
 * @disposition: disposition of the MIME part
 *
 * Set a disposition on the MIME part.
 **/
void
camel_mime_part_set_disposition (CamelMimePart *mime_part,
                                 const gchar *disposition)
{
	CamelMedium *medium;
	gchar *text;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));

	medium = CAMEL_MEDIUM (mime_part);

	/* we poke in a new disposition (so we dont lose 'filename', etc) */
	if (mime_part->priv->disposition == NULL)
		mime_part_set_disposition (mime_part, disposition);

	if (mime_part->priv->disposition != NULL) {
		g_free (mime_part->priv->disposition->disposition);
		mime_part->priv->disposition->disposition = g_strdup (disposition);
	}

	text = camel_content_disposition_format (mime_part->priv->disposition);
	camel_medium_set_header (medium, "Content-Disposition", text);
	g_free (text);

	g_object_notify (G_OBJECT (mime_part), "disposition");
}

/**
 * camel_mime_part_get_encoding:
 * @mime_part: a #CamelMimePart
 *
 * Get the Content-Transfer-Encoding of a MIME part.
 *
 * Returns: a #CamelTransferEncoding
 **/
CamelTransferEncoding
camel_mime_part_get_encoding (CamelMimePart *mime_part)
{
	g_return_val_if_fail (
		CAMEL_IS_MIME_PART (mime_part),
		CAMEL_TRANSFER_ENCODING_DEFAULT);

	return mime_part->priv->encoding;
}

/**
 * camel_mime_part_set_encoding:
 * @mime_part: a #CamelMimePart
 * @encoding: a #CamelTransferEncoding
 *
 * Set the Content-Transfer-Encoding to use on a MIME part.
 **/
void
camel_mime_part_set_encoding (CamelMimePart *mime_part,
                              CamelTransferEncoding encoding)
{
	CamelMedium *medium;
	const gchar *text;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));

	medium = CAMEL_MEDIUM (mime_part);

	text = camel_transfer_encoding_to_string (encoding);
	camel_medium_set_header (medium, "Content-Transfer-Encoding", text);
}

/**
 * camel_mime_part_get_filename:
 * @mime_part: a #CamelMimePart
 *
 * Get the filename of a MIME part.
 *
 * Returns: the filename of the MIME part
 **/
const gchar *
camel_mime_part_get_filename (CamelMimePart *mime_part)
{
	if (mime_part->priv->disposition) {
		const gchar *name = camel_header_param (
			mime_part->priv->disposition->params, "filename");
		if (name)
			return name;
	}

	return camel_content_type_param (
		((CamelDataWrapper *) mime_part)->mime_type, "name");
}

/**
 * camel_mime_part_set_filename:
 * @mime_part: a #CamelMimePart
 * @filename: filename given to the MIME part
 *
 * Set the filename on a MIME part.
 **/
void
camel_mime_part_set_filename (CamelMimePart *mime_part,
                              const gchar *filename)
{
	CamelDataWrapper *dw;
	CamelMedium *medium;
	gchar *str;

	medium = CAMEL_MEDIUM (mime_part);

	if (mime_part->priv->disposition == NULL)
		mime_part->priv->disposition =
			camel_content_disposition_decode ("attachment");

	camel_header_set_param (
		&mime_part->priv->disposition->params, "filename", filename);
	str = camel_content_disposition_format (mime_part->priv->disposition);

	camel_medium_set_header (medium, "Content-Disposition", str);
	g_free (str);

	dw = (CamelDataWrapper *) mime_part;
	if (!dw->mime_type)
		dw->mime_type = camel_content_type_new ("application", "octet-stream");
	camel_content_type_set_param (dw->mime_type, "name", filename);
	str = camel_content_type_format (dw->mime_type);
	camel_medium_set_header (medium, "Content-Type", str);
	g_free (str);
}

/**
 * camel_mime_part_construct_from_parser_sync:
 * @mime_part: a #CamelMimePart
 * @parser: a #CamelMimeParser
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * Constructs a MIME part from a parser.
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_mime_part_construct_from_parser_sync (CamelMimePart *mime_part,
                                            CamelMimeParser *parser,
                                            GCancellable *cancellable,
                                            GError **error)
{
	CamelMimePartClass *class;
	gboolean success;

	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), FALSE);
	g_return_val_if_fail (CAMEL_IS_MIME_PARSER (parser), FALSE);

	class = CAMEL_MIME_PART_GET_CLASS (mime_part);
	g_return_val_if_fail (class->construct_from_parser_sync != NULL, FALSE);

	success = class->construct_from_parser_sync (
		mime_part, parser, cancellable, error);
	CAMEL_CHECK_GERROR (
		mime_part, construct_from_parser_sync, success, error);

	return success;
}

/**
 * camel_mime_part_construct_from_parser:
 * @mime_part: a #CamelMimePart
 * @parser: a #CamelMimeParser
 * @io_priority: the I/O priority of the request
 * @cancellable: optional #GCancellable object, or %NULL
 * @callback: a #GAsyncReadyCallback to call when the request is satisfied
 * @user_data: data to pass to the callback function
 *
 * Asynchronously constructs a MIME part from a parser.
 *
 * When the operation is finished, @callback will be called.  You can then
 * call camel_mime_part_construct_from_parser_finish() to get the result of
 * the operation.
 *
 * Since: 3.0
 **/
void
camel_mime_part_construct_from_parser (CamelMimePart *mime_part,
                                       CamelMimeParser *parser,
                                       gint io_priority,
                                       GCancellable *cancellable,
                                       GAsyncReadyCallback callback,
                                       gpointer user_data)
{
	CamelMimePartClass *class;

	g_return_if_fail (CAMEL_IS_MIME_PART (mime_part));
	g_return_if_fail (CAMEL_IS_MIME_PARSER (parser));

	class = CAMEL_MIME_PART_GET_CLASS (mime_part);
	g_return_if_fail (class->construct_from_parser != NULL);

	class->construct_from_parser (
		mime_part, parser, io_priority,
		cancellable, callback, user_data);
}

/**
 * camel_mime_part_construct_from_parser_finish:
 * @mime_part: a #CamelMimePart
 * @result: a #GAsyncResult
 * @error: return location for a #GError, or %NULL
 *
 * Finishes the operation started with camel_mime_part_construct_from_parser().
 *
 * Returns: %TRUE on success, %FALSE on error
 *
 * Since: 3.0
 **/
gboolean
camel_mime_part_construct_from_parser_finish (CamelMimePart *mime_part,
                                              GAsyncResult *result,
                                              GError **error)
{
	CamelMimePartClass *class;

	g_return_val_if_fail (CAMEL_IS_MIME_PART (mime_part), FALSE);
	g_return_val_if_fail (G_IS_ASYNC_RESULT (result), FALSE);

	class = CAMEL_MIME_PART_GET_CLASS (mime_part);
	g_return_val_if_fail (class->construct_from_parser_finish != NULL, FALSE);

	return class->construct_from_parser_finish (mime_part, result, error);
}
