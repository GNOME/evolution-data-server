/*
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

#ifndef E_CAL_ENUMS_H
#define E_CAL_ENUMS_H

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

/**
 * ECalComponentVType:
 * @E_CAL_COMPONENT_NO_TYPE: Unknown or unsupported component type
 * @E_CAL_COMPONENT_EVENT: vEvent type
 * @E_CAL_COMPONENT_TODO: vTodo type
 * @E_CAL_COMPONENT_JOURNAL: vJournal type
 * @E_CAL_COMPONENT_FREEBUSY: vFreeBusy type
 * @E_CAL_COMPONENT_TIMEZONE: vTimezone type
 *
 * Types of calendar components to be stored by a ECalComponent, as per RFC 2445.
 * We don't put the alarm component type here since we store alarms as separate
 * structures inside the other "real" components.
 **/
typedef enum {
	E_CAL_COMPONENT_NO_TYPE,
	E_CAL_COMPONENT_EVENT,
	E_CAL_COMPONENT_TODO,
	E_CAL_COMPONENT_JOURNAL,
	E_CAL_COMPONENT_FREEBUSY,
	E_CAL_COMPONENT_TIMEZONE
} ECalComponentVType;

/**
 * ECalComponentClassification:
 * @E_CAL_COMPONENT_CLASS_NONE: None
 * @E_CAL_COMPONENT_CLASS_PUBLIC: Public
 * @E_CAL_COMPONENT_CLASS_PRIVATE: Private
 * @E_CAL_COMPONENT_CLASS_CONFIDENTIAL: Confidential
 * @E_CAL_COMPONENT_CLASS_UNKNOWN: Unknown
 *
 * CLASSIFICATION property
 **/
typedef enum {
	E_CAL_COMPONENT_CLASS_NONE,
	E_CAL_COMPONENT_CLASS_PUBLIC,
	E_CAL_COMPONENT_CLASS_PRIVATE,
	E_CAL_COMPONENT_CLASS_CONFIDENTIAL,
	E_CAL_COMPONENT_CLASS_UNKNOWN
} ECalComponentClassification;

/**
 * ECalComponentPeriodKind:
 * @E_CAL_COMPONENT_PERIOD_DATETIME: Date and time
 * @E_CAL_COMPONENT_PERIOD_DURATION: Duration
 *
 * Way in which a period of time is specified
 **/
typedef enum {
	E_CAL_COMPONENT_PERIOD_DATETIME,
	E_CAL_COMPONENT_PERIOD_DURATION
} ECalComponentPeriodKind;

/**
 * ECalComponentRangeKind:
 * @E_CAL_COMPONENT_RANGE_SINGLE: Single
 * @E_CAL_COMPONENT_RANGE_THISPRIOR: This and prior
 * @E_CAL_COMPONENT_RANGE_THISFUTURE: This and future
 *
 * The kind of range
 **/
typedef enum {
	E_CAL_COMPONENT_RANGE_SINGLE,
	E_CAL_COMPONENT_RANGE_THISPRIOR,
	E_CAL_COMPONENT_RANGE_THISFUTURE
} ECalComponentRangeKind;

/**
 * ECalComponentTransparency:
 * @E_CAL_COMPONENT_TRANSP_NONE: None
 * @E_CAL_COMPONENT_TRANSP_TRANSPARENT: Transparent
 * @E_CAL_COMPONENT_TRANSP_OPAQUE: Opaque
 * @E_CAL_COMPONENT_TRANSP_UNKNOWN: Unknown
 *
 * Time transparency
 **/
typedef enum {
	E_CAL_COMPONENT_TRANSP_NONE,
	E_CAL_COMPONENT_TRANSP_TRANSPARENT,
	E_CAL_COMPONENT_TRANSP_OPAQUE,
	E_CAL_COMPONENT_TRANSP_UNKNOWN
} ECalComponentTransparency;

/**
 * ECalComponentAlarmAction:
 * @E_CAL_COMPONENT_ALARM_NONE: None
 * @E_CAL_COMPONENT_ALARM_AUDIO: Audio
 * @E_CAL_COMPONENT_ALARM_DISPLAY: Display message
 * @E_CAL_COMPONENT_ALARM_EMAIL: Email
 * @E_CAL_COMPONENT_ALARM_PROCEDURE: Procedure
 * @E_CAL_COMPONENT_ALARM_UNKNOWN: Unknown
 *
 * Alarm types
 **/
typedef enum {
	E_CAL_COMPONENT_ALARM_NONE,
	E_CAL_COMPONENT_ALARM_AUDIO,
	E_CAL_COMPONENT_ALARM_DISPLAY,
	E_CAL_COMPONENT_ALARM_EMAIL,
	E_CAL_COMPONENT_ALARM_PROCEDURE,
	E_CAL_COMPONENT_ALARM_UNKNOWN
} ECalComponentAlarmAction;

/**
 * ECalComponentAlarmTriggerkind:
 * @E_CAL_COMPONENT_ALARM_TRIGGER_NONE: None
 * @E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START: Relative to the start
 * @E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END: Relative to the end
 * @E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE: Absolute
 *
 * Whether a trigger is relative to the start or end of an event occurrence, or
 * whether it is specified to occur at an absolute time.
 */
typedef enum {
	E_CAL_COMPONENT_ALARM_TRIGGER_NONE,
	E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_START,
	E_CAL_COMPONENT_ALARM_TRIGGER_RELATIVE_END,
	E_CAL_COMPONENT_ALARM_TRIGGER_ABSOLUTE
} ECalComponentAlarmTriggerKind;

G_END_DECLS

#endif /* E_CAL_ENUMS_H */
