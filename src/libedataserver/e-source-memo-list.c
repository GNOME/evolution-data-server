/*
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/**
 * SECTION: e-source-memo-list
 * @include: libedataserver/libedataserver.h
 * @short_description: #ESource extension for a memo list
 *
 * The #ESourceCalendar extension identifies the #ESource as a memo list.
 *
 * Access the extension as follows:
 *
 * |[
 *   #include <libedataserver/libedataserver.h>
 *
 *   ESourceCalendar *extension;
 *
 *   extension = e_source_get_extension (source, E_SOURCE_EXTENSION_MEMO_LIST);
 * ]|
 **/

#include "e-source-memo-list.h"

#include <libedataserver/e-data-server-util.h>

G_DEFINE_TYPE (
	ESourceMemoList,
	e_source_memo_list,
	E_TYPE_SOURCE_SELECTABLE)

static void
e_source_memo_list_class_init (ESourceMemoListClass *class)
{
	ESourceExtensionClass *extension_class;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_MEMO_LIST;
}

static void
e_source_memo_list_init (ESourceMemoList *extension)
{
}
