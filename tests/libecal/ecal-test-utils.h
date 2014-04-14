/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2009 Intel Corporation
 *
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
 *
 * Authors: Travis Reitter (travis.reitter@collabora.co.uk)
 */

#ifndef _ECAL_TEST_UTILS_H
#define _ECAL_TEST_UTILS_H

#include <libecal/libecal.h>

void
test_print (const gchar *format,
            ...);

gchar *
ecal_test_utils_cal_get_alarm_email_address (ECal *cal);

gchar *
ecal_test_utils_cal_get_cal_address (ECal *cal);

void
ecal_test_utils_cal_get_capabilities (ECal *cal);

GList *
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

icalcomponent *
ecal_test_utils_cal_get_object (ECal       *cal,
                                const gchar *uid);

void
ecal_test_utils_cal_modify_object (ECal          *cal,
                                   icalcomponent *component,
                                   CalObjModType  mod_type);

void
ecal_test_utils_cal_remove_object (ECal       *cal,
				   const gchar *uid);

icalcomponent *
ecal_test_utils_cal_get_default_object (ECal *cal);

GList *
ecal_test_utils_cal_get_object_list (ECal       *cal,
                                     const gchar *query);

GList *
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

icaltimezone *
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

ECalView *
ecal_test_utils_get_query (ECal       *cal,
                           const gchar *sexp);

#endif /* _ECAL_TEST_UTILS_H */
