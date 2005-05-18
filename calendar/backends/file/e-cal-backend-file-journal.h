/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 2005 Novell, Inc.
 *
 * Author: Rodrigo Moya <rodrigo@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef E_CAL_BACKEND_FILE_JOURNAL_H
#define E_CAL_BACKEND_FILE_JOURNAL_H

#include "e-cal-backend-file.h"

#define E_TYPE_CAL_BACKEND_FILE_JOURNAL            (e_cal_backend_file_journal_get_type ())
#define E_CAL_BACKEND_FILE_JOURNAL(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_FILE_JOURNAL,		\
					  ECalBackendFileJournal))
#define E_CAL_BACKEND_FILE_JOURNAL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_FILE_JOURNAL,	\
					  ECalBackendFileJournalClass))
#define E_IS_CAL_BACKEND_FILE_JOURNAL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_FILE_JOURNAL))
#define E_IS_CAL_BACKEND_FILE_JOURNAL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_FILE_JOURNAL))

typedef struct _ECalBackendFileJournal ECalBackendFileJournal;
typedef struct _ECalBackendFileJournalClass ECalBackendFileJournalClass;

typedef struct _ECalBackendFileJournalPrivate ECalBackendFileJournalPrivate;

struct _ECalBackendFileJournal {
	ECalBackendFile backend;

	/* Private data */
	ECalBackendFileJournalPrivate *priv;
};

struct _ECalBackendFileJournalClass {
	ECalBackendFileClass parent_class;
};

GType e_cal_backend_file_journal_get_type (void);

#endif
