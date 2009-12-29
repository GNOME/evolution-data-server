/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	char *uri = NULL;
	char *attr;

	g_type_init ();

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);

	attr = ecal_test_utils_cal_get_ldap_attribute (cal);
	test_print ("LDAP attribute: '%s'\n", attr);

	ecal_test_utils_cal_remove (cal);

	g_free (attr);

	return 0;
}
