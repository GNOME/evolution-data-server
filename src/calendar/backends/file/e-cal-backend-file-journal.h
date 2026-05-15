/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Rodrigo Moya <rodrigo@novell.com>
 */

#ifndef E_CAL_BACKEND_FILE_JOURNAL_H
#define E_CAL_BACKEND_FILE_JOURNAL_H

#include "e-cal-backend-file.h"

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_FILE_JOURNAL \
	(e_cal_backend_file_journal_get_type ())
#define E_CAL_BACKEND_FILE_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_FILE_JOURNAL, ECalBackendFileJournal))
#define E_CAL_BACKEND_FILE_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_FILE_JOURNAL, ECalBackendFileJournalClass))
#define E_IS_CAL_BACKEND_FILE_JOURNAL(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_FILE_JOURNAL))
#define E_IS_CAL_BACKEND_FILE_JOURNAL_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_FILE_JOURNAL))
#define E_CAL_BACKEND_FILE_JOURNAL_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_FILE_JOURNAL, ECalBackendFileJournalClass))

G_BEGIN_DECLS

typedef ECalBackendFile ECalBackendFileJournal;
typedef ECalBackendFileClass ECalBackendFileJournalClass;

GType		e_cal_backend_file_journal_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_FILE_JOURNAL_H */
