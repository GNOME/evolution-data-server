/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * Author: Travis Reitter (travis.reitter@collabora.co.uk)
 */

#include <stdlib.h>
#include <glib.h>
#include <gio/gio.h>
#include <libecal/e-cal.h>

#include "ecal-test-utils.h"

ECal*
ecal_test_utils_cal_new_temp (char           **uri,
                              ECalSourceType   type)
{
        ECal *cal;
        GError *error = NULL;
        gchar *file_template;
        char *uri_result;

        file_template = g_build_filename (g_get_tmp_dir (),
                        "ecal-test-XXXXXX/", NULL);
        g_mkstemp (file_template);

        uri_result = g_filename_to_uri (file_template, NULL, &error);
        if (!uri_result) {
                g_warning ("failed to convert %s to an URI: %s", file_template,
                                error->message);
                exit (1);
        }
        g_free (file_template);

        /* create a temp calendar in /tmp */
        g_print ("loading calendar\n");
        cal = e_cal_new_from_uri (uri_result, type);
        if (!cal) {
                g_warning ("failed to create calendar: `%s'", *uri);
                exit(1);
        }

        if (uri)
                *uri = g_strdup (uri_result);

        g_free (uri_result);

        return cal;
}

void
ecal_test_utils_cal_open (ECal     *cal,
                          gboolean  only_if_exists)
{
        GError *error = NULL;

        if (!e_cal_open (cal, only_if_exists, &error)) {
                const char *uri;

                uri = e_cal_get_uri (cal);

                g_warning ("failed to open calendar: `%s': %s", uri,
                                error->message);
                exit(1);
        }
}

static void
open_cb (ECal            *cal,
	 ECalendarStatus  status,
	 ECalTestClosure *closure)
{
	if (FALSE) {
	} else if (status == E_CALENDAR_STATUS_BUSY) {
		g_print ("calendar server is busy; waiting...");
		return;
	} else if (status != E_CALENDAR_STATUS_OK) {
                g_warning ("failed to asynchronously remove the calendar: "
                                "status %d", status);
                exit (1);
        }

        g_print ("successfully asynchronously removed the temporary "
                        "calendar\n");
        if (closure)
                (*closure->cb) (closure);

	g_signal_handlers_disconnect_by_func (cal, open_cb, closure);
	g_free (closure);
}

void
ecal_test_utils_cal_async_open (ECal        *cal,
				gboolean     only_if_exists,
                                GSourceFunc  callback,
                                gpointer     user_data)
{
        ECalTestClosure *closure;

        closure = g_new0 (ECalTestClosure, 1);
        closure->cb = callback;
        closure->user_data = user_data;

	g_signal_connect (G_OBJECT (cal), "cal_opened", G_CALLBACK (open_cb), closure);
	e_cal_open_async (cal, only_if_exists);
}

void
ecal_test_utils_cal_remove (ECal *cal)
{
        GError *error = NULL;

        if (!e_cal_remove (cal, &error)) {
                g_warning ("failed to remove calendar; %s\n", error->message);
                exit(1);
        }
        g_print ("successfully removed the temporary calendar\n");

        g_object_unref (cal);
}

char*
ecal_test_utils_cal_get_alarm_email_address (ECal *cal)
{
        GError *error = NULL;
	char *address = NULL;

        if (!e_cal_get_alarm_email_address (cal, &address, &error)) {
                g_warning ("failed to get alarm email address; %s\n", error->message);
                exit(1);
        }
        g_print ("successfully got the alarm email address\n");

	return address;
}

char*
ecal_test_utils_cal_get_cal_address (ECal *cal)
{
        GError *error = NULL;
	char *address = NULL;

        if (!e_cal_get_cal_address (cal, &address, &error)) {
                g_warning ("failed to get calendar address; %s\n", error->message);
                exit(1);
        }
        g_print ("successfully got the calendar address\n");

	return address;
}

char*
ecal_test_utils_cal_get_ldap_attribute (ECal *cal)
{
        GError *error = NULL;
	char *attr = NULL;

        if (!e_cal_get_ldap_attribute (cal, &attr, &error)) {
                g_warning ("failed to get ldap attribute; %s\n", error->message);
                exit(1);
        }
        g_print ("successfully got the ldap attribute\n");

	return attr;
}
