/*
 * camel-imapx-command.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include "camel-imapx-command.h"

#include <string.h>
#include <glib/gstdio.h>

#include "camel-imapx-store.h"

#define c(...) camel_imapx_debug(command, __VA_ARGS__)

typedef struct _CamelIMAPXRealCommand CamelIMAPXRealCommand;

/* CamelIMAPXCommand + some private bits */
struct _CamelIMAPXRealCommand {
	CamelIMAPXCommand public;

	volatile gint ref_count;

	/* For building the part. */
	GString *buffer;

	/* Used for running some commands synchronously. */
	GCond *done_sync_cond;
	GMutex *done_sync_mutex;
	gboolean done_sync_flag;
};

CamelIMAPXCommand *
camel_imapx_command_new (CamelIMAPXServer *is,
                         const gchar *name,
                         CamelFolder *select,
                         const gchar *format,
                         ...)
{
	CamelIMAPXRealCommand *real_ic;
	static gint tag = 0;
	va_list ap;

	real_ic = g_slice_new0 (CamelIMAPXRealCommand);

	/* Initialize private bits. */
	real_ic->ref_count = 1;
	real_ic->buffer = g_string_sized_new (512);
	real_ic->done_sync_cond = g_cond_new ();
	real_ic->done_sync_mutex = g_mutex_new ();

	/* Initialize public bits. */
	real_ic->public.is = is;
	real_ic->public.tag = tag++;
	real_ic->public.name = name;
	real_ic->public.select = select;
	camel_dlist_init (&real_ic->public.parts);

	if (format != NULL && *format != '\0') {
		va_start (ap, format);
		camel_imapx_command_addv (
			(CamelIMAPXCommand *) real_ic, format, ap);
		va_end (ap);
	}

	return (CamelIMAPXCommand *) real_ic;
}

CamelIMAPXCommand *
camel_imapx_command_ref (CamelIMAPXCommand *ic)
{
	CamelIMAPXRealCommand *real_ic;

	real_ic = (CamelIMAPXRealCommand *) ic;

	g_return_val_if_fail (real_ic != NULL, NULL);
	g_return_val_if_fail (real_ic->ref_count > 0, NULL);

	g_atomic_int_inc (&real_ic->ref_count);

	return ic;
}

void
camel_imapx_command_unref (CamelIMAPXCommand *ic)
{
	CamelIMAPXRealCommand *real_ic;

	real_ic = (CamelIMAPXRealCommand *) ic;

	g_return_if_fail (real_ic != NULL);
	g_return_if_fail (real_ic->ref_count > 0);

	if (g_atomic_int_dec_and_test (&real_ic->ref_count)) {
		CamelIMAPXCommandPart *cp;

		/* Free the public stuff. */

		imapx_free_status (ic->status);

		while ((cp = ((CamelIMAPXCommandPart *) camel_dlist_remhead (&ic->parts)))) {
			g_free (cp->data);
			if (cp->ob) {
				switch (cp->type & CAMEL_IMAPX_COMMAND_MASK) {
				case CAMEL_IMAPX_COMMAND_FILE:
				case CAMEL_IMAPX_COMMAND_STRING:
					g_free (cp->ob);
					break;
				default:
					g_object_unref (cp->ob);
				}
			}
			g_free (cp);
		}

		/* Free the private stuff. */

		g_string_free (real_ic->buffer, TRUE);

		g_cond_free (real_ic->done_sync_cond);
		g_mutex_free (real_ic->done_sync_mutex);

		/* Do NOT try to free the GError.  If set it should have been
		 * propagated to the CamelIMAPXJob, so it's either NULL or the
		 * CamelIMAPXJob owns it now. */

		g_slice_free (CamelIMAPXRealCommand, real_ic);
	}
}

void
camel_imapx_command_add (CamelIMAPXCommand *ic,
                         const gchar *format,
                         ...)
{
	va_list ap;

	g_return_if_fail (ic != NULL);

	if (format != NULL && *format != '\0') {
		va_start (ap, format);
		camel_imapx_command_addv (ic, format, ap);
		va_end (ap);
	}
}

void
camel_imapx_command_addv (CamelIMAPXCommand *ic,
                          const gchar *format,
                          va_list ap)
{
	const gchar *p, *ps, *start;
	guchar c;
	guint width;
	gchar ch;
	gint llong;
	gchar *s;
	gchar *P;
	gint d;
	glong l;
	guint32 f;
	CamelFlag *F;
	CamelStream *S;
	CamelDataWrapper *D;
	CamelSasl *A;
	gchar literal_format[16];
	CamelFolder *folder;
	CamelStore *parent_store;
	GString *buffer;
	gchar *fname = NULL, *encoded = NULL;
	const gchar *full_name;

	g_return_if_fail (ic != NULL);

	c(ic->is->tagprefix, "adding command, format = '%s'\n", format);

	buffer = ((CamelIMAPXRealCommand *) ic)->buffer;

	p = format;
	ps = format;
	while ((c = *p++) != '\0') {
		switch (c) {
		case '%':
			if (*p == '%') {
				g_string_append_len (buffer, ps, p - ps);
				p++;
				ps = p;
				continue;
			}

			g_string_append_len (buffer, ps, p - ps - 1);
			start = p - 1;
			width = 0;
			llong = 0;

			do {
				c = *p++;
				if (c == '0')
					;
				else if ( c== '-')
					;
				else
					break;
			} while (c);

			do {
				if (g_ascii_isdigit (c))
					width = width * 10 + (c - '0');
				else
					break;
			} while ((c = *p++));

			while (c == 'l') {
				llong++;
				c = *p++;
			}

			switch (c) {
			case 'A': /* auth object - sasl auth, treat as special kind of continuation */
				A = va_arg (ap, CamelSasl *);
				camel_imapx_command_add_part (ic, CAMEL_IMAPX_COMMAND_AUTH, A);
				break;
			case 'S': /* stream */
				S = va_arg (ap, CamelStream *);
				c(ic->is->tagprefix, "got stream '%p'\n", S);
				camel_imapx_command_add_part (ic, CAMEL_IMAPX_COMMAND_STREAM, S);
				break;
			case 'D': /* datawrapper */
				D = va_arg (ap, CamelDataWrapper *);
				c(ic->is->tagprefix, "got data wrapper '%p'\n", D);
				camel_imapx_command_add_part (ic, CAMEL_IMAPX_COMMAND_DATAWRAPPER, D);
				break;
			case 'P': /* filename path */
				P = va_arg (ap, gchar *);
				c(ic->is->tagprefix, "got file path '%s'\n", P);
				camel_imapx_command_add_part (ic, CAMEL_IMAPX_COMMAND_FILE, P);
				break;
			case 't': /* token */
				s = va_arg (ap, gchar *);
				g_string_append (buffer, s);
				break;
			case 's': /* simple string */
				s = va_arg (ap, gchar *);
				c(ic->is->tagprefix, "got string '%s'\n", g_str_has_prefix (format, "LOGIN") ? "***" : s);
			output_string:
				if (*s) {
					guchar mask = imapx_is_mask (s);

					if (mask & IMAPX_TYPE_ATOM_CHAR)
						g_string_append (buffer, s);
					else if (mask & IMAPX_TYPE_TEXT_CHAR) {
						g_string_append_c (buffer, '"');
						while (*s) {
							gchar *start = s;

							while (*s && imapx_is_quoted_char (*s))
								s++;
							g_string_append_len (buffer, start, s - start);
							if (*s) {
								g_string_append_c (buffer, '\\');
								g_string_append_c (buffer, *s);
								s++;
							}
						}
						g_string_append_c (buffer, '"');
					} else {
						camel_imapx_command_add_part (ic, CAMEL_IMAPX_COMMAND_STRING, s);
					}
				} else {
					g_string_append (buffer, "\"\"");
				}
				if (encoded) {
					g_free (encoded);
					encoded = NULL;
				}
				break;
			case 'f': /* imap folder name */
				folder = va_arg (ap, CamelFolder *);
				full_name = camel_folder_get_full_name (folder);
				c(ic->is->tagprefix, "got folder '%s'\n", full_name);
				parent_store = camel_folder_get_parent_store (folder);
				fname = camel_imapx_store_summary_full_from_path (((CamelIMAPXStore *) parent_store)->summary, full_name);
				if (fname) {
					encoded = camel_utf8_utf7 (fname);
					g_free (fname);
				} else
					encoded = camel_utf8_utf7 (full_name);

				if (encoded) {
					s = encoded;
					goto output_string;
				} else
					g_string_append (buffer, "\"\"");

				break;
			case 'F': /* IMAP flags set */
				f = va_arg (ap, guint32);
				F = va_arg (ap, CamelFlag *);
				imapx_write_flags (buffer, f, F);
				break;
			case 'c':
				d = va_arg (ap, gint);
				ch = d;
				g_string_append_c (buffer, ch);
				break;
			case 'd': /* int/unsigned */
			case 'u':
				if (llong == 1) {
					l = va_arg (ap, glong);
					c(ic->is->tagprefix, "got glong '%d'\n", (gint)l);
					memcpy (literal_format, start, p - start);
					literal_format[p - start] = 0;
					g_string_append_printf (buffer, literal_format, l);
				} else if (llong == 2) {
					guint64 i64 = va_arg (ap, guint64);
					c(ic->is->tagprefix, "got guint64 '%d'\n", (gint)i64);
					memcpy (literal_format, start, p - start);
					literal_format[p - start] = 0;
					g_string_append_printf (buffer, literal_format, i64);
				} else {
					d = va_arg (ap, gint);
					c(ic->is->tagprefix, "got gint '%d'\n", d);
					memcpy (literal_format, start, p - start);
					literal_format[p - start] = 0;
					g_string_append_printf (buffer, literal_format, d);
				}
				break;
			}

			ps = p;
			break;

		case '\\':	/* only for \\ really, we dont support \n\r etc at all */
			c = *p;
			if (c) {
				g_assert (c == '\\');
				g_string_append_len (buffer, ps, p - ps);
				p++;
				ps = p;
			}
		}
	}

	g_string_append_len (buffer, ps, p - ps - 1);
}

void
camel_imapx_command_add_part (CamelIMAPXCommand *ic,
                              CamelIMAPXCommandPartType type,
                              gpointer data)
{
	CamelIMAPXCommandPart *cp;
	CamelStreamNull *null;
	GString *buffer;
	guint ob_size = 0;

	buffer = ((CamelIMAPXRealCommand *) ic)->buffer;

	/* TODO: literal+? */

	switch (type & CAMEL_IMAPX_COMMAND_MASK) {
	case CAMEL_IMAPX_COMMAND_DATAWRAPPER:
	case CAMEL_IMAPX_COMMAND_STREAM: {
		CamelObject *ob = data;

		/* TODO: seekable streams we could just seek to the end and back */
		null = (CamelStreamNull *) camel_stream_null_new ();
		if ( (type & CAMEL_IMAPX_COMMAND_MASK) == CAMEL_IMAPX_COMMAND_DATAWRAPPER) {
			camel_data_wrapper_write_to_stream_sync ((CamelDataWrapper *) ob, (CamelStream *) null, NULL, NULL);
		} else {
			g_seekable_seek (G_SEEKABLE (ob), 0, G_SEEK_SET, NULL, NULL);
			camel_stream_write_to_stream ((CamelStream *) ob, (CamelStream *) null, NULL, NULL);
			g_seekable_seek (G_SEEKABLE (ob), 0, G_SEEK_SET, NULL, NULL);
		}
		type |= CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
		g_object_ref (ob);
		ob_size = null->written;
		g_object_unref (null);
		break;
	}
	case CAMEL_IMAPX_COMMAND_AUTH: {
		CamelObject *ob = data;
		const gchar *mechanism;

		/* we presume we'll need to get additional data only if we're not authenticated yet */
		g_object_ref (ob);
		mechanism = camel_sasl_get_mechanism (CAMEL_SASL (ob));
		g_string_append (buffer, mechanism);
		if (!camel_sasl_get_authenticated ((CamelSasl *) ob))
			type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		break;
	}
	case CAMEL_IMAPX_COMMAND_FILE: {
		gchar *path = data;
		struct stat st;

		if (g_stat (path, &st) == 0) {
			data = g_strdup (data);
			ob_size = st.st_size;
		} else
			data = NULL;

		type |= CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
		break;
	}
	case CAMEL_IMAPX_COMMAND_STRING:
		data = g_strdup (data);
		ob_size = strlen (data);
		type |= CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
		break;
	default:
		ob_size = 0;
	}

	if (type & CAMEL_IMAPX_COMMAND_LITERAL_PLUS) {
		g_string_append_c (buffer, '{');
		g_string_append_printf (buffer, "%u", ob_size);
		if (ic->is->cinfo && ic->is->cinfo->capa & IMAPX_CAPABILITY_LITERALPLUS) {
			g_string_append_c (buffer, '+');
		} else {
			type &= ~CAMEL_IMAPX_COMMAND_LITERAL_PLUS;
			type |= CAMEL_IMAPX_COMMAND_CONTINUATION;
		}
		g_string_append_c (buffer, '}');
	}

	cp = g_malloc0 (sizeof (*cp));
	cp->type = type;
	cp->ob_size = ob_size;
	cp->ob = data;
	cp->data_size = buffer->len;
	cp->data = g_strdup (buffer->str);

	g_string_set_size (buffer, 0);

	camel_dlist_addtail (&ic->parts, (CamelDListNode *) cp);
}

void
camel_imapx_command_close (CamelIMAPXCommand *ic)
{
	GString *buffer;

	g_return_if_fail (ic != NULL);

	buffer = ((CamelIMAPXRealCommand *) ic)->buffer;

	if (buffer->len > 5 && g_ascii_strncasecmp (buffer->str, "LOGIN", 5) == 0) {
		c(ic->is->tagprefix, "completing command buffer is [%d] 'LOGIN...'\n", buffer->len);
	} else {
		c(ic->is->tagprefix, "completing command buffer is [%d] '%.*s'\n", buffer->len, buffer->len, buffer->str);
	}
	if (buffer->len > 0)
		camel_imapx_command_add_part (ic, CAMEL_IMAPX_COMMAND_SIMPLE, NULL);

	g_string_set_size (buffer, 0);
}

void
camel_imapx_command_wait (CamelIMAPXCommand *ic)
{
	CamelIMAPXRealCommand *real_ic;

	g_return_if_fail (ic != NULL);

	real_ic = (CamelIMAPXRealCommand *) ic;

	g_mutex_lock (real_ic->done_sync_mutex);
	while (!real_ic->done_sync_flag)
		g_cond_wait (
			real_ic->done_sync_cond,
			real_ic->done_sync_mutex);
	g_mutex_unlock (real_ic->done_sync_mutex);
}

void
camel_imapx_command_done (CamelIMAPXCommand *ic)
{
	CamelIMAPXRealCommand *real_ic;

	g_return_if_fail (ic != NULL);

	real_ic = (CamelIMAPXRealCommand *) ic;

	g_mutex_lock (real_ic->done_sync_mutex);
	real_ic->done_sync_flag = TRUE;
	g_cond_broadcast (real_ic->done_sync_cond);
	g_mutex_unlock (real_ic->done_sync_mutex);
}
