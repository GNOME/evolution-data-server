/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2007 Novell, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */



#ifndef E_CAL_BACKEND_MAPI_CONSTANTS_H
#define E_CAL_BACKEND_MAPI_CONSTANTS_H

G_BEGIN_DECLS

/* GENERAL */
enum OlDaysOfWeek
{
    olSunday = 1,
    olMonday = 2,
    olTuesday = 4,
    olWednesday = 8,
    olThursday = 16,
    olFriday = 32,
    olSaturday = 64
};

enum OlSensitivity
{
    olNormal = 0,
    olPersonal = 1,
    olPrivate = 2,
    olConfidential = 3
};

enum OlImportance
{
    olImportanceLow = 0,
    olImportanceNormal = 1,
    olImportanceHigh = 2
};

/* APPOINTMENTS */
enum OlBusyStatus
{
    olFree = 0,
    olTentative = 1,
    olBusy = 2,
    olOutOfOffice = 3
};

enum OlMeetingRecipientType
{
    olOrganizer = 0,
    olRequired = 1,
    olOptional = 2,
    olResource = 3
};

enum OlMeetingResponse
{
    olMeetingTentative = 2,
    olMeetingAccepted = 3,
    olMeetingDeclined = 4
};

enum OlMeetingStatus
{
    olNonMeeting = 0,
    olMeeting = 1,
    olMeetingReceived = 3,
    olMeetingCanceled = 5
};

/* TASKS */
enum OlTaskDelegationState
{
    olTaskNotDelegated = 0,
    olTaskDelegationUnknown = 1,
    olTaskDelegationAccepted = 2,
    olTaskDelegationDeclined = 3
};
/*
enum OlTaskOwnership
{
    olNewTask = 0,
    olDelegatedTask = 1,
    olOwnTask = 2
};
*/
enum OlTaskRecipientType
{
    olUpdate = 2,
    olFinalStatus = 3
};

enum OlTaskResponse
{
    olTaskSimple = 0,
    olTaskAssign = 1,
    olTaskAccept = 2,
    olTaskDecline = 3
};
/*
enum OlTaskStatus
{
    olTaskNotStarted = 0,
    olTaskInProgress = 1,
    olTaskComplete = 2,
    olTaskWaiting = 3,
    olTaskDeferred = 4
};
*/
/* RECURRENCE */
enum OlRecurrenceState
{
    olApptNotRecurring = 0,
    olApptMaster = 1,
    olApptOccurrence = 2,
    olApptException = 3
};

enum OlRecurrenceType
{
    olRecursDaily = 0,
    olRecursWeekly = 1,
    olRecursMonthly = 2,
    olRecursMonthNth = 3,
    olRecursYearly = 5,
    olRecursYearNth = 6
};

G_END_DECLS

#endif
