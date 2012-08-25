/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-multipart.c : Abstract class for a multipart */
/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
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

#include <errno.h>
#include <string.h> /* strlen() */
#include <time.h>   /* for time */
#include <unistd.h> /* for getpid */

#include "camel-mime-part.h"
#include "camel-multipart.h"
#include "camel-stream-mem.h"

#define d(x)

G_DEFINE_TYPE (CamelMultipart, camel_multipart, CAMEL_TYPE_DATA_WRAPPER)

static void
multipart_dispose (GObject *object)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (object);

	g_list_foreach (multipart->parts, (GFunc) g_object_unref, NULL);
	g_list_free (multipart->parts);
	multipart->parts = NULL;

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (camel_multipart_parent_class)->dispose (object);
}

static void
multipart_finalize (GObject *object)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (object);

	g_free (multipart->preface);
	g_free (multipart->postface);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_multipart_parent_class)->finalize (object);
}

static gboolean
multipart_is_offline (CamelDataWrapper *data_wrapper)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (data_wrapper);
	GList *node;
	CamelDataWrapper *part;

	if (CAMEL_DATA_WRAPPER_CLASS (camel_multipart_parent_class)->is_offline (data_wrapper))
		return TRUE;
	for (node = multipart->parts; node; node = node->next) {
		part = node->data;
		if (camel_data_wrapper_is_offline (part))
			return TRUE;
	}

	return FALSE;
}

/* this is MIME specific, doesn't belong here really */
static gssize
multipart_write_to_stream_sync (CamelDataWrapper *data_wrapper,
                                CamelStream *stream,
                                GCancellable *cancellable,
                                GError **error)
{
	CamelMultipart *multipart = CAMEL_MULTIPART (data_wrapper);
	const gchar *boundary;
	GList *node;
	gchar *content;
	gssize total = 0;
	gssize count;

	/* get the bundary text */
	boundary = camel_multipart_get_boundary (multipart);

	/* we cannot write a multipart without a boundary string */
	g_return_val_if_fail (boundary, -1);

	/*
	 * write the preface text (usually something like
	 *   "This is a mime message, if you see this, then
	 *    your mail client probably doesn't support ...."
	 */
	if (multipart->preface) {
		count = camel_stream_write_string (
			stream, multipart->preface, cancellable, error);
		if (count == -1)
			return -1;
		total += count;
	}

	/*
	 * Now, write all the parts, separated by the boundary
	 * delimiter
	 */
	node = multipart->parts;
	while (node) {
		content = g_strdup_printf ("\n--%s\n", boundary);
		count = camel_stream_write_string (
			stream, content, cancellable, error);
		g_free (content);
		if (count == -1)
			return -1;
		total += count;

		count = camel_data_wrapper_write_to_stream_sync (
			CAMEL_DATA_WRAPPER (node->data),
			stream, cancellable, error);
		if (count == -1)
			return -1;
		total += count;
		node = node->next;
	}

	/* write the terminating boudary delimiter */
	content = g_strdup_printf ("\n--%s--\n", boundary);
	count = camel_stream_write_string (
		stream, content, cancellable, error);
	g_free (content);
	if (count == -1)
		return -1;
	total += count;

	/* and finally the postface */
	if (multipart->postface) {
		count = camel_stream_write_string (
			stream, multipart->postface, cancellable, error);
		if (count == -1)
			return -1;
		total += count;
	}

	return total;
}

static void
multipart_add_part (CamelMultipart *multipart,
                    CamelMimePart *part)
{
	multipart->parts = g_list_append (
		multipart->parts, g_object_ref (part));
}

static void
multipart_add_part_at (CamelMultipart *multipart,
                       CamelMimePart *part,
                       guint index)
{
	multipart->parts = g_list_insert (
		multipart->parts, g_object_ref (part), index);
}

static void
multipart_remove_part (CamelMultipart *multipart,
                       CamelMimePart *part)
{
	/* Make sure we don't unref a part we don't have. */
	if (g_list_find (multipart->parts, part) == NULL)
		return;

	multipart->parts = g_list_remove (multipart->parts, part);
	g_object_unref (part);
}

static CamelMimePart *
multipart_remove_part_at (CamelMultipart *multipart,
                          guint index)
{
	CamelMimePart *removed_part;
	GList *link;

	if (!(multipart->parts))
		return NULL;

	link = g_list_nth (multipart->parts, index);
	if (link == NULL) {
		g_warning (
			"CamelMultipart::remove_part_at: "
			"part to remove is NULL\n");
		return NULL;
	}
	removed_part = CAMEL_MIME_PART (link->data);

	multipart->parts = g_list_remove_link (multipart->parts, link);
	if (link->data)
		g_object_unref (link->data);
	g_list_free_1 (link);

	return removed_part;
}

static CamelMimePart *
multipart_get_part (CamelMultipart *multipart,
                    guint index)
{
	GList *part;

	if (!(multipart->parts))
		return NULL;

	part = g_list_nth (multipart->parts, index);
	if (part)
		return CAMEL_MIME_PART (part->data);
	else
		return NULL;
}

static guint
multipart_get_number (CamelMultipart *multipart)
{
	return g_list_length (multipart->parts);
}

static void
multipart_set_boundary (CamelMultipart *multipart,
                        const gchar *boundary)
{
	CamelDataWrapper *cdw = CAMEL_DATA_WRAPPER (multipart);
	gchar *bgen, bbuf[27], *p;
	guint8 *digest;
	gsize length;
	gint state, save;

	g_return_if_fail (cdw->mime_type != NULL);

	length = g_checksum_type_get_length (G_CHECKSUM_MD5);
	digest = g_alloca (length);

	if (!boundary) {
		GChecksum *checksum;

		/* Generate a fairly random boundary string. */
		bgen = g_strdup_printf (
			"%p:%lu:%lu",
			(gpointer) multipart,
			(gulong) getpid (),
			(gulong) time (NULL));

		checksum = g_checksum_new (G_CHECKSUM_MD5);
		g_checksum_update (checksum, (guchar *) bgen, -1);
		g_checksum_get_digest (checksum, digest, &length);
		g_checksum_free (checksum);

		g_free (bgen);
		strcpy (bbuf, "=-");
		p = bbuf + 2;
		state = save = 0;
		p += g_base64_encode_step (
			(guchar *) digest, length, FALSE, p, &state, &save);
		*p = '\0';

		boundary = bbuf;
	}

	camel_content_type_set_param (cdw->mime_type, "boundary", boundary);
}

static const gchar *
multipart_get_boundary (CamelMultipart *multipart)
{
	CamelDataWrapper *cdw = CAMEL_DATA_WRAPPER (multipart);

	g_return_val_if_fail (cdw->mime_type != NULL, NULL);
	return camel_content_type_param (cdw->mime_type, "boundary");
}

static gint
multipart_construct_from_parser (CamelMultipart *multipart,
                                 CamelMimeParser *mp)
{
	gint err;
	CamelContentType *content_type;
	CamelMimePart *bodypart;
	gchar *buf;
	gsize len;

	g_assert (camel_mime_parser_state (mp) == CAMEL_MIME_PARSER_STATE_MULTIPART);

	/* FIXME: we should use a came-mime-mutlipart, not jsut a camel-multipart, but who cares */
	d (printf ("Creating multi-part\n"));

	content_type = camel_mime_parser_content_type (mp);
	camel_multipart_set_boundary (
		multipart,
		camel_content_type_param (content_type, "boundary"));

	while (camel_mime_parser_step (mp, &buf, &len) != CAMEL_MIME_PARSER_STATE_MULTIPART_END) {
		camel_mime_parser_unstep (mp);
		bodypart = camel_mime_part_new ();
		camel_mime_part_construct_from_parser_sync (
			bodypart, mp, NULL, NULL);
		camel_multipart_add_part (multipart, bodypart);
		g_object_unref (bodypart);
	}

	/* these are only return valid data in the MULTIPART_END state */
	camel_multipart_set_preface (multipart, camel_mime_parser_preface (mp));
	camel_multipart_set_postface (multipart, camel_mime_parser_postface (mp));

	err = camel_mime_parser_errno (mp);
	if (err != 0) {
		errno = err;
		return -1;
	} else
		return 0;
}

static void
camel_multipart_class_init (CamelMultipartClass *class)
{
	GObjectClass *object_class;
	CamelDataWrapperClass *data_wrapper_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = multipart_dispose;
	object_class->finalize = multipart_finalize;

	data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS (class);
	data_wrapper_class->is_offline = multipart_is_offline;
	data_wrapper_class->write_to_stream_sync = multipart_write_to_stream_sync;
	data_wrapper_class->decode_to_stream_sync = multipart_write_to_stream_sync;

	class->add_part = multipart_add_part;
	class->add_part_at = multipart_add_part_at;
	class->remove_part = multipart_remove_part;
	class->remove_part_at = multipart_remove_part_at;
	class->get_part = multipart_get_part;
	class->get_number = multipart_get_number;
	class->set_boundary = multipart_set_boundary;
	class->get_boundary = multipart_get_boundary;
	class->construct_from_parser = multipart_construct_from_parser;
}

static void
camel_multipart_init (CamelMultipart *multipart)
{
	camel_data_wrapper_set_mime_type (
		CAMEL_DATA_WRAPPER (multipart), "multipart/mixed");
	multipart->parts = NULL;
	multipart->preface = NULL;
	multipart->postface = NULL;
}

/**
 * camel_multipart_new:
 *
 * Create a new #CamelMultipart object.
 *
 * Returns: a new #CamelMultipart object
 **/
CamelMultipart *
camel_multipart_new (void)
{
	CamelMultipart *multipart;

	multipart = g_object_new (CAMEL_TYPE_MULTIPART, NULL);
	multipart->preface = NULL;
	multipart->postface = NULL;

	return multipart;
}

/**
 * camel_multipart_add_part:
 * @multipart: a #CamelMultipart object
 * @part: a #CamelMimePart to add
 *
 * Appends the part to the multipart object.
 **/
void
camel_multipart_add_part (CamelMultipart *multipart,
                          CamelMimePart *part)
{
	CamelMultipartClass *class;

	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_if_fail (class->add_part != NULL);

	class->add_part (multipart, part);
}

/**
 * camel_multipart_add_part_at:
 * @multipart: a #CamelMultipart object
 * @part: a #CamelMimePart to add
 * @index: index to add the multipart at
 *
 * Adds the part to the multipart object after the @index'th
 * element. If @index is greater than the number of parts, it is
 * equivalent to camel_multipart_add_part().
 **/
void
camel_multipart_add_part_at (CamelMultipart *multipart,
                             CamelMimePart *part,
                             guint index)
{
	CamelMultipartClass *class;

	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_if_fail (class->add_part_at != NULL);

	class->add_part_at (multipart, part, index);
}

/**
 * camel_multipart_remove_part:
 * @multipart: a #CamelMultipart object
 * @part: a #CamelMimePart to remove
 *
 * Removes @part from @multipart.
 **/
void
camel_multipart_remove_part (CamelMultipart *multipart,
                             CamelMimePart *part)
{
	CamelMultipartClass *class;

	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));
	g_return_if_fail (CAMEL_IS_MIME_PART (part));

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_if_fail (class->remove_part != NULL);

	class->remove_part (multipart, part);
}

/**
 * camel_multipart_remove_part_at:
 * @multipart: a #CamelMultipart object
 * @index: a zero-based index indicating the part to remove
 *
 * Remove the indicated part from the multipart object.
 *
 * Returns: the removed part. Note that it is g_object_unref()'ed
 * before being returned, which may cause it to be destroyed.
 **/
CamelMimePart *
camel_multipart_remove_part_at (CamelMultipart *multipart,
                                guint index)
{
	CamelMultipartClass *class;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_val_if_fail (class->remove_part_at != NULL, NULL);

	return class->remove_part_at (multipart, index);
}

/**
 * camel_multipart_get_part:
 * @multipart: a #CamelMultipart object
 * @index: a zero-based index indicating the part to get
 *
 * Returns: the indicated subpart, or %NULL
 **/
CamelMimePart *
camel_multipart_get_part (CamelMultipart *multipart,
                          guint index)
{
	CamelMultipartClass *class;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_val_if_fail (class->get_part != NULL, NULL);

	return class->get_part (multipart, index);
}

/**
 * camel_multipart_get_number:
 * @multipart: a #CamelMultipart object
 *
 * Returns: the number of subparts in @multipart
 **/
guint
camel_multipart_get_number (CamelMultipart *multipart)
{
	CamelMultipartClass *class;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), 0);

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_val_if_fail (class->get_number != NULL, 0);

	return class->get_number (multipart);
}

/**
 * camel_multipart_set_boundary:
 * @multipart: a #CamelMultipart object
 * @boundary: the message boundary, or %NULL
 *
 * Sets the message boundary for @multipart to @boundary. This should
 * be a string which does not occur anywhere in any of @multipart's
 * subparts. If @boundary is %NULL, a randomly-generated boundary will
 * be used.
 **/
void
camel_multipart_set_boundary (CamelMultipart *multipart,
                              const gchar *boundary)
{
	CamelMultipartClass *class;

	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_if_fail (class->set_boundary != NULL);

	class->set_boundary (multipart, boundary);
}

/**
 * camel_multipart_get_boundary:
 * @multipart: a #CamelMultipart object
 *
 * Returns: the boundary
 **/
const gchar *
camel_multipart_get_boundary (CamelMultipart *multipart)
{
	CamelMultipartClass *class;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), NULL);

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_val_if_fail (class->get_boundary != NULL, NULL);

	return class->get_boundary (multipart);
}

/**
 * camel_multipart_set_preface:
 * @multipart: a #CamelMultipart object
 * @preface: the multipart preface
 *
 * Set the preface text for this multipart.  Will be written out infront
 * of the multipart.  This text should only include US-ASCII strings, and
 * be relatively short, and will be ignored by any MIME mail client.
 **/
void
camel_multipart_set_preface (CamelMultipart *multipart,
                             const gchar *preface)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));

	if (multipart->preface == preface)
		return;

	g_free (multipart->preface);
	multipart->preface = g_strdup (preface);
}

/**
 * camel_multipart_set_postface:
 * @multipart: a #CamelMultipart object
 * @postface: multipat postface
 *
 * Set the postfix text for this multipart.  Will be written out after
 * the last boundary of the multipart, and ignored by any MIME mail
 * client.
 *
 * Generally postface texts should not be sent with multipart messages.
 **/
void
camel_multipart_set_postface (CamelMultipart *multipart,
                              const gchar *postface)
{
	g_return_if_fail (CAMEL_IS_MULTIPART (multipart));

	if (multipart->postface == postface)
		return;

	g_free (multipart->postface);
	multipart->postface = g_strdup (postface);
}

/**
 * camel_multipart_construct_from_parser:
 * @multipart: a #CamelMultipart object
 * @parser: a #CamelMimeParser object
 *
 * Construct a multipart from a parser.
 *
 * Returns: %0 on success or %-1 on fail
 **/
gint
camel_multipart_construct_from_parser (CamelMultipart *multipart,
                                       CamelMimeParser *mp)
{
	CamelMultipartClass *class;

	g_return_val_if_fail (CAMEL_IS_MULTIPART (multipart), -1);
	g_return_val_if_fail (CAMEL_IS_MIME_PARSER (mp), -1);

	class = CAMEL_MULTIPART_GET_CLASS (multipart);
	g_return_val_if_fail (class->construct_from_parser != NULL, -1);

	return class->construct_from_parser (multipart, mp);
}
