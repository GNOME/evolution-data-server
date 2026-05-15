/*
 * SPDX-FileCopyrightText: (C) 1999-2008 Novell, Inc. (www.novell.com)
 * SPDX-License-Identifier: LGPL-2.0-or-later
 * SPDX-FileContributor: Michael Zucchi <notzed@ximian.com>
 * SPDX-FileContributor: Dan Winship <danw@ximian.com>
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_VEE_SUMMARY_H
#define CAMEL_VEE_SUMMARY_H

#include <camel/camel-folder-summary.h>
#include <camel/camel-store-search.h>
#include <camel/camel-vee-message-info.h>

/* Standard GObject macros */
#define CAMEL_TYPE_VEE_SUMMARY \
	(camel_vee_summary_get_type ())
#define CAMEL_VEE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_VEE_SUMMARY, CamelVeeSummary))
#define CAMEL_VEE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_VEE_SUMMARY, CamelVeeSummaryClass))
#define CAMEL_IS_VEE_SUMMARY(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_VEE_SUMMARY))
#define CAMEL_IS_VEE_SUMMARY_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_VEE_SUMMARY))
#define CAMEL_VEE_SUMMARY_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_VEE_SUMMARY, CamelVeeSummaryClass))

G_BEGIN_DECLS

struct _CamelVeeMessageInfoData;
struct _CamelVeeFolder;
struct _CamelFolder;

typedef struct _CamelVeeSummary CamelVeeSummary;
typedef struct _CamelVeeSummaryClass CamelVeeSummaryClass;
typedef struct _CamelVeeSummaryPrivate CamelVeeSummaryPrivate;

struct _CamelVeeSummary {
	CamelFolderSummary parent;

	CamelVeeSummaryPrivate *priv;
};

struct _CamelVeeSummaryClass {
	CamelFolderSummaryClass parent_class;

	/* Padding for future expansion */
	gpointer reserved[20];
};

GType		camel_vee_summary_get_type	(void);
CamelFolderSummary *
		camel_vee_summary_new		(CamelFolder *parent);
CamelVeeMessageInfo *
		camel_vee_summary_add		(CamelVeeSummary *summary,
						 CamelFolder *subfolder,
						 const gchar *vuid);
void		camel_vee_summary_remove	(CamelVeeSummary *summary,
						 CamelFolder *subfolder,
						 const gchar *vuid);
gboolean	camel_vee_summary_replace_flags	(CamelVeeSummary *vsummary,
						 const gchar *vuid);
GHashTable *	camel_vee_summary_get_uids_for_subfolder
						(CamelVeeSummary *summary,
						 CamelFolder *subfolder);
CamelStoreSearchIndex *
		camel_vee_summary_to_match_index(CamelVeeSummary *self);
GHashTable * /* CamelFolder *~>NULL */
		camel_vee_summary_dup_subfolders(CamelVeeSummary *self);

G_END_DECLS

#endif /* CAMEL_VEE_SUMMARY_H */

