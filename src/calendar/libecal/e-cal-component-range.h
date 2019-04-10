/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 * Copyright (C) 2019 Red Hat, Inc. (www.redhat.com)
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
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_COMPONENT_RANGE_H
#define E_CAL_COMPONENT_RANGE_H

#include <glib-object.h>
#include <libecal/e-cal-enums.h>
#include <libecal/e-cal-component-datetime.h>

G_BEGIN_DECLS

/**
 * ECalComponentRange:
 *
 * Describes a range. Use the functions below to work with it.
 **/
typedef struct _ECalComponentRange ECalComponentRange;

GType		e_cal_component_range_get_type	(void);
ECalComponentRange *
		e_cal_component_range_new	(ECalComponentRangeKind kind,
						 const ECalComponentDateTime *datetime);
ECalComponentRange *
		e_cal_component_range_new_take	(ECalComponentRangeKind kind,
						 ECalComponentDateTime *datetime);
ECalComponentRange *
		e_cal_component_range_copy	(const ECalComponentRange *range);
void		e_cal_component_range_free	(gpointer range); /* ECalComponentRange * */
ECalComponentRangeKind
		e_cal_component_range_get_kind	(const ECalComponentRange *range);
void		e_cal_component_range_set_kind	(ECalComponentRange *range,
						 ECalComponentRangeKind kind);
ECalComponentDateTime *
		e_cal_component_range_get_datetime
						(const ECalComponentRange *range);
void		e_cal_component_range_set_datetime
						(ECalComponentRange *range,
						 const ECalComponentDateTime *datetime);
void		e_cal_component_range_take_datetime
						(ECalComponentRange *range,
						 ECalComponentDateTime *datetime);

G_END_DECLS

#endif /* E_CAL_COMPONENT_RANGE_H */
