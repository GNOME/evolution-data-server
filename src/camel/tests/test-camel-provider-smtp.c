/*
 * SPDX-FileCopyrightText: (C) 2026 Collabora, Ltd. (www.collabora.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <string.h>
#include <glib.h>

#include <camel/camel.h>

#include "camel-test.h"
#include "camel-test-provider.h"
#include "dovecot-helper.h"

typedef struct _SmtpTestSession SmtpTestSession;
typedef struct _SmtpTestSessionClass SmtpTestSessionClass;

struct _SmtpTestSession {
	CamelSession parent;
};

struct _SmtpTestSessionClass {
	CamelSessionClass parent_class;
};

GType smtp_test_session_get_type (void);

G_DEFINE_TYPE (SmtpTestSession, smtp_test_session, CAMEL_TYPE_SESSION)

static gboolean
smtp_test_session_authenticate_sync (CamelSession *session,
				     CamelService *service,
				     const gchar *mechanism,
				     GCancellable *cancellable,
				     GError **error)
{
	CamelAuthenticationResult result;

	result = camel_service_authenticate_sync (service, mechanism, cancellable, error);

	return result == CAMEL_AUTHENTICATION_ACCEPTED;
}

static void
smtp_test_session_class_init (SmtpTestSessionClass *klass)
{
	CamelSessionClass *session_class;

	session_class = CAMEL_SESSION_CLASS (klass);
	session_class->authenticate_sync = smtp_test_session_authenticate_sync;
}

static void
smtp_test_session_init (SmtpTestSession *session)
{
}

static DovecotTestServer *test_server = NULL;
static const gchar *smtp_drivers[] = { "smtp" };

static CamelSession *
test_smtp_session_new (void)
{
	return g_object_new (
		smtp_test_session_get_type (),
		"user-data-dir", camel_test_get_dir (),
		"user-cache-dir", camel_test_get_dir (),
		NULL);
}

static CamelService *
test_smtp_create_service (CamelSession *session,
			  const gchar *uid)
{
	CamelService *service;
	CamelSettings *settings;
	GError *error = NULL;

	service = camel_session_add_service (session, uid, "smtp",
		CAMEL_PROVIDER_TRANSPORT, &error);
	g_assert_no_error (error);
	g_assert_nonnull (service);

	settings = camel_service_ref_settings (service);

	camel_network_settings_set_host (
		CAMEL_NETWORK_SETTINGS (settings), dovecot_test_server_get_host (test_server));
	camel_network_settings_set_port (
		CAMEL_NETWORK_SETTINGS (settings), dovecot_test_server_get_smtp_port (test_server));
	camel_network_settings_set_user (
		CAMEL_NETWORK_SETTINGS (settings), dovecot_test_server_get_user (test_server));
	camel_network_settings_set_security_method (
		CAMEL_NETWORK_SETTINGS (settings), CAMEL_NETWORK_SECURITY_METHOD_NONE);
	camel_network_settings_set_auth_mechanism (
		CAMEL_NETWORK_SETTINGS (settings), "PLAIN");

	g_object_unref (settings);

	camel_service_set_password (service, dovecot_test_server_get_password (test_server));

	return service;
}

static void
test_flush_main_context (void)
{
	while (g_main_context_iteration (NULL, FALSE)) {
	}
}

static void
test_smtp_connect_service (CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_service_connect_sync (service, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	test_flush_main_context ();
}

static void
test_smtp_disconnect_service (CamelService *service)
{
	GError *error = NULL;
	gboolean success;

	success = camel_service_disconnect_sync (service, TRUE, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	test_flush_main_context ();
}

static void
test_smtp_teardown (CamelSession *session,
		    CamelService *service)
{
	test_smtp_disconnect_service (service);
	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

static CamelMimeMessage *
test_create_message (const gchar *subject,
		     const gchar *from_name,
		     const gchar *from_addr,
		     const gchar *to_name,
		     const gchar *to_addr,
		     const gchar *body)
{
	CamelMimeMessage *msg;
	CamelInternetAddress *from;
	CamelInternetAddress *to;

	msg = camel_mime_message_new ();
	camel_mime_message_set_subject (msg, subject);
	camel_mime_message_set_date (msg, CAMEL_MESSAGE_DATE_CURRENT, 0);

	from = camel_internet_address_new ();
	camel_internet_address_add (from, from_name, from_addr);
	camel_mime_message_set_from (msg, from);
	g_object_unref (from);

	to = camel_internet_address_new ();
	camel_internet_address_add (to, to_name, to_addr);
	camel_mime_message_set_recipients (msg, CAMEL_RECIPIENT_TYPE_TO, to);
	g_object_unref (to);

	camel_mime_part_set_content (CAMEL_MIME_PART (msg), body, strlen (body), "text/plain");

	return msg;
}

static void
test_connect (void)
{
	CamelSession *session;
	CamelService *service;

	session = test_smtp_session_new ();
	service = test_smtp_create_service (session, "test-connect");

	test_smtp_connect_service (service);
	test_smtp_disconnect_service (service);

	camel_session_remove_service (session, service);
	test_flush_main_context ();
	g_object_unref (service);
	test_flush_main_context ();
	g_object_unref (session);
	test_flush_main_context ();
}

static void
test_send_message (void)
{
	CamelSession *session;
	CamelService *service;
	CamelMimeMessage *msg;
	CamelInternetAddress *from;
	CamelInternetAddress *to;
	gboolean sent_message_saved = FALSE;
	GError *error = NULL;
	gboolean success;

	session = test_smtp_session_new ();
	service = test_smtp_create_service (session, "test-send");

	test_smtp_connect_service (service);

	msg = test_create_message (
		"Test SMTP Send",
		"Sender", "sender@example.com",
		"Recipient", "recipient@example.com",
		"Test message body.\n");

	from = camel_internet_address_new ();
	camel_internet_address_add (from, "Sender", "sender@example.com");

	to = camel_internet_address_new ();
	camel_internet_address_add (to, "Recipient", "recipient@example.com");

	success = camel_transport_send_to_sync (
		CAMEL_TRANSPORT (service), msg,
		CAMEL_ADDRESS (from), CAMEL_ADDRESS (to),
		&sent_message_saved, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_object_unref (from);
	g_object_unref (to);
	g_object_unref (msg);

	test_smtp_teardown (session, service);
}

static void
test_send_message_multiple_recipients (void)
{
	CamelSession *session;
	CamelService *service;
	CamelMimeMessage *msg;
	CamelInternetAddress *from;
	CamelInternetAddress *to;
	gboolean sent_message_saved = FALSE;
	GError *error = NULL;
	gboolean success;

	session = test_smtp_session_new ();
	service = test_smtp_create_service (session, "test-send-multi");

	test_smtp_connect_service (service);

	msg = test_create_message (
		"Test SMTP Multiple Recipients",
		"Sender", "sender@example.com",
		"Recipient One", "recipient1@example.com",
		"Test message body.\n");

	from = camel_internet_address_new ();
	camel_internet_address_add (from, "Sender", "sender@example.com");

	to = camel_internet_address_new ();
	camel_internet_address_add (to, "Recipient One", "recipient1@example.com");
	camel_internet_address_add (to, "Recipient Two", "recipient2@example.com");
	camel_internet_address_add (to, "Recipient Three", "recipient3@example.com");

	success = camel_transport_send_to_sync (
		CAMEL_TRANSPORT (service), msg,
		CAMEL_ADDRESS (from), CAMEL_ADDRESS (to),
		&sent_message_saved, NULL, &error);
	g_assert_no_error (error);
	g_assert_true (success);

	g_object_unref (from);
	g_object_unref (to);
	g_object_unref (msg);

	test_smtp_teardown (session, service);
}

/* Dovecot's submission service does not advertise the SMTPUTF8 (RFC 6531)
 * extension, so this exercises the fallback path added for servers that
 * lack SMTPUTF8 support: sending a message that involves a non-ASCII
 * envelope address must fail early with a clear error, instead of sending
 * a malformed command to the server. */
static void
test_send_message_smtputf8_unsupported (void)
{
	CamelSession *session;
	CamelService *service;
	CamelMimeMessage *msg;
	CamelInternetAddress *from;
	CamelInternetAddress *to;
	gboolean sent_message_saved = FALSE;
	GError *error = NULL;
	gboolean success;

	session = test_smtp_session_new ();
	service = test_smtp_create_service (session, "test-send-smtputf8");

	test_smtp_connect_service (service);

	msg = test_create_message (
		"Test SMTPUTF8 Fallback",
		"Sender", "sender@example.com",
		"Ünïcode Recipient", "üser@example.com",
		"Test message body.\n");

	from = camel_internet_address_new ();
	camel_internet_address_add (from, "Sender", "sender@example.com");

	to = camel_internet_address_new ();
	camel_internet_address_add (to, "Ünïcode Recipient", "üser@example.com");

	success = camel_transport_send_to_sync (
		CAMEL_TRANSPORT (service), msg,
		CAMEL_ADDRESS (from), CAMEL_ADDRESS (to),
		&sent_message_saved, NULL, &error);
	g_assert_false (success);
	g_assert_nonnull (error);
	g_assert_error (error, CAMEL_ERROR, CAMEL_ERROR_GENERIC);

	g_clear_error (&error);
	g_object_unref (from);
	g_object_unref (to);
	g_object_unref (msg);

	test_smtp_teardown (session, service);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);
	camel_test_provider_init (1, smtp_drivers);

	test_server = dovecot_test_server_new ();
	if (!test_server) {
		g_print ("Dovecot not installed, skipping\n");
		camel_test_shutdown ();
		return 0;
	}

	if (dovecot_test_server_get_smtp_port (test_server) == 0) {
		g_print ("Dovecot SMTP submission service not available, skipping\n");
		dovecot_test_server_free (test_server);
		test_server = NULL;
		camel_test_shutdown ();
		return 0;
	}

	g_test_add_func ("/Camel/SMTP/Connect", test_connect);
	g_test_add_func ("/Camel/SMTP/SendMessage", test_send_message);
	g_test_add_func ("/Camel/SMTP/SendMessageMultipleRecipients", test_send_message_multiple_recipients);
	g_test_add_func ("/Camel/SMTP/SendMessageSMTPUTF8Unsupported", test_send_message_smtputf8_unsupported);

	ret = g_test_run ();

	dovecot_test_server_free (test_server);
	test_server = NULL;

	camel_test_shutdown ();

	return ret;
}
