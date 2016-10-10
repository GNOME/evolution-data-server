/*
 * This library is free software: you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License
 * for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library. If not, see <http://www.gnu.org/licenses/>.
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
