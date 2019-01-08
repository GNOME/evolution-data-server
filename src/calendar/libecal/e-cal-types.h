/* Evolution calendar utilities and types
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Authors: Federico Mena-Quintero <federico@ximian.com>
 *          JP Rosevear <jpr@ximian.com>
 */

#if !defined (__LIBECAL_H_INSIDE__) && !defined (LIBECAL_COMPILATION)
#error "Only <libecal/libecal.h> should be included directly."
#endif

#ifndef E_CAL_TYPES_H
#define E_CAL_TYPES_H

#include <libecal/e-cal-component.h>

G_BEGIN_DECLS

/**
 * ECalClientSourceType:
 * @E_CAL_CLIENT_SOURCE_TYPE_EVENTS: Events calander
 * @E_CAL_CLIENT_SOURCE_TYPE_TASKS: Task list calendar
 * @E_CAL_CLIENT_SOURCE_TYPE_MEMOS: Memo list calendar
 * @E_CAL_CLIENT_SOURCE_TYPE_LAST: Artificial 'last' value of the enum
 *
 * Indicates the type of calendar
 *
 * Since: 3.2
 **/
typedef enum {
	E_CAL_CLIENT_SOURCE_TYPE_EVENTS,
	E_CAL_CLIENT_SOURCE_TYPE_TASKS,
	E_CAL_CLIENT_SOURCE_TYPE_MEMOS,
	E_CAL_CLIENT_SOURCE_TYPE_LAST  /*< skip >*/
} ECalClientSourceType;

/**
 * ECalObjModType:
 * @E_CAL_OBJ_MOD_THIS: Modify this component
 * @E_CAL_OBJ_MOD_THIS_AND_PRIOR: Modify this component and all prior occurrances
 * @E_CAL_OBJ_MOD_THIS_AND_FUTURE: Modify this component and all future occurrances
 * @E_CAL_OBJ_MOD_ALL: Modify all occurrances of this component
 * @E_CAL_OBJ_MOD_ONLY_THIS: Modify only this component
 *
 * Indicates the type of modification made to a calendar
 *
 * Since: 3.8
 **/
typedef enum {
	E_CAL_OBJ_MOD_THIS = 1 << 0,
	E_CAL_OBJ_MOD_THIS_AND_PRIOR = 1 << 1,
	E_CAL_OBJ_MOD_THIS_AND_FUTURE = 1 << 2,
	E_CAL_OBJ_MOD_ALL = 0x07,
	E_CAL_OBJ_MOD_ONLY_THIS = 1 << 3
} ECalObjModType;

G_END_DECLS

#endif /* E_CAL_TYPES_H */

