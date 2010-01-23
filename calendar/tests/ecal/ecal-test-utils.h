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

#ifndef _ECAL_TEST_UTILS_H
#define _ECAL_TEST_UTILS_H

#include <glib.h>
#include <libecal/e-cal.h>

typedef struct {
        GSourceFunc  cb;
        gpointer     user_data;
	CalMode      mode;
	ECal        *cal;
} ECalTestClosure;

void
test_print (const gchar *format,
            ...);

ECal*
ecal_test_utils_cal_new_from_uri (const gchar     *uri,
				  ECalSourceType  type);

ECal*
ecal_test_utils_cal_new_temp (gchar           **uri,
			      ECalSourceType   type);

void
ecal_test_utils_cal_open (ECal     *cal,
                          gboolean  only_if_exists);

void
ecal_test_utils_cal_async_open (ECal        *cal,
                                gboolean     only_if_exists,
                                GSourceFunc  callback,
                                gpointer     user_data);

void
ecal_test_utils_cal_remove (ECal *cal);

gchar *
ecal_test_utils_cal_get_alarm_email_address (ECal *cal);

gchar *
ecal_test_utils_cal_get_cal_address (ECal *cal);

gchar *
ecal_test_utils_cal_get_ldap_attribute (ECal *cal);

void
ecal_test_utils_cal_get_capabilities (ECal *cal);

GList*
ecal_test_utils_cal_get_free_busy (ECal   *cal,
                                   GList  *users,
                                   time_t  start,
                                   time_t  end);

void
ecal_test_utils_cal_assert_objects_equal_shallow (icalcomponent *a,
                                                  icalcomponent *b);

void
ecal_test_utils_cal_assert_e_cal_components_equal (ECalComponent *a,
                                                   ECalComponent *b);

icalcomponent*
ecal_test_utils_cal_get_object (ECal       *cal,
                                const gchar *uid);

void
ecal_test_utils_cal_modify_object (ECal          *cal,
                                   icalcomponent *component,
                                   CalObjModType  mod_type);

void
ecal_test_utils_cal_remove_object (ECal       *cal,
				   const gchar *uid);

icalcomponent*
ecal_test_utils_cal_get_default_object (ECal *cal);

GList*
ecal_test_utils_cal_get_object_list (ECal       *cal,
                                     const gchar *query);

GList*
ecal_test_utils_cal_get_objects_for_uid (ECal       *cal,
					 const gchar *uid);

gchar *
ecal_test_utils_cal_create_object (ECal          *cal,
				   icalcomponent *component);

void
ecal_test_utils_cal_set_mode (ECal        *cal,
                              CalMode      mode,
                              GSourceFunc  callback,
                              gpointer     user_data);

void
ecal_test_utils_create_component (ECal           *cal,
                                  const gchar     *dtstart,
                                  const gchar     *dtstart_tzid,
                                  const gchar     *dtend,
                                  const gchar     *dtend_tzid,
                                  const gchar     *summary,
                                  ECalComponent **comp_out,
                                  gchar          **uid_out);

void
ecal_test_utils_cal_component_set_icalcomponent (ECalComponent *e_component,
						 icalcomponent *component);

icaltimezone*
ecal_test_utils_cal_get_timezone (ECal       *cal,
                                  const gchar *tzid);

void
ecal_test_utils_cal_add_timezone (ECal         *cal,
                                  icaltimezone *zone);

void
ecal_test_utils_cal_set_default_timezone (ECal         *cal,
					  icaltimezone *zone);

void
ecal_test_utils_cal_send_objects (ECal           *cal,
                                  icalcomponent  *component,
                                  GList         **users,
                                  icalcomponent **component_final);

void
ecal_test_utils_cal_receive_objects (ECal          *cal,
                                     icalcomponent *component);

ECalView*
ecal_test_utils_get_query (ECal       *cal,
                           const gchar *sexp);

#endif /* _ECAL_TEST_UTILS_H */
