/*
 * camel-enums.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#if !defined (__CAMEL_H_INSIDE__) && !defined (CAMEL_COMPILATION)
#error "Only <camel/camel.h> can be included directly."
#endif

#ifndef CAMEL_ENUMS_H
#define CAMEL_ENUMS_H

typedef enum { /*< flags >*/
	CAMEL_FOLDER_HAS_SUMMARY_CAPABILITY = 1 << 0,
	CAMEL_FOLDER_HAS_SEARCH_CAPABILITY  = 1 << 1,
	CAMEL_FOLDER_FILTER_RECENT          = 1 << 2,
	CAMEL_FOLDER_HAS_BEEN_DELETED       = 1 << 3,
	CAMEL_FOLDER_IS_TRASH               = 1 << 4,
	CAMEL_FOLDER_IS_JUNK                = 1 << 5,
	CAMEL_FOLDER_FILTER_JUNK            = 1 << 6
} CamelFolderFlags;

#define CAMEL_FOLDER_TYPE_BIT (10)

/**
 * CamelFolderInfoFlags:
 * @CAMEL_FOLDER_NOSELECT:
 *    The folder cannot contain messages.
 * @CAMEL_FOLDER_NOINFERIORS:
 *    The folder cannot have child folders.
 * @CAMEL_FOLDER_CHILDREN:
 *    The folder has children (not yet fully implemented).
 * @CAMEL_FOLDER_NOCHILDREN:
 *    The folder does not have children (not yet fully implemented).
 * @CAMEL_FOLDER_SUBSCRIBED:
 *    The folder is subscribed.
 * @CAMEL_FOLDER_VIRTUAL:
 *    The folder is virtual.  Messages cannot be copied or moved to
 *    virtual folders since they are only queries of other folders.
 * @CAMEL_FOLDER_SYSTEM:
 *    The folder is a built-in "system" folder.  System folders
 *    cannot be renamed or deleted.
 * @CAMEL_FOLDER_VTRASH:
 *    The folder is a virtual trash folder.  It cannot be copied to,
 *    and can only be moved to if in an existing folder.
 * @CAMEL_FOLDER_SHARED_TO_ME:
 *    A folder being shared by someone else.
 * @CAMEL_FOLDER_SHARED_BY_ME:
 *    A folder being shared by the user.
 * @CAMEL_FOLDER_TYPE_NORMAL:
 *    The folder is a normal folder.
 * @CAMEL_FOLDER_TYPE_INBOX:
 *    The folder is an inbox folder.
 * @CAMEL_FOLDER_TYPE_OUTBOX:
 *    The folder is an outbox folder.
 * @CAMEL_FOLDER_TYPE_TRASH:
 *    The folder shows deleted messages.
 * @CAMEL_FOLDER_TYPE_JUNK:
 *    The folder shows junk messages.
 * @CAMEL_FOLDER_TYPE_SENT:
 *    The folder shows sent messages.
 *
 * These flags are abstractions.  It's up to the CamelProvider to give
 * them suitable interpretations.  Use #CAMEL_FOLDER_TYPE_MASK to isolate
 * the folder's type.
 **/
typedef enum { /*< flags >*/
	CAMEL_FOLDER_NOSELECT     = 1 << 0,
	CAMEL_FOLDER_NOINFERIORS  = 1 << 1,
	CAMEL_FOLDER_CHILDREN     = 1 << 2,
	CAMEL_FOLDER_NOCHILDREN   = 1 << 3,
	CAMEL_FOLDER_SUBSCRIBED   = 1 << 4,
	CAMEL_FOLDER_VIRTUAL      = 1 << 5,
	CAMEL_FOLDER_SYSTEM       = 1 << 6,
	CAMEL_FOLDER_VTRASH       = 1 << 7,
	CAMEL_FOLDER_SHARED_TO_ME = 1 << 8,
	CAMEL_FOLDER_SHARED_BY_ME = 1 << 9,
	CAMEL_FOLDER_TYPE_NORMAL  = 0 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_INBOX   = 1 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_OUTBOX  = 2 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_TRASH   = 3 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_JUNK    = 4 << CAMEL_FOLDER_TYPE_BIT,
	CAMEL_FOLDER_TYPE_SENT    = 5 << CAMEL_FOLDER_TYPE_BIT
} CamelFolderInfoFlags;

#define CAMEL_FOLDER_TYPE_MASK (63 << CAMEL_FOLDER_TYPE_BIT)

typedef enum {
	CAMEL_JUNK_STATUS_INCONCLUSIVE,
	CAMEL_JUNK_STATUS_MESSAGE_IS_JUNK,
	CAMEL_JUNK_STATUS_MESSAGE_IS_NOT_JUNK
} CamelJunkStatus;

typedef enum {
	CAMEL_MIME_FILTER_BASIC_INVALID,
	CAMEL_MIME_FILTER_BASIC_BASE64_ENC,
	CAMEL_MIME_FILTER_BASIC_BASE64_DEC,
	CAMEL_MIME_FILTER_BASIC_QP_ENC,
	CAMEL_MIME_FILTER_BASIC_QP_DEC,
	CAMEL_MIME_FILTER_BASIC_UU_ENC,
	CAMEL_MIME_FILTER_BASIC_UU_DEC
} CamelMimeFilterBasicType;

typedef enum {
	CAMEL_MIME_FILTER_CRLF_ENCODE,
	CAMEL_MIME_FILTER_CRLF_DECODE
} CamelMimeFilterCRLFDirection;

typedef enum {
	CAMEL_MIME_FILTER_CRLF_MODE_CRLF_DOTS,
	CAMEL_MIME_FILTER_CRLF_MODE_CRLF_ONLY
} CamelMimeFilterCRLFMode;

typedef enum {
	CAMEL_MIME_FILTER_GZIP_MODE_ZIP,
	CAMEL_MIME_FILTER_GZIP_MODE_UNZIP
} CamelMimeFilterGZipMode;

typedef enum {
	CAMEL_MIME_FILTER_YENC_DIRECTION_ENCODE,
	CAMEL_MIME_FILTER_YENC_DIRECTION_DECODE
} CamelMimeFilterYencDirection;

typedef enum {
	CAMEL_PROVIDER_CONF_END,
	CAMEL_PROVIDER_CONF_SECTION_START,
	CAMEL_PROVIDER_CONF_SECTION_END,
	CAMEL_PROVIDER_CONF_CHECKBOX,
	CAMEL_PROVIDER_CONF_CHECKSPIN,
	CAMEL_PROVIDER_CONF_ENTRY,
	CAMEL_PROVIDER_CONF_LABEL,
	CAMEL_PROVIDER_CONF_HIDDEN,
	CAMEL_PROVIDER_CONF_OPTIONS
} CamelProviderConfType;

/* CamelProviderFlags;
 * @CAMEL_PROVIDER_IS_REMOTE:
 *   Provider works with remote data.
 * @CAMEL_PROVIDER_IS_LOCAL:
 *   Provider can be used as a backend for local folder tree folders.
 *   (Not just the opposite of #CAMEL_PROVIDER_IS_REMOTE.)
 * @CAMEL_PROVIDER_IS_SOURCE:
 *   Mail arrives there, so it should be offered as an option in the
 *   mail config dialog.
 * @CAMEL_PROVIDER_IS_STORAGE:
 *   Mail is stored there.  It will appear in the folder tree.
 * @CAMEL_PROVIDER_IS_EXTERNAL:
 *   Provider appears in the folder tree but is not created by the
 *   mail component.
 * @CAMEL_PROVIDER_HAS_LICENSE:
 *   Provider configuration first needs the license to be accepted.
 *   (No longer used.)
 * @CAMEL_PROVIDER_ALLOW_REAL_TRASH_FOLDER:
 *   Provider may use a real trash folder instead of a virtual folder.
 * @CAMEL_PROVIDER_ALLOW_REAL_JUNK_FOLDER:
 *   Provider may use a real junk folder instead of a virtual folder.
 */
typedef enum { /*< flags >*/
	CAMEL_PROVIDER_IS_REMOTE               = 1 << 0,
	CAMEL_PROVIDER_IS_LOCAL                = 1 << 1,
	CAMEL_PROVIDER_IS_EXTERNAL             = 1 << 2,
	CAMEL_PROVIDER_IS_SOURCE               = 1 << 3,
	CAMEL_PROVIDER_IS_STORAGE              = 1 << 4,
	CAMEL_PROVIDER_SUPPORTS_SSL            = 1 << 5,
	CAMEL_PROVIDER_HAS_LICENSE             = 1 << 6,
	CAMEL_PROVIDER_DISABLE_SENT_FOLDER     = 1 << 7,
	CAMEL_PROVIDER_ALLOW_REAL_TRASH_FOLDER = 1 << 8,
	CAMEL_PROVIDER_ALLOW_REAL_JUNK_FOLDER  = 1 << 9
} CamelProviderFlags;

typedef enum {
	CAMEL_PROVIDER_STORE,
	CAMEL_PROVIDER_TRANSPORT,
	CAMEL_NUM_PROVIDER_TYPES  /*< skip >*/
} CamelProviderType;

typedef enum {
	CAMEL_SASL_ANON_TRACE_EMAIL,
	CAMEL_SASL_ANON_TRACE_OPAQUE,
	CAMEL_SASL_ANON_TRACE_EMPTY
} CamelSaslAnonTraceType;

typedef enum {
	CAMEL_SERVICE_DISCONNECTED,
	CAMEL_SERVICE_CONNECTING,
	CAMEL_SERVICE_CONNECTED,
	CAMEL_SERVICE_DISCONNECTING
} CamelServiceConnectionStatus;

typedef enum {
	CAMEL_SESSION_ALERT_INFO,
	CAMEL_SESSION_ALERT_WARNING,
	CAMEL_SESSION_ALERT_ERROR
} CamelSessionAlertType;

typedef enum { /*< flags >*/
	CAMEL_STORE_SUBSCRIPTIONS    = 1 << 0,
	CAMEL_STORE_VTRASH           = 1 << 1,
	CAMEL_STORE_FILTER_INBOX     = 1 << 2,
	CAMEL_STORE_VJUNK            = 1 << 3,
	CAMEL_STORE_PROXY            = 1 << 4,
	CAMEL_STORE_IS_MIGRATING     = 1 << 5,
	CAMEL_STORE_ASYNC            = 1 << 6,
	CAMEL_STORE_REAL_JUNK_FOLDER = 1 << 7
} CamelStoreFlags;

/**
 * CamelStoreGetFolderInfoFlags:
 * @CAMEL_STORE_FOLDER_INFO_FAST:
 * @CAMEL_STORE_FOLDER_INFO_RECURSIVE:
 * @CAMEL_STORE_FOLDER_INFO_SUBSCRIBED:
 * @CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL:
 *   Do not include virtual trash or junk folders.
 * @CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST:
 *   Fetch only the subscription list. Clients should use this
 *   flag for requesting the list of folders available for
 *   subscription. Used in Exchange / IMAP connectors for public
 *   folder fetching.
 **/
typedef enum { /*< flags >*/
	CAMEL_STORE_FOLDER_INFO_FAST              = 1 << 0,
	CAMEL_STORE_FOLDER_INFO_RECURSIVE         = 1 << 1,
	CAMEL_STORE_FOLDER_INFO_SUBSCRIBED        = 1 << 2,
	CAMEL_STORE_FOLDER_INFO_NO_VIRTUAL        = 1 << 3,
	CAMEL_STORE_FOLDER_INFO_SUBSCRIPTION_LIST = 1 << 4
} CamelStoreGetFolderInfoFlags;

/* FIXME: this needs to track the CAMEL_FOLDER_* flags in camel-store.h */
typedef enum { /*< flags >*/
	CAMEL_STORE_INFO_FOLDER_NOSELECT      = 1 << 0,
	CAMEL_STORE_INFO_FOLDER_NOINFERIORS   = 1 << 1,
	CAMEL_STORE_INFO_FOLDER_CHILDREN      = 1 << 2,
	CAMEL_STORE_INFO_FOLDER_NOCHILDREN    = 1 << 3,
	CAMEL_STORE_INFO_FOLDER_SUBSCRIBED    = 1 << 4,
	CAMEL_STORE_INFO_FOLDER_VIRTUAL       = 1 << 5,
	CAMEL_STORE_INFO_FOLDER_SYSTEM        = 1 << 6,
	CAMEL_STORE_INFO_FOLDER_VTRASH        = 1 << 7,
	CAMEL_STORE_INFO_FOLDER_SHARED_BY_ME  = 1 << 8,
	CAMEL_STORE_INFO_FOLDER_SHARED_TO_ME  = 1 << 9,

	/* not in camle-store.h yet */
	CAMEL_STORE_INFO_FOLDER_READONLY      = 1 << 13,
	CAMEL_STORE_INFO_FOLDER_CHECK_FOR_NEW = 1 << 14,

	CAMEL_STORE_INFO_FOLDER_FLAGGED       = 1 << 31
} CamelStoreInfoFlags;

typedef enum { /*< flags >*/
	CAMEL_STORE_READ  = 1 << 0,
	CAMEL_STORE_WRITE = 1 << 1
} CamelStorePermissionFlags;

typedef enum { /*< flags >*/
	CAMEL_TCP_STREAM_SSL_ENABLE_SSL2 = 1 << 0,
	CAMEL_TCP_STREAM_SSL_ENABLE_SSL3 = 1 << 1,
	CAMEL_TCP_STREAM_SSL_ENABLE_TLS  = 1 << 2
} CamelTcpStreamSSLFlags;

/* Note: If you change this, make sure you change the
 *       'encodings' array in camel-mime-part.c. */
typedef enum {
	CAMEL_TRANSFER_ENCODING_DEFAULT,
	CAMEL_TRANSFER_ENCODING_7BIT,
	CAMEL_TRANSFER_ENCODING_8BIT,
	CAMEL_TRANSFER_ENCODING_BASE64,
	CAMEL_TRANSFER_ENCODING_QUOTEDPRINTABLE,
	CAMEL_TRANSFER_ENCODING_BINARY,
	CAMEL_TRANSFER_ENCODING_UUENCODE,
	CAMEL_TRANSFER_NUM_ENCODINGS
} CamelTransferEncoding;

/**
 * CamelStreamVFSOpenMethod:
 * CAMEL_STREAM_VFS_CREATE:
 *	Writable, creates new file or replaces old file.
 * CAMEL_STREAM_VFS_APPEND:
 *	Writable, creates new file or appends at the end of the old file.
 * CAMEL_STREAM_VFS_READ:
 *	Readable, opens existing file for reading.
 *
 * Since: 2.24
 **/
typedef enum {
	CAMEL_STREAM_VFS_CREATE,
	CAMEL_STREAM_VFS_APPEND,
	CAMEL_STREAM_VFS_READ
} CamelStreamVFSOpenMethod;

#endif /* CAMEL_ENUMS_H */
