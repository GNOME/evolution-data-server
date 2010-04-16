/* Evolution calendar system timezone functions
 * Based on gnome-panel's clock-applet system-timezone.c file.
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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

#ifndef E_CAL_SYSTEM_TIMEZONE_H
#define E_CAL_SYSTEM_TIMEZONE_H

#include <libical/ical.h>

#define ETC_TIMEZONE        "/etc/timezone"
#define ETC_TIMEZONE_MAJ    "/etc/TIMEZONE"
#define ETC_RC_CONF         "/etc/rc.conf"
#define ETC_SYSCONFIG_CLOCK "/etc/sysconfig/clock"
#define ETC_CONF_D_CLOCK    "/etc/conf.d/clock"
#define ETC_LOCALTIME       "/etc/localtime"

gchar *e_cal_system_timezone_get_location (void);

#endif
