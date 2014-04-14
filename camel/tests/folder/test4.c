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

/* store testing, for remote folders */

#include "camel-test.h"
#include "camel-test-provider.h"
#include "folders.h"
#include "session.h"

static const gchar *imap_drivers[] = { "imap4" };

static const gchar *remote_providers[] = {
	"IMAP_TEST_URL",
};

gint main (gint argc, gchar **argv)
{
	CamelSession *session;
	gint i;
	gchar *path;

	camel_test_init (argc, argv);
	camel_test_provider_init (1, imap_drivers);

	/* clear out any camel-test data */
	system ("/bin/rm -rf /tmp/camel-test");

	session = camel_test_session_new ("/tmp/camel-test");

	/* todo: cross-check everything with folder_info checks as well */
	/* todo: subscriptions? */
	for (i = 0; i < G_N_ELEMENTS (remote_providers); i++) {
		path = getenv (remote_providers[i]);

		if (path == NULL) {
			printf ("Aborted (ignored).\n");
			printf ("Set '%s', to re-run test.\n", remote_providers[i]);
			/* tells make check to ignore us in the total count */
			_exit (77);
		}
		/*camel_test_nonfatal("The IMAP code is just rooted");*/
		test_folder_basic (session, path, FALSE, FALSE);
		/*camel_test_fatal();*/
	}

	g_object_unref (session);

	return 0;
}
