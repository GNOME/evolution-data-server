/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <glib/gi18n-lib.h>

#include "camel-sasl-xoauth2-yahoo.h"

static CamelServiceAuthType sasl_xoauth2_yahoo_auth_type = {
	N_("OAuth2 (Yahoo!)"),
	N_("This option will use an OAuth 2.0 access token to connect to the Yahoo! server"),
	"Yahoo",
	FALSE
};

G_DEFINE_TYPE (CamelSaslXOAuth2Yahoo, camel_sasl_xoauth2_yahoo, CAMEL_TYPE_SASL_XOAUTH2)

static void
camel_sasl_xoauth2_yahoo_class_init (CamelSaslXOAuth2YahooClass *klass)
{
	CamelSaslClass *sasl_class;

	sasl_class = CAMEL_SASL_CLASS (klass);
	sasl_class->auth_type = &sasl_xoauth2_yahoo_auth_type;
}

static void
camel_sasl_xoauth2_yahoo_init (CamelSaslXOAuth2Yahoo *sasl)
{
}
