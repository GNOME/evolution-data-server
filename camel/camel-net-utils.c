/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Authors: Michael Zucchi <notzed@ximian.com>
 * 	    Jeffrey Stedfast <fejj@ximian.com>
 *
 * Copyright (C) 2004 Ximian Inc.
 *
 * This program is free software; you can redistribute it and/or 
 * modify it under the terms of version 2 of the GNU General Public 
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/poll.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>

#include <glib.h>

#include "camel-i18n.h"
#include "camel-operation.h"
#include "camel-exception.h"
#include "camel-net-utils.h"

#include "libedataserver/e-msgport.h"

#define d(x)

/* ********************************************************************** */
struct _addrinfo_msg {
	EMsg msg;
	unsigned int cancelled:1;

	/* for host lookup */
	const char *name;
	const char *service;
	int result;
	const struct addrinfo *hints;
	struct addrinfo **res;

	/* for host lookup emulation */
#ifdef NEED_ADDRINFO
	struct hostent hostbuf;
	int hostbuflen;
	char *hostbufmem;
#endif

	/* for name lookup */
	const struct sockaddr *addr;
	socklen_t addrlen;
	char *host;
	int hostlen;
	char *serv;
	int servlen;
	int flags;
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

/* returns -1 if cancelled */
static int
cs_waitinfo(void *(worker)(void *), struct _addrinfo_msg *msg, const char *error, CamelException *ex)
{
	EMsgPort *reply_port;
	pthread_t id;
	int err, cancel_fd, cancel = 0, fd;

	cancel_fd = camel_operation_cancel_fd(NULL);
	if (cancel_fd == -1) {
		worker(msg);
		return 0;
	}
	
	reply_port = msg->msg.reply_port = e_msgport_new();
	fd = e_msgport_fd(msg->msg.reply_port);
	if ((err = pthread_create(&id, NULL, worker, msg)) == 0) {
		struct pollfd polls[2];
		int status;

		polls[0].fd = fd;
		polls[0].events = POLLIN;
		polls[1].fd = cancel_fd;
		polls[1].events = POLLIN;

		d(printf("waiting for name return/cancellation in main process\n"));
		do {
			polls[0].revents = 0;
			polls[1].revents = 0;
			status = poll(polls, 2, -1);
		} while (status == -1 && errno == EINTR);

		if (status == -1 || (polls[1].revents & POLLIN)) {
			if (status == -1)
				camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, "%s: %s", error, g_strerror(errno));
			else
				camel_exception_setv(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled"));
			
			/* We cancel so if the thread impl is decent it causes immediate exit.
			   We detach so we dont need to wait for it to exit if it isn't.
			   We check the reply port incase we had a reply in the mean time, which we free later */
			d(printf("Cancelling lookup thread and leaving it\n"));
			msg->cancelled = 1;
			pthread_detach(id);
			pthread_cancel(id);
			cancel = 1;
		} else {
			struct _addrinfo_msg *reply = (struct _addrinfo_msg *)e_msgport_get(reply_port);

			g_assert(reply == msg);
			d(printf("waiting for child to exit\n"));
			pthread_join(id, NULL);
			d(printf("child done\n"));
		}
	} else {
		camel_exception_setv(ex, CAMEL_EXCEPTION_SYSTEM, "%s: %s: %s", error, _("cannot create thread"), g_strerror(err));
	}
	e_msgport_destroy(reply_port);

	return cancel;
}

#ifdef NEED_ADDRINFO
static void *
cs_getaddrinfo(void *data)
{
	struct _addrinfo_msg *msg = data;
	int herr;
	struct hostent h;
	struct addrinfo *res, *last = NULL;
	struct sockaddr_in *sin;
	in_port_t port = 0;
	int i;

	/* This is a pretty simplistic emulation of getaddrinfo */

	while ((msg->result = e_gethostbyname_r(msg->name, &h, msg->hostbufmem, msg->hostbuflen, &herr)) == ERANGE) {
		pthread_testcancel();
                msg->hostbuflen *= 2;
                msg->hostbufmem = g_realloc(msg->hostbufmem, msg->hostbuflen);
	}
	
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
		const char *p = msg->service;

		while (*p) {
			if (*p < '0' || *p > '9')
				break;
			p++;
		}

		if (*p) {
			const char *socktype = NULL;
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
	e_msgport_reply((EMsg *)msg);
	return NULL;
cancel:
	cs_freeinfo(msg);
	return NULL;
}
#else
static void *
cs_getaddrinfo(void *data)
{
	struct _addrinfo_msg *info = data;

	info->result = getaddrinfo(info->name, info->service, info->hints, info->res);
	
	if (info->cancelled) {
		g_free(info);
	} else {
		e_msgport_reply((EMsg *)info);
	}
	
	return NULL;
}
#endif /* NEED_ADDRINFO */

struct addrinfo *
camel_getaddrinfo(const char *name, const char *service, const struct addrinfo *hints, CamelException *ex)
{
	struct _addrinfo_msg *msg;
	struct addrinfo *res = NULL;
#ifndef ENABLE_IPv6
	struct addrinfo myhints;
#endif
	g_return_val_if_fail(name != NULL, NULL);
	
	if (camel_operation_cancel_check(NULL)) {
		camel_exception_set(ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled"));
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
		if (msg->result != 0)
			camel_exception_setv (ex, CAMEL_EXCEPTION_SYSTEM, _("Host lookup failed: %s: %s"),
					      name, gai_strerror (msg->result));
		
		cs_freeinfo(msg);
	} else
		res = NULL;
	
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
static void *
cs_getnameinfo(void *data)
{
	struct _addrinfo_msg *msg = data;
	int herr;
	struct hostent h;
	struct sockaddr_in *sin = (struct sockaddr_in *)msg->addr;

	/* FIXME: error code */
	if (msg->addr->sa_family != AF_INET) {
		msg->result = -1;
		return NULL;
	}

	/* FIXME: honour getnameinfo flags: do we care, not really */

	while ((msg->result = e_gethostbyaddr_r((const char *)&sin->sin_addr, sizeof(sin->sin_addr), AF_INET, &h,
						msg->hostbufmem, msg->hostbuflen, &herr)) == ERANGE) {
		pthread_testcancel ();
                msg->hostbuflen *= 2;
                msg->hostbufmem = g_realloc(msg->hostbufmem, msg->hostbuflen);
	}
	
	if (msg->cancelled)
		goto cancel;

	if (msg->host) {
		g_free(msg->host);
		if (msg->result == 0 && h.h_name && h.h_name[0]) {
			msg->host = g_strdup(h.h_name);
		} else {
			unsigned char *in = (unsigned char *)&sin->sin_addr;
			
			/* sin_addr is always network order which is big-endian */
			msg->host = g_strdup_printf("%u.%u.%u.%u", in[0], in[1], in[2], in[3]);
		}
	}

	/* we never actually use this anyway */
	if (msg->serv)
		sprintf(msg->serv, "%d", sin->sin_port);

	e_msgport_reply((EMsg *)msg);
	return NULL;
cancel:
	cs_freeinfo(msg);
	return NULL;
}
#else
static void *
cs_getnameinfo(void *data)
{
	struct _addrinfo_msg *msg = data;

	/* there doens't appear to be a return code which says host or serv buffers are too short, lengthen them */
	msg->result = getnameinfo(msg->addr, msg->addrlen, msg->host, msg->hostlen, msg->serv, msg->servlen, msg->flags);
	
	if (msg->cancelled)
		cs_freeinfo(msg);
	else
		e_msgport_reply((EMsg *)msg);

	return NULL;
}
#endif

int
camel_getnameinfo(const struct sockaddr *sa, socklen_t salen, char **host, char **serv, int flags, CamelException *ex)
{
	struct _addrinfo_msg *msg;
	int result;

	if (camel_operation_cancel_check(NULL)) {
		camel_exception_set (ex, CAMEL_EXCEPTION_USER_CANCEL, _("Cancelled"));
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

	if (host)
		*host = g_strdup(msg->host);
	if (serv)
		*serv = g_strdup(msg->serv);

	g_free(msg->host);
	g_free(msg->serv);
	g_free(msg);

	camel_operation_end(NULL);

	return result;
}
