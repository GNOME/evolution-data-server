/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *
 * Author :
 *  Bertrand Guiheneuf <bertrand@helixcode.com>
 *
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
 * USA
 */

#ifndef CAMEL_H
#define CAMEL_H 1

#include <camel/camel-address.h>
#include <camel/camel-arg.h>
#include <camel/camel-block-file.h>
#include <camel/camel-certdb.h>
#include <camel/camel-charset-map.h>
#include <camel/camel-cipher-context.h>
#include <camel/camel-data-cache.h>
#include <camel/camel-data-wrapper.h>
#include <camel/camel-digest-folder.h>
#include <camel/camel-digest-store.h>
#include <camel/camel-digest-summary.h>
#include <camel/camel-disco-diary.h>
#include <camel/camel-disco-folder.h>
#include <camel/camel-disco-store.h>
#include <camel/camel-exception.h>
#include <camel/camel-file-utils.h>
#include <camel/camel-filter-driver.h>
#include <camel/camel-filter-search.h>
#include <camel/camel-folder.h>
#include <camel/camel-folder-search.h>
#include <camel/camel-folder-summary.h>
#include <camel/camel-folder-thread.h>
#include <camel/camel-gpg-context.h>
#include <camel/camel-html-parser.h>
#include <camel/camel-http-stream.h>
#include <camel/camel-iconv.h>
#include <camel/camel-index.h>
#include <camel/camel-internet-address.h>
#include <camel/camel-junk-plugin.h>
#include <camel/camel-list-utils.h>
#include <camel/camel-lock.h>
#include <camel/camel-lock-client.h>
#include <camel/camel-lock-helper.h>
#include <camel/camel-medium.h>
#include <camel/camel-mime-filter.h>
#include <camel/camel-mime-filter-basic.h>
#include <camel/camel-mime-filter-bestenc.h>
#include <camel/camel-mime-filter-canon.h>
#include <camel/camel-mime-filter-charset.h>
#include <camel/camel-mime-filter-crlf.h>
#include <camel/camel-mime-filter-enriched.h>
#include <camel/camel-mime-filter-from.h>
#include <camel/camel-mime-filter-gzip.h>
#include <camel/camel-mime-filter-html.h>
#include <camel/camel-mime-filter-index.h>
#include <camel/camel-mime-filter-linewrap.h>
#include <camel/camel-mime-filter-pgp.h>
#include <camel/camel-mime-filter-save.h>
#include <camel/camel-mime-filter-tohtml.h>
#include <camel/camel-mime-filter-yenc.h>
#include <camel/camel-mime-message.h>
#include <camel/camel-mime-parser.h>
#include <camel/camel-mime-part.h>
#include <camel/camel-mime-part-utils.h>
#include <camel/camel-mime-utils.h>
#include <camel/camel-movemail.h>
#include <camel/camel-msgport.h>
#include <camel/camel-multipart.h>
#include <camel/camel-multipart-encrypted.h>
#include <camel/camel-multipart-signed.h>
#include <camel/camel-net-utils.h>
#include <camel/camel-news-address.h>
#include <camel/camel-nntp-address.h>
#include <camel/camel-object.h>
#include <camel/camel-offline-folder.h>
#include <camel/camel-offline-journal.h>
#include <camel/camel-offline-store.h>
#include <camel/camel-operation.h>
#include <camel/camel-partition-table.h>
#include <camel/camel-process.h>
#include <camel/camel-provider.h>
#include <camel/camel-sasl.h>
#include <camel/camel-sasl-anonymous.h>
#include <camel/camel-sasl-cram-md5.h>
#include <camel/camel-sasl-digest-md5.h>
#include <camel/camel-sasl-gssapi.h>
#include <camel/camel-sasl-login.h>
#include <camel/camel-sasl-ntlm.h>
#include <camel/camel-sasl-plain.h>
#include <camel/camel-sasl-popb4smtp.h>
#include <camel/camel-seekable-stream.h>
#include <camel/camel-seekable-substream.h>
#include <camel/camel-service.h>
#include <camel/camel-session.h>
#include <camel/camel-smime-context.h>
#include <camel/camel-store.h>
#include <camel/camel-store-summary.h>
#include <camel/camel-stream.h>
#include <camel/camel-stream-buffer.h>
#include <camel/camel-stream-filter.h>
#include <camel/camel-stream-fs.h>
#include <camel/camel-stream-mem.h>
#include <camel/camel-stream-null.h>
#include <camel/camel-stream-process.h>
#include <camel/camel-stream-vfs.h>
#include <camel/camel-string-utils.h>
#include <camel/camel-tcp-stream.h>
#include <camel/camel-tcp-stream-raw.h>
#include <camel/camel-tcp-stream-ssl.h>
#include <camel/camel-text-index.h>
#include <camel/camel-transport.h>
#include <camel/camel-trie.h>
#include <camel/camel-types.h>
#include <camel/camel-uid-cache.h>
#include <camel/camel-url.h>
#include <camel/camel-url-scanner.h>
#include <camel/camel-utf8.h>
#include <camel/camel-vee-folder.h>
#include <camel/camel-vee-store.h>
#include <camel/camel-vee-summary.h>
#include <camel/camel-vtrash-folder.h>

#include <glib.h>

G_BEGIN_DECLS

gint camel_init (const gchar *certdb_dir, gboolean nss_init);
void camel_shutdown (void);

G_END_DECLS

#endif /* CAMEL_H */
