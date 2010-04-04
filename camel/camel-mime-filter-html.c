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

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "camel-html-parser.h"
#include "camel-mime-filter-html.h"

#define d(x)

static CamelMimeFilterClass *camel_mime_filter_html_parent;

struct _CamelMimeFilterHTMLPrivate {
	CamelHTMLParser *ctxt;
};

/* ********************************************************************** */

#if 0

/* well we odnt use this stuff yet */

static struct {
	gchar *element;
	gchar *remap;
} map_start[] = {
	{ "p", "\n\n" },
	{ "br", "\n" },
	{ "h1", "\n" }, { "h2", "\n" }, { "h3", "\n" }, { "h4", "\n" }, { "h5", "\n" }, { "h6", "\n" },
};

static struct {
	gchar *element;
	gchar *remap;
} map_end[] = {
	{ "h1", "\n" }, { "h2", "\n" }, { "h3", "\n" }, { "h4", "\n" }, { "h5", "\n" }, { "h6", "\n" },
};
#endif

/* ********************************************************************** */

static void
mime_filter_html_run (CamelMimeFilter *mime_filter,
                      const gchar *in,
                      gsize inlen,
                      gsize prespace,
                      gchar **out,
                      gsize *outlenptr,
                      gsize *outprespace,
                      gint last)
{
	CamelMimeFilterHTMLPrivate *priv;
	camel_html_parser_t state;
	gchar *outp;

	priv = CAMEL_MIME_FILTER_HTML (mime_filter)->priv;

	d(printf("converting html:\n%.*s\n", (gint)inlen, in));

	/* We should generally shrink the data, but this'll do */
	camel_mime_filter_set_size (mime_filter, inlen * 2 + 256, FALSE);
	outp = mime_filter->outbuf;

	camel_html_parser_set_data (priv->ctxt, in, inlen, last);
	do {
		const gchar *data;
		gint len;

		state = camel_html_parser_step(priv->ctxt, &data, &len);

		switch (state) {
		case CAMEL_HTML_PARSER_DATA:
		case CAMEL_HTML_PARSER_ENT:
			memcpy(outp, data, len);
			outp += len;
			break;
		case CAMEL_HTML_PARSER_ELEMENT:
			/* FIXME: do some whitespace processing here */
			break;
		default:
			/* ignore everything else */
			break;
		}
	} while (state != CAMEL_HTML_PARSER_EOF && state != CAMEL_HTML_PARSER_EOD);

	*out = mime_filter->outbuf;
	*outlenptr = outp - mime_filter->outbuf;
	*outprespace = mime_filter->outbuf - mime_filter->outreal;

	d(printf("converted html end:\n%.*s\n", (gint)*outlenptr, *out));
}

static void
mime_filter_html_finalize (CamelMimeFilterHTML *filter)
{
	camel_object_unref (filter->priv->ctxt);
	g_free (filter->priv);
}

static void
mime_filter_html_filter (CamelMimeFilter *mime_filter,
                         const gchar *in,
                         gsize len,
                         gsize prespace,
                         gchar **out,
                         gsize *outlenptr,
                         gsize *outprespace)
{
	mime_filter_html_run (
		mime_filter, in, len, prespace,
		out, outlenptr, outprespace, FALSE);
}

static void
mime_filter_html_complete (CamelMimeFilter *mime_filter,
                           const gchar *in,
                           gsize len,
                           gsize prespace,
                           gchar **out,
                           gsize *outlenptr,
                           gsize *outprespace)
{
	mime_filter_html_run (
		mime_filter, in, len, prespace,
		out, outlenptr, outprespace, TRUE);
}

static void
mime_filter_html_reset (CamelMimeFilter *mime_filter)
{
	CamelMimeFilterHTMLPrivate *priv;

	priv = CAMEL_MIME_FILTER_HTML (mime_filter)->priv;

	camel_object_unref (priv->ctxt);
	priv->ctxt = camel_html_parser_new ();
}

static void
camel_mime_filter_html_class_init (CamelMimeFilterHTMLClass *class)
{
	CamelMimeFilterClass *mime_filter_class;

	camel_mime_filter_html_parent = CAMEL_MIME_FILTER_CLASS (camel_type_get_global_classfuncs (camel_mime_filter_get_type ()));

	mime_filter_class = CAMEL_MIME_FILTER_CLASS (class);
	mime_filter_class->filter = mime_filter_html_filter;
	mime_filter_class->complete = mime_filter_html_complete;
	mime_filter_class->reset = mime_filter_html_reset;
}

static void
camel_mime_filter_html_init (CamelMimeFilterHTML *mime_filter)
{
	mime_filter->priv = g_new0 (CamelMimeFilterHTMLPrivate, 1);
	mime_filter->priv->ctxt = camel_html_parser_new ();
}

CamelType
camel_mime_filter_html_get_type (void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		type = camel_type_register (camel_mime_filter_get_type (), "CamelMimeFilterHTML",
					    sizeof (CamelMimeFilterHTML),
					    sizeof (CamelMimeFilterHTMLClass),
					    (CamelObjectClassInitFunc) camel_mime_filter_html_class_init,
					    NULL,
					    (CamelObjectInitFunc) camel_mime_filter_html_init,
					    (CamelObjectFinalizeFunc) mime_filter_html_finalize);
	}

	return type;
}

/**
 * camel_mime_filter_html_new:
 *
 * Create a new #CamelMimeFilterHTML object.
 *
 * Returns: a new #CamelMimeFilterHTML object
 **/
CamelMimeFilter *
camel_mime_filter_html_new (void)
{
	return (CamelMimeFilter *) camel_object_new (camel_mime_filter_html_get_type ());
}
