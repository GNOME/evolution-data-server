/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - generic backend class
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Rodrigo Moya <rodrigo@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#ifndef E_CAL_BACKEND_UTIL_H
#define E_CAL_BACKEND_UTIL_H

#include <libedata-cal/e-cal-backend.h>

G_BEGIN_DECLS

/*
 * Functions for accessing mail configuration
 */

gboolean e_cal_backend_mail_account_get_default (gchar **address, gchar **name);
gboolean e_cal_backend_mail_account_is_valid (gchar *user, gchar **name);

const gchar *e_cal_backend_status_to_string (GNOME_Evolution_Calendar_CallStatus status);

gboolean e_cal_backend_user_declined (icalcomponent *icalcomp);

G_END_DECLS

#endif
