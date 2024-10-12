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

#include "evolution-data-server-config.h"

#include <stdio.h>
#include <string.h>

#include "camel-mime-filter-tohtml.h"
#include "camel-net-utils.h"
#include "camel-url-scanner.h"
#include "camel-utf8.h"

struct _CamelMimeFilterToHTMLPrivate {

	CamelUrlScanner *scanner;
	GString *backup;

	CamelMimeFilterToHTMLFlags flags;
	guint32 color;

	guint blockquote_depth;

	guint32 column : 31;
	guint32 pre_open : 1;
	gboolean div_open;
};

/*
 * TODO: convert common text/plain 'markup' to html. eg.:
 *
 * _word_ -> <u>_word_</u>
 * *word* -> <b>*word*</b>
 * /word/ -> <i>/word/</i>
 */

#define d(x)

#define FOOLISHLY_UNMUNGE_FROM 0

#define CONVERT_WEB_URLS  CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS
#define CONVERT_ADDRSPEC  CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES

static struct {
	CamelMimeFilterToHTMLFlags mask;
	CamelUrlPattern pattern;
} patterns[] = {
	{ CONVERT_WEB_URLS, { "file://",   "",        camel_url_file_start,     camel_url_file_end     } },
	{ CONVERT_WEB_URLS, { "ftp://",    "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "sftp://",   "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "http://",   "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "https://",  "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "news://",   "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "nntp://",   "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "telnet://", "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "webcal://", "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "webcals://","",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "mailto:",   "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "callto:",   "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "h323:",     "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "sip:",      "",        camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "www.",      "http://", camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_WEB_URLS, { "ftp.",      "ftp://",  camel_url_web_start,      camel_url_web_end      } },
	{ CONVERT_ADDRSPEC, { "@",         "mailto:", camel_url_addrspec_start, camel_url_addrspec_end } },
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelMimeFilterToHTML, camel_mime_filter_tohtml, CAMEL_TYPE_MIME_FILTER)

static gchar *
check_size (CamelMimeFilter *mime_filter,
            gchar *outptr,
            gchar **outend,
            gsize len)
{
	gsize offset;

	if (*outend - outptr >= len)
		return outptr;

	offset = outptr - mime_filter->outbuf;

	camel_mime_filter_set_size (
		mime_filter, mime_filter->outsize + len, TRUE);

	*outend = mime_filter->outbuf + mime_filter->outsize;

	return mime_filter->outbuf + offset;
}

static gchar *
append_string_verbatim (CamelMimeFilter *mime_filter,
                        const gchar *str,
                        gchar *outptr,
                        gchar **outend)
{
	gsize len = strlen (str);

	outptr = check_size (mime_filter, outptr, outend, len);
	memcpy (outptr, str, len);
	outptr += len;

	return outptr;
}

static gint
citation_depth (const gchar *in,
                const gchar *inend,
                goffset *out_skip)
{
	register const gchar *inptr = in;
	gint depth = 0;
	goffset skip = 0;

	if (out_skip != NULL)
		*out_skip = 0;

	if (!strchr (">|", *inptr++))
		goto exit;

#if FOOLISHLY_UNMUNGE_FROM
	/* check that it isn't an escaped From line */
	if (!strncmp (inptr, "From", 4)) {
		goto exit;
#endif

	depth = 1;
	skip = 1;

	while (inptr < inend && *inptr != '\n') {
		if (*inptr == ' ') {
			inptr++;
			skip++;
		}

		if (inptr >= inend || !strchr (">|", *inptr++))
			break;

		depth++;
		skip++;
	}

exit:
	if (out_skip != NULL)
		*out_skip = (depth > 0) ? skip : 0;

	return depth;
}

static gchar *
writeln (CamelMimeFilter *mime_filter,
         const gchar *in_anycharset,
         const gchar *inend_char,
         gchar *outptr,
         gchar **outend)
{
	CamelMimeFilterToHTMLPrivate *priv;
	const guchar *inptr, *inend, *inbegin;
	gchar *in_utf8 = NULL;

	priv = CAMEL_MIME_FILTER_TOHTML (mime_filter)->priv;

	if (!g_utf8_validate (in_anycharset, inend_char - in_anycharset, NULL)) {
		in_utf8 = camel_utf8_make_valid_len (in_anycharset, inend_char - in_anycharset);

		if (!in_utf8)
			return outptr;

		inptr = (const guchar *) in_utf8;
		inend = inptr + strlen (in_utf8);
	} else {
		inptr = (const guchar *) in_anycharset;
		inend = (const guchar *) inend_char;
	}

	inbegin = inptr;
	while (inptr < inend) {
		guint32 u;

		outptr = check_size (mime_filter, outptr, outend, 16);

		u = camel_utf8_getc_limit (&inptr, inend);
		switch (u) {
		case 0xffff:
			g_warning (
				"Truncated UTF-8 buffer (The cause might "
				"be missing character encoding information "
				"in the message header. Try a different "
				"character encoding.)");
			return outptr;
		case '<':
			outptr = g_stpcpy (outptr, "&lt;");
			priv->column++;
			break;
		case '>':
			outptr = g_stpcpy (outptr, "&gt;");
			priv->column++;
			break;
		case '&':
			outptr = g_stpcpy (outptr, "&amp;");
			priv->column++;
			break;
		case '"':
			outptr = g_stpcpy (outptr, "&quot;");
			priv->column++;
			break;
		case '\t':
			if (priv->flags & (CAMEL_MIME_FILTER_TOHTML_CONVERT_SPACES)) {
				if ((priv->flags & CAMEL_MIME_FILTER_TOHTML_PRESERVE_TABS) != 0) {
					const gchar *tmp = "<span class=\"Apple-tab-span\" style=\"white-space:pre\">\t</span>";
					gint tmp_len = 61 /* strlen (tmp) */;
					#ifdef ENABLE_MAINTAINER_MODE
					static gboolean tested = FALSE;
					if (!tested) {
						tested = TRUE;
						g_warn_if_fail (strlen (tmp) == tmp_len);
					}
					#endif
					outptr = check_size (mime_filter, outptr, outend, tmp_len + 1);
					outptr = g_stpcpy (outptr, tmp);
				} else {
					do {
						outptr = check_size (mime_filter, outptr, outend, 7);
						outptr = g_stpcpy (outptr, "&nbsp;");
						priv->column++;
					} while (priv->column % 8);
				}
				break;
			}
			/* falls through */
		case ' ':
			if (priv->flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_SPACES
			    && ((inptr == (inbegin + 1) || (inptr < inend && (*inptr == ' ' || *inptr == '\t'))))) {
				outptr = g_stpcpy (outptr, "&nbsp;");
				priv->column++;
				break;
			}
			/* falls through */
		default:
			if (u == '\r' && inptr >= inend) {
				/* This constructs \r\n sequence at the end of the line, thus pass it in
				   only if not converting the new-line breaks */
				if (!(priv->flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_NL))
					*outptr++ = u;
			} else if (u >= 0x20 && u < 0x80) {
				*outptr++ = u;
			} else {
				if (priv->flags & CAMEL_MIME_FILTER_TOHTML_ESCAPE_8BIT)
					*outptr++ = '?';
				else
					outptr += sprintf (outptr, "&#%u;", u);
			}
			priv->column++;
			break;
		}
	}

	g_free (in_utf8);

	return outptr;
}

static void
html_convert (CamelMimeFilter *mime_filter,
              const gchar *in,
              gsize inlen,
              gsize prespace,
              gchar **out,
              gsize *outlen,
              gsize *outprespace,
              gboolean flush)
{
	CamelMimeFilterToHTMLPrivate *priv;
	const gchar *inptr;
	gchar *outptr, *outend, *backup_str = NULL;
	const gchar *start;
	const gchar *inend;
	gint depth;

	priv = CAMEL_MIME_FILTER_TOHTML (mime_filter)->priv;

	if (inlen == 0 && !priv->backup) {
		if (!priv->pre_open && !priv->div_open && !priv->blockquote_depth) {
			/* No closing tags needed. */
			*out = (gchar *) in;
			*outlen = 0;
			*outprespace = 0;
			return;
		}

		outptr = mime_filter->outbuf;
		outend = mime_filter->outbuf + mime_filter->outsize;

		if (priv->div_open) {
			outptr = check_size (mime_filter, outptr, &outend, 7);
			outptr = g_stpcpy (outptr, "</div>");
			priv->div_open = FALSE;
		}

		while (priv->blockquote_depth > 0) {
			outptr = check_size (mime_filter, outptr, &outend, 15);
			outptr = g_stpcpy (outptr, "</blockquote>");
			priv->blockquote_depth--;
		}

		if (priv->pre_open) {
			/* close the pre-tag */
			outptr = check_size (mime_filter, outptr, &outend, 10);
			outptr = g_stpcpy (outptr, "</pre>");
			priv->pre_open = FALSE;
		}

		*out = mime_filter->outbuf;
		*outlen = outptr - mime_filter->outbuf;
		*outprespace = mime_filter->outpre;

		return;
	}

	camel_mime_filter_set_size (mime_filter, inlen * 2 + 6, FALSE);

	if (inlen == 0 && priv->backup) {
		gsize backup_len;

		backup_len = priv->backup->len;
		backup_str = g_string_free (priv->backup, FALSE);
		priv->backup = NULL;

		inptr = backup_str + backup_len;
		inend = inptr;
		start = backup_str;
	} else {
		inptr = in;
		inend = in + inlen;
		start = inptr;
	}

	outptr = mime_filter->outbuf;
	outend = mime_filter->outbuf + mime_filter->outsize;

	if ((priv->flags & CAMEL_MIME_FILTER_TOHTML_PRE) != 0 && !priv->pre_open) {
		outptr = check_size (mime_filter, outptr, &outend, 6);
		outptr = g_stpcpy (outptr, "<pre>");
		priv->pre_open = TRUE;
	}

	do {
		while (inptr < inend && *inptr != '\n')
			inptr++;

		if (inptr >= inend && !flush)
			break;

		if (priv->backup) {
			gsize backup_len, backup_len_old;

			backup_len_old = priv->backup->len;
			g_string_append_len (priv->backup, start, (gsize) (inend - start));
			backup_len = priv->backup->len;
			backup_str = g_string_free (priv->backup, FALSE);
			priv->backup = NULL;

			inptr = backup_str + backup_len_old + (inptr - start);
			start = backup_str;
			inend = start + backup_len;
		}

		priv->column = 0;
		depth = 0;

		if (priv->flags & CAMEL_MIME_FILTER_TOHTML_MARK_CITATION) {
			depth = citation_depth (start, inend, NULL);

			if (depth > 0) {
				/* FIXME: we could easily support multiple color depths here */

				if ((priv->flags & CAMEL_MIME_FILTER_TOHTML_DIV) != 0 && !priv->div_open) {
					outptr = check_size (mime_filter, outptr, &outend, 6);
					outptr = g_stpcpy (outptr, "<div>");
					priv->div_open = TRUE;
				}

				outptr = check_size (mime_filter, outptr, &outend, 25);
				outptr += sprintf (outptr, "<font color=\"#%06x\">", (priv->color & 0xffffff));
			}
#if FOOLISHLY_UNMUNGE_FROM
			else if (*start == '>') {
				/* >From line */
				start++;
			}
#endif

		} else if (priv->flags & CAMEL_MIME_FILTER_TOHTML_QUOTE_CITATION) {
			goffset skip = 0;

			depth = citation_depth (start, inend, &skip);

			if (priv->div_open && depth != priv->blockquote_depth) {
				outptr = check_size (mime_filter, outptr, &outend, 7);
				outptr = g_stpcpy (outptr, "</div>");
				priv->div_open = FALSE;
			}

			while (priv->blockquote_depth < depth) {
				outptr = check_size (mime_filter, outptr, &outend, 25);
				outptr = g_stpcpy (outptr, "<blockquote type=\"cite\">");
				priv->blockquote_depth++;
			}
			while (priv->blockquote_depth > depth) {
				outptr = check_size (mime_filter, outptr, &outend, 14);
				outptr = g_stpcpy (outptr, "</blockquote>");
				priv->blockquote_depth--;
			}
#if FOOLISHLY_UNMUNGE_FROM
			if (depth == 0 && *start == '>') {
				/* >From line */
				skip = 1;
			}
#endif
			start += skip;

		} else if (priv->flags & CAMEL_MIME_FILTER_TOHTML_CITE) {
			if ((priv->flags & CAMEL_MIME_FILTER_TOHTML_DIV) != 0 && !priv->div_open) {
				outptr = check_size (mime_filter, outptr, &outend, 6);
				outptr = g_stpcpy (outptr, "<div>");
				priv->div_open = TRUE;
			}

			outptr = check_size (mime_filter, outptr, &outend, 6);
			outptr = g_stpcpy (outptr, "&gt; ");
			priv->column += 2;
		}

		if ((priv->flags & CAMEL_MIME_FILTER_TOHTML_DIV) != 0 && !priv->div_open) {
			outptr = check_size (mime_filter, outptr, &outend, 6);
			outptr = g_stpcpy (outptr, "<div>");
			priv->div_open = TRUE;
		}

#define CONVERT_URLS (CAMEL_MIME_FILTER_TOHTML_CONVERT_URLS | CAMEL_MIME_FILTER_TOHTML_CONVERT_ADDRESSES)
		if (priv->flags & CONVERT_URLS) {
			gsize matchlen, len;
			CamelUrlMatch match;

			len = inptr - start;

			do {
				if (camel_url_scanner_scan (priv->scanner, start, len - (len > 0 && start[len - 1] == 0 ? 1 : 0), &match)) {
					gchar *url_str, *sanitized_url;
					gint prefix_len = strlen (match.prefix), url_len;

					/* write out anything before the first regex match */
					outptr = writeln (
						mime_filter,
						start,
						start + match.um_so,
						outptr, &outend);

					start += match.um_so;
					len -= match.um_so;

					matchlen = match.um_eo - match.um_so;

					url_str = g_strdup_printf ("%s%.*s", match.prefix, (gint) matchlen, start);
					sanitized_url = camel_utils_sanitize_ascii_domain_in_url_str (url_str);
					if (sanitized_url) {
						g_free (url_str);
						url_str = sanitized_url;
						sanitized_url = NULL;
					}

					url_len = strlen (url_str);

					/* write out the href tag */
					outptr = append_string_verbatim (mime_filter, "<a href=\"", outptr, &outend);
					outptr = writeln (
						mime_filter,
						url_str,
						url_str + url_len,
						outptr, &outend);
					outptr = append_string_verbatim (
						mime_filter, "\">",
						outptr, &outend);

					/* now write the matched string */
					outptr = writeln (
						mime_filter,
						url_str + prefix_len,
						url_str + url_len,
						outptr, &outend);
					priv->column += url_len - prefix_len;
					start += matchlen;
					len -= matchlen;

					/* close the href tag */
					outptr = append_string_verbatim (
						mime_filter, "</a>",
						outptr, &outend);

					g_free (url_str);
				} else {
					/* nothing matched so write out the remainder of this line buffer */
					outptr = writeln (
						mime_filter,
						start,
						start + len,
						outptr, &outend);
					break;
				}
			} while (len > 0);
		} else {
			outptr = writeln (
				mime_filter,
				start,
				inptr,
				outptr, &outend);
		}

		if ((priv->flags & CAMEL_MIME_FILTER_TOHTML_MARK_CITATION) && depth > 0) {
			outptr = check_size (mime_filter, outptr, &outend, 8);
			outptr = g_stpcpy (outptr, "</font>");
		}

		if (inptr < inend) {
			if (priv->flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_NL) {
				outptr = check_size (mime_filter, outptr, &outend, 5);
				outptr = g_stpcpy (outptr, "<br>");
			}

			if (priv->div_open) {
				if (inptr == start && ((*start != '\r' && *start != '\n') || !(priv->flags & CAMEL_MIME_FILTER_TOHTML_CONVERT_NL))) {
					outptr = check_size (mime_filter, outptr, &outend, 5);
					outptr = g_stpcpy (outptr, "<br>");
				}

				outptr = check_size (mime_filter, outptr, &outend, 7);
				outptr = g_stpcpy (outptr, "</div>");
				priv->div_open = FALSE;
			}

			outptr = append_string_verbatim (mime_filter, "\n", outptr, &outend);
		}

		start = ++inptr;
	} while (inptr < inend);

	if (flush) {
		/* flush the rest of our input buffer */
		if (start < inend)
			outptr = writeln (
				mime_filter,
				start,
				inend,
				outptr, &outend);

		if (priv->div_open) {
			outptr = check_size (mime_filter, outptr, &outend, 7);
			outptr = g_stpcpy (outptr, "</div>");
			priv->div_open = FALSE;
		}

		while (priv->blockquote_depth > 0) {
			outptr = check_size (mime_filter, outptr, &outend, 14);
			outptr = g_stpcpy (outptr, "</blockquote>");
			priv->blockquote_depth--;
		}

		if (priv->pre_open) {
			/* close the pre-tag */
			outptr = check_size (mime_filter, outptr, &outend, 7);
			outptr = g_stpcpy (outptr, "</pre>");
			priv->pre_open = FALSE;
		}
	} else if (start < inend) {
		/* backup, but do not use camel_mime_filter_backup() to avoid round trip for long lines */
		if (priv->backup)
			g_string_append_len (priv->backup, start, (gsize) (inend - start));
		else
			priv->backup = g_string_new_len (start, (gsize) (inend - start));
	}

	g_free (backup_str);

	*out = mime_filter->outbuf;
	*outlen = outptr - mime_filter->outbuf;
	*outprespace = mime_filter->outpre;
}

static void
mime_filter_tohtml_finalize (GObject *object)
{
	CamelMimeFilterToHTMLPrivate *priv;

	priv = CAMEL_MIME_FILTER_TOHTML (object)->priv;

	camel_url_scanner_free (priv->scanner);

	if (priv->backup)
		g_string_free (priv->backup, TRUE);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_mime_filter_tohtml_parent_class)->finalize (object);
}

static void
mime_filter_tohtml_filter (CamelMimeFilter *mime_filter,
                           const gchar *in,
                           gsize len,
                           gsize prespace,
                           gchar **out,
                           gsize *outlen,
                           gsize *outprespace)
{
	html_convert (
		mime_filter, in, len, prespace,
		out, outlen, outprespace, FALSE);
}

static void
mime_filter_tohtml_complete (CamelMimeFilter *mime_filter,
                             const gchar *in,
                             gsize len,
                             gsize prespace,
                             gchar **out,
                             gsize *outlen,
                             gsize *outprespace)
{
	html_convert (
		mime_filter, in, len, prespace,
		out, outlen, outprespace, TRUE);
}

static void
mime_filter_tohtml_reset (CamelMimeFilter *mime_filter)
{
	CamelMimeFilterToHTMLPrivate *priv;

	priv = CAMEL_MIME_FILTER_TOHTML (mime_filter)->priv;

	priv->column = 0;
	priv->pre_open = FALSE;

	if (priv->backup) {
		g_string_free (priv->backup, TRUE);
		priv->backup = NULL;
	}
}

static void
camel_mime_filter_tohtml_class_init (CamelMimeFilterToHTMLClass *class)
{
	GObjectClass *object_class;
	CamelMimeFilterClass *filter_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = mime_filter_tohtml_finalize;

	filter_class = CAMEL_MIME_FILTER_CLASS (class);
	filter_class->filter = mime_filter_tohtml_filter;
	filter_class->complete = mime_filter_tohtml_complete;
	filter_class->reset = mime_filter_tohtml_reset;
}

static void
camel_mime_filter_tohtml_init (CamelMimeFilterToHTML *filter)
{
	filter->priv = camel_mime_filter_tohtml_get_instance_private (filter);
	filter->priv->scanner = camel_url_scanner_new ();
	filter->priv->backup = NULL;
}

/**
 * camel_mime_filter_tohtml_new:
 * @flags: bitwise flags defining the behaviour
 * @color: color to use when highlighting quoted text
 *
 * Create a new #CamelMimeFilterToHTML object to convert plain text
 * into HTML.
 *
 * Returns: a new #CamelMimeFilterToHTML object
 **/
CamelMimeFilter *
camel_mime_filter_tohtml_new (CamelMimeFilterToHTMLFlags flags,
                              guint32 color)
{
	CamelMimeFilter *filter;
	CamelMimeFilterToHTMLPrivate *priv;
	gint i;

	/* Prefer PRE over DIV, when used together (which they should not) */
	if ((flags & (CAMEL_MIME_FILTER_TOHTML_PRE | CAMEL_MIME_FILTER_TOHTML_DIV)) ==
	    (CAMEL_MIME_FILTER_TOHTML_PRE | CAMEL_MIME_FILTER_TOHTML_DIV)) {
		flags = flags & ~CAMEL_MIME_FILTER_TOHTML_DIV;
	}

	filter = g_object_new (CAMEL_TYPE_MIME_FILTER_TOHTML, NULL);
	priv = CAMEL_MIME_FILTER_TOHTML (filter)->priv;

	priv->flags = flags;
	priv->color = color;

	for (i = 0; i < G_N_ELEMENTS (patterns); i++) {
		if (patterns[i].mask & flags)
			camel_url_scanner_add (
				priv->scanner, &patterns[i].pattern);
	}

	return filter;
}

/**
 * camel_text_to_html:
 * @in: input text
 * @flags: bitwise flags defining the html conversion behaviour
 * @color: color to use when syntax highlighting
 *
 * Convert @in from plain text into HTML.
 *
 * Returns: a newly allocated string containing the HTMLified version
 * of @in
 **/
gchar *
camel_text_to_html (const gchar *in,
                    CamelMimeFilterToHTMLFlags flags,
                    guint32 color)
{
	CamelMimeFilter *filter;
	gsize outlen, outpre;
	gchar *outbuf;

	g_return_val_if_fail (in != NULL, NULL);

	filter = camel_mime_filter_tohtml_new (flags, color);

	camel_mime_filter_complete (
		filter, (gchar *) in, strlen (in), 0,
		&outbuf, &outlen, &outpre);

	outbuf = g_strndup (outbuf, outlen);

	g_object_unref (filter);

	return outbuf;
}
