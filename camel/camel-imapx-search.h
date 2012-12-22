/*
 * camel-imapx-search.h
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

#ifndef CAMEL_IMAPX_SEARCH_H
#define CAMEL_IMAPX_SEARCH_H

#include <camel/camel-folder-search.h>
#include <camel/camel-imapx-server.h>

/* Standard GObject macros */
#define CAMEL_TYPE_IMAPX_SEARCH \
	(camel_imapx_search_get_type ())
#define CAMEL_IMAPX_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), CAMEL_TYPE_IMAPX_SEARCH, CamelIMAPXSearch))
#define CAMEL_IMAPX_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), CAMEL_TYPE_IMAPX_SEARCH, CamelIMAPXSearchClass))
#define CAMEL_IS_IMAPX_SEARCH(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), CAMEL_TYPE_IMAPX_SEARCH))
#define CAMEL_IS_IMAPX_SEARCH_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), CAMEL_TYPE_IMAPX_SEARCH))
#define CAMEL_IMAPX_SEARCH_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), CAMEL_TYPE_IMAPX_SEARCH, CamelIMAPXSearchClass))

G_BEGIN_DECLS

typedef struct _CamelIMAPXSearch CamelIMAPXSearch;
typedef struct _CamelIMAPXSearchClass CamelIMAPXSearchClass;
typedef struct _CamelIMAPXSearchPrivate CamelIMAPXSearchPrivate;

/**
 * CamelIMAPXSearch:
 *
 * Contains only private data that should be read and manipulated using the
 * functions below.
 *
 * Since: 3.8
 **/
struct _CamelIMAPXSearch {
	CamelFolderSearch parent;
	CamelIMAPXSearchPrivate *priv;
};

struct _CamelIMAPXSearchClass {
	CamelFolderSearchClass parent_class;
};

GType		camel_imapx_search_get_type	(void) G_GNUC_CONST;
CamelFolderSearch *
		camel_imapx_search_new		(void);
CamelIMAPXServer *
		camel_imapx_search_ref_server	(CamelIMAPXSearch *search);
void		camel_imapx_search_set_server	(CamelIMAPXSearch *search,
						 CamelIMAPXServer *server);

#endif /* CAMEL_IMAPX_SEARCH_H */

