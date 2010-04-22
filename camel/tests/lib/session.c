#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
	CamelSession *session;

	session = g_object_new (CAMEL_TYPE_TEST_SESSION, NULL);
	camel_session_construct (session, path);

	return session;
}
