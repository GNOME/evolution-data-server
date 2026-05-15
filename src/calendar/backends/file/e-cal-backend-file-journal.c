/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@novell.com>
 */

#include "e-cal-backend-file-journal.h"

G_DEFINE_TYPE (
	ECalBackendFileJournal,
	e_cal_backend_file_journal,
	E_TYPE_CAL_BACKEND_FILE)

static void
e_cal_backend_file_journal_class_init (ECalBackendFileJournalClass *class)
{
}

static void
e_cal_backend_file_journal_init (ECalBackendFileJournal *cbfile)
{
	e_cal_backend_file_set_file_name (
		E_CAL_BACKEND_FILE (cbfile), "journal.ics");
}

