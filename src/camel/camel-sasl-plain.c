/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Jeffrey Stedfast <fejj@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <string.h>

#include <glib/gi18n-lib.h>

#include "camel-network-settings.h"
#include "camel-sasl-plain.h"
#include "camel-service.h"

struct _CamelSaslPlainPrivate {
	gint placeholder;  /* allow for future expansion */
};

static CamelServiceAuthType sasl_plain_auth_type = {
	N_("PLAIN"),

	N_("This option will connect to the server using a "
	   "simple password."),

	"PLAIN",
	TRUE
};

G_DEFINE_TYPE_WITH_PRIVATE (CamelSaslPlain, camel_sasl_plain, CAMEL_TYPE_SASL)

static GByteArray *
sasl_plain_challenge_sync (CamelSasl *sasl,
                           GByteArray *token,
                           GCancellable *cancellable,
                           GError **error)
{
	CamelNetworkSettings *network_settings;
	CamelSettings *settings;
	CamelService *service;
	GByteArray *buf = NULL;
	const gchar *password;
	gchar *user;

	service = camel_sasl_get_service (sasl);

	settings = camel_service_ref_settings (service);
	g_return_val_if_fail (CAMEL_IS_NETWORK_SETTINGS (settings), NULL);

	network_settings = CAMEL_NETWORK_SETTINGS (settings);
	user = camel_network_settings_dup_user (network_settings);

	g_object_unref (settings);

	g_return_val_if_fail (user != NULL, NULL);

	password = camel_service_get_password (service);
	g_return_val_if_fail (password != NULL, NULL);

	/* FIXME: make sure these are "UTF8-SAFE" */
	buf = g_byte_array_new ();
	g_byte_array_append (buf, (guint8 *) "", 1);
	g_byte_array_append (buf, (guint8 *) user, strlen (user));
	g_byte_array_append (buf, (guint8 *) "", 1);
	g_byte_array_append (buf, (guint8 *) password, strlen (password));

	camel_sasl_set_authenticated (sasl, TRUE);

	g_free (user);

	return buf;
}

static void
camel_sasl_plain_class_init (CamelSaslPlainClass *class)
{
	CamelSaslClass *sasl_class;

	sasl_class = CAMEL_SASL_CLASS (class);
	sasl_class->auth_type = &sasl_plain_auth_type;
	sasl_class->challenge_sync = sasl_plain_challenge_sync;
}

static void
camel_sasl_plain_init (CamelSaslPlain *sasl)
{
	sasl->priv = camel_sasl_plain_get_instance_private (sasl);
}
