/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* camel-sendmail-transport.c: Sendmail-based transport class. */

/*
 *
 * Authors: Dan Winship <danw@ximian.com>
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
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include <glib/gi18n-lib.h>

#include "camel-sendmail-transport.h"

G_DEFINE_TYPE (
	CamelSendmailTransport,
	camel_sendmail_transport, CAMEL_TYPE_TRANSPORT)

static gchar *
sendmail_get_name (CamelService *service,
                   gboolean brief)
{
	if (brief)
		return g_strdup (_("sendmail"));
	else
		return g_strdup (_("Mail delivery via the sendmail program"));
}

static gboolean
sendmail_send_to_sync (CamelTransport *transport,
                       CamelMimeMessage *message,
                       CamelAddress *from,
                       CamelAddress *recipients,
                       GCancellable *cancellable,
                       GError **error)
{
	struct _camel_header_raw *header, *savedbcc, *n, *tail;
	const gchar *from_addr, *addr, **argv;
	gint i, len, fd[2], nullfd, wstat;
	CamelStream *filter;
	CamelMimeFilter *crlf;
	sigset_t mask, omask;
	CamelStream *out;
	gboolean success;
	pid_t pid;

	success = camel_internet_address_get (
		CAMEL_INTERNET_ADDRESS (from), 0, NULL, &from_addr);

	if (!success)
		return FALSE;

	len = camel_address_length (recipients);
	argv = g_malloc ((len + 6) * sizeof (gchar *));
	argv[0] = "sendmail";
	argv[1] = "-i";
	argv[2] = "-f";
	argv[3] = from_addr;
	argv[4] = "--";

	for (i = 0; i < len; i++) {
		success = camel_internet_address_get (
			CAMEL_INTERNET_ADDRESS (recipients), i, NULL, &addr);

		if (!success) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Could not parse recipient list"));
			g_free (argv);
			return FALSE;
		}

		argv[i + 5] = addr;
	}

	argv[i + 5] = NULL;

	/* unlink the bcc headers */
	savedbcc = NULL;
	tail = (struct _camel_header_raw *) &savedbcc;

	header = (struct _camel_header_raw *) &CAMEL_MIME_PART (message)->headers;
	n = header->next;
	while (n != NULL) {
		if (!g_ascii_strcasecmp (n->name, "Bcc")) {
			header->next = n->next;
			tail->next = n;
			n->next = NULL;
			tail = n;
		} else {
			header = n;
		}

		n = header->next;
	}

	if (pipe (fd) == -1) {
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not create pipe to sendmail: %s: "
			"mail not sent"), g_strerror (errno));

		/* restore the bcc headers */
		header->next = savedbcc;

		return FALSE;
	}

	/* Block SIGCHLD so the calling application doesn't notice
	 * sendmail exiting before we do.
	 */
	sigemptyset (&mask);
	sigaddset (&mask, SIGCHLD);
	sigprocmask (SIG_BLOCK, &mask, &omask);

	pid = fork ();
	switch (pid) {
	case -1:
		g_set_error (
			error, G_IO_ERROR,
			g_io_error_from_errno (errno),
			_("Could not fork sendmail: %s: "
			"mail not sent"), g_strerror (errno));
		close (fd[0]);
		close (fd[1]);
		sigprocmask (SIG_SETMASK, &omask, NULL);
		g_free (argv);

		/* restore the bcc headers */
		header->next = savedbcc;

		return FALSE;
	case 0:
		/* Child process */
		nullfd = open ("/dev/null", O_RDWR);
		dup2 (fd[0], STDIN_FILENO);
		if (nullfd != -1) {
			/*dup2 (nullfd, STDOUT_FILENO);
			  dup2 (nullfd, STDERR_FILENO);*/
			close (nullfd);
		}
		close (fd[1]);

		execv (SENDMAIL_PATH, (gchar **) argv);
		_exit (255);
	}
	g_free (argv);

	/* Parent process. Write the message out. */
	close (fd[0]);
	out = camel_stream_fs_new_with_fd (fd[1]);

	/* XXX Workaround for lame sendmail implementations
	 *     that can't handle CRLF eoln sequences. */
	filter = camel_stream_filter_new (out);
	crlf = camel_mime_filter_crlf_new (
		CAMEL_MIME_FILTER_CRLF_DECODE,
		CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY);
	camel_stream_filter_add (CAMEL_STREAM_FILTER (filter), crlf);
	g_object_unref (crlf);
	g_object_unref (out);

	out = (CamelStream *) filter;
	if (camel_data_wrapper_write_to_stream_sync (
		CAMEL_DATA_WRAPPER (message), out, cancellable, error) == -1
	    || camel_stream_close (out, cancellable, error) == -1) {
		g_object_unref (out);
		g_prefix_error (error, _("Could not send message: "));

		/* Wait for sendmail to exit. */
		while (waitpid (pid, &wstat, 0) == -1 && errno == EINTR)
			;

		sigprocmask (SIG_SETMASK, &omask, NULL);

		/* restore the bcc headers */
		header->next = savedbcc;

		return FALSE;
	}

	g_object_unref (out);

	/* Wait for sendmail to exit. */
	while (waitpid (pid, &wstat, 0) == -1 && errno == EINTR)
		;

	sigprocmask (SIG_SETMASK, &omask, NULL);

	/* restore the bcc headers */
	header->next = savedbcc;

	if (!WIFEXITED (wstat)) {
		g_set_error (
			error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
			_("sendmail exited with signal %s: mail not sent."),
			g_strsignal (WTERMSIG (wstat)));
		return FALSE;
	} else if (WEXITSTATUS (wstat) != 0) {
		if (WEXITSTATUS (wstat) == 255) {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("Could not execute %s: mail not sent."),
				SENDMAIL_PATH);
		} else {
			g_set_error (
				error, CAMEL_ERROR, CAMEL_ERROR_GENERIC,
				_("sendmail exited with status %d: "
				"mail not sent."),
				WEXITSTATUS (wstat));
		}
		return FALSE;
	}

	return TRUE;
}

static void
camel_sendmail_transport_class_init (CamelSendmailTransportClass *class)
{
	CamelServiceClass *service_class;
	CamelTransportClass *transport_class;

	service_class = CAMEL_SERVICE_CLASS (class);
	service_class->get_name = sendmail_get_name;

	transport_class = CAMEL_TRANSPORT_CLASS (class);
	transport_class->send_to_sync = sendmail_send_to_sync;
}

static void
camel_sendmail_transport_init (CamelSendmailTransport *sendmail_transport)
{
}

