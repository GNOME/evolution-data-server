/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- 
 * camel-multipart.c : Abstract class for a multipart 
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *
 * Copyright 2002 Ximian, Inc. (www.ximian.com)
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_ALLOCA_H
#include <alloca.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "libedataserver/md5-utils.h"

#include "camel-exception.h"
#include "camel-i18n.h"
#include "camel-mime-filter-canon.h"
#include "camel-mime-filter-crlf.h"
#include "camel-mime-message.h"
#include "camel-mime-parser.h"
#include "camel-mime-part.h"
#include "camel-mime-part.h"
#include "camel-multipart-signed.h"
#include "camel-seekable-substream.h"
#include "camel-stream-filter.h"
#include "camel-stream-mem.h"

#define d(x) /*(printf("%s(%d): ", __FILE__, __LINE__),(x))
	       #include <stdio.h>;*/

static void signed_add_part (CamelMultipart *multipart, CamelMimePart *part);
static void signed_add_part_at (CamelMultipart *multipart, CamelMimePart *part, guint index);
static void signed_remove_part (CamelMultipart *multipart, CamelMimePart *part);
static CamelMimePart *signed_remove_part_at (CamelMultipart *multipart, guint index);
static CamelMimePart *signed_get_part (CamelMultipart *multipart, guint index);
static guint signed_get_number (CamelMultipart *multipart);

static ssize_t write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream);
static void set_mime_type_field (CamelDataWrapper *data_wrapper, CamelContentType *mime_type);
static int construct_from_stream (CamelDataWrapper *data_wrapper, CamelStream *stream);
static int signed_construct_from_parser (CamelMultipart *multipart, struct _CamelMimeParser *mp);

static CamelMultipartClass *parent_class = NULL;

/* Returns the class for a CamelMultipartSigned */
#define CMP_CLASS(so) CAMEL_MULTIPART_SIGNED_CLASS (CAMEL_OBJECT_GET_CLASS(so))

/* Returns the class for a CamelDataWrapper */
#define CDW_CLASS(so) CAMEL_DATA_WRAPPER_CLASS (CAMEL_OBJECT_GET_CLASS(so))

static void
camel_multipart_signed_class_init (CamelMultipartSignedClass *camel_multipart_signed_class)
{
	CamelDataWrapperClass *camel_data_wrapper_class = CAMEL_DATA_WRAPPER_CLASS(camel_multipart_signed_class);
	CamelMultipartClass *mpclass = (CamelMultipartClass *)camel_multipart_signed_class;

	parent_class = (CamelMultipartClass *)camel_multipart_get_type();

	/* virtual method overload */
	camel_data_wrapper_class->construct_from_stream = construct_from_stream;
	camel_data_wrapper_class->write_to_stream = write_to_stream;
	camel_data_wrapper_class->decode_to_stream = write_to_stream;
	camel_data_wrapper_class->set_mime_type_field = set_mime_type_field;

	mpclass->add_part = signed_add_part;
	mpclass->add_part_at = signed_add_part_at;
	mpclass->remove_part = signed_remove_part;
	mpclass->remove_part_at = signed_remove_part_at;
	mpclass->get_part = signed_get_part;
	mpclass->get_number = signed_get_number;
	mpclass->construct_from_parser = signed_construct_from_parser;

/*
	mpclass->get_boundary = signed_get_boundary;
	mpclass->set_boundary = signed_set_boundary;
*/
}

static void
camel_multipart_signed_init (gpointer object, gpointer klass)
{
	CamelMultipartSigned *multipart = (CamelMultipartSigned *)object;

	camel_data_wrapper_set_mime_type(CAMEL_DATA_WRAPPER(multipart), "multipart/signed");
	multipart->start1 = -1;
}

static void
camel_multipart_signed_finalize (CamelObject *object)
{
	CamelMultipartSigned *mps = (CamelMultipartSigned *)object;

	g_free(mps->protocol);
	g_free(mps->micalg);
	if (mps->signature)
		camel_object_unref((CamelObject *)mps->signature);
	if (mps->content)
		camel_object_unref((CamelObject *)mps->content);
	if (mps->contentraw)
		camel_object_unref((CamelObject *)mps->contentraw);
}

CamelType
camel_multipart_signed_get_type (void)
{
	static CamelType camel_multipart_signed_type = CAMEL_INVALID_TYPE;

	if (camel_multipart_signed_type == CAMEL_INVALID_TYPE) {
		camel_multipart_signed_type = camel_type_register (camel_multipart_get_type (), "CamelMultipartSigned",
								   sizeof (CamelMultipartSigned),
								   sizeof (CamelMultipartSignedClass),
								   (CamelObjectClassInitFunc) camel_multipart_signed_class_init,
								   NULL,
								   (CamelObjectInitFunc) camel_multipart_signed_init,
								   (CamelObjectFinalizeFunc) camel_multipart_signed_finalize);
	}

	return camel_multipart_signed_type;
}

/**
 * camel_multipart_signed_new:
 *
 * Create a new #CamelMultipartSigned object.
 *
 * A MultipartSigned should be used to store and create parts of
 * type "multipart/signed".  This is because multipart/signed is
 * entirely broken-by-design (tm) and uses completely
 * different semantics to other mutlipart types.  It must be treated
 * as opaque data by any transport.  See rfc 3156 for details.
 *
 * There are 3 ways to create the part:
 * Use construct_from_stream.  If this is used, then you must
 * set the mime_type appropriately to match the data uses, so
 * that the multiple parts my be extracted.
 *
 * Use construct_from_parser.  The parser MUST be in the #CAMEL_MIME_PARSER_STATE_HEADER
 * state, and the current content_type MUST be "multipart/signed" with
 * the appropriate boundary and it SHOULD include the appropriate protocol
 * and hash specifiers.
 *
 * Use sign_part.  A signature part will automatically be created
 * and the whole part may be written using write_to_stream to
 * create a 'transport-safe' version (as safe as can be expected with
 * such a broken specification).
 *
 * Returns a new #CamelMultipartSigned object
 **/
CamelMultipartSigned *
camel_multipart_signed_new (void)
{
	CamelMultipartSigned *multipart;

	multipart = (CamelMultipartSigned *)camel_object_new(CAMEL_MULTIPART_SIGNED_TYPE);

	return multipart;
}

static int
skip_content(CamelMimeParser *cmp)
{
	char *buf;
	size_t len;
	int state;

	switch (camel_mime_parser_state(cmp)) {
	case CAMEL_MIME_PARSER_STATE_HEADER:
		/* body part */
		while (camel_mime_parser_step(cmp, &buf, &len) != CAMEL_MIME_PARSER_STATE_BODY_END)
			/* NOOP */ ;
		break;
	case CAMEL_MIME_PARSER_STATE_MESSAGE:
		/* message body part */
		(void)camel_mime_parser_step(cmp, &buf, &len);
		skip_content(cmp);

		/* clean up followon state if any, see camel-mime-message.c */
		state = camel_mime_parser_step(cmp, &buf, &len);
		switch (state) {
		case CAMEL_MIME_PARSER_STATE_EOF:
		case CAMEL_MIME_PARSER_STATE_FROM_END: /* these doesn't belong to us */
			camel_mime_parser_unstep(cmp);
		case CAMEL_MIME_PARSER_STATE_MESSAGE_END:
			break;
		default:
			g_error ("Bad parser state: Expecting MESSAGE_END or EOF or EOM, got: %u", camel_mime_parser_state (cmp));
			camel_mime_parser_unstep(cmp);
			return -1;
		}
		break;
	case CAMEL_MIME_PARSER_STATE_MULTIPART:
		/* embedded multipart */
		while ((state = camel_mime_parser_step(cmp, &buf, &len)) != CAMEL_MIME_PARSER_STATE_MULTIPART_END)
			skip_content(cmp);
		break;
	default:
		g_warning("Invalid state encountered???: %u", camel_mime_parser_state (cmp));
	}

	return 0;
}

static int
parse_content(CamelMultipartSigned *mps)
{
	CamelMimeParser *cmp;
	CamelMultipart *mp = (CamelMultipart *)mps;
	CamelStreamMem *mem;
	const char *boundary;
	char *buf;
	size_t len;
	int state;

	boundary = camel_multipart_get_boundary(mp);
	if (boundary == NULL) {
		g_warning("Trying to get multipart/signed content without setting boundary first");
		return -1;
	}

	mem = (CamelStreamMem *)((CamelDataWrapper *)mps)->stream;
	if (mem == NULL) {
		g_warning("Trying to parse multipart/signed without constructing first");
		return -1;
	}

	/* This is all seriously complex.
	   This is so we can parse all cases properly, without altering the content.
	   All we are doing is finding part offsets. */

	camel_stream_reset((CamelStream *)mem);
	cmp = camel_mime_parser_new();
	camel_mime_parser_init_with_stream(cmp, (CamelStream *)mem);
	camel_mime_parser_push_state(cmp, CAMEL_MIME_PARSER_STATE_MULTIPART, boundary);

	mps->start1 = -1;
	mps->end1 = -1;
	mps->start2 = -1;
	mps->end2 = -1;

	while ((state = camel_mime_parser_step(cmp, &buf, &len)) != CAMEL_MIME_PARSER_STATE_MULTIPART_END) {
		if (mps->start1 == -1) {
			mps->start1 = camel_mime_parser_tell_start_headers(cmp);
		} else if (mps->start2 == -1) {
			mps->start2 = camel_mime_parser_tell_start_headers(cmp);
			mps->end1 = camel_mime_parser_tell_start_boundary(cmp);
			if (mps->end1 > mps->start1 && mem->buffer->data[mps->end1-1] == '\n')
				mps->end1--;
			if (mps->end1 > mps->start1 && mem->buffer->data[mps->end1-1] == '\r')
				mps->end1--;
		} else {
			g_warning("multipart/signed has more than 2 parts, remaining parts ignored");
			break;
		}

		if (skip_content(cmp) == -1)
			break;
	}

	if (state == CAMEL_MIME_PARSER_STATE_MULTIPART_END) {
		mps->end2 = camel_mime_parser_tell_start_boundary(cmp);
		
		camel_multipart_set_preface(mp, camel_mime_parser_preface(cmp));
		camel_multipart_set_postface(mp, camel_mime_parser_postface(cmp));
	}

	camel_object_unref(cmp);

	if (mps->end2 == -1 || mps->start2 == -1) {
		return -1;
	}

	return 0;
}

/* we snoop the mime type to get boundary and hash info */
static void
set_mime_type_field(CamelDataWrapper *data_wrapper, CamelContentType *mime_type)
{
	CamelMultipartSigned *mps = (CamelMultipartSigned *)data_wrapper;

	((CamelDataWrapperClass *)parent_class)->set_mime_type_field(data_wrapper, mime_type);
	if (mime_type) {
		const char *micalg, *protocol;

		protocol = camel_content_type_param(mime_type, "protocol");
		g_free(mps->protocol);
		mps->protocol = g_strdup(protocol);

		micalg = camel_content_type_param(mime_type, "micalg");
		g_free(mps->micalg);
		mps->micalg = g_strdup(micalg);
	}
}

static void
signed_add_part(CamelMultipart *multipart, CamelMimePart *part)
{
	g_warning("Cannot add parts to a signed part using add_part");
}

static void
signed_add_part_at(CamelMultipart *multipart, CamelMimePart *part, guint index)
{
	g_warning("Cannot add parts to a signed part using add_part_at");
}

static void
signed_remove_part(CamelMultipart *multipart, CamelMimePart *part)
{
	g_warning("Cannot remove parts from a signed part using remove_part");
}

static CamelMimePart *
signed_remove_part_at (CamelMultipart *multipart, guint index)
{
	g_warning("Cannot remove parts from a signed part using remove_part");
	return NULL;
}

static CamelMimePart *
signed_get_part(CamelMultipart *multipart, guint index)
{
	CamelMultipartSigned *mps = (CamelMultipartSigned *)multipart;
	CamelDataWrapper *dw = (CamelDataWrapper *)multipart;
	CamelStream *stream;

	switch (index) {
	case CAMEL_MULTIPART_SIGNED_CONTENT:
		if (mps->content)
			return mps->content;
		if (mps->contentraw) {
			stream = mps->contentraw;
			camel_object_ref((CamelObject *)stream);
		} else if (mps->start1 == -1
			   && parse_content(mps) == -1
			   && (stream = ((CamelDataWrapper *)mps)->stream) == NULL) {
			g_warning("Trying to get content on an invalid multipart/signed");
			return NULL;
		} else if (dw->stream == NULL) {
			return NULL;
		} else if (mps->start1 == -1) {
			stream = dw->stream;
			camel_object_ref(stream);
		} else {
			stream = camel_seekable_substream_new((CamelSeekableStream *)dw->stream, mps->start1, mps->end1);
		}
		camel_stream_reset(stream);
		mps->content = camel_mime_part_new();
		camel_data_wrapper_construct_from_stream((CamelDataWrapper *)mps->content, stream);
		camel_object_unref(stream);
		return mps->content;
	case CAMEL_MULTIPART_SIGNED_SIGNATURE:
		if (mps->signature)
			return mps->signature;
		if (mps->start1 == -1
		    && parse_content(mps) == -1) {
			g_warning("Trying to get signature on invalid multipart/signed");
			return NULL;
		} else if (dw->stream == NULL) {
			return NULL;
		}
		stream = camel_seekable_substream_new((CamelSeekableStream *)dw->stream, mps->start2, mps->end2);
		camel_stream_reset(stream);
		mps->signature = camel_mime_part_new();
		camel_data_wrapper_construct_from_stream((CamelDataWrapper *)mps->signature, stream);
		camel_object_unref((CamelObject *)stream);
		return mps->signature;
	default:
		g_warning("trying to get object out of bounds for multipart");
	}

	return NULL;
}

static guint
signed_get_number(CamelMultipart *multipart)
{
	CamelDataWrapper *dw = (CamelDataWrapper *)multipart;
	CamelMultipartSigned *mps = (CamelMultipartSigned *)multipart;

	/* check what we have, so we return something reasonable */

	if ((mps->content || mps->contentraw) && mps->signature)
		return 2;

	if (mps->start1 == -1 && parse_content(mps) == -1) {
		if (dw->stream == NULL)
			return 0;
		else
			return 1;
	} else {
		return 2;
	}
}

static void
set_stream(CamelMultipartSigned *mps, CamelStream *mem)
{
	CamelDataWrapper *dw = (CamelDataWrapper *)mps;

	if (dw->stream)
		camel_object_unref((CamelObject *)dw->stream);
	dw->stream = (CamelStream *)mem;

	mps->start1 = -1;
	if (mps->content) {
		camel_object_unref((CamelObject *)mps->content);
		mps->content = NULL;
	}
	if (mps->contentraw) {
		camel_object_unref((CamelObject *)mps->contentraw);
		mps->contentraw = NULL;
	}
	if (mps->signature) {
		camel_object_unref((CamelObject *)mps->signature);
		mps->signature = NULL;
	}
}

static int
construct_from_stream(CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	CamelMultipartSigned *mps = (CamelMultipartSigned *)data_wrapper;
	CamelStream *mem = camel_stream_mem_new();

	if (camel_stream_write_to_stream(stream, mem) == -1)
		return -1;

	set_stream(mps, mem);

	return 0;
}

static int
signed_construct_from_parser(CamelMultipart *multipart, struct _CamelMimeParser *mp)
{
	int err;
	CamelContentType *content_type;
	CamelMultipartSigned *mps = (CamelMultipartSigned *)multipart;
	char *buf;
	size_t len;
	CamelStream *mem;

	/* we *must not* be in multipart state, otherwise the mime parser will
	   parse the headers which is a no no @#$@# stupid multipart/signed spec */
	g_assert(camel_mime_parser_state(mp) == CAMEL_MIME_PARSER_STATE_HEADER);

	/* All we do is copy it to a memstream */
	content_type = camel_mime_parser_content_type(mp);
	camel_multipart_set_boundary(multipart, camel_content_type_param(content_type, "boundary"));

	mem = camel_stream_mem_new();
	while (camel_mime_parser_step(mp, &buf, &len) != CAMEL_MIME_PARSER_STATE_BODY_END)
		camel_stream_write(mem, buf, len);

	set_stream(mps, mem);

	err = camel_mime_parser_errno(mp);
	if (err != 0) {
		errno = err;
		return -1;
	} else
		return 0;
}

static ssize_t
write_to_stream (CamelDataWrapper *data_wrapper, CamelStream *stream)
{
	CamelMultipartSigned *mps = (CamelMultipartSigned *)data_wrapper;
	CamelMultipart *mp = (CamelMultipart *)mps;
	const char *boundary;
	ssize_t total = 0;
	ssize_t count;
	
	/* we have 3 basic cases:
	   1. constructed, we write out the data wrapper stream we got
	   2. signed content, we create and write out a new stream
	   3. invalid
	*/

	/* 1 */
	/* FIXME: locking? */
	if (data_wrapper->stream) {
		camel_stream_reset(data_wrapper->stream);
		return camel_stream_write_to_stream(data_wrapper->stream, stream);
	}

	/* 3 */
	if (mps->signature == NULL || mps->contentraw == NULL)
		return -1;

	/* 2 */
	boundary = camel_multipart_get_boundary(mp);
	if (mp->preface) {
		count = camel_stream_write_string(stream, mp->preface);
		if (count == -1)
			return -1;
		total += count;
	}

	/* first boundary */
	count = camel_stream_printf(stream, "\n--%s\n", boundary);
	if (count == -1)
		return -1;
	total += count;

	/* output content part */
	camel_stream_reset(mps->contentraw);
	count = camel_stream_write_to_stream(mps->contentraw, stream);
	if (count == -1)
		return -1;
	total += count;
	
	/* boundary */
	count = camel_stream_printf(stream, "\n--%s\n", boundary);
	if (count == -1)
		return -1;
	total += count;

	/* signature */
	count = camel_data_wrapper_write_to_stream((CamelDataWrapper *)mps->signature, stream);
	if (count == -1)
		return -1;
	total += count;

	/* write the terminating boudary delimiter */
	count = camel_stream_printf(stream, "\n--%s--\n", boundary);
	if (count == -1)
		return -1;
	total += count;

	/* and finally the postface */
	if (mp->postface) {
		count = camel_stream_write_string(stream, mp->postface);
		if (count == -1)
			return -1;
		total += count;
	}

	return total;	
}


/**
 * camel_multipart_signed_get_content_stream:
 * @mps: a #CamlMultipartSigned object
 * @ex: a #CamelException
 *
 * Get the raw signed content stream of the multipart/signed MIME part
 * suitable for use with verification of the signature.
 *
 * Returns the signed content stream
 **/
CamelStream *
camel_multipart_signed_get_content_stream(CamelMultipartSigned *mps, CamelException *ex)
{
	CamelStream *constream;

	/* we need to be able to verify stuff we just signed as well as stuff we loaded from a stream/parser */

	if (mps->contentraw) {
		constream = mps->contentraw;
		camel_object_ref((CamelObject *)constream);
	} else {
		CamelStream *sub;
		CamelMimeFilter *canon_filter;

		if (mps->start1 == -1 && parse_content(mps) == -1) {
			camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, _("parse error"));
			return NULL;
		}

		/* first, prepare our parts */
		sub = camel_seekable_substream_new((CamelSeekableStream *)((CamelDataWrapper *)mps)->stream, mps->start1, mps->end1);
		constream = (CamelStream *)camel_stream_filter_new_with_stream(sub);
		camel_object_unref((CamelObject *)sub);
		
		/* Note: see rfc2015 or rfc3156, section 5 */
		canon_filter = camel_mime_filter_canon_new (CAMEL_MIME_FILTER_CANON_CRLF);
		camel_stream_filter_add((CamelStreamFilter *)constream, (CamelMimeFilter *)canon_filter);
		camel_object_unref((CamelObject *)canon_filter);
	}

	return constream;
}
