/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#include <string.h>

#include "addresses.h"
#include "camel-test.h"

void
test_address_compare (CamelInternetAddress *addr,
                      CamelInternetAddress *addr2)
{
	const gchar *r1, *r2, *a1, *a2;
	gchar *e1, *e2, *f1, *f2;
	gint j;

	g_assert_cmpint (camel_address_length (CAMEL_ADDRESS (addr)), ==, camel_address_length (CAMEL_ADDRESS (addr2)));
	for (j = 0; j < camel_address_length (CAMEL_ADDRESS (addr)); j++) {

		g_assert_true (camel_internet_address_get (addr, j, &r1, &a1));
		g_assert_true (camel_internet_address_get (addr2, j, &r2, &a2));

		g_assert_true (string_equal (r1, r2));
		g_assert_cmpstr (a1, ==, a2);
	}
	g_assert_false (camel_internet_address_get (addr, j, &r1, &a1));
	g_assert_false (camel_internet_address_get (addr2, j, &r2, &a2));

	e1 = camel_address_encode (CAMEL_ADDRESS (addr));
	e2 = camel_address_encode (CAMEL_ADDRESS (addr2));

	if (camel_address_length (CAMEL_ADDRESS (addr)) == 0)
		g_assert_true (e1 == NULL && e2 == NULL);
	else
		g_assert_true (e1 != NULL && e2 != NULL);

	if (e1 != NULL) {
		g_assert_true (string_equal (e1, e2));
		g_free (e1);
		g_free (e2);
	}

	f1 = camel_address_format (CAMEL_ADDRESS (addr));
	f2 = camel_address_format (CAMEL_ADDRESS (addr2));

	if (camel_address_length (CAMEL_ADDRESS (addr)) == 0)
		g_assert_true (f1 == NULL && f2 == NULL);
	else
		g_assert_true (f1 != NULL && f2 != NULL);

	if (f1 != NULL) {
		g_assert_true (string_equal (f1, f2));
		g_free (f1);
		g_free (f2);
	}
}
