/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: Michael Zucchi <notzed@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>

#include "camel-charset-map.h"
#include "camel-iconv.h"
#include "camel-mime-filter-charset.h"

#define d(x)
#define w(x)

struct _CamelMimeFilterCharsetPrivate {
	iconv_t ic;
	gchar *from;
	gchar *to;
};

static CamelMimeFilterClass *camel_mime_filter_charset_parent;

static void
mime_filter_charset_finalize (CamelMimeFilterCharset *filter)
{
	CamelMimeFilterCharsetPrivate *priv;

	priv = CAMEL_MIME_FILTER_CHARSET (filter)->priv;

	g_free (priv->from);
	g_free (priv->to);

	if (priv->ic != (iconv_t) -1) {
		camel_iconv_close (priv->ic);
		priv->ic = (iconv_t) -1;
	}
	g_free (priv);
}

static void
mime_filter_charset_complete (CamelMimeFilter *mime_filter,
                              const gchar *in,
                              gsize len,
                              gsize prespace,
                              gchar **out,
                              gsize *outlen,
                              gsize *outprespace)
{
	CamelMimeFilterCharsetPrivate *priv;
	gsize inleft, outleft, converted = 0;
	const gchar *inbuf;
	gchar *outbuf;

	priv = CAMEL_MIME_FILTER_CHARSET (mime_filter)->priv;

	if (priv->ic == (iconv_t) -1)
		goto noop;

	camel_mime_filter_set_size (mime_filter, len * 5 + 16, FALSE);
	outbuf = mime_filter->outbuf;
	outleft = mime_filter->outsize;

	inbuf = in;
	inleft = len;

	if (inleft > 0) {
		do {
			converted = camel_iconv (priv->ic, &inbuf, &inleft, &outbuf, &outleft);
			if (converted == (gsize) -1) {
				if (errno == E2BIG) {
					/*
					 * E2BIG   There is not sufficient room at *outbuf.
					 *
					 * We just need to grow our outbuffer and try again.
					 */

					converted = outbuf - mime_filter->outbuf;
					camel_mime_filter_set_size (mime_filter, inleft * 5 + mime_filter->outsize + 16, TRUE);
					outbuf = mime_filter->outbuf + converted;
					outleft = mime_filter->outsize - converted;
				} else if (errno == EILSEQ) {
					/*
					 * EILSEQ An invalid multibyte sequence has been  encountered
					 *        in the input.
					 *
					 * What we do here is eat the invalid bytes in the sequence and continue
					 */

					inbuf++;
					inleft--;
				} else if (errno == EINVAL) {
					/*
					 * EINVAL  An  incomplete  multibyte sequence has been encoun­
					 *         tered in the input.
					 *
					 * We assume that this can only happen if we've run out of
					 * bytes for a multibyte sequence, if not we're in trouble.
					 */

					break;
				} else
					goto noop;
			}
		} while (((gint) inleft) > 0);
	}

	/* flush the iconv conversion */
	camel_iconv (priv->ic, NULL, NULL, &outbuf, &outleft);

	*out = mime_filter->outbuf;
	*outlen = mime_filter->outsize - outleft;
	*outprespace = mime_filter->outpre;

	return;

 noop:

	*out = (gchar *) in;
	*outlen = len;
	*outprespace = prespace;
}

static void
mime_filter_charset_filter (CamelMimeFilter *mime_filter,
                            const gchar *in,
                            gsize len,
                            gsize prespace,
                            gchar **out,
                            gsize *outlen,
                            gsize *outprespace)
{
	CamelMimeFilterCharsetPrivate *priv;
	gsize inleft, outleft, converted = 0;
	const gchar *inbuf;
	gchar *outbuf;

	priv = CAMEL_MIME_FILTER_CHARSET (mime_filter)->priv;

	if (priv->ic == (iconv_t) -1)
		goto noop;

	camel_mime_filter_set_size (mime_filter, len * 5 + 16, FALSE);
	outbuf = mime_filter->outbuf + converted;
	outleft = mime_filter->outsize - converted;

	inbuf = in;
	inleft = len;

	do {
		converted = camel_iconv (priv->ic, &inbuf, &inleft, &outbuf, &outleft);
		if (converted == (gsize) -1) {
			if (errno == E2BIG || errno == EINVAL)
				break;

			if (errno == EILSEQ) {
				/*
				 * EILSEQ An invalid multibyte sequence has been  encountered
				 *        in the input.
				 *
				 * What we do here is eat the invalid bytes in the sequence and continue
				 */

				inbuf++;
				inleft--;
			} else {
				/* unknown error condition */
				goto noop;
			}
		}
	} while (((gint) inleft) > 0);

	if (((gint) inleft) > 0) {
		/* We've either got an E2BIG or EINVAL. Save the
                   remainder of the buffer as we'll process this next
                   time through */
		camel_mime_filter_backup (mime_filter, inbuf, inleft);
	}

	*out = mime_filter->outbuf;
	*outlen = outbuf - mime_filter->outbuf;
	*outprespace = mime_filter->outpre;

	return;

 noop:

	*out = (gchar *) in;
	*outlen = len;
	*outprespace = prespace;
}

static void
mime_filter_charset_reset (CamelMimeFilter *mime_filter)
{
	CamelMimeFilterCharsetPrivate *priv;
	gchar buf[16];
	gchar *buffer;
	gsize outlen = 16;

	priv = CAMEL_MIME_FILTER_CHARSET (mime_filter)->priv;

	/* what happens with the output bytes if this resets the state? */
	if (priv->ic != (iconv_t) -1) {
		buffer = buf;
		camel_iconv (priv->ic, NULL, NULL, &buffer, &outlen);
	}
}

static void
camel_mime_filter_charset_class_init (CamelMimeFilterCharsetClass *class)
{
	CamelMimeFilterClass *mime_filter_class;

	camel_mime_filter_charset_parent = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_charset_filter;
	mime_filter_class->complete = mime_filter_charset_complete;
	mime_filter_class->reset = mime_filter_charset_reset;
}

static void
camel_mime_filter_charset_init (CamelMimeFilterCharset *filter)
{
	filter->priv = g_new0 (CamelMimeFilterCharsetPrivate, 1);
	filter->priv->ic = (iconv_t) -1;
}

CamelType
camel_mime_filter_charset_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type (), "CamelMimeFilterCharset",
					    sizeof (CamelMimeFilterCharset),
					    sizeof (CamelMimeFilterCharsetClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_charset_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_charset_init,
					    (CamelObjectFinalizeFunc) mime_filter_charset_finalize);
	}

	return type;
}

/**
 * camel_mime_filter_charset_new:
 * @from_charset: charset to convert from
 * @to_charset: charset to convert to
 *
 * Create a new #CamelMimeFiletrCharset object to convert text from
 * @from_charset to @to_charset.
 *
 * Returns: a new #CamelMimeFilterCharset object
 **/
CamelMimeFilter *
camel_mime_filter_charset_new (const gchar *from_charset,
                               const gchar *to_charset)
{
	CamelMimeFilter *new;
	CamelMimeFilterCharsetPrivate *priv;

	new = CAMEL_MIME_FILTER (camel_object_new (camel_mime_filter_charset_get_type ()));
	priv = CAMEL_MIME_FILTER_CHARSET (new)->priv;

	priv->ic = camel_iconv_open (to_charset, from_charset);
	if (priv->ic == (iconv_t) -1) {
		w(g_warning ("Cannot create charset conversion from %s to %s: %s",
			     from_charset ? from_charset : "(null)",
			     to_charset ? to_charset : "(null)",
			     g_strerror (errno)));
		camel_object_unref (new);
		new = NULL;
	} else {
		priv->from = g_strdup (from_charset);
		priv->to = g_strdup (to_charset);
	}

	return new;
}
