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
} ECalTestClosure;

ECal*
ecal_test_utils_cal_new_temp (char           **uri,
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

char*
ecal_test_utils_cal_get_alarm_email_address (ECal *cal);

char*
ecal_test_utils_cal_get_cal_address (ECal *cal);

char*
ecal_test_utils_cal_get_ldap_attribute (ECal *cal);

void
ecal_test_utils_cal_get_capabilities (ECal *cal);

#endif /* _ECAL_TEST_UTILS_H */
