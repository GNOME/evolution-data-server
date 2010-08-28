/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Authors: Michael Zucchi <notzed@ximian.com>
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

#include <string.h>
#include <time.h>

#include <glib/gi18n-lib.h>

#include "camel-sasl-popb4smtp.h"
#include "camel-service.h"
#include "camel-session.h"
#include "camel-store.h"

#define CAMEL_SASL_POPB4SMTP_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), CAMEL_TYPE_SASL_POPB4SMTP, CamelSaslPOPB4SMTPPrivate))

struct _CamelSaslPOPB4SMTPPrivate {
	gint placeholder;  /* allow for future expansion */
};

CamelServiceAuthType camel_sasl_popb4smtp_authtype = {
	N_("POP before SMTP"),

	N_("This option will authorise a POP connection before attempting SMTP"),

	"POPB4SMTP",
	FALSE,
};

/* last time the pop was accessed (through the auth method anyway), *time_t */
static GHashTable *poplast;

/* use 1 hour as our pop timeout */
#define POPB4SMTP_TIMEOUT (60*60)

static GStaticMutex lock = G_STATIC_MUTEX_INIT;
#define POPB4SMTP_LOCK(l) g_static_mutex_lock(&l)
#define POPB4SMTP_UNLOCK(l) g_static_mutex_unlock(&l)

G_DEFINE_TYPE (CamelSaslPOPB4SMTP, camel_sasl_popb4smtp, CAMEL_TYPE_SASL)

static GByteArray *
sasl_popb4smtp_challenge (CamelSasl *sasl,
                          GByteArray *token,
                          GError **error)
{
	gchar *popuri;
	CamelService *service;
	CamelSession *session;
	CamelStore *store;
	time_t now, *timep;

	service = camel_sasl_get_service (sasl);
	session = service->session;
	camel_sasl_set_authenticated (sasl, FALSE);

	popuri = camel_session_get_password (
		session, service, NULL, _("POP Source URI"),
		"popb4smtp_uri", 0, error);

	if (popuri == NULL) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("POP Before SMTP authentication using an unknown transport"));
		return NULL;
	}

	if (g_ascii_strncasecmp(popuri, "pop:", 4) != 0) {
		g_set_error (
			error, CAMEL_SERVICE_ERROR,
			CAMEL_SERVICE_ERROR_CANT_AUTHENTICATE,
			_("POP Before SMTP authentication using a non-POP source"));
		return NULL;
	}

	/* check if we've done it before recently in this session */
	now = time(NULL);

	/* need to lock around the whole thing until finished with timep */

	POPB4SMTP_LOCK(lock);
	timep = g_hash_table_lookup(poplast, popuri);
	if (timep) {
		if ((*timep + POPB4SMTP_TIMEOUT) > now) {
			camel_sasl_set_authenticated (sasl, TRUE);
			POPB4SMTP_UNLOCK(lock);
			g_free(popuri);
			return NULL;
		}
	} else {
		timep = g_malloc0(sizeof(*timep));
		g_hash_table_insert(poplast, g_strdup(popuri), timep);
	}

	/* connect to pop session */
	store = camel_session_get_store(session, popuri, error);
	if (store) {
		camel_sasl_set_authenticated (sasl, TRUE);
		g_object_unref (store);
		*timep = now;
	} else {
		camel_sasl_set_authenticated (sasl, FALSE);
		*timep = 0;
	}

	POPB4SMTP_UNLOCK(lock);

	g_free(popuri);

	return NULL;
}

static void
camel_sasl_popb4smtp_class_init (CamelSaslPOPB4SMTPClass *class)
{
	CamelSaslClass *sasl_class;

	g_type_class_add_private (class, sizeof (CamelSaslPOPB4SMTPPrivate));

	sasl_class = CAMEL_SASL_CLASS (class);
	sasl_class->challenge = sasl_popb4smtp_challenge;

	poplast = g_hash_table_new (g_str_hash, g_str_equal);
}

static void
camel_sasl_popb4smtp_init (CamelSaslPOPB4SMTP *sasl)
{
	sasl->priv = CAMEL_SASL_POPB4SMTP_GET_PRIVATE (sasl);
}
