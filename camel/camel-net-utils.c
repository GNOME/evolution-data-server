/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 *	    Jeffrey Stedfast <fejj@ximian.com>
 *	    Chris Toshok <toshok@ximian.com>
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
#include <pthread.h>
#include <stdio.h>

#include <glib.h>
#include <glib/gi18n-lib.h>

#include "camel-exception.h"
#include "camel-msgport.h"
#include "camel-net-utils.h"
#ifdef G_OS_WIN32
#include "camel-net-utils-win32.h"
#endif
#include "camel-operation.h"

#define d(x)

/* These are GNU extensions */
#ifndef NI_MAXHOST
#define NI_MAXHOST	1025
#endif
#ifndef NI_MAXSERV
#define NI_MAXSERV	32
#endif

#ifdef G_OS_WIN32

typedef short in_port_t;

#undef gai_strerror
#define gai_strerror my_gai_strerror

/* gai_strerror() is implemented as an inline function in Microsoft's
 * SDK, but mingw lacks that. So implement here. The EAI_* errors can
 * be handled with the normal FormatMessage() API,
 * i.e. g_win32_error_message().
 */

static const gchar *
gai_strerror (gint error_code)
{
	gchar *msg = g_win32_error_message (error_code);
	GQuark quark = g_quark_from_string (msg);
	const gchar *retval = g_quark_to_string (quark);

	g_free (msg);

	return retval;
}

#endif

/* gethostbyname emulation code for emulating getaddrinfo code ...

This should probably go away */

#ifdef NEED_ADDRINFO

#if !defined (HAVE_GETHOSTBYNAME_R) || !defined (HAVE_GETHOSTBYADDR_R)
G_LOCK_DEFINE_STATIC (gethost_mutex);
#endif

#define ALIGN(x) (((x) + (sizeof (gchar *) - 1)) & ~(sizeof (gchar *) - 1))

#define GETHOST_PROCESS(h, host, buf, buflen, herr) G_STMT_START {     \
	gint num_aliases = 0, num_addrs = 0;                            \
	gint req_length;                                                \
	gchar *p;                                                       \
	gint i;                                                         \
								       \
	/* check to make sure we have enough room in our buffer */     \
	req_length = 0;                                                \
	if (h->h_aliases) {                                            \
		for (i = 0; h->h_aliases[i]; i++)                      \
			req_length += strlen (h->h_aliases[i]) + 1;    \
		num_aliases = i;                                       \
	}                                                              \
								       \
	if (h->h_addr_list) {                                          \
		for (i = 0; h->h_addr_list[i]; i++)                    \
			req_length += h->h_length;                     \
		num_addrs = i;                                         \
	}                                                              \
								       \
	req_length += sizeof (gchar *) * (num_aliases + 1);             \
	req_length += sizeof (gchar *) * (num_addrs + 1);               \
	req_length += strlen (h->h_name) + 1;                          \
								       \
	if (buflen < req_length) {                                     \
		*herr = ERANGE;                                        \
		G_UNLOCK (gethost_mutex);                              \
		return ERANGE;                                         \
	}                                                              \
								       \
	/* we store the alias/addr pointers in the buffer */           \
        /* their addresses here. */                                    \
	p = buf;                                                       \
	if (num_aliases) {                                             \
		host->h_aliases = (gchar **) p;                         \
		p += sizeof (gchar *) * (num_aliases + 1);              \
	} else                                                         \
		host->h_aliases = NULL;                                \
                                                                       \
	if (num_addrs) {                                               \
		host->h_addr_list = (gchar **) p;                       \
		p += sizeof (gchar *) * (num_addrs + 1);                \
	} else                                                         \
		host->h_addr_list = NULL;                              \
								       \
	/* copy the host name into the buffer */                       \
	host->h_name = p;                                              \
	strcpy (p, h->h_name);                                         \
	p += strlen (h->h_name) + 1;                                   \
	host->h_addrtype = h->h_addrtype;                              \
	host->h_length = h->h_length;                                  \
								       \
	/* copy the aliases/addresses into the buffer */               \
        /* and assign pointers into the hostent */                     \
	*p = 0;                                                        \
	if (num_aliases) {                                             \
		for (i = 0; i < num_aliases; i++) {                    \
			strcpy (p, h->h_aliases[i]);                   \
			host->h_aliases[i] = p;                        \
			p += strlen (h->h_aliases[i]);                 \
		}                                                      \
		host->h_aliases[num_aliases] = NULL;                   \
	}                                                              \
								       \
	if (num_addrs) {                                               \
		for (i = 0; i < num_addrs; i++) {                      \
			memcpy (p, h->h_addr_list[i], h->h_length);    \
			host->h_addr_list[i] = p;                      \
			p += h->h_length;                              \
		}                                                      \
		host->h_addr_list[num_addrs] = NULL;                   \
	}                                                              \
} G_STMT_END

#ifdef ENABLE_IPv6
/* some helpful utils for IPv6 lookups */
#define IPv6_BUFLEN_MIN  (sizeof (gchar *) * 3)

static gint
ai_to_herr (gint error)
{
	switch (error) {
	case EAI_NONAME:
	case EAI_FAIL:
		return HOST_NOT_FOUND;
		break;
	case EAI_SERVICE:
		return NO_DATA;
		break;
	case EAI_ADDRFAMILY:
		return NO_ADDRESS;
		break;
	case EAI_NODATA:
		return NO_DATA;
		break;
	case EAI_MEMORY:
		return ENOMEM;
		break;
	case EAI_AGAIN:
		return TRY_AGAIN;
		break;
	case EAI_SYSTEM:
		return errno;
		break;
	default:
		return NO_RECOVERY;
		break;
	}
}

#endif /* ENABLE_IPv6 */

static gint
camel_gethostbyname_r (const gchar *name, struct hostent *host,
		       gchar *buf, gsize buflen, gint *herr)
{
#ifdef ENABLE_IPv6
	struct addrinfo hints, *res;
	gint retval, len;
	gchar *addr;

	memset (&hints, 0, sizeof (struct addrinfo));
#ifdef HAVE_AI_ADDRCONFIG
	hints.ai_flags = AI_CANONNAME | AI_ADDRCONFIG;
#else
	hints.ai_flags = AI_CANONNAME;
#endif
	hints.ai_family = PF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	if ((retval = getaddrinfo (name, NULL, &hints, &res)) != 0) {
		*herr = ai_to_herr (retval);
		return -1;
	}

	len = ALIGN (strlen (res->ai_canonname) + 1);
	if (buflen < IPv6_BUFLEN_MIN + len + res->ai_addrlen + sizeof (gchar *))
		return ERANGE;

	/* h_name */
	strcpy (buf, res->ai_canonname);
	host->h_name = buf;
	buf += len;

	/* h_aliases */
	((gchar **) buf)[0] = NULL;
	host->h_aliases = (gchar **) buf;
	buf += sizeof (gchar *);

	/* h_addrtype and h_length */
	host->h_length = res->ai_addrlen;
	if (res->ai_family == PF_INET6) {
		host->h_addrtype = AF_INET6;

		addr = (gchar *) &((struct sockaddr_in6 *) res->ai_addr)->sin6_addr;
	} else {
		host->h_addrtype = AF_INET;

		addr = (gchar *) &((struct sockaddr_in *) res->ai_addr)->sin_addr;
	}

	memcpy (buf, addr, host->h_length);
	addr = buf;
	buf += ALIGN (host->h_length);

	/* h_addr_list */
	((gchar **) buf)[0] = addr;
	((gchar **) buf)[1] = NULL;
	host->h_addr_list = (gchar **) buf;

	freeaddrinfo (res);

	return 0;
#else /* No support for IPv6 addresses */
#ifdef HAVE_GETHOSTBYNAME_R
#ifdef GETHOSTBYNAME_R_FIVE_ARGS
	if (gethostbyname_r (name, host, buf, buflen, herr))
		return 0;
	else
		return errno;
#else
	struct hostent *hp;
	gint retval;

	retval = gethostbyname_r (name, host, buf, buflen, &hp, herr);
	if (hp != NULL) {
		*herr = 0;
	} else if (retval == 0) {
		/* glibc 2.3.2 workaround - it seems that
		 * gethostbyname_r will sometimes return 0 on fail and
		 * not set the hostent values (hence the crash in bug
		 * #56337).  Hopefully we can depend on @hp being NULL
		 * in this error case like we do with
		 * gethostbyaddr_r().
		 */
		retval = -1;
	}

	return retval;
#endif
#else /* No support for gethostbyname_r */
	struct hostent *h;

	G_LOCK (gethost_mutex);

	h = gethostbyname (name);

	if (!h) {
		*herr = h_errno;
		G_UNLOCK (gethost_mutex);
		return -1;
	}

	GETHOST_PROCESS (h, host, buf, buflen, herr);

	G_UNLOCK (gethost_mutex);

	return 0;
#endif /* HAVE_GETHOSTBYNAME_R */
#endif /* ENABLE_IPv6 */
}

static gint
camel_gethostbyaddr_r (const gchar *addr, gint addrlen, gint type, struct hostent *host,
		       gchar *buf, gsize buflen, gint *herr)
{
#ifdef ENABLE_IPv6
	gint retval, len;

	if ((retval = getnameinfo (addr, addrlen, buf, buflen, NULL, 0, NI_NAMEREQD)) != 0) {
		*herr = ai_to_herr (retval);
		return -1;
	}

	len = ALIGN (strlen (buf) + 1);
	if (buflen < IPv6_BUFLEN_MIN + len + addrlen + sizeof (gchar *))
		return ERANGE;

	/* h_name */
	host->h_name = buf;
	buf += len;

	/* h_aliases */
	((gchar **) buf)[0] = NULL;
	host->h_aliases = (gchar **) buf;
	buf += sizeof (gchar *);

	/* h_addrtype and h_length */
	host->h_length = addrlen;
	host->h_addrtype = type;

	memcpy (buf, addr, host->h_length);
	addr = buf;
	buf += ALIGN (host->h_length);

	/* h_addr_list */
	((gchar **) buf)[0] = addr;
	((gchar **) buf)[1] = NULL;
	host->h_addr_list = (gchar **) buf;

	return 0;
#else /* No support for IPv6 addresses */
#ifdef HAVE_GETHOSTBYADDR_R
#ifdef GETHOSTBYADDR_R_SEVEN_ARGS
	if (gethostbyaddr_r (addr, addrlen, type, host, buf, buflen, herr))
		return 0;
	else
		return errno;
#else
	struct hostent *hp;
	gint retval;

	retval = gethostbyaddr_r (addr, addrlen, type, host, buf, buflen, &hp, herr);
	if (hp != NULL) {
		*herr = 0;
		retval = 0;
	} else if (retval == 0) {
		/* glibc 2.3.2 workaround - it seems that
		 * gethostbyaddr_r will sometimes return 0 on fail and
		 * fill @host with garbage strings from /etc/hosts
		 * (failure to parse the file? who knows). Luckily, it
		 * seems that we can rely on @hp being NULL on
		 * fail.
		 */
		retval = -1;
	}

	return retval;
#endif
#else /* No support for gethostbyaddr_r */
	struct hostent *h;

	G_LOCK (gethost_mutex);

	h = gethostbyaddr (addr, addrlen, type);

	if (!h) {
		*herr = h_errno;
		G_UNLOCK (gethost_mutex);
		return -1;
	}

	GETHOST_PROCESS (h, host, buf, buflen, herr);

	G_UNLOCK (gethost_mutex);

	return 0;
#endif /* HAVE_GETHOSTBYADDR_R */
#endif /* ENABLE_IPv6 */
}
#endif /* NEED_ADDRINFO */

/* ********************************************************************** */
struct _addrinfo_msg {
	CamelMsg msg;
	guint cancelled:1;

	/* for host lookup */
	const gchar *name;
	const gchar *service;
	gint result;
	const struct addrinfo *hints;
	struct addrinfo **res;

	/* for host lookup emulation */
#ifdef NEED_ADDRINFO
	struct hostent hostbuf;
	gint hostbuflen;
	gchar *hostbufmem;
#endif

	/* for name lookup */
	const struct sockaddr *addr;
	socklen_t addrlen;
	gchar *host;
	gint hostlen;
	gchar *serv;
	gint servlen;
	gint flags;
};

static void
cs_freeinfo(struct _addrinfo_msg *msg)
{
	g_free(msg->host);
	g_free(msg->serv);
#ifdef NEED_ADDRINFO
	g_free(msg->hostbufmem);
#endif
	g_free(msg);
}

/* returns -1 if we didn't wait for reply from thread */
static gint
cs_waitinfo(gpointer (worker)(gpointer), struct _addrinfo_msg *msg, const gchar *error, CamelException *ex)
{
	CamelMsgPort *reply_port;
	pthread_t id;
	gint err, cancel_fd, cancel = 0, fd;

	cancel_fd = camel_operation_cancel_fd(NULL);
	if (cancel_fd == -1) {
		worker(msg);
		return 0;
	}

	reply_port = msg->msg.reply_port = camel_msgport_new();
	fd = camel_msgport_fd(msg->msg.reply_port);
	if ((err = pthread_create(&id, NULL, worker, msg)) == 0) {
		gint status;
#ifndef G_OS_WIN32
		GPollFD polls[2];

		polls[0].fd = fd;
		polls[0].events = G_IO_IN;
		polls[1].fd = cancel_fd;
		polls[1].events = G_IO_IN;

		d(printf("waiting for name return/cancellation in main process\n"));
		do {
			polls[0].revents = 0;
			polls[1].revents = 0;
			status = g_poll(polls, 2, -1);
		} while (status == -1 && errno == EINTR);
#else
		fd_set read_set;

		FD_ZERO(&read_set);
		FD_SET(fd, &read_set);
		FD_SET(cancel_fd, &read_set);

		status = select(MAX(fd, cancel_fd) + 1, &read_set, NULL, NULL, NULL);
#endif

		if (status == -1 ||
#ifndef G_OS_WIN32
		    (polls[1].revents & G_IO_IN)
#else
		    FD_ISSET (cancel_fd, &read_set)
#endif
						   ) {
			if (status == -1)
				camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, "%s: %s", error,
#ifndef G_OS_WIN32
						     g_strerror(errno)
#else
						     g_win32_error_message (WSAGetLastError ())
#endif
						     );
			else
				camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Canceled"));

			/* We cancel so if the thread impl is decent it causes immediate exit.
			   We check the reply port incase we had a reply in the mean time, which we free later */
			d(printf("Canceling lookup thread and leaving it\n"));
			msg->cancelled = 1;
			pthread_cancel(id);
			pthread_join (id, NULL);
			cancel = 1;
		} else {
			struct _addrinfo_msg *reply = (struct _addrinfo_msg *)camel_msgport_try_pop(reply_port);

			g_assert(reply == msg);
			d(printf("waiting for child to exit\n"));
			pthread_join(id, NULL);
			d(printf("child done\n"));
		}
	} else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, "%s: %s: %s", error, _("cannot create thread"), g_strerror(err));
	}
	camel_msgport_destroy(reply_port);

	return cancel;
}

#ifdef NEED_ADDRINFO
static gpointer
cs_getaddrinfo(gpointer data)
{
	struct _addrinfo_msg *msg = data;
	gint herr;
	struct hostent h;
	struct addrinfo *res, *last = NULL;
	struct sockaddr_in *sin;
	in_port_t port = 0;
	gint i;

	/* This is a pretty simplistic emulation of getaddrinfo */

	while ((msg->result = camel_gethostbyname_r(msg->name, &h, msg->hostbufmem, msg->hostbuflen, &herr)) == ERANGE) {
		pthread_testcancel();
                msg->hostbuflen *= 2;
                msg->hostbufmem = g_realloc(msg->hostbufmem, msg->hostbuflen);
	}

	pthread_testcancel ();

	/* If we got cancelled, dont reply, just free it */
	if (msg->cancelled)
		goto cancel;

	/* FIXME: map error numbers across */
	if (msg->result != 0)
		goto reply;

	/* check hints matched */
	if (msg->hints && msg->hints->ai_family && msg->hints->ai_family != h.h_addrtype) {
		msg->result = EAI_FAMILY;
		goto reply;
	}

	/* we only support ipv4 for this interface, even if it could supply ipv6 */
	if (h.h_addrtype != AF_INET) {
		msg->result = EAI_FAMILY;
		goto reply;
	}

	/* check service mapping */
	if (msg->service) {
		const gchar *p = msg->service;

		while (*p) {
			if (*p < '0' || *p > '9')
				break;
			p++;
		}

		if (*p) {
			const gchar *socktype = NULL;
			struct servent *serv;

			if (msg->hints && msg->hints->ai_socktype) {
				if (msg->hints->ai_socktype == SOCK_STREAM)
					socktype = "tcp";
				else if (msg->hints->ai_socktype == SOCK_DGRAM)
					socktype = "udp";
			}

			serv = getservbyname(msg->service, socktype);
			if (serv == NULL) {
				msg->result = EAI_NONAME;
				goto reply;
			}
			port = serv->s_port;
		} else {
			port = htons(strtoul(msg->service, NULL, 10));
		}
	}

	pthread_testcancel ();

	for (i=0;h.h_addr_list[i];i++) {
		res = g_malloc0(sizeof(*res));
		if (msg->hints) {
			res->ai_flags = msg->hints->ai_flags;
			if (msg->hints->ai_flags & AI_CANONNAME)
				res->ai_canonname = g_strdup(h.h_name);
			res->ai_socktype = msg->hints->ai_socktype;
			res->ai_protocol = msg->hints->ai_protocol;
		} else {
			res->ai_flags = 0;
			res->ai_socktype = SOCK_STREAM;	/* fudge */
			res->ai_protocol = 0;	/* fudge */
		}
		res->ai_family = AF_INET;
		res->ai_addrlen = sizeof(*sin);
		res->ai_addr = g_malloc(sizeof(*sin));
		sin = (struct sockaddr_in *)res->ai_addr;
		sin->sin_family = AF_INET;
		sin->sin_port = port;
		memcpy(&sin->sin_addr, h.h_addr_list[i], sizeof(sin->sin_addr));

		if (last == NULL) {
			*msg->res = last = res;
		} else {
			last->ai_next = res;
			last = res;
		}
	}
reply:
	camel_msgport_reply((CamelMsg *)msg);
cancel:
	return NULL;
}
#else
static gpointer
cs_getaddrinfo(gpointer data)
{
	struct _addrinfo_msg *info = data;

	info->result = getaddrinfo(info->name, info->service, info->hints, info->res);

	/* On Solaris, the service name 'http' or 'https' is not defined.
	   Use the port as the service name directly. */
	if (info->result && info->service) {
		if (strcmp (info->service, "http") == 0)
			info->result = getaddrinfo(info->name, "80", info->hints, info->res);
		else if (strcmp (info->service, "https") == 0)
			info->result = getaddrinfo(info->name, "443", info->hints, info->res);
	}

	pthread_testcancel ();

	if (!info->cancelled)
		camel_msgport_reply((CamelMsg *)info);

	return NULL;
}
#endif /* NEED_ADDRINFO */

struct addrinfo *
camel_getaddrinfo(const gchar *name, const gchar *service, const struct addrinfo *hints, CamelException *ex)
{
	struct _addrinfo_msg *msg;
	struct addrinfo *res = NULL;
#ifndef ENABLE_IPv6
	struct addrinfo myhints;
#endif
	g_return_val_if_fail(name != NULL, NULL);

	if (camel_operation_cancel_check(NULL)) {
		camel_exception_set(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Canceled"));
		return NULL;
	}

	camel_operation_start_transient(NULL, _("Resolving: %s"), name);

	/* force ipv4 addresses only */
#ifndef ENABLE_IPv6
	if (hints == NULL)
		memset(&myhints, 0, sizeof(myhints));
	else
		memcpy (&myhints, hints, sizeof (myhints));

	myhints.ai_family = AF_INET;
	hints = &myhints;
#endif

	msg = g_malloc0(sizeof(*msg));
	msg->name = name;
	msg->service = service;
	msg->hints = hints;
	msg->res = &res;
#ifdef NEED_ADDRINFO
	msg->hostbuflen = 1024;
	msg->hostbufmem = g_malloc(msg->hostbuflen);
#endif
	if (cs_waitinfo(cs_getaddrinfo, msg, _("Host lookup failed"), ex) == 0) {
		if (msg->result != 0) {
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Host lookup failed: %s: %s"),
					      name, gai_strerror (msg->result));
		}
	} else
		res = NULL;

	cs_freeinfo (msg);
	camel_operation_end(NULL);

	return res;
}

void
camel_freeaddrinfo(struct addrinfo *host)
{
#ifdef NEED_ADDRINFO
	while (host) {
		struct addrinfo *next = host->ai_next;

		g_free(host->ai_canonname);
		g_free(host->ai_addr);
		g_free(host);
		host = next;
	}
#else
	freeaddrinfo(host);
#endif
}

#ifdef NEED_ADDRINFO
static gpointer
cs_getnameinfo(gpointer data)
{
	struct _addrinfo_msg *msg = data;
	gint herr;
	struct hostent h;
	struct sockaddr_in *sin = (struct sockaddr_in *)msg->addr;

	/* FIXME: error code */
	if (msg->addr->sa_family != AF_INET) {
		msg->result = -1;
		return NULL;
	}

	/* FIXME: honour getnameinfo flags: do we care, not really */

	while ((msg->result = camel_gethostbyaddr_r((const gchar *)&sin->sin_addr, sizeof(sin->sin_addr), AF_INET, &h,
						    msg->hostbufmem, msg->hostbuflen, &herr)) == ERANGE) {
		pthread_testcancel ();
                msg->hostbuflen *= 2;
                msg->hostbufmem = g_realloc(msg->hostbufmem, msg->hostbuflen);
	}

	if (msg->cancelled)
		goto cancel;

	pthread_testcancel ();

	if (msg->host) {
		g_free(msg->host);
		if (msg->result == 0 && h.h_name && h.h_name[0]) {
			msg->host = g_strdup(h.h_name);
		} else {
			guchar *in = (guchar *)&sin->sin_addr;

			/* sin_addr is always network order which is big-endian */
			msg->host = g_strdup_printf("%u.%u.%u.%u", in[0], in[1], in[2], in[3]);
		}
	}

	/* we never actually use this anyway */
	if (msg->serv)
		sprintf(msg->serv, "%d", sin->sin_port);

	camel_msgport_reply((CamelMsg *)msg);
cancel:
	return NULL;
}
#else
static gpointer
cs_getnameinfo(gpointer data)
{
	struct _addrinfo_msg *msg = data;

	/* there doens't appear to be a return code which says host or serv buffers are too short, lengthen them */
	msg->result = getnameinfo(msg->addr, msg->addrlen, msg->host, msg->hostlen, msg->serv, msg->servlen, msg->flags);

	pthread_testcancel ();

	if (!msg->cancelled)
		camel_msgport_reply((CamelMsg *)msg);

	return NULL;
}
#endif

gint
camel_getnameinfo(const struct sockaddr *sa, socklen_t salen, gchar **host, gchar **serv, gint flags, CamelException *ex)
{
	struct _addrinfo_msg *msg;
	gint result;

	if (camel_operation_cancel_check(NULL)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL, _("Canceled"));
		return -1;
	}

	camel_operation_start_transient(NULL, _("Resolving address"));

	msg = g_malloc0(sizeof(*msg));
	msg->addr = sa;
	msg->addrlen = salen;
	if (host) {
		msg->hostlen = NI_MAXHOST;
		msg->host = g_malloc(msg->hostlen);
		msg->host[0] = 0;
	}
	if (serv) {
		msg->servlen = NI_MAXSERV;
		msg->serv = g_malloc(msg->servlen);
		msg->serv[0] = 0;
	}
	msg->flags = flags;
#ifdef NEED_ADDRINFO
	msg->hostbuflen = 1024;
	msg->hostbufmem = g_malloc(msg->hostbuflen);
#endif
	cs_waitinfo(cs_getnameinfo, msg, _("Name lookup failed"), ex);

	if ((result = msg->result) != 0)
		camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Name lookup failed: %s"),
				      gai_strerror (result));
	else {
		if (host)
			*host = g_strdup(msg->host);
		if (serv)
			*serv = g_strdup(msg->serv);
	}

	cs_freeinfo (msg);
	camel_operation_end(NULL);

	return result;
}
