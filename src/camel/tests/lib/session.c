/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include "session.h"

G_DEFINE_TYPE (CamelTestSession, camel_test_session, CAMEL_TYPE_SESSION)

static void
camel_test_session_class_init (CamelTestSessionClass *class)
{
}

static void
camel_test_session_init (CamelTestSession *test_session)
{
}

CamelSession *
camel_test_session_new (const gchar *path)
{
	return g_object_new (
		CAMEL_TYPE_TEST_SESSION,
		"user-data-dir", path, NULL);
}
