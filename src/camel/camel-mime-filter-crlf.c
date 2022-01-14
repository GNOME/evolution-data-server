/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
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
 * Authors: Dan Winship <danw@ximian.com>
 *          Jeffrey Stedfast <fejj@ximian.com>
 */

#include "camel-mime-filter-crlf.h"

struct _CamelMimeFilterCRLFPrivate {
	CamelMimeFilterCRLFDirection direction;
	CamelMimeFilterCRLFMode mode;
	gboolean saw_cr;
	gboolean saw_lf;
	gboolean saw_dot;
	gboolean ends_with_lf;
	gboolean ensure_crlf_end;
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelMimeFilterCRLF, camel_mime_filter_crlf, CAMEL_TYPE_MIME_FILTER)

static void
mime_filter_crlf_filter (CamelMimeFilter *mime_filter,
                         const gchar *in,
                         gsize len,
                         gsize prespace,
                         gchar **out,
                         gsize *outlen,
                         gsize *outprespace)
{
	CamelMimeFilterCRLFPrivate *priv;
	register const gchar *inptr;
	const gchar *inend;
	gboolean do_dots;
	gchar *outptr;

	priv = CAMEL_MIME_FILTER_CRLF (mime_filter)->priv;

	do_dots = priv->mode == CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS;

	inptr = in;
	inend = in + len;

	if (priv->direction == CAMEL_MIME_FILTER_CRLF_ENCODE) {
		camel_mime_filter_set_size (mime_filter, 3 * len, FALSE);

		outptr = mime_filter->outbuf;
		while (inptr < inend) {
			if (*inptr == '\r') {
				if (priv->saw_cr && !priv->saw_lf)
					*outptr++ = '\n';
				priv->saw_cr = TRUE;
				priv->saw_lf = FALSE;
			} else if (*inptr == '\n') {
				priv->saw_lf = TRUE;
				if (!priv->saw_cr)
					*outptr++ = '\r';
				priv->saw_cr = FALSE;
			} else {
				if (priv->saw_cr && !priv->saw_lf) {
					priv->saw_lf = TRUE;
					*outptr++ = '\n';
				}

				if (do_dots && *inptr == '.' && priv->saw_lf)
					*outptr++ = '.';

				priv->saw_cr = FALSE;
				priv->saw_lf = FALSE;
			}

			*outptr++ = *inptr++;
		}

		if (len > 0)
			priv->ends_with_lf = in[len - 1] == '\n';
	} else {
		/* Output can "grow" by one byte if priv->saw_cr was set as
		 * a carry-over from the previous invocation. This will happen
		 * in practice, as the input is processed in arbitrarily-sized
		 * blocks. */
		camel_mime_filter_set_size (mime_filter, len + 1, FALSE);

		outptr = mime_filter->outbuf;
		while (inptr < inend) {
			if (*inptr == '\r') {
				priv->saw_cr = TRUE;
			} else {
				if (priv->saw_cr) {
					priv->saw_cr = FALSE;

					if (*inptr == '\n') {
						priv->saw_lf = TRUE;
						*outptr++ = *inptr++;
						continue;
					} else
						*outptr++ = '\r';
				}

				*outptr++ = *inptr;
			}

			if (do_dots && *inptr == '.') {
				if (priv->saw_lf) {
					priv->saw_dot = TRUE;
					priv->saw_lf = FALSE;
					inptr++;
				} else if (priv->saw_dot) {
					priv->saw_dot = FALSE;
				}
			}

			priv->saw_lf = FALSE;

			inptr++;
		}
	}

	*out = mime_filter->outbuf;
	*outlen = outptr - mime_filter->outbuf;
	*outprespace = mime_filter->outpre;
}

static void
mime_filter_crlf_complete (CamelMimeFilter *mime_filter,
                           const gchar *in,
                           gsize len,
                           gsize prespace,
                           gchar **out,
                           gsize *outlen,
                           gsize *outprespace)
{
	CamelMimeFilterCRLF *crlf_filter;

	if (len)
		mime_filter_crlf_filter (
			mime_filter, in, len, prespace,
			out, outlen, outprespace);
	else
		*outlen = 0;

	crlf_filter = CAMEL_MIME_FILTER_CRLF (mime_filter);

	if (crlf_filter->priv->direction == CAMEL_MIME_FILTER_CRLF_ENCODE &&
	    crlf_filter->priv->saw_cr && !crlf_filter->priv->saw_lf) {
		gchar *outptr;

		camel_mime_filter_set_size (mime_filter, *outlen + 1, FALSE);

		outptr = mime_filter->outbuf + *outlen;
		*outptr++ = '\n';

		*out = mime_filter->outbuf;
		*outlen = outptr - mime_filter->outbuf;
		*outprespace = mime_filter->outpre;

		crlf_filter->priv->saw_cr = FALSE;
		crlf_filter->priv->ends_with_lf = TRUE;
	}

	if (crlf_filter->priv->direction == CAMEL_MIME_FILTER_CRLF_ENCODE &&
	    crlf_filter->priv->ensure_crlf_end &&
	    !crlf_filter->priv->ends_with_lf) {
		gchar *outptr;

		camel_mime_filter_set_size (mime_filter, *outlen + 2, FALSE);

		outptr = mime_filter->outbuf + *outlen;
		*outptr++ = '\r';
		*outptr++ = '\n';

		crlf_filter->priv->ends_with_lf = TRUE;

		*out = mime_filter->outbuf;
		*outlen = outptr - mime_filter->outbuf;
		*outprespace = mime_filter->outpre;
	}
}

static void
mime_filter_crlf_reset (CamelMimeFilter *mime_filter)
{
	CamelMimeFilterCRLFPrivate *priv;

	priv = CAMEL_MIME_FILTER_CRLF (mime_filter)->priv;

	priv->saw_cr = FALSE;
	priv->saw_lf = TRUE;
	priv->saw_dot = FALSE;
	priv->ends_with_lf = FALSE;
}

static void
camel_mime_filter_crlf_class_init (CamelMimeFilterCRLFClass *class)
{
	CamelMimeFilterClass *mime_filter_class;

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_crlf_filter;
	mime_filter_class->complete = mime_filter_crlf_complete;
	mime_filter_class->reset = mime_filter_crlf_reset;
}

static void
camel_mime_filter_crlf_init (CamelMimeFilterCRLF *filter)
{
	filter->priv = camel_mime_filter_crlf_get_instance_private (filter);

	filter->priv->saw_cr = FALSE;
	filter->priv->saw_lf = TRUE;
	filter->priv->saw_dot = FALSE;
	filter->priv->ends_with_lf = FALSE;
	filter->priv->ensure_crlf_end = FALSE;
}

/**
 * camel_mime_filter_crlf_new:
 * @direction: encode vs decode
 * @mode: whether or not to perform SMTP dot-escaping
 *
 * Create a new #CamelMimeFilterCRLF object.
 *
 * Returns: a new #CamelMimeFilterCRLF object
 **/
CamelMimeFilter *
camel_mime_filter_crlf_new (CamelMimeFilterCRLFDirection direction,
                            CamelMimeFilterCRLFMode mode)
{
	CamelMimeFilter *filter;
	CamelMimeFilterCRLFPrivate *priv;

	filter = g_object_new (CAMEL_TYPE_MIME_FILTER_CRLF, NULL);
	priv = CAMEL_MIME_FILTER_CRLF (filter)->priv;

	priv->direction = direction;
	priv->mode = mode;

	return filter;
}

/**
 * camel_mime_filter_crlf_set_ensure_crlf_end:
 * @filter: a #CamelMimeFilterCRLF
 * @ensure_crlf_end: value to set
 *
 * When set to true, the filter will ensure that the output stream will
 * end with CRLF, in case it does not. The default is to not do that.
 * The option is used only when encoding the stream.
 *
 * Since: 3.42
 **/
void
camel_mime_filter_crlf_set_ensure_crlf_end (CamelMimeFilterCRLF *filter,
					    gboolean ensure_crlf_end)
{
	g_return_if_fail (CAMEL_IS_MIME_FILTER_CRLF (filter));

	filter->priv->ensure_crlf_end = ensure_crlf_end;
}

/**
 * camel_mime_filter_crlf_get_ensure_crlf_end:
 * @filter: a #CamelMimeFilterCRLF
 *
 * Returns: whether the filter will ensure that the output stream will
 *    end with CRLF
 *
 * Since: 3.42
 **/
gboolean
camel_mime_filter_crlf_get_ensure_crlf_end (CamelMimeFilterCRLF *filter)
{
	g_return_val_if_fail (CAMEL_IS_MIME_FILTER_CRLF (filter), FALSE);

	return filter->priv->ensure_crlf_end;
}
