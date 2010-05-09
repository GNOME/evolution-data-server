/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*- */
/* camel-stream-process.c : stream over piped process */

/*
 *  Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 *  Authors: David Woodhouse <dwmw2@infradead.org>,
 *	     Jeffrey Stedfast <fejj@ximian.com>
 *
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "camel-file-utils.h"
#include "camel-stream-process.h"

extern gint camel_verbose_debug;

G_DEFINE_TYPE (CamelStreamProcess, camel_stream_process, CAMEL_TYPE_STREAM)

static void
stream_process_finalize (GObject *object)
{
	/* Ensure we clean up after ourselves -- kill
	   the child process and reap it. */
	camel_stream_close (CAMEL_STREAM (object), NULL);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (camel_stream_process_parent_class)->finalize (object);
}

static gssize
stream_process_read (CamelStream *stream,
                     gchar *buffer,
                     gsize n,
                     GError **error)
{
	CamelStreamProcess *stream_process = CAMEL_STREAM_PROCESS (stream);

	return camel_read (stream_process->sockfd, buffer, n, error);
}

static gssize
stream_process_write (CamelStream *stream,
                      const gchar *buffer,
                      gsize n,
                      GError **error)
{
	CamelStreamProcess *stream_process = CAMEL_STREAM_PROCESS (stream);

	return camel_write (stream_process->sockfd, buffer, n, error);
}

static gint
stream_process_close (CamelStream *object,
                      GError **error)
{
	CamelStreamProcess *stream = CAMEL_STREAM_PROCESS (object);

	if (camel_verbose_debug)
		fprintf (stderr, "Process stream close. sockfd %d, childpid %d\n",
			 stream->sockfd, stream->childpid);

	if (stream->sockfd != -1) {
		close (stream->sockfd);
		stream->sockfd = -1;
	}

	if (stream->childpid) {
		gint ret, i;
		for (i = 0; i < 4; i++) {
			ret = waitpid (stream->childpid, NULL, WNOHANG);
			if (camel_verbose_debug)
				fprintf (stderr, "waitpid() for pid %d returned %d (errno %d)\n",
					 stream->childpid, ret, ret == -1 ? errno : 0);
			if (ret == stream->childpid || errno == ECHILD)
				break;
			switch (i) {
			case 0:
				if (camel_verbose_debug)
					fprintf (stderr, "Sending SIGTERM to pid %d\n",
						 stream->childpid);
				kill (stream->childpid, SIGTERM);
				break;
			case 2:
				if (camel_verbose_debug)
					fprintf (stderr, "Sending SIGKILL to pid %d\n",
						 stream->childpid);
				kill (stream->childpid, SIGKILL);
				break;
			case 1:
			case 3:
				sleep (1);
				break;
			}
		}

		stream->childpid = 0;
	}

	return 0;
}

static gint
stream_process_flush (CamelStream *stream,
                      GError **error)
{
	return 0;
}

static void
camel_stream_process_class_init (CamelStreamProcessClass *class)
{
	GObjectClass *object_class;
	CamelStreamClass *stream_class;

	object_class = G_OBJECT_CLASS (class);
	object_class->finalize = stream_process_finalize;

	stream_class = CAMEL_STREAM_CLASS (class);
	stream_class->read = stream_process_read;
	stream_class->write = stream_process_write;
	stream_class->close = stream_process_close;
	stream_class->flush = stream_process_flush;
}

static void
camel_stream_process_init (CamelStreamProcess *stream)
{
	stream->sockfd = -1;
	stream->childpid = 0;
}

/**
 * camel_stream_process_new:
 *
 * Returns a PROCESS stream.
 *
 * Returns: the stream
 **/
CamelStream *
camel_stream_process_new (void)
{
	return g_object_new (CAMEL_TYPE_STREAM_PROCESS, NULL);
}

G_GNUC_NORETURN static void
do_exec_command (gint fd, const gchar *command, gchar **env)
{
	gint i, maxopen;

	/* Not a lot we can do if there's an error other than bail. */
	if (dup2 (fd, 0) == -1)
		exit (1);
	if (dup2 (fd, 1) == -1)
		exit (1);

	/* What to do with stderr? Possibly put it through a separate pipe
	   and bring up a dialog box with its output if anything does get
	   spewed to it? It'd help the user understand what was going wrong
	   with their command, but it's hard to do cleanly. For now we just
	   leave it as it is. Perhaps we should close it and reopen /dev/null? */

	maxopen = sysconf (_SC_OPEN_MAX);
	for (i = 3; i < maxopen; i++)
		fcntl (i, F_SETFD, FD_CLOEXEC);

	setsid ();
#ifdef TIOCNOTTY
	/* Detach from the controlling tty if we have one. Otherwise,
	   SSH might do something stupid like trying to use it instead
	   of running $SSH_ASKPASS. Doh. */
	if ((fd = open ("/dev/tty", O_RDONLY)) != -1) {
		ioctl (fd, TIOCNOTTY, NULL);
		close (fd);
	}
#endif /* TIOCNOTTY */

	/* Set up child's environment. We _add_ to it, don't use execle,
	   because otherwise we'd destroy stuff like SSH_AUTH_SOCK etc. */
	for (; env && *env; env++)
		putenv(*env);

	execl ("/bin/sh", "/bin/sh", "-c", command, NULL);

	if (camel_verbose_debug)
		fprintf (stderr, "exec failed %d\n", errno);

	exit (1);
}

gint
camel_stream_process_connect (CamelStreamProcess *stream,
                              const gchar *command,
                              const gchar **env)
{
	gint sockfds[2];

	g_return_val_if_fail (CAMEL_IS_STREAM_PROCESS (stream), -1);
	g_return_val_if_fail (command != NULL, -1);

	if (stream->sockfd != -1 || stream->childpid)
		camel_stream_close (CAMEL_STREAM (stream), NULL);

	if (socketpair (AF_UNIX, SOCK_STREAM, 0, sockfds))
		return -1;

	stream->childpid = fork ();
	if (!stream->childpid) {
		do_exec_command (sockfds[1], command, (gchar **)env);
	} else if (stream->childpid == -1) {
		close (sockfds[0]);
		close (sockfds[1]);
		stream->sockfd = -1;
		return -1;
	}

	close (sockfds[1]);
	stream->sockfd = sockfds[0];

	return 0;
}
