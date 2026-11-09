/*
 * SPDX-FileCopyrightText: (C) 2026 Tobias Mueller <muelli@cryptobitch.de>
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include "evolution-data-server-config.h"

#include <glib/gstdio.h>

#include "camel-test.h"

#define TEST_TYPE_CERTDB (test_certdb_get_type ())
G_DECLARE_FINAL_TYPE (TestCertDB, test_certdb, TEST, CERTDB, CamelCertDB)

struct _TestCertDB {
	CamelCertDB parent_instance;

	GThread *put_thread;
	gint put_done;
	guint n_saved;
};

G_DEFINE_TYPE (TestCertDB, test_certdb, CAMEL_TYPE_CERTDB)

static CamelCert *
test_cert_new (const gchar *hostname)
{
	CamelCert *cert;

	cert = camel_cert_new ();
	cert->hostname = g_strdup (hostname);
	cert->fingerprint = g_strdup ("00:11:22:33");

	return cert;
}

static gpointer
put_thread_func (gpointer user_data)
{
	TestCertDB *self = user_data;
	CamelCert *cert;

	cert = test_cert_new ("second.example.com");
	camel_certdb_put (CAMEL_CERTDB (self), cert);
	camel_cert_unref (cert);

	g_atomic_int_set (&self->put_done, 1);

	return NULL;
}

static gint
test_certdb_cert_save (CamelCertDB *certdb,
		       CamelCert *cert,
		       FILE *ostream)
{
	TestCertDB *self = TEST_CERTDB (certdb);

	if (!self->put_thread) {
		gint64 deadline = g_get_monotonic_time () + 5 * G_USEC_PER_SEC;

		/* give a put() from another thread the chance to finish during the save */
		self->put_thread = g_thread_new ("put", put_thread_func, self);
		while (!g_atomic_int_get (&self->put_done) && g_get_monotonic_time () < deadline) {
			g_usleep (G_USEC_PER_SEC / 100);
		}
	}

	self->n_saved++;

	return CAMEL_CERTDB_CLASS (test_certdb_parent_class)->cert_save (certdb, cert, ostream);
}

static void
test_certdb_class_init (TestCertDBClass *klass)
{
	CamelCertDBClass *certdb_class;

	certdb_class = CAMEL_CERTDB_CLASS (klass);
	certdb_class->cert_save = test_certdb_cert_save;
}

static void
test_certdb_init (TestCertDB *self)
{
}

static void
test_certdb_put_during_save (void)
{
	CamelCertDB *certdb;
	CamelCert *cert;
	gchar *tmpdir, *filename;
	GError *local_error = NULL;

	tmpdir = g_dir_make_tmp ("test-camel-certdb-XXXXXX", &local_error);
	g_assert_no_error (local_error);
	filename = g_build_filename (tmpdir, "certs.db", NULL);

	certdb = g_object_new (TEST_TYPE_CERTDB, NULL);
	camel_certdb_set_filename (certdb, filename);

	cert = test_cert_new ("first.example.com");
	camel_certdb_put (certdb, cert);
	camel_cert_unref (cert);

	g_assert_cmpint (camel_certdb_save (certdb), ==, 0);
	g_thread_join (TEST_CERTDB (certdb)->put_thread);

	/* the put during the first save leaves the database dirty, so this writes both */
	TEST_CERTDB (certdb)->n_saved = 0;
	g_assert_cmpint (camel_certdb_save (certdb), ==, 0);
	g_assert_cmpuint (TEST_CERTDB (certdb)->n_saved, ==, 2);

	g_object_unref (certdb);

	g_assert_cmpint (g_unlink (filename), ==, 0);
	g_assert_cmpint (g_rmdir (tmpdir), ==, 0);
	g_free (filename);
	g_free (tmpdir);
}

gint
main (gint argc,
      gchar **argv)
{
	gint ret;

	camel_test_init (&argc, &argv);

	g_test_add_func ("/Camel/CertDB/PutDuringSave", test_certdb_put_during_save);

	ret = g_test_run ();
	camel_test_shutdown ();

	return ret;
}
