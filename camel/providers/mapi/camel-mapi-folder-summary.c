/*
 *  Copyright (C) Jean-Baptiste Arnoult 2007.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
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

#include "camel-mapi-folder-summary.h"

static void camel_imap4_summary_class_init (CamelIMAP4SummaryClass *klass);
static void camel_imap4_summary_init (CamelIMAP4Summary *summary, CamelIMAP4SummaryClass *klass);
static void camel_imap4_summary_finalize (CamelObject *object);

static int imap4_header_load (CamelFolderSummary *summary, FILE *fin);
static int imap4_header_save (CamelFolderSummary *summary, FILE *fout);
static CamelMessageInfo *imap4_message_info_new_from_header (CamelFolderSummary *summary, struct _camel_header_raw *header);
static CamelMessageInfo *imap4_message_info_load (CamelFolderSummary *summary, FILE *fin);
static int imap4_message_info_save (CamelFolderSummary *summary, FILE *fout, CamelMessageInfo *info);
static CamelMessageInfo *imap4_message_info_clone (CamelFolderSummary *summary, const CamelMessageInfo *mi);
static CamelMessageContentInfo *imap4_content_info_load (CamelFolderSummary *summary, FILE *in);
static int imap4_content_info_save (CamelFolderSummary *summary, FILE *out, CamelMessageContentInfo *info);

static CamelFolderSummaryClass *parent_class = NULL;


CamelType
camel_openchange_summary_get_type (void)
{
  static CamelType type = 0;
  
  if (!type) {
    type = camel_type_register (CAMEL_FOLDER_SUMMARY_TYPE,
				"CamelIMAP4Summary",
				sizeof (CamelOpenchangeSummary),
				sizeof (CamelOpenchangeSummaryClass),
				(CamelObjectClassInitFunc) camel_openchange_summary_class_init,
				NULL,
				(CamelObjectInitFunc) camel_openchange_summary_init,
				(CamelObjectFinalizeFunc) camel_openchange_summary_finalize);
  }
  
  return type;
}


static void
camel_openchange_summary_class_init (CamelOpenchangeSummaryClass *klass)
{
  CamelFolderSummaryClass *summary_class = (CamelFolderSummaryClass *) klass;
  
  parent_class = (CamelFolderSummaryClass *) camel_type_get_global_classfuncs (camel_folder_summary_get_type ());
}

static void
camel_openchange_summary_init (CamelOpenchangeSummary *summary, CamelOpenchangeSummaryClass *klass)
{
  CamelFolderSummary *folder_summary = (CamelFolderSummary *) summary;
  
  folder_summary->flags = CAMEL_MESSAGE_ANSWERED | CAMEL_MESSAGE_DELETED |
    CAMEL_MESSAGE_DRAFT | CAMEL_MESSAGE_FLAGGED | CAMEL_MESSAGE_SEEN;
  
  folder_summary->message_info_size = sizeof (CamelIMAP4MessageInfo);
  folder_summary->content_info_size = sizeof (CamelIMAP4MessageContentInfo);
  
  ((CamelFolderSummary *) summary)->flags |= CAMEL_IMAP4_SUMMARY_HAVE_MLIST;
  
  summary->update_flags = TRUE;
  summary->uidvalidity_changed = FALSE;
}
