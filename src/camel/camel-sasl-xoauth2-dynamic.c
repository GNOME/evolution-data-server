/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: camel-sasl-xoauth2-dynamic
 * @include: camel/camel.h
 * @short_description: XOAUTH2 for dynamically registered OAuth2 clients
 *
 * The mechanism of #EOAuth2ServiceDynamic, named "OAuth2Dynamic".
 *
 * Since: 3.64
 **/

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "camel-sasl-xoauth2-dynamic.h"

static CamelServiceAuthType sasl_xoauth2_dynamic_auth_type = {
	N_("OAuth2 (dynamic client)"),
	N_("This option will use an OAuth 2.0 access token, of a dynamically registered client, to connect to the server"),
	"OAuth2Dynamic",
	FALSE
};

struct _CamelSaslXOAuth2Dynamic {
	CamelSaslXOAuth2 parent;
};

G_DEFINE_TYPE (CamelSaslXOAuth2Dynamic, camel_sasl_xoauth2_dynamic, CAMEL_TYPE_SASL_XOAUTH2)

static void
camel_sasl_xoauth2_dynamic_class_init (CamelSaslXOAuth2DynamicClass *klass)
{
	CamelSaslClass *sasl_class;

	sasl_class = CAMEL_SASL_CLASS (klass);
	sasl_class->auth_type = &sasl_xoauth2_dynamic_auth_type;
}

static void
camel_sasl_xoauth2_dynamic_init (CamelSaslXOAuth2Dynamic *sasl)
{
}
