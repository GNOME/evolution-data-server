/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-imap-command.c: IMAP command sending/parsing routines */

/*
 *  Authors:
 *    Dan Winship <danw@ximian.com>
 *    Jeffrey Stedfast <fejj@ximian.com>
 *
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-imap-command.h"
#include "camel-imap-folder.h"
#include "camel-imap-store-summary.h"
#include "camel-imap-store.h"
#include "camel-imap-utils.h"

extern gint camel_verbose_debug;

static gboolean imap_command_start (CamelImapStore *store, CamelFolder *folder,
				    const gchar *cmd, GCancellable *cancellable,
				    GError **error);
static CamelImapResponse *imap_read_response (CamelImapStore *store,
					      CamelFolder *folder,
					      GCancellable *cancellable,
					      GError **error);
static gchar *imap_read_untagged (CamelImapStore *store, gchar *line,
				 GCancellable *cancellable, GError **error);
static gchar *imap_command_strdup_vprintf (CamelImapStore *store,
					  const gchar *fmt, va_list ap);
static gchar *imap_command_strdup_printf (CamelImapStore *store,
					 const gchar *fmt, ...);

/**
 * camel_imap_command:
 * @store: the IMAP store
 * @folder: The folder to perform the operation in (or %NULL if not
 * relevant).
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 * @fmt: a sort of printf-style format string, followed by arguments
 *
 * This function calls camel_imap_command_start() to send the
 * command, then reads the complete response to it using
 * camel_imap_command_response() and returns a CamelImapResponse
 * structure.
 *
 * As a special case, if @fmt is %NULL, it will just select @folder
 * and return the response from doing so.
 *
 * See camel_imap_command_start() for details on @fmt.
 *
 * On success, the store's connect_lock will be locked. It will be freed
 * when you call camel_imap_response_free. (The lock is recursive, so
 * callers can grab and release it themselves if they need to run
 * multiple commands atomically.)
 *
 * Returns: %NULL if an error occurred (in which case @ex will
 * be set). Otherwise, a CamelImapResponse describing the server's
 * response, which the caller must free with camel_imap_response_free().
 **/
CamelImapResponse *
camel_imap_command (CamelImapStore *store,
                    CamelFolder *folder,
                    GCancellable *cancellable,
                    GError **error,
                    const gchar *fmt, ...)
{
	va_list ap;
	gchar *cmd;

	g_static_rec_mutex_lock (&store->command_and_response_lock);

	if (fmt) {
		va_start (ap, fmt);
		cmd = imap_command_strdup_vprintf (store, fmt, ap);
		va_end (ap);
	} else {
		const gchar *full_name;

		g_object_ref (folder);
		if (store->current_folder)
			g_object_unref (store->current_folder);
		store->current_folder = folder;

		full_name = camel_folder_get_full_name (folder);
		cmd = imap_command_strdup_printf (store, "SELECT %F", full_name);
	}

	if (!imap_command_start (store, folder, cmd, cancellable, error)) {
		g_free (cmd);
		g_static_rec_mutex_unlock (&store->command_and_response_lock);
		return NULL;
	}
	g_free (cmd);

	return imap_read_response (store, folder, cancellable, error);
}

/**
 * camel_imap_command_start:
 * @store: the IMAP store
 * @folder: The folder to perform the operation in (or %NULL if not
 * relevant).
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 * @fmt: a sort of printf-style format string, followed by arguments
 *
 * This function makes sure that @folder (if non-%NULL) is the
 * currently-selected folder on @store and then sends the IMAP command
 * specified by @fmt and the following arguments.
 *
 * @fmt can include the following %-escapes ONLY:
 *	%s, %d, %%: as with printf
 *	%S: an IMAP "string" (quoted string or literal)
 *	%F: an IMAP folder name
 *	%G: an IMAP folder name, with namespace already prepended
 *
 * %S strings will be passed as literals if the server supports LITERAL+
 * and quoted strings otherwise. (%S does not support strings that
 * contain newlines.)
 *
 * %F will have the imap store's namespace prepended; %F and %G will then
 * be converted to UTF-7 and processed like %S.
 *
 * On success, the store's connect_lock will be locked. It will be
 * freed when %CAMEL_IMAP_RESPONSE_TAGGED or %CAMEL_IMAP_RESPONSE_ERROR
 * is returned from camel_imap_command_response(). (The lock is
 * recursive, so callers can grab and release it themselves if they
 * need to run multiple commands atomically.)
 *
 * Returns: %TRUE if the command was sent successfully, %FALSE if
 * an error occurred (in which case @ex will be set).
 **/
gboolean
camel_imap_command_start (CamelImapStore *store,
                          CamelFolder *folder,
                          GCancellable *cancellable,
                          GError **error,
                          const gchar *fmt, ...)
{
	va_list ap;
	gchar *cmd;
	gboolean ok;

	va_start (ap, fmt);
	cmd = imap_command_strdup_vprintf (store, fmt, ap);
	va_end (ap);

	g_static_rec_mutex_lock (&store->command_and_response_lock);
	ok = imap_command_start (store, folder, cmd, cancellable, error);
	g_free (cmd);

	if (!ok)
		g_static_rec_mutex_unlock (&store->command_and_response_lock);

	return ok;
}

static gboolean
imap_command_start (CamelImapStore *store,
                    CamelFolder *folder,
                    const gchar *cmd,
                    GCancellable *cancellable,
                    GError **error)
{
	gchar *content;
	gssize nwritten;

	if (!store->ostream) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("No output stream"));
		return FALSE;
	}

	if (!store->istream) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("No input stream"));
		return FALSE;
	}

	/* Check for current folder */
	if (folder && folder != store->current_folder) {
		CamelImapResponse *response;
		GError *local_error = NULL;

		response = camel_imap_command (
			store, folder, cancellable, error, NULL);
		if (!response)
			return FALSE;

		/* FIXME Pass a GCancellable */
		camel_imap_folder_selected (
			folder, response, NULL, &local_error);
		camel_imap_response_free (store, response);

		if (local_error != NULL) {
			g_propagate_error (error, local_error);
			return FALSE;
		}
	}

	/* Send the command */
	if (camel_verbose_debug) {
		const gchar *mask;

		if (!strncmp ("LOGIN \"", cmd, 7))
			mask = "LOGIN \"xxx\" xxx";
		else if (!strncmp ("LOGIN {", cmd, 7))
			mask = "LOGIN {N+}\r\nxxx {N+}\r\nxxx";
		else if (!strncmp ("LOGIN ", cmd, 6))
			mask = "LOGIN xxx xxx";
		else
			mask = cmd;

		fprintf (stderr, "sending : %c%.5u %s\r\n", store->tag_prefix, store->command, mask);
	}

	content = g_strdup_printf (
		"%c%.5u %s\r\n",
		store->tag_prefix, store->command++, cmd);
	nwritten = camel_stream_write_string (
		store->ostream, content, cancellable, error);
	g_free (content);

	if (nwritten == -1) {
		/* do not pass cancellable, the connection is gone or
		 * the cancellable cancelled, thus there will be no I/O */
		camel_service_disconnect_sync (
			CAMEL_SERVICE (store), FALSE, NULL, NULL);
		return FALSE;
	}

	return TRUE;
}

/**
 * camel_imap_command_continuation:
 * @store: the IMAP store
 * @folder: a #CamelFolder on which the command was run; can be %NULL
 * @cmd: buffer containing the response/request data
 * @cmdlen: command length
 * @error: return location for a #GError, or %NULL
 *
 * This method is for sending continuing responses to the IMAP server
 * after camel_imap_command() or camel_imap_command_response() returns
 * a continuation response.
 *
 * This function assumes you have an exclusive lock on the imap stream.
 *
 * Returns: as for camel_imap_command(). On failure, the store's
 * connect_lock will be released.
 **/
CamelImapResponse *
camel_imap_command_continuation (CamelImapStore *store,
                                 CamelFolder *folder,
                                 const gchar *cmd,
                                 gsize cmdlen,
                                 GCancellable *cancellable,
                                 GError **error)
{
	if (!camel_imap_store_connected (store, error))
		return NULL;

	if (!store->ostream) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("No output stream"));
		return NULL;
	}

	if (!store->istream) {
		g_set_error (
			error, CAMEL_STORE_ERROR,
			CAMEL_STORE_ERROR_INVALID,
			_("No input stream"));
		return NULL;
	}

	if (camel_stream_write (store->ostream, cmd, cmdlen, cancellable, error) == -1 ||
	    camel_stream_write (store->ostream, "\r\n", 2, cancellable, error) == -1) {
		/* do not pass cancellable, the connection is gone or
		 * the cancellable cancelled, thus there will be no I/O */
		camel_service_disconnect_sync (
			CAMEL_SERVICE (store), FALSE, NULL, NULL);
		g_static_rec_mutex_unlock (&store->command_and_response_lock);
		return NULL;
	}

	return imap_read_response (store, folder, cancellable, error);
}

/**
 * camel_imap_command_response:
 * @store: the IMAP store
 * @folder: a #CamelFolder on which the command was run; can be %NULL
 * @response: a pointer to pass back the response data in
 * @cancellable: optional #GCancellable object, or %NULL
 * @error: return location for a #GError, or %NULL
 *
 * This reads a single tagged, untagged, or continuation response from
 * @store into *@response. The caller must free the string when it is
 * done with it.
 *
 * Returns: One of %CAMEL_IMAP_RESPONSE_CONTINUATION,
 * %CAMEL_IMAP_RESPONSE_UNTAGGED, %CAMEL_IMAP_RESPONSE_TAGGED, or
 * %CAMEL_IMAP_RESPONSE_ERROR. If either of the last two, @store's
 * command lock will be unlocked.
 **/
CamelImapResponseType
camel_imap_command_response (CamelImapStore *store,
                             CamelFolder *folder,
                             gchar **response,
                             GCancellable *cancellable,
                             GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelService *service;
	CamelSession *session;
	CamelImapResponseType type;
	gchar *respbuf;
	gchar *host;
	gchar *user;

	service = CAMEL_SERVICE (store);
	session = camel_service_get_session (service);

	settings = camel_service_ref_settings (service);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	host = camel_network_settings_dup_host (network_settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	if (camel_imap_store_readline (store, &respbuf, cancellable, error) < 0) {
		g_static_rec_mutex_unlock (&store->command_and_response_lock);
		type = CAMEL_IMAP_RESPONSE_ERROR;
		goto exit;
	}

	switch (*respbuf) {
	case '*':
		if (!g_ascii_strncasecmp (respbuf, "* BYE", 5)) {
			const gchar *err = NULL;

			if (respbuf[5] && g_ascii_strncasecmp (respbuf + 6, "[ALERT] ", 8) == 0)
				err = respbuf + 14;

			if (!err || !*err)
				err = g_strerror (104);

			/* do not pass cancellable, the connection is gone or
			 * the cancellable cancelled, thus there will be no I/O */
			camel_service_disconnect_sync (
				service, FALSE, NULL, NULL);
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Server unexpectedly disconnected: %s"), err);
			store->connected = FALSE;
			g_free (respbuf);
			respbuf = NULL;
			type = CAMEL_IMAP_RESPONSE_ERROR;
			break;
		}

		/* Read the rest of the response. */
		type = CAMEL_IMAP_RESPONSE_UNTAGGED;
		respbuf = imap_read_untagged (
			store, respbuf, cancellable, error);
		if (!respbuf)
			type = CAMEL_IMAP_RESPONSE_ERROR;
		else {
			gboolean ok_alert = g_ascii_strncasecmp (respbuf, "* OK [ALERT]", 12) == 0;
			gboolean bad_alert = g_ascii_strncasecmp (respbuf, "* BAD [ALERT]", 13) == 0;

			if (ok_alert || bad_alert || g_ascii_strncasecmp (respbuf, "* NO [ALERT]", 12) == 0) {
				const gchar *alert;
				gchar *msg;

				alert = respbuf + 12 + (bad_alert ? 1 : 0);

				if (ok_alert) {
					if (g_hash_table_lookup (store->known_alerts, alert))
						break;

					g_hash_table_insert (store->known_alerts, g_strdup (alert), GINT_TO_POINTER (1));
				}

				if (folder)
					msg = g_strdup_printf (
						_("Alert from IMAP server %s@%s in folder %s:\n%s"),
						user, host, camel_folder_get_full_name (folder), alert);
				else
					msg = g_strdup_printf (
						_("Alert from IMAP server %s@%s:\n%s"),
						user, host, alert);
				camel_session_alert_user (
					session, CAMEL_SESSION_ALERT_WARNING,
					msg, NULL);
				g_free (msg);
			}
		}

		break;
	case '+':
		type = CAMEL_IMAP_RESPONSE_CONTINUATION;
		break;
	default:
		type = CAMEL_IMAP_RESPONSE_TAGGED;
		break;
	}
	*response = respbuf;

	if (type == CAMEL_IMAP_RESPONSE_ERROR ||
	    type == CAMEL_IMAP_RESPONSE_TAGGED)
		g_static_rec_mutex_unlock (&store->command_and_response_lock);

exit:
	g_free (host);
	g_free (user);

	return type;
}

static CamelImapResponse *
imap_read_response (CamelImapStore *store,
                    CamelFolder *folder,
                    GCancellable *cancellable,
                    GError **error)
{
	CamelImapResponse *response;
	CamelImapResponseType type;
	gchar *respbuf, *p;

	/* Get another lock so that when we reach the tagged
	 * response and camel_imap_command_response unlocks,
	 * we're still locked. This lock is owned by response
	 * and gets unlocked when response is freed.
	 */
	g_static_rec_mutex_lock (&store->command_and_response_lock);

	response = g_new0 (CamelImapResponse, 1);
/*FIXME	if (store->current_folder && camel_disco_store_status (CAMEL_DISCO_STORE (store)) != CAMEL_DISCO_STORE_RESYNCING) {
		response->folder = store->current_folder;
		g_object_ref (response->folder);
	} */

	response->untagged = g_ptr_array_new ();
	while ((type = camel_imap_command_response (
		store, folder, &respbuf, cancellable, error))
		== CAMEL_IMAP_RESPONSE_UNTAGGED)
		g_ptr_array_add (response->untagged, respbuf);

	if (type == CAMEL_IMAP_RESPONSE_ERROR) {
		camel_imap_response_free_without_processing (store, response);
		return NULL;
	}

	response->status = respbuf;

	/* Check for OK or continuation response. */
	if (*respbuf == '+')
		return response;
	p = strchr (respbuf, ' ');
	if (p && !g_ascii_strncasecmp (p, " OK", 3))
		return response;

	/* We should never get BAD, or anything else but +, OK, or NO
	 * for that matter.  Well, we could get BAD, treat as NO.
	 */
	if (!p || (g_ascii_strncasecmp (p, " NO", 3) != 0 && g_ascii_strncasecmp (p, " BAD", 4)) ) {
		g_warning (
			"Unexpected response from IMAP server: %s",
			respbuf);
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("Unexpected response from IMAP server: %s"),
			respbuf);
		camel_imap_response_free_without_processing (store, response);
		return NULL;
	}

	p += 3;
	if (!*p++)
		p = NULL;
	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_INVALID,
		_("IMAP command failed: %s"),
		(p != NULL) ? p : _("Unknown error"));
	camel_imap_response_free_without_processing (store, response);
	return NULL;
}

/* Given a line that is the start of an untagged response, read and
 * return the complete response, which may include an arbitrary number
 * of literals.
 */
static gchar *
imap_read_untagged (CamelImapStore *store,
                    gchar *line,
                    GCancellable *cancellable,
                    GError **error)
{
	gint fulllen, ldigits, nread, n, i, sexp = 0;
	guint length;
	GPtrArray *data;
	GString *str;
	gchar *end, *p, *s, *d;

	p = strrchr (line, '{');
	if (!p)
		return line;

	data = g_ptr_array_new ();
	fulllen = 0;

	while (1) {
		str = g_string_new (line);
		g_free (line);
		fulllen += str->len;
		g_ptr_array_add (data, str);

		if (!(p = strrchr (str->str, '{')) || p[1] == '-')
			break;

		/* HACK ALERT: We scan the non-literal part of the string, looking for possible s expression braces.
		 * This assumes we're getting s-expressions, which we should be.
		 * This is so if we get a blank line after a literal, in an s-expression, we can keep going, since
		 * we do no other parsing at this level.
		 * TODO: handle quoted strings? */
		for (s = str->str; s < p; s++) {
			if (*s == '(')
				sexp++;
			else if (*s == ')')
				sexp--;
		}

		length = strtoul (p + 1, &end, 10);
		if (*end != '}' || *(end + 1) || end == p + 1 || length >= UINT_MAX - 2)
			break;
		ldigits = end - (p + 1);

		/* Read the literal */
		str = g_string_sized_new (length + 2);
		str->str[0] = '\n';
		nread = 0;

		do {
			n = camel_stream_read (
				store->istream,
				str->str + nread + 1,
				length - nread,
				cancellable, error);
			if (n == -1) {
				/* do not pass cancellable, the connection is gone or
				 * the cancellable cancelled, thus there will be no I/O */
				camel_service_disconnect_sync (
					CAMEL_SERVICE (store),
					FALSE, NULL, NULL);
				g_string_free (str, TRUE);
				goto lose;
			}

			nread += n;
		} while (n > 0 && nread < length);

		if (nread < length) {
			g_set_error (
				error, CAMEL_SERVICE_ERROR,
				CAMEL_SERVICE_ERROR_UNAVAILABLE,
				_("Server response ended too soon."));
			/* do not pass cancellable, the connection is gone or
			 * the cancellable cancelled, thus there will be no I/O */
			camel_service_disconnect_sync (
				CAMEL_SERVICE (store),
				FALSE, NULL, NULL);
			g_string_free (str, TRUE);
			goto lose;
		}
		str->str[length + 1] = '\0';

		if (camel_debug ("imap")) {
			printf ("Literal: -->");
			fwrite (str->str + 1, 1, length, stdout);
			printf ("<--\n");
		}

		/* Fix up the literal, turning CRLFs into LF. Also, if
		 * we find any embedded NULs, strip them. This is
		 * dubious, but:
		 *   - The IMAP grammar says you can't have NULs here
		 *     anyway, so this will not affect our behavior
		 *     against any completely correct server.
		 *   - WU-imapd 12.264 (at least) will cheerily pass
		 *     NULs along if they are embedded in the message
		 */

		s = d = str->str + 1;
		end = str->str + 1 + length;
		while (s < end) {
			while (s < end && *s == '\0') {
				s++;
				length--;
			}
			if (*s == '\r' && *(s + 1) == '\n') {
				s++;
				length--;
			}
			*d++ = *s++;
		}
		*d = '\0';
		str->len = length + 1;

		/* p points to the "{" in the line that starts the
		 * literal. The length of the CR-less response must be
		 * less than or equal to the length of the response
		 * with CRs, therefore overwriting the old value with
		 * the new value cannot cause an overrun. However, we
		 * don't want it to be shorter either, because then the
		 * GString's length would be off...
		 */
		sprintf (p, "{%0*u}", ldigits, length);

		fulllen += str->len;
		g_ptr_array_add (data, str);

		/* Read the next line. */
		do {
			if (camel_imap_store_readline (store, &line, cancellable, error) < 0)
				goto lose;

			/* MAJOR HACK ALERT, gropuwise sometimes sends an extra blank line after literals, check that here
			 * But only do it if we're inside an sexpression */
			if (line[0] == 0 && sexp > 0)
				g_warning ("Server sent empty line after a literal, assuming in error");
		} while (line[0] == 0 && sexp > 0);
	}

	/* Now reassemble the data. */
	p = line = g_malloc (fulllen + 1);
	for (i = 0; i < data->len; i++) {
		str = data->pdata[i];
		memcpy (p, str->str, str->len);
		p += str->len;
		g_string_free (str, TRUE);
	}
	*p = '\0';
	g_ptr_array_free (data, TRUE);
	return line;

 lose:
	for (i = 0; i < data->len; i++)
		g_string_free (data->pdata[i], TRUE);
	g_ptr_array_free (data, TRUE);
	return NULL;
}

/**
 * camel_imap_response_free:
 * @store: the CamelImapStore the response is from
 * @response: a CamelImapResponse
 *
 * Frees all of the data in @response and processes any untagged
 * EXPUNGE and EXISTS responses in it. Releases @store's connect_lock.
 **/
void
camel_imap_response_free (CamelImapStore *store,
                          CamelImapResponse *response)
{
	gint i, number, exists = 0;
	GArray *expunged = NULL;
	gchar *resp, *p;

	if (!response)
		return;

	for (i = 0; i < response->untagged->len; i++) {
		resp = response->untagged->pdata[i];

		if (response->folder) {
			/* Check if it's something we need to handle. */
			number = strtoul (resp + 2, &p, 10);
			if (!g_ascii_strcasecmp (p, " EXISTS")) {
				exists = number;
			} else if (!g_ascii_strcasecmp (p, " EXPUNGE")
				   || !g_ascii_strcasecmp (p, " XGWMOVE")) {
				/* XGWMOVE response is the same as an EXPUNGE response */
				if (!expunged) {
					expunged = g_array_new (
						FALSE, FALSE, sizeof (gint));
				}
				g_array_append_val (expunged, number);
			}
		}
		g_free (resp);
	}

	g_ptr_array_free (response->untagged, TRUE);
	g_free (response->status);

	if (response->folder) {
		if (exists > 0 || expunged) {
			/* Update the summary */
			/* FIXME Pass a GCancellable */
			camel_imap_folder_changed (
				response->folder, exists,
				expunged, NULL, NULL);
			if (expunged)
				g_array_free (expunged, TRUE);
		}

		g_object_unref (response->folder);
	}

	g_free (response);
	g_static_rec_mutex_unlock (&store->command_and_response_lock);
}

/**
 * camel_imap_response_free_without_processing:
 * @store: the CamelImapStore the response is from.
 * @response: a CamelImapResponse:
 *
 * Frees all of the data in @response without processing any untagged
 * responses. Releases @store's command lock.
 **/
void
camel_imap_response_free_without_processing (CamelImapStore *store,
                                             CamelImapResponse *response)
{
	if (!response)
		return;

	if (response->folder) {
		g_object_unref (response->folder);
		response->folder = NULL;
	}
	camel_imap_response_free (store, response);
}

/**
 * camel_imap_response_extract:
 * @store: the store the response came from
 * @response: the response data returned from camel_imap_command
 * @type: the response type to extract
 * @error: return location for a #GError, or %NULL
 *
 * This checks that @response contains a single untagged response of
 * type @type and returns just that response data. If @response
 * doesn't contain the right information, the function will set @ex
 * and return %NULL. Either way, @response will be freed and the
 * store's connect_lock released.
 *
 * Returns: the desired response string, which the caller must free.
 **/
gchar *
camel_imap_response_extract (CamelImapStore *store,
                             CamelImapResponse *response,
                             const gchar *type,
                             GError **error)
{
	gint len, i;
	gchar *resp;

	len = strlen (type);

	for (i = 0; i < response->untagged->len; i++) {
		resp = response->untagged->pdata[i];
		/* Skip "* ", and initial sequence number, if present */
		strtoul (resp + 2, &resp, 10);
		if (*resp == ' ')
			resp = (gchar *) imap_next_word (resp);

		if (!g_ascii_strncasecmp (resp, type, len))
			break;
	}

	if (i < response->untagged->len) {
		resp = response->untagged->pdata[i];
		g_ptr_array_remove_index (response->untagged, i);
	} else {
		resp = NULL;
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_UNAVAILABLE,
			_("IMAP server response did not "
			"contain %s information"), type);
	}

	camel_imap_response_free (store, response);
	return resp;
}

/**
 * camel_imap_response_extract_continuation:
 * @store: the store the response came from
 * @response: the response data returned from camel_imap_command
 * @error: return location for a #GError, or %NULL
 *
 * This checks that @response contains a continuation response, and
 * returns just that data. If @response doesn't contain a continuation
 * response, the function will set @ex, release @store's connect_lock,
 * and return %NULL. Either way, @response will be freed.
 *
 * Returns: the desired response string, which the caller must free.
 **/
gchar *
camel_imap_response_extract_continuation (CamelImapStore *store,
                                          CamelImapResponse *response,
                                          GError **error)
{
	gchar *status;

	if (response->status && *response->status == '+') {
		status = response->status;
		response->status = NULL;
		camel_imap_response_free (store, response);
		return status;
	}

	g_set_error (
		error, CAMEL_SERVICE_ERROR,
		CAMEL_SERVICE_ERROR_UNAVAILABLE,
		_("Unexpected OK response from IMAP server: %s"),
		response->status);

	camel_imap_response_free (store, response);
	return NULL;
}

static gchar *
imap_command_strdup_vprintf (CamelImapStore *store,
                             const gchar *fmt,
                             va_list ap)
{
	GPtrArray *args;
	const gchar *p, *start;
	gchar *out, *outptr, *string;
	gint num, len, i, arglen;

	args = g_ptr_array_new ();

	/* Determine the length of the data */
	len = strlen (fmt);
	p = start = fmt;
	while (*p) {
		p = strchr (start, '%');
		if (!p)
			break;

		switch (*++p) {
		case 'd':
			num = va_arg (ap, gint);
			g_ptr_array_add (args, GINT_TO_POINTER (num));
			start = p + 1;
			len += 10;
			break;
		case 's':
			string = va_arg (ap, gchar *);
			g_ptr_array_add (args, string);
			start = p + 1;
			len += strlen (string);
			break;
		case 'S':
		case 'F':
		case 'G':
			string = va_arg (ap, gchar *);
			/* NB: string is freed during output for %F and %G */
			if (*p == 'F') {
				gchar *s = camel_imap_store_summary_full_from_path (store->summary, string);
				if (s) {
					string = camel_utf8_utf7 (s);
					g_free (s);
				} else {
					string = camel_utf8_utf7 (string);
				}
			} else if (*p == 'G') {
				string = camel_utf8_utf7 (string);
			}

			arglen = strlen (string);
			g_ptr_array_add (args, string);
			if (imap_is_atom (string)) {
				len += arglen;
			} else {
				if (store->capabilities & IMAP_CAPABILITY_LITERALPLUS)
					len += arglen + 15;
				else
					len += arglen * 2;
			}
			start = p + 1;
			break;
		case '%':
			start = p;
			break;
		default:
			g_warning (
				"camel-imap-command is not printf. "
				"I don't know what '%%%c' means.", *p);
			start = *p ? p + 1 : p;
			break;
		}
	}

	/* Now write out the string */
	outptr = out = g_malloc (len + 1);
	p = start = fmt;
	i = 0;
	while (*p) {
		p = strchr (start, '%');
		if (!p) {
			strcpy (outptr, start);
			break;
		} else {
			strncpy (outptr, start, p - start);
			outptr += p - start;
		}

		switch (*++p) {
		case 'd':
			num = GPOINTER_TO_INT (args->pdata[i++]);
			outptr += sprintf (outptr, "%d", num);
			break;

		case 's':
			string = args->pdata[i++];
			outptr += sprintf (outptr, "%s", string);
			break;
		case 'S':
		case 'F':
		case 'G':
			string = args->pdata[i++];
			if (imap_is_atom (string)) {
				outptr += sprintf (outptr, "%s", string);
			} else {
				len = strlen (string);
				if (len && store->capabilities & IMAP_CAPABILITY_LITERALPLUS) {
					outptr += sprintf (outptr, "{%d+}\r\n%s", len, string);
				} else {
					gchar *quoted = imap_quote_string (string);

					outptr += sprintf (outptr, "%s", quoted);
					g_free (quoted);
				}
			}

			if (*p == 'F' || *p == 'G')
				g_free (string);
			break;
		default:
			*outptr++ = '%';
			*outptr++ = *p;
		}

		start = *p ? p + 1 : p;
	}

	g_ptr_array_free (args, TRUE);

	return out;
}

static gchar *
imap_command_strdup_printf (CamelImapStore *store,
                            const gchar *fmt,
                            ...)
{
	va_list ap;
	gchar *result;

	va_start (ap, fmt);
	result = imap_command_strdup_vprintf (store, fmt, ap);
	va_end (ap);

	return result;
}
