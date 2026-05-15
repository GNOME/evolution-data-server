/*
 * SPDX-FileCopyrightText: (C) 2020 Red Hat (www.redhat.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef E_CAL_BACKEND_WEBDAV_NOTES_H
#define E_CAL_BACKEND_WEBDAV_NOTES_H

#include <libedata-cal/libedata-cal.h>

/* Standard GObject macros */
#define E_TYPE_CAL_BACKEND_WEBDAV_NOTES \
	(e_cal_backend_webdav_notes_get_type ())
#define E_CAL_BACKEND_WEBDAV_NOTES(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_CAL_BACKEND_WEBDAV_NOTES, ECalBackendWebDAVNotes))
#define E_CAL_BACKEND_WEBDAV_NOTES_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_CAL_BACKEND_WEBDAV_NOTES, ECalBackendWebDAVNotesClass))
#define E_IS_CAL_BACKEND_WEBDAV_NOTES(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_CAL_BACKEND_WEBDAV_NOTES))
#define E_IS_CAL_BACKEND_WEBDAV_NOTES_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_CAL_BACKEND_WEBDAV_NOTES))
#define E_CAL_BACKEND_WEBDAV_NOTES_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_CAL_BACKEND_WEBDAV_NOTES, ECalBackendWebDAVNotesClass))

G_BEGIN_DECLS

typedef struct _ECalBackendWebDAVNotes ECalBackendWebDAVNotes;
typedef struct _ECalBackendWebDAVNotesClass ECalBackendWebDAVNotesClass;
typedef struct _ECalBackendWebDAVNotesPrivate ECalBackendWebDAVNotesPrivate;

struct _ECalBackendWebDAVNotes {
	ECalMetaBackend parent;
	ECalBackendWebDAVNotesPrivate *priv;
};

struct _ECalBackendWebDAVNotesClass {
	ECalMetaBackendClass parent_class;
};

GType		e_cal_backend_webdav_notes_get_type	(void);

G_END_DECLS

#endif /* E_CAL_BACKEND_WEBDAV_NOTES_H */
