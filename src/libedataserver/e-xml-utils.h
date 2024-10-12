/*
 * Copyright (C) 1999-2008 Novell, Inc. (www.novell.com)
 *
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

#ifndef E_XML_UTILS_H
#define E_XML_UTILS_H

#include <glib.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

G_BEGIN_DECLS

void		e_xml_initialize_in_main	(void);

xmlDoc *	e_xml_parse_file		(const gchar *filename);
gint		e_xml_save_file			(const gchar *filename,
						 xmlDoc *doc);
xmlNode *	e_xml_get_child_by_name		(const xmlNode *parent,
						 const xmlChar *child_name);

xmlDoc *	e_xml_parse_data		(gconstpointer data,
						 gsize length);
xmlXPathContext *
		e_xml_new_xpath_context_with_namespaces
						(xmlDoc *doc,
						 ...) G_GNUC_NULL_TERMINATED;
void		e_xml_xpath_context_register_namespaces
						(xmlXPathContext *xpath_ctx,
						 const gchar *prefix,
						 const gchar *href,
						 ...) G_GNUC_NULL_TERMINATED;
xmlXPathObject *
		e_xml_xpath_eval		(xmlXPathContext *xpath_ctx,
						 const gchar *format,
						 ...) G_GNUC_PRINTF (2, 3);
gchar *		e_xml_xpath_eval_as_string	(xmlXPathContext *xpath_ctx,
						 const gchar *format,
						 ...) G_GNUC_PRINTF (2, 3);
gboolean	e_xml_xpath_eval_exists		(xmlXPathContext *xpath_ctx,
						 const gchar *format,
						 ...) G_GNUC_PRINTF (2, 3);

gboolean	e_xml_is_element_name		(xmlNode *node,
						 const gchar *ns_href,
						 const gchar *name);
xmlNode *	e_xml_find_sibling		(xmlNode *sibling,
						 const gchar *ns_href,
						 const gchar *name);
xmlNode *	e_xml_find_next_sibling		(xmlNode *sibling,
						 const gchar *ns_href,
						 const gchar *name);
xmlNode *	e_xml_find_child		(xmlNode *parent,
						 const gchar *ns_href,
						 const gchar *name);
xmlChar *	e_xml_dup_node_content		(const xmlNode *node);
xmlChar *	e_xml_find_child_and_dup_content(xmlNode *parent,
						 const gchar *ns_href,
						 const gchar *name);
const xmlChar *	e_xml_get_node_text		(const xmlNode *node);
const xmlChar *	e_xml_find_child_and_get_text	(xmlNode *parent,
						 const gchar *ns_href,
						 const gchar *name);
void		e_xml_find_children_nodes	(xmlNode *parent,
						 guint count,
						 ...);
xmlNode *	e_xml_find_in_hierarchy		(xmlNode *parent,
						 const gchar *child_ns_href,
						 const gchar *child_name,
						 ...) G_GNUC_NULL_TERMINATED; /* requires two NULL-s at the end of the arguments */

G_END_DECLS

#endif /* E_XML_UTILS_H */
