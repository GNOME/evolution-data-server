/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <locale.h>
#include <string.h>
#include <glib.h>

#include <camel/camel.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "session.h"

static const gchar *imapx_drivers[] = { "imapx" };

/* NTLM type 2 (challenge) message without the "Negotiate NTLM2 Key" flag,
 * thus the response contains the LanManager (LM) response, which does not
 * depend on any random data. */
static const guchar ntlm1_challenge[] = {
	'N', 'T', 'L', 'M', 'S', 'S', 'P', 0x00,
	0x02, 0x00, 0x00, 0x00,				/* message type 2 */
	0x06, 0x00, 0x06, 0x00, 0x30, 0x00, 0x00, 0x00,	/* target name: 6 bytes at offset 48 */
	0x01, 0x82, 0x00, 0x00,				/* flags */
	0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,	/* server nonce */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* context */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* target information */
	'D', 'O', 'M', 'A', 'I', 'N'
};

static GByteArray *
test_ntlm_get_response (CamelService *service)
{
	CamelSasl *sasl;
	GByteArray *token, *response;
	GError *error = NULL;

	sasl = camel_sasl_new ("imap", "NTLM", service);
	g_assert_nonnull (sasl);

	token = g_byte_array_new ();
	g_byte_array_append (token, ntlm1_challenge, sizeof (ntlm1_challenge));

	response = camel_sasl_challenge_sync (sasl, token, NULL, &error);
	g_assert_no_error (error);
	g_assert_nonnull (response);
	g_assert_cmpuint (response->len, >, 64);

	g_byte_array_unref (token);
	g_object_unref (sasl);

	return response;
}

/* The LM hash is computed from the upper-cased password. The upper-casing
 * must not follow the locale, otherwise 'i' is not converted to 'I' in
 * the Turkish and Azerbaijani locales and the response is wrong. */
static void
test_ntlm_lm_response_locale (void)
{
	const gchar *locales[] = { "tr_TR.UTF-8", "az_AZ.UTF-8" };
	CamelSession *session;
	CamelService *service;
	CamelSettings *settings;
	GByteArray *expected;
	gchar *saved_locale;
	gboolean tested = FALSE;
	guint ii;
	GError *error = NULL;

	session = g_object_new (
		CAMEL_TYPE_TEST_SESSION,
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);
	service = camel_session_add_service (session, "ntlm-test", "imapx", CAMEL_PROVIDER_STORE, &error);
	g_assert_no_error (error);
	g_assert_nonnull (service);

	settings = camel_service_ref_settings (service);
	camel_network_settings_set_user (CAMEL_NETWORK_SETTINGS (settings), "user");
	g_object_unref (settings);

	/* The password contains 'i', which is where the locales differ */
	camel_service_set_password (service, "admin1");

	saved_locale = g_strdup (setlocale (LC_ALL, NULL));

	g_assert_nonnull (setlocale (LC_ALL, "C"));
	expected = test_ntlm_get_response (service);

	for (ii = 0; ii < G_N_ELEMENTS (locales); ii++) {
		GByteArray *response;

		if (!setlocale (LC_ALL, locales[ii])) {
			g_test_message ("Locale %s is not available, skipping it", locales[ii]);
			continue;
		}

		response = test_ntlm_get_response (service);
		g_assert_cmpmem (response->data, response->len, expected->data, expected->len);
		g_byte_array_unref (response);

		tested = TRUE;
	}

	setlocale (LC_ALL, saved_locale);
	g_free (saved_locale);

	g_byte_array_unref (expected);
	g_object_unref (service);
	g_object_unref (session);

	if (!tested)
		g_test_skip ("Neither tr_TR.UTF-8 nor az_AZ.UTF-8 locale is available");
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);
	camel_test_provider_init (G_N_ELEMENTS (imapx_drivers), imapx_drivers);

	g_test_add_func ("/Camel/SaslNTLM/LMResponseLocale", test_ntlm_lm_response_locale);

	ret = g_test_run ();

	camel_test_shutdown ();

	return ret;
}
