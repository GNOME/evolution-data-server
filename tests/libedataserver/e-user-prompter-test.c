/*
 * e-user-prompter-test.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <libebackend/libebackend.h>

typedef struct _TestClosure TestClosure;
typedef struct _TestFixture TestFixture;

struct _TestClosure {
	gboolean only_certificate;
};

struct _TestFixture {
	gboolean only_certificate;
	EUserPrompter *prompter;
	GMainLoop *main_loop;
};

static void
test_fixture_setup_session (TestFixture *fixture,
                            gconstpointer user_data)
{
	g_assert (fixture->prompter == NULL);
	g_assert (fixture->main_loop == NULL);

	fixture->prompter = e_user_prompter_new ();
	g_assert (fixture->prompter != NULL);

	fixture->main_loop = g_main_loop_new (NULL, FALSE);
}

static void
test_fixture_teardown_session (TestFixture *fixture,
                               gconstpointer user_data)
{
	g_object_unref (fixture->prompter);
	fixture->prompter = NULL;

	g_main_loop_unref (fixture->main_loop);
	fixture->main_loop = NULL;
}

static void
test_trust_prompt (EUserPrompter *prompter)
{
	const gchar *der_certificate =
		"MIIIKzCCBxOgAwIBAgIDAMP1MA0GCSqGSIb3DQEBBQUAMIGMMQswCQYDVQQGEwJJTDEWMBQGA1UEChM"
		"NU3RhcnRDb20gTHRkLjErMCkGA1UECxMiU2VjdXJlIERpZ2l0YWwgQ2VydGlmaWNhdGUgU2lnbmluZz"
		"E4MDYGA1UEAxMvU3RhcnRDb20gQ2xhc3MgMiBQcmltYXJ5IEludGVybWVkaWF0ZSBTZXJ2ZXIgQ0EwH"
		"hcNMTIwNDEwMDIyNzU2WhcNMTQwNDExMTQzNjUyWjCBqTEZMBcGA1UEDRMQbGZVMVd1OUVydm9EOW0z"
		"MDELMAkGA1UEBhMCVVMxFjAUBgNVBAgTDU1hc3NhY2h1c2V0dHMxDzANBgNVBAcTBkJvc3RvbjEZMBc"
		"GA1UEChMQR05PTUUgRm91bmRhdGlvbjEWMBQGA1UEAxMNd3d3Lmdub21lLm9yZzEjMCEGCSqGSIb3DQ"
		"EJARYUaG9zdG1hc3RlckBnbm9tZS5vcmcwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDIG"
		"1jjf88ZZw/oUIpXHj75jV8Fn5QrnKkWZzFzs9oIHwIhjJC/A0W1zweUzxZQZuj/r8Pn7RLj86NU8qfX"
		"UijnXmqpNd71iapUbnmpAbkD7FGDT0Z8ekzzMQmEI1qhZt3emJUgMCqU/OFAO8ooID0oYLw6VeXFZNK"
		"hOxNifWGSFor77r8GbeDTkIoLdm3YlaIwuiKCzA2ti/SCveFyu/oSrXw0XOJGHc1XSfpHrrQ0xHPKeH"
		"hzpsOlTBZHZHR0fGpHpyuqhKfCA8Mp0HIJ05IeJwC4Cg41ZnPA2y1sIeos0CbNvrUHPNd+WPdZR/qdR"
		"phh1OQgeB1bv/AnZ8pUzu9LAgMBAAGjggR1MIIEcTAJBgNVHRMEAjAAMAsGA1UdDwQEAwIDqDAdBgNV"
		"HSUEFjAUBggrBgEFBQcDAgYIKwYBBQUHAwEwHQYDVR0OBBYEFDPkRLQJulDChRFa7dLD7DARlxdVMB8"
		"GA1UdIwQYMBaAFBHbI0X9VMxqcW+EigPXvvcBLyaGMIHlBgNVHREEgd0wgdqCDXd3dy5nbm9tZS5vcm"
		"eCCWdub21lLm9yZ4IObGl2ZS5nbm9tZS5vcmeCDHJ0Lmdub21lLm9yZ4ISYnVnemlsbGEuZ25vbWUub"
		"3Jngg52b3RlLmdub21lLm9yZ4IObWFpbC5nbm9tZS5vcmeCDmxkYXAuZ25vbWUub3JnghFtZW51YmFy"
		"Lmdub21lLm9yZ4IRd2ViYXBwcy5nbm9tZS5vcmeCD2xhYmVsLmdub21lLm9yZ4IPbWFuZ28uZ25vbWU"
		"ub3JnghRleHRlbnNpb25zLmdub21lLm9yZzCCAiEGA1UdIASCAhgwggIUMIICEAYLKwYBBAGBtTcBAg"
		"IwggH/MC4GCCsGAQUFBwIBFiJodHRwOi8vd3d3LnN0YXJ0c3NsLmNvbS9wb2xpY3kucGRmMDQGCCsGA"
		"QUFBwIBFihodHRwOi8vd3d3LnN0YXJ0c3NsLmNvbS9pbnRlcm1lZGlhdGUucGRmMIH3BggrBgEFBQcC"
		"AjCB6jAnFiBTdGFydENvbSBDZXJ0aWZpY2F0aW9uIEF1dGhvcml0eTADAgEBGoG+VGhpcyBjZXJ0aWZ"
		"pY2F0ZSB3YXMgaXNzdWVkIGFjY29yZGluZyB0byB0aGUgQ2xhc3MgMiBWYWxpZGF0aW9uIHJlcXVpcm"
		"VtZW50cyBvZiB0aGUgU3RhcnRDb20gQ0EgcG9saWN5LCByZWxpYW5jZSBvbmx5IGZvciB0aGUgaW50Z"
		"W5kZWQgcHVycG9zZSBpbiBjb21wbGlhbmNlIG9mIHRoZSByZWx5aW5nIHBhcnR5IG9ibGlnYXRpb25z"
		"LjCBnAYIKwYBBQUHAgIwgY8wJxYgU3RhcnRDb20gQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwAwIBAhp"
		"kTGlhYmlsaXR5IGFuZCB3YXJyYW50aWVzIGFyZSBsaW1pdGVkISBTZWUgc2VjdGlvbiAiTGVnYWwgYW"
		"5kIExpbWl0YXRpb25zIiBvZiB0aGUgU3RhcnRDb20gQ0EgcG9saWN5LjA1BgNVHR8ELjAsMCqgKKAmh"
		"iRodHRwOi8vY3JsLnN0YXJ0c3NsLmNvbS9jcnQyLWNybC5jcmwwgY4GCCsGAQUFBwEBBIGBMH8wOQYI"
		"KwYBBQUHMAGGLWh0dHA6Ly9vY3NwLnN0YXJ0c3NsLmNvbS9zdWIvY2xhc3MyL3NlcnZlci9jYTBCBgg"
		"rBgEFBQcwAoY2aHR0cDovL2FpYS5zdGFydHNzbC5jb20vY2VydHMvc3ViLmNsYXNzMi5zZXJ2ZXIuY2"
		"EuY3J0MCMGA1UdEgQcMBqGGGh0dHA6Ly93d3cuc3RhcnRzc2wuY29tLzANBgkqhkiG9w0BAQUFAAOCA"
		"QEAZ+mc7DDWtNBPiWmM62/rWv6c6l77CMLfAfWks8YDf8T69/agJfA+I7tJyP0WoviyVHaoD2LS8FcU"
		"RQNFErk+8ry50d3NFFZaAfKRMFkob6gBx19YdQ64n0ZvS/0+a+ye6NL/yttjLE0ynei8nPmmgzaf5M3"
		"3zOMCaTr7Cq6SJqnlrYUYbdBkobjadcfG2eAKfbhOiVGVEOee4O6JJ+nCrqXpqj42EGuQ8mKvl7Kao+"
		"xerxctag0jzlLRFWJ69l7DZZyyFzY+/I9IWSVj8i0VCz0FkulK9adKeYD4E4BAOQvDFY4ED2FckW3AZ"
		"zVueeiqTSIKwkDFhSDwTJsIfsOaEQ==";
	const gchar *der_certificate_issuer =
		"MIIGNDCCBBygAwIBAgIBGjANBgkqhkiG9w0BAQUFADB9MQswCQYDVQQGEwJJTDEWMBQGA1UEChMNU3Rh"
		"cnRDb20gTHRkLjErMCkGA1UECxMiU2VjdXJlIERpZ2l0YWwgQ2VydGlmaWNhdGUgU2lnbmluZzEpMCcG"
		"A1UEAxMgU3RhcnRDb20gQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkwHhcNMDcxMDI0MjA1NzA5WhcNMTcx"
		"MDI0MjA1NzA5WjCBjDELMAkGA1UEBhMCSUwxFjAUBgNVBAoTDVN0YXJ0Q29tIEx0ZC4xKzApBgNVBAsT"
		"IlNlY3VyZSBEaWdpdGFsIENlcnRpZmljYXRlIFNpZ25pbmcxODA2BgNVBAMTL1N0YXJ0Q29tIENsYXNz"
		"IDIgUHJpbWFyeSBJbnRlcm1lZGlhdGUgU2VydmVyIENBMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB"
		"CgKCAQEA4k85L6GMmoWtCA4IPlfyiAEhG5SpbOK426oZGEY6UqH1D/RujOqWjJaHeRNAUS8i8gyLhw9l"
		"33F0NENVsTUJm9m8H/rrQtCXQHK3Q5Y9upadXVACHJuRjZzArNe7LxfXyz6CnXPrB0KSss1ks3RVG7RL"
		"hiEs93iHMuAW5Nq9TJXqpAp+tgoNLorPVavD5d1Bik7mb2VsskDPF125w2oLJxGEd2H2wnztwI14FBiZ"
		"gZl1Y7foU9O6YekO+qIw80aiuckfbIBaQKwn7UhHM7BUxkYa8zVhwQIpkFR+ZE3EMFICgtffziFuGJHX"
		"uKuMJxe18KMBL47SLoc6PbQpZ4rEAwIDAQABo4IBrTCCAakwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8B"
		"Af8EBAMCAQYwHQYDVR0OBBYEFBHbI0X9VMxqcW+EigPXvvcBLyaGMB8GA1UdIwQYMBaAFE4L7xqkQFul"
		"F2mHMMo0aEPQQa7yMGYGCCsGAQUFBwEBBFowWDAnBggrBgEFBQcwAYYbaHR0cDovL29jc3Auc3RhcnRz"
		"c2wuY29tL2NhMC0GCCsGAQUFBzAChiFodHRwOi8vd3d3LnN0YXJ0c3NsLmNvbS9zZnNjYS5jcnQwWwYD"
		"VR0fBFQwUjAnoCWgI4YhaHR0cDovL3d3dy5zdGFydHNzbC5jb20vc2ZzY2EuY3JsMCegJaAjhiFodHRw"
		"Oi8vY3JsLnN0YXJ0c3NsLmNvbS9zZnNjYS5jcmwwgYAGA1UdIAR5MHcwdQYLKwYBBAGBtTcBAgEwZjAu"
		"BggrBgEFBQcCARYiaHR0cDovL3d3dy5zdGFydHNzbC5jb20vcG9saWN5LnBkZjA0BggrBgEFBQcCARYo"
		"aHR0cDovL3d3dy5zdGFydHNzbC5jb20vaW50ZXJtZWRpYXRlLnBkZjANBgkqhkiG9w0BAQUFAAOCAgEA"
		"nQfh7pB2MWcWRXCMy4SLS1doRKWJwfJ+yyiL9edwd9W29AshYKWhdHMkIoDW2LqNomJdCTVCKfs5Y0UL"
		"pLA4Gmj0lRPM4EOU7Os5GuxXKdmZbfWEzY5zrsncavqenRZkkwjHHMKJVJ53gJD2uSl26xNnSFn4Ljox"
		"uMnTiOVfTtIZPUOO15L/zzi24VuKUx3OrLR2L9j3QGPV7mnzRX2gYsFhw3XtsntNrCEnME5ZRmqTF8rI"
		"OS0Bc2Vb6UGbERecyMhK76F2YC2uk/8M1TMTn08Tzt2G8fz4NVQVqFvnhX76Nwn/i7gxSZ4Nbt600hIt"
		"uO3Iw/G2QqBMl3nf/sOjn6H0bSyEd6SiBeEX/zHdmvO4esNSwhERt1Axin/M51qJzPeGmmGSTy+UtpjH"
		"eOBiS0N9PN7WmrQQoUCcSyrcuNDUnv3xhHgbDlePaVRCaHvqoO91DweijHOZq1X1BwnSrzgDapADDC+P"
		"4uhDwjHpb62H5Y29TiyJS1HmnExUdsASgVOb7KD8LJzaGJVuHjgmQid4YAjff20y6NjAbx/rJnWfk/x7"
		"G/41kNxTowemP4NVCitOYoIlzmYwXSzg+RkbdbmdmFamgyd60Y+NWZP8P3PXLrQsldiL98l+x/ydrHIE"
		"H9LMF/TtNGCbnkqXBP7dcg5XVFEGcE3vqhykguAzx/Q=";
	ENamedParameters *parameters;
	GError *error = NULL;
	gint result;

	g_return_if_fail (prompter != NULL);

	parameters = e_named_parameters_new ();

	e_named_parameters_set (parameters, "host", "bugzilla.gnome.org");
	e_named_parameters_set (parameters, "certificate", der_certificate);
	e_named_parameters_set (parameters, "certificate-errors", "007f");
	e_named_parameters_set (parameters, "issuer", der_certificate_issuer);

	result = e_user_prompter_extension_prompt_sync (prompter, "ETrustPrompt::trust-prompt", parameters, NULL, NULL, &error);

	g_print ("Trust prompt result: %s (%d)%s%s\n", result == 0 ? "Reject" :
		result == 1 ? "Accept permanently" :
		result == 2 ? "Accept temporarily" : "Unknown",
		result,
		error ? ", error: " : "",
		error ? error->message : "");
	g_assert_no_error (error);

	e_named_parameters_free (parameters);

	/* test for an unknown dialog prompt */
	result = e_user_prompter_extension_prompt_sync (prompter, "Uknown-Dialog-Prompt", NULL, NULL, NULL, &error);

	g_print ("Unknown dialog prompt, result:%d, error: %s\n", result, error ? error->message : "None");

	g_assert (result == -1);
	g_assert (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND));

	g_clear_error (&error);
}

struct _Prompts {
	const gchar *type;
	const gchar *primary;
	const gchar *secondary;
	gboolean use_markup;
	const gchar *buttons;
} prompts[] = {
	{ "info",     "%d) <u>info</u> primary text", "info <i>secondary</i> text\nmarkup", TRUE, NULL },
	{ "warning",  "%d) warning primary text", "warning secondary text", FALSE, NULL },
	{ "question", "%d) <u>question</u> primary text", "question <i>secondary</i> text\nmarkup text, but not used as markup", FALSE, NULL },
	{ "error",    "%d) error primary text", "error <i>secondary</i> text\nmarkup", TRUE, NULL },
	{ "other",    "%d) other primary text", "other <i>secondary</i> text\nmarkup", TRUE, NULL },
	{ "#$%@$#%",  "%d) totally unknown type primary text", "totally unknown type secondary text\nmarkup without markup texts", TRUE, NULL },
	{ "",	      NULL, "%d) a very long secondary text, with no primary text and no icon,"
			    " which should wrap ideally, and be on multiple lines, like one may"
			    " expect for such long messages, even without markup", FALSE, NULL },
	{ "",	      "%d) a very long primary text, with no secondary text and no icon,"
			" which should wrap ideally, and be on multiple lines, like one may"
			" expect for such long messages, even without markup", NULL, FALSE, NULL },
	{ "",	      "%d) This one has primary text...", "...and secondary text, and 5 buttons", FALSE, "1st button:2nd button:3rd button:4th button:5th button" }
};

static void
user_prompt_respond_cb (GObject *source,
                        GAsyncResult *result,
                        gpointer user_data)
{
	gint result_button;
	GError *error = NULL;

	result_button = e_user_prompter_prompt_finish (E_USER_PROMPTER (source), result, &error);

	g_print (
		"   Prompt [%d] returned %d%s%s\n", GPOINTER_TO_INT (user_data), result_button,
		error ? ", error: " : "", error ? error->message : "");

	g_assert_no_error (error);
}

static gboolean
quit_main_loop_cb (gpointer main_loop)
{
	g_main_loop_quit (main_loop);
	return FALSE;
}

static gboolean
test_user_prompts_idle_cb (gpointer user_data)
{
	TestFixture *fixture = user_data;
	gint ii, sz;
	GMainContext *main_context = g_main_loop_get_context (fixture->main_loop);
	GSource *source;
	GError *error = NULL;

	/* all but the last run asynchronously, to test they will come
	 * in the right order and only one at a time, and then run
	 * the last synchronously, to wait for the result */
	sz = G_N_ELEMENTS (prompts);
	for (ii = 0; !fixture->only_certificate && ii < sz && !error; ii++) {
		gchar *title, *primary, *secondary, **buttons = NULL;
		GList *captions = NULL;

		title = g_strdup_printf ("Prompt %d...", ii);
		primary = g_strdup_printf (prompts[ii].primary, ii);
		secondary = g_strdup_printf (prompts[ii].secondary, ii);
		if (prompts[ii].buttons) {
			gint jj;

			buttons = g_strsplit (prompts[ii].buttons, ":", -1);
			for (jj = 0; buttons[jj]; jj++) {
				captions = g_list_append (captions, buttons[jj]);
			}
		}

		if (ii + 1 == sz) {
			gint result_button;

			result_button = e_user_prompter_prompt_sync (
				fixture->prompter,
				prompts[ii].type, title, primary, secondary, prompts[ii].use_markup, captions,
				NULL, &error);
			g_print (
				"   Prompt [%d] (sync) returned %d%s%s\n", ii, result_button,
				error ? ", error: " : "", error ? error->message : "");
		} else {
			e_user_prompter_prompt (
				fixture->prompter,
				prompts[ii].type, title, primary, secondary, prompts[ii].use_markup, captions,
				NULL, user_prompt_respond_cb, GINT_TO_POINTER (ii));

			/* give it a chance to be delivered in this order */
			g_main_context_iteration (main_context, FALSE);

			g_usleep (G_USEC_PER_SEC);
		}

		g_free (title);
		g_free (primary);
		g_free (secondary);
		g_strfreev (buttons);
		g_list_free (captions);
	}

	g_assert_no_error (error);

	test_trust_prompt (fixture->prompter);

	g_print ("Waiting 5 seconds for response deliveries...\n");
	source = g_timeout_source_new_seconds (5);
	g_source_set_callback (source, quit_main_loop_cb, fixture->main_loop, NULL);
	g_source_attach (source, main_context);

	return FALSE;
}

static void
test_user_prompts (TestFixture *fixture,
                   gconstpointer user_data)
{
	const TestClosure *closure = user_data;

	fixture->only_certificate = closure->only_certificate;

	g_idle_add (test_user_prompts_idle_cb, fixture);
	g_main_loop_run (fixture->main_loop);
}

gint
main (gint argc,
      gchar **argv)
{
	TestClosure closure;
	gint retval;

	g_test_init (&argc, &argv, NULL);
	g_test_bug_base ("http://bugzilla.gnome.org/");

	closure.only_certificate = argc > 1 && g_ascii_strcasecmp (argv[1], "cert-only") == 0;

	g_test_add (
		"/e-user-prompter-test/UserPrompts",
		TestFixture, &closure,
		test_fixture_setup_session,
		test_user_prompts,
		test_fixture_teardown_session);

	retval = g_test_run ();

	return retval;
}
