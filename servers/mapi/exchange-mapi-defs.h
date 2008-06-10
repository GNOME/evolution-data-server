/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Authors:
 *  	Suman Manjunath <msuman@novell.com>
 *  Copyright (C) 2008 Novell, Inc.
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


/* Someday, all these definitions should be pushed in libmapi/mapidefs.h */
/* NOTE: Some of the enumerations are commented out since they conflict with libmapi/mapidefs.h */

#ifndef EXCHANGE_MAPI_DEFS_H
#define EXCHANGE_MAPI_DEFS_H

G_BEGIN_DECLS

/* GENERAL */
typedef enum 
{
    olSunday = 1,
    olMonday = 2,
    olTuesday = 4,
    olWednesday = 8,
    olThursday = 16,
    olFriday = 32,
    olSaturday = 64
} OlDaysOfWeek;
 
typedef enum 
{
    olNormal = 0,
    olPersonal = 1,
    olPrivate = 2,
    olConfidential = 3
} OlSensitivity;
 
typedef enum 
{
    olImportanceLow = 0,
    olImportanceNormal = 1,
    olImportanceHigh = 2
} OlImportance;
 
typedef enum 
{
    olOriginator = 0, 
    olTo = 1, 
    olCC = 2, 
    olBCC = 3
} OlMailRecipientType;
 
/* APPOINTMENTS */
 /*
  * Appointment flags with PR_APPOINTMENT_BUSY_STATUS
  */
typedef enum 
{
    olFree = 0,
    olTentative = 1,
    olBusy = 2,
    olOutOfOffice = 3
} OlBusyStatus;
 
typedef enum 
{
    olOrganizer = 0,
    olRequired = 1,
    olOptional = 2,
    olResource = 3
} OlMeetingRecipientType;
 
typedef enum 
{
    olMeetingTentative = 2,
    olMeetingAccepted = 3,
    olMeetingDeclined = 4
} OlMeetingResponse;
 
typedef enum  
{
    olResponseNone = 0, 
    olResponseOrganized = 1, 
    olResponseTentative = 2, 
    olResponseAccepted = 3, 
    olResponseDeclined = 4, 
    olResponseNotResponded = 5
} OlResponseStatus;
 
typedef enum 
{
    olNonMeeting = 0,
    olMeeting = 1,
    olMeetingReceived = 3,
    olMeetingCanceled = 5
} OlMeetingStatus;
 
typedef enum 
{
    olNetMeeting = 0,
    olNetShow = 1,
    olChat = 2
} OlNetMeetingType;
 
/* TASKS */
typedef enum 
{
    olTaskNotDelegated = 0,
    olTaskDelegationUnknown = 1,
    olTaskDelegationAccepted = 2,
    olTaskDelegationDeclined = 3
} OlTaskDelegationState;

#if 0
typedef enum 
{
    olNewTask = 0,
    olDelegatedTask = 1,
    olOwnTask = 2
} OlTaskOwnership;
#endif 

typedef enum 
{
    olUpdate = 2,
    olFinalStatus = 3
} OlTaskRecipientType;

typedef enum 
{
    olTaskSimple = 0,
    olTaskAssign = 1,
    olTaskAccept = 2,
    olTaskDecline = 3
} OlTaskResponse;

#if 0
typedef enum 
{
    olTaskNotStarted = 0,
    olTaskInProgress = 1,
    olTaskComplete = 2,
    olTaskWaiting = 3,
    olTaskDeferred = 4
} OlTaskStatus;
#endif


/* NOTES */
#if 0
typedef enum  
{
    olBlue = 0, 
    olGreen = 1, 
    olPink = 2, 
    olYellow = 3, 
    olWhite = 4
} OlNoteColor;
#endif

/* RECURRENCE (APPOINTMENTS/MEETINGS/TASKS) */
typedef enum 
{
    olApptNotRecurring = 0,
    olApptMaster = 1,
    olApptOccurrence = 2,
    olApptException = 3
} OlRecurrenceState;

typedef enum 
{
    olRecursDaily = 0,
    olRecursWeekly = 1,
    olRecursMonthly = 2,
    olRecursMonthNth = 3,
    olRecursYearly = 5,
    olRecursYearNth = 6
} OlRecurrenceType;


/*
 * PR_MESSAGE_EDITOR_FORMAT type
 */
typedef enum 
{
    olEditorText = 1,
    olEditorHTML = 2,
    olEditorRTF = 3,
    olEditorWord = 4
} OlEditorType;
 
/*
 * Default folders
 */
#if 0
typedef enum  {
    olFolderDeletedItems = 3,
    olFolderOutbox = 4,
    olFolderSentMail = 5,
    olFolderInbox = 6,
    olFolderCalendar = 9,
    olFolderContacts = 10,
    olFolderJournal = 11,
    olFolderNotes = 12,
    olFolderTasks = 13,
    olFolderDrafts = 16,
    olPublicFoldersAllPublicFolders = 18,
    olFolderConflicts = 19,
    olFolderSyncIssues = 20,
    olFolderLocalFailures = 21,
    olFolderServerFailures = 22,
    olFolderJunk = 23,
    olFolderRssFeeds = 25,
    olFolderToDo = 28,
    olFolderManagedEmail = 29
} OlDefaultFolders;

#define	olFolderTopInformationStore	1
#define	olFolderCommonView		8
#define	olFolderFinder			24
#define	olFolderPublicRoot		25
#define	olFolderPublicIPMSubtree	26
#endif

/*
 * Priority
 */

#define	PRIORITY_LOW 	-1
#define	PRIORITY_NORMAL 0
#define	PRIORITY_HIGH 	1

G_END_DECLS

#endif

