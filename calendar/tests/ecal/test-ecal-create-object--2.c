/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

#include <stdlib.h>
#include <libecal/e-cal.h>
#include <libical/ical.h>

#include "ecal-test-utils.h"

static void
create_test_component (ECal           *cal,
		       ECalComponent **comp_out,
		       char          **uid_out)
{
        ECalComponent *comp;
        icalcomponent *icalcomp;
        struct icaltimetype tt;
        ECalComponentText text;
        ECalComponentDateTime dt;
	char *uid;

        comp = e_cal_component_new ();
        /* set fields */
        e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
        text.value = "Creation of new test event";
        text.altrep = NULL;
        e_cal_component_set_summary (comp, &text);
        tt = icaltime_from_string ("20040109T090000Z");
        dt.value = &tt;
        dt.tzid ="UTC";
        e_cal_component_set_dtstart (comp, &dt);
        tt = icaltime_from_string ("20040109T103000");
        dt.value = &tt;
        dt.tzid ="UTC";
        e_cal_component_set_dtend (comp, &dt);
        e_cal_component_set_transparency (comp, E_CAL_COMPONENT_TRANSP_OPAQUE);

        e_cal_component_commit_sequence (comp);
        icalcomp = e_cal_component_get_icalcomponent (comp);

	uid = ecal_test_utils_cal_create_object (cal, icalcomp);
        e_cal_component_commit_sequence (comp);

	*comp_out = comp;
	*uid_out = uid;
}

gint
main (gint argc, gchar **argv)
{
	ECal *cal;
	char *uri = NULL;
        ECalComponent *comp, *comp_retrieved;
        icalcomponent *icalcomp_retrieved;
	char *uid;

	cal = ecal_test_utils_cal_new_temp (&uri, E_CAL_SOURCE_TYPE_EVENT);
	ecal_test_utils_cal_open (cal, FALSE);
	create_test_component (cal, &comp, &uid);

	icalcomp_retrieved = ecal_test_utils_cal_get_object (cal, uid);
        comp_retrieved = e_cal_component_new ();
        if (!e_cal_component_set_icalcomponent (comp_retrieved, icalcomp_retrieved)) {
                g_error ("Could not set icalcomponent\n");
        }

	ecal_test_utils_cal_assert_e_cal_components_equal (comp, comp_retrieved);

        g_object_unref (comp_retrieved);
	g_object_unref (comp);
	g_free (uid);

	return 0;
}
