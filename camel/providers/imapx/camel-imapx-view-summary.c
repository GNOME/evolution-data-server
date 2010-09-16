/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8; fill-column: 160 -*-
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include "camel-record.h"
#include "camel-imapx-view-summary.h"
#include "camel-imapx-utils.h"

/* NB, this is only for the messy iterator_get interface, which could be better hidden */
#include "libdb/dist/db.h"

#define d(x) camel_imapx_debug(debug, x)
#define io(x) camel_imapx_debug(io, x)

#define CVSD_CLASS(x) ((CamelViewSummaryDiskClass *)((CamelObject *)x)->klass)
#define CVS_CLASS(x) ((CamelViewSummaryClass *)((CamelObject *)x)->klass)

static CamelViewSummaryDiskClass *cmvs_parent;

/*
 * camel_imapx_view_summary_new:
 *
 * Create a new CamelIMAPXViewSummary object.
 *
 * Returns: A new CamelIMAPXViewSummary widget.
 **/
CamelIMAPXViewSummary *
camel_imapx_view_summary_new(const gchar *base, CamelException *ex)
{
	return (CamelIMAPXViewSummary *)camel_view_summary_disk_construct(camel_object_new(camel_imapx_view_summary_get_type()), base, ex);
}

/* NB: must have write lock on folder */
guint32 camel_imapx_view_next_uid(CamelIMAPXView *view)
{
#if 0
	guint32 uid;

	uid = view->nextuid++;
	camel_view_changed((CamelView *)view);

	return uid;
#endif
}

/* NB: must have write lock on folder */
void camel_imapx_view_last_uid(CamelIMAPXView *view, guint32 uid)
{
#if 0
	uid++;
	if (uid > view->nextuid) {
		view->nextuid = uid;
		camel_view_changed((CamelView *)view);
	}
#endif
}

static gint
imapx_view_decode(CamelViewSummaryDisk *s, CamelView *view, CamelRecordDecoder *crd)
{
	gint tag, ver;

	((CamelViewSummaryDiskClass *)cmvs_parent)->decode(s, view, crd);

	if (strchr(view->vid, 1) == NULL) {
		camel_record_decoder_reset(crd);
		while ((tag = camel_record_decoder_next_section(crd, &ver)) != CR_SECTION_INVALID) {
			switch (tag) {
			case CVS_IMAPX_SECTION_VIEWINFO:
				((CamelIMAPXView *)view)->uidvalidity = camel_record_decoder_int32(crd);
				((CamelIMAPXView *)view)->permanentflags = camel_record_decoder_int32(crd);
				((CamelIMAPXView *)view)->exists = camel_record_decoder_int32(crd);
				((CamelIMAPXView *)view)->separator = camel_record_decoder_int8(crd);
				((CamelIMAPXView *)view)->raw_name = g_strdup(camel_record_decoder_string(crd));
				break;
			}
		}
	}

	return 0;
}

static void
imapx_view_encode(CamelViewSummaryDisk *s, CamelView *view, CamelRecordEncoder *cre)
{
	((CamelViewSummaryDiskClass *)cmvs_parent)->encode(s, view, cre);

	/* We only store extra data on the root view */

	if (strchr(view->vid, 1) == NULL) {
		camel_record_encoder_start_section(cre, CVS_IMAPX_SECTION_VIEWINFO, 0);
		camel_record_encoder_int32(cre, ((CamelIMAPXView *)view)->uidvalidity);
		camel_record_encoder_int32(cre, ((CamelIMAPXView *)view)->permanentflags);
		camel_record_encoder_int32(cre, ((CamelIMAPXView *)view)->exists);
		camel_record_encoder_int8(cre, ((CamelIMAPXView *)view)->separator);
		camel_record_encoder_string(cre, ((CamelIMAPXView *)view)->raw_name);
		camel_record_encoder_end_section(cre);
	}
}

static void
camel_imapx_view_summary_init(CamelIMAPXViewSummary *obj)
{
	struct _CamelFolderSummary *s = (CamelFolderSummary *)obj;

	s = s;
}

static void
camel_imapx_view_summary_finalise(CamelObject *obj)
{
	/*CamelIMAPXViewSummary *mbs = CAMEL_IMAPX_VIEW_SUMMARY(obj);*/
}

static void
camel_imapx_view_summary_class_init(CamelIMAPXViewSummaryClass *klass)
{
	((CamelViewSummaryClass *)klass)->view_sizeof = sizeof(CamelIMAPXView);

	((CamelViewSummaryDiskClass *)klass)->encode = imapx_view_encode;
	((CamelViewSummaryDiskClass *)klass)->decode = imapx_view_decode;
}

CamelType
camel_imapx_view_summary_get_type(void)
{
	static CamelType type = CAMEL_INVALID_TYPE;

	if (type == CAMEL_INVALID_TYPE) {
		cmvs_parent = (CamelViewSummaryDiskClass *)camel_view_summary_disk_get_type();
		type = camel_type_register((CamelType)cmvs_parent, "CamelIMAPXViewSummary",
					   sizeof (CamelIMAPXViewSummary),
					   sizeof (CamelIMAPXViewSummaryClass),
					   (CamelObjectClassInitFunc) camel_imapx_view_summary_class_init,
					   NULL,
					   (CamelObjectInitFunc) camel_imapx_view_summary_init,
					   (CamelObjectFinalizeFunc) camel_imapx_view_summary_finalise);
	}

	return type;
}
