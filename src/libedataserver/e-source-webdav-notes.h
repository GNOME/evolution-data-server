/*
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

#if !defined (__LIBEDATASERVER_H_INSIDE__) && !defined (LIBEDATASERVER_COMPILATION)
#error "Only <libedataserver/libedataserver.h> should be included directly."
#endif

#ifndef E_SOURCE_WEBDAV_NOTES_H
#define E_SOURCE_WEBDAV_NOTES_H

#include <libedataserver/e-source-extension.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_WEBDAV_NOTES \
	(e_source_webdav_notes_get_type ())
#define E_SOURCE_WEBDAV_NOTES(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_WEBDAV_NOTES, ESourceWebDAVNotes))
#define E_SOURCE_WEBDAV_NOTES_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_WEBDAV_NOTES, ESourceWebDAVNotesClass))
#define E_IS_SOURCE_WEBDAV_NOTES(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_WEBDAV_NOTES))
#define E_IS_SOURCE_WEBDAV_NOTES_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_WEBDAV_NOTES))
#define E_SOURCE_WEBDAV_NOTES_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_WEBDAV_NOTES, ESourceWebDAVNotesClass))

/**
 * E_SOURCE_EXTENSION_WEBDAV_NOTES:
 *
 * Pass this extension name to e_source_get_extension() to access
 * #ESourceWebDAVNotes.  This is also used as a group name in key files.
 *
 * Since: 3.44
 **/
#define E_SOURCE_EXTENSION_WEBDAV_NOTES "WebDAV Notes"

G_BEGIN_DECLS

typedef struct _ESourceWebDAVNotes ESourceWebDAVNotes;
typedef struct _ESourceWebDAVNotesClass ESourceWebDAVNotesClass;
typedef struct _ESourceWebDAVNotesPrivate ESourceWebDAVNotesPrivate;

/**
 * ESourceWebDAVNotes:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.44
 **/
struct _ESourceWebDAVNotes {
	/*< private >*/
	ESourceExtension parent;
	ESourceWebDAVNotesPrivate *priv;
};

struct _ESourceWebDAVNotesClass {
	ESourceExtensionClass parent_class;
};

GType		e_source_webdav_notes_get_type	(void) G_GNUC_CONST;
const gchar *	e_source_webdav_notes_get_default_ext
						(ESourceWebDAVNotes *extension);
gchar *		e_source_webdav_notes_dup_default_ext
						(ESourceWebDAVNotes *extension);
void		e_source_webdav_notes_set_default_ext
						(ESourceWebDAVNotes *extension,
						 const gchar *default_ext);

G_END_DECLS

#endif /* E_SOURCE_WEBDAV_NOTES_H */
