/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@ximian.com>
 */

#if !defined (__LIBEDATA_CAL_H_INSIDE__) && !defined (LIBEDATA_CAL_COMPILATION)
#error "Only <libedata-cal/libedata-cal.h> should be included directly."
#endif

#ifndef E_CAL_BACKEND_UTIL_H
#define E_CAL_BACKEND_UTIL_H

#include <libedataserver/libedataserver.h>

#include <libedata-cal/e-cal-backend.h>

G_BEGIN_DECLS

/*
 * Functions for accessing mail configuration
 */

gboolean	e_cal_backend_mail_account_get_default
						(ESourceRegistry *registry,
						 gchar **address,
						 gchar **name);
gboolean	e_cal_backend_mail_account_is_valid
						(ESourceRegistry *registry,
						 const gchar *user,
						 gchar **name);
gboolean	e_cal_backend_user_declined	(ESourceRegistry *registry,
                                                 ICalComponent *icalcomp);

G_END_DECLS

#endif /* E_CAL_BACKEND_UTIL_H */
