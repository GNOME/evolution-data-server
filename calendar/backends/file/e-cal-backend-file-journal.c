/* Evolution calendar - iCalendar file backend for tasks
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * Authors: Rodrigo Moya <rodrigo@novell.com>
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

#include "e-cal-backend-file-journal.h"

G_DEFINE_TYPE (ECalBackendFileJournal, e_cal_backend_file_journal, E_TYPE_CAL_BACKEND_FILE)

struct _ECalBackendFileJournalPrivate {
	guint reserved;
};

static void e_cal_backend_file_journal_dispose (GObject *object);
static void e_cal_backend_file_journal_finalize (GObject *object);

static ECalBackendFileClass *parent_class;

/* Class initialization function for the journal file backend */
static void
e_cal_backend_file_journal_class_init (ECalBackendFileJournalClass *klass)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;

	object_class = G_OBJECT_CLASS (klass);
	backend_class = E_CAL_BACKEND_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->dispose = e_cal_backend_file_journal_dispose;
	object_class->finalize = e_cal_backend_file_journal_finalize;
}

/* Object initialization function for the journal file backend */
static void
e_cal_backend_file_journal_init (ECalBackendFileJournal *cbfile)
{
	ECalBackendFileJournalPrivate *priv;

	priv = g_new0 (ECalBackendFileJournalPrivate, 1);
	cbfile->priv = priv;

	e_cal_backend_file_set_file_name (E_CAL_BACKEND_FILE (cbfile), "journal.ics");
}

/* Dispose handler for the journal file backend */
static void
e_cal_backend_file_journal_dispose (GObject *object)
{
	ECalBackendFileJournal *cbfile;
	ECalBackendFileJournalPrivate *priv;

	cbfile = E_CAL_BACKEND_FILE_JOURNAL (object);
	priv = cbfile->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the journal file backend */
static void
e_cal_backend_file_journal_finalize (GObject *object)
{
	ECalBackendFileJournal *cbfile;
	ECalBackendFileJournalPrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_FILE_JOURNAL (object));

	cbfile = E_CAL_BACKEND_FILE_JOURNAL (object);
	priv = cbfile->priv;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}
