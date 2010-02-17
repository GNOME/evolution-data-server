/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Authors :
 *  Ebby Wiselyn <ebbywiselyn@gmail.com>
 *  Jason Willis <zenbrother@gmail.com>
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
 * * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#include <config.h>

#include <gdata-feed.h>
#include <gdata-entry.h>

/* LibXML2 includes */
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <string.h>

#define GDATA_FEED_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), GDATA_TYPE_FEED, GDataFeedPrivate))

G_DEFINE_TYPE (GDataFeed, gdata_feed, G_TYPE_OBJECT)

struct _GDataFeedAuthor {
	gchar *email;
	gchar *name;
	gchar *uri;
};
typedef struct _GDataFeedAuthor GDataFeedAuthor;

struct _GDataFeedCategory {
	gchar *label;
	gchar *scheme;
	gchar *scheme_prefix;
	gchar *scheme_suffix;
	gchar *term;
};
typedef struct _GDataFeedCategory GDataFeedCategory;

struct _GDataFeedLink {
	gchar *href;
	gint  length;
	gchar *rel;
	gchar *title;
	gchar *type;
};
typedef struct _GDataFeedLink GDataFeedLink;

struct _GDataFeedPrivate {

	/* feed information */
	GSList *authors;
	GSList *categories;
	GSList *links;
	GSList *entries;

	gchar *content;
	gchar *contributor;
	gchar *id;
	gchar *link;
	gchar *published;
	gchar *rights;
	gchar *source;
	gchar *summary;
	gchar *title;
	gchar *updated;
	gchar *timezone;

	GHashTable *field_table;
	gchar *feedXML;

	gboolean dispose_has_run;
};

static void destroy_authors(gpointer data, gpointer user_data)
{
	GDataFeedAuthor *author = (GDataFeedAuthor *)data;
	if (author->email != NULL)
		g_free(author->email);

	if (author->name != NULL)
		g_free(author->name);

	if (author->uri != NULL)
		g_free(author->uri);

	g_free(author);
}

static void destroy_categories(gpointer data, gpointer user_data)
{
	GDataFeedCategory *category = (GDataFeedCategory *)data;
	if (category->label != NULL)
		g_free(category->label);

	if (category->scheme != NULL)
		g_free(category->scheme);

	if (category->scheme_prefix != NULL)
		g_free(category->scheme_prefix);

	if (category->scheme_suffix != NULL)
		g_free(category->scheme_suffix);

	if (category->term != NULL)
		g_free(category->term);

	g_free(category);
}

static void destroy_links(gpointer data, gpointer user_data)
{
	GDataFeedLink *link = (GDataFeedLink *)data;

	if (link->href != NULL)
		g_free(link->href);

	if (link->rel != NULL)
		g_free(link->rel);

	if (link->title != NULL)
		g_free(link->title);

	if (link->type != NULL)
		g_free(link->type);

	g_free(link);
}

static void destroy_entries(gpointer data, gpointer user_data)
{
	GDataEntry *entry = GDATA_ENTRY(data);
	g_object_unref(G_OBJECT(entry));
}

static void
gdata_feed_init (GDataFeed *instance)
{
	GDataFeed *self = instance;
	GDataFeedPrivate *priv;

	/* Private data set by g_type_class_add_private */
	priv = GDATA_FEED_GET_PRIVATE(self);
	priv->dispose_has_run = FALSE;

	priv->content = NULL;
	priv->contributor = NULL;
	priv->id = NULL;
	priv->link = NULL;
	priv->published = NULL;
	priv->rights = NULL;
	priv->source = NULL;
	priv->summary = NULL;
	priv->title = NULL;
	priv->updated = NULL;
	priv->timezone = NULL;

	priv->authors = NULL;
	priv->links = NULL;
	priv->categories = NULL;
	priv->entries = NULL;

	priv->field_table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	priv->feedXML = NULL;
}

static void
gdata_feed_dispose(GObject *obj)
{
	GObjectClass *parent_class;
	GDataFeedClass *klass;

	GDataFeed *self = GDATA_FEED(obj);
	GDataFeedPrivate *priv = GDATA_FEED_GET_PRIVATE(self);

	if (priv->dispose_has_run) {
		/* Don't run dispose twice */
		return;
	}

	priv->dispose_has_run = TRUE;

	/* Chain up to the parent class */
	klass = GDATA_FEED_CLASS(g_type_class_peek(GDATA_TYPE_FEED));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	parent_class->dispose(obj);
}

static void
gdata_feed_finalize(GObject *obj)
{
	GDataFeedPrivate *priv;
	GDataFeed *self = GDATA_FEED(obj);

	GObjectClass *parent_class;
	GDataFeedClass *klass;

	priv = GDATA_FEED_GET_PRIVATE(self);
	if (priv->entries != NULL) {
		g_slist_foreach(priv->entries, (GFunc)destroy_entries, NULL);
		g_slist_free(priv->entries);
	}

	if (priv->authors != NULL) {
		g_slist_foreach(priv->authors, (GFunc)destroy_authors, NULL);
		g_slist_free(priv->authors);
	}

	if (priv->links != NULL) {
		g_slist_foreach(priv->links, (GFunc)destroy_links, NULL);
		g_slist_free(priv->links);
	}

	if (priv->categories != NULL) {
		g_slist_foreach(priv->categories, (GFunc)destroy_categories, NULL);
		g_slist_free(priv->categories);
	}

	g_free (priv->updated);
	g_free (priv->timezone);

	if (priv->field_table != NULL)
		g_hash_table_destroy(priv->field_table);

	if (priv->feedXML != NULL)
		g_free(priv->feedXML);

	/* Chain up to the parent class */
	klass = GDATA_FEED_CLASS(g_type_class_peek(GDATA_TYPE_FEED));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	parent_class->finalize(obj);
}

static void
gdata_feed_get_property (GObject *obj,
		guint    property_id,
		GValue  *value,
		GParamSpec *pspec)
{
	GDataFeedPrivate *priv;

	priv = GDATA_FEED_GET_PRIVATE(obj);

}

static void
gdata_feed_set_property (GObject *obj,
		guint    property_id,
		const GValue *value,
		GParamSpec   *pspec)
{
	GDataFeedPrivate *priv;
	GDataFeed *self = (GDataFeed *) obj;

	priv = GDATA_FEED_GET_PRIVATE(self);

}

static GObject * gdata_feed_constructor(GType type,
		guint n_construct_properties,
		GObjectConstructParam *construct_properties)
{
	GObject *obj;

	GObjectClass *parent_class;
	GDataFeedClass *klass;

	/* Chain up to the parent class */
	klass = GDATA_FEED_CLASS(g_type_class_peek(GDATA_TYPE_FEED));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));

	obj = parent_class->constructor(type, n_construct_properties, construct_properties);

	return obj;

}

static void
gdata_feed_class_init (GDataFeedClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private(klass, sizeof (GDataFeedPrivate));

	gobject_class->set_property = gdata_feed_set_property;
	gobject_class->get_property = gdata_feed_get_property;

	gobject_class->dispose     = gdata_feed_dispose;
	gobject_class->finalize    = gdata_feed_finalize;
	gobject_class->constructor = gdata_feed_constructor;
}

/*** API ***/
	static GDataFeedAuthor *
xmlnode_to_author(xmlDocPtr doc, xmlNodePtr cur)
{
	GDataFeedAuthor *author;
	xmlChar *value;

	author = g_new0(GDataFeedAuthor, 1);
	author->email = NULL;
	author->name  = NULL;
	author->uri   = NULL;

	cur = cur->xmlChildrenNode;
	while (cur != NULL) {
		if (!xmlStrcmp(cur->name, (xmlChar *)"email")) {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			author->email = g_strdup((gchar *)value);
			xmlFree(value);
		}

		if (!xmlStrcmp(cur->name, (xmlChar *)"name")) {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			author->name = g_strdup((gchar *)value);
			xmlFree(value);
		}

		if (!xmlStrcmp(cur->name, (xmlChar *)"uri")) {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			author->uri = g_strdup((gchar *)value);
			xmlFree(value);
		}
		cur = cur->next;
	}

	return author;
}

static GDataFeedLink *
xmlnode_to_link(xmlDocPtr doc, xmlNodePtr cur)
{
	GDataFeedLink *link;
	xmlChar *value;

	link = g_new0(GDataFeedLink, 1);
	link->href = NULL;
	link->rel = NULL;
	link->title = NULL;
	link->type = NULL;

	value = xmlGetProp(cur, (xmlChar *)"href");
	if (value) {
		link->href = g_strdup((gchar *)value);
		xmlFree(value);
	}

	value = xmlGetProp(cur, (xmlChar *)"rel");
	if (value) {
		link->rel = g_strdup((gchar *)value);
		xmlFree(value);
	}

	value = xmlGetProp(cur, (xmlChar *)"title");
	if (value) {
		link->title = g_strdup((gchar *)value);
		xmlFree(value);
	}

	value = xmlGetProp(cur, (xmlChar *)"type");
	if (value) {
		link->type = g_strdup((gchar *)value);
		xmlFree(value);
	}

	return link;
}

static GDataFeedCategory *
xmlnode_to_category(xmlDocPtr doc, xmlNodePtr cur)
{
	GDataFeedCategory *category;
	xmlChar *value;

	category = g_new0(GDataFeedCategory, 1);
	category->label = NULL;
	category->scheme = NULL;
	category->scheme_prefix = NULL;
	category->scheme_suffix = NULL;
	category->term = NULL;

	value = xmlGetProp(cur, (xmlChar *)"label");
	if (value) {
		category->label = g_strdup((gchar *)value);
		xmlFree(value);
	}

	value = xmlGetProp(cur, (xmlChar *)"scheme");
	if (value) {
		category->scheme = g_strdup((gchar *)value);
		xmlFree(value);
	}

	value = xmlGetProp(cur, (xmlChar *)"term");
	if (value) {
		category->term = g_strdup((gchar *)value);
		xmlFree(value);
	}
	return category;
}

static xmlNodePtr
link_to_xmlnode (GDataFeedLink *link)
{
	xmlNodePtr cur;
	cur = xmlNewNode (NULL, (xmlChar *)"link");

	if (link->href) {
		xmlSetProp (cur, (xmlChar *)"href", (xmlChar *)link->href);
	}
	if (link->rel) {
		xmlSetProp (cur, (xmlChar *)"rel", (xmlChar *)link->rel);
	}
	if (link->title) {
		xmlSetProp (cur, (xmlChar *)"title", (xmlChar *)link->title);
	}
	if (link->type) {
		xmlSetProp (cur, (xmlChar *)"type", (xmlChar *)link->type);
	}

	return cur;
}

static xmlNodePtr
author_to_xmlnode (GDataFeedAuthor *author)
{
	xmlNodePtr cur;
	cur = xmlNewNode (NULL, (xmlChar *)"author");

	if (author->name)
	xmlNewChild (cur, NULL, (xmlChar *)"name", (xmlChar *)author->name);

	if (author->email)
	xmlNewChild (cur, NULL, (xmlChar *)"email", (xmlChar *)author->email);

	if (author->uri)
	xmlNewChild (cur, NULL, (xmlChar *)"uri", (xmlChar *)author->uri);

	return cur;
}

static xmlNodePtr
category_to_xmlnode (GDataFeedCategory *category)
{
	xmlNodePtr cur;
	cur = xmlNewNode (NULL, (xmlChar *)"category");

	if (category->label)
		xmlSetProp (cur, (xmlChar *)"label", (xmlChar *)category->label);
	if (category->scheme)
		xmlSetProp (cur, (xmlChar *)"scheme", (xmlChar *)category->scheme);

	return cur;
}

static xmlNodePtr
entry_to_xmlnode (GDataEntry *entry)
{
	xmlDocPtr doc;
	xmlNodePtr cur;
	gchar *xmlEntry;

	xmlEntry = gdata_entry_generate_xml (entry);
	doc = xmlReadMemory (xmlEntry, strlen (xmlEntry), "feed.xml", NULL, 0);
	cur = xmlDocGetRootElement (doc);

	return cur;
}

GDataFeed *
gdata_feed_new(void)
{
	return g_object_new(GDATA_TYPE_FEED, NULL);
}

GDataFeed *
gdata_feed_new_from_xml(const gchar * feedXML, const gint length)
{
	GDataFeed *feed;
	GDataFeedPrivate *priv;

	xmlDocPtr doc;
	xmlNodePtr cur;

	xmlChar *value;
	gint value_size;

	g_return_val_if_fail(feedXML != NULL && *feedXML != '\0', NULL);
	doc = xmlReadMemory(feedXML, length,
			"feed.xml",
			NULL,
			0);

	if (doc == NULL)
		return NULL;

	cur = xmlDocGetRootElement(doc);
	if (cur == NULL) {
		/* Empty */
		xmlFreeDoc(doc);
		return NULL;
	}

	if (xmlStrcmp(cur->name, (xmlChar *)"feed")) {
		xmlFreeDoc(doc);
		return NULL;
	}

	feed = g_object_new(GDATA_TYPE_FEED, NULL);
	priv  = GDATA_FEED_GET_PRIVATE(feed);

	cur = cur->xmlChildrenNode;
	while (cur != NULL) {

		if (!xmlStrcmp(cur->name, (xmlChar *)"author")) {
			priv->authors = g_slist_prepend(priv->authors, xmlnode_to_author(doc, cur));
		}
		else if (!xmlStrcmp(cur->name, (xmlChar *)"link")) {
			priv->links = g_slist_prepend(priv->links, xmlnode_to_link(doc, cur));
		}
		else if (!xmlStrcmp(cur->name, (xmlChar *)"category")) {
			priv->categories = g_slist_prepend(priv->categories, xmlnode_to_category(doc, cur));
		}
		else if (!xmlStrcmp(cur->name, (xmlChar *)"updated")) {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			priv->updated = g_strdup ((gchar *)value);
			xmlFree(value);
		} else if (!xmlStrcmp(cur->name, (xmlChar *)"timezone")) {
			value = xmlGetProp (cur, (xmlChar *)"value");
			g_free (priv->timezone);
			priv->timezone = g_strdup ((gchar *)value);
			xmlFree (value);
		}
		else if (!xmlStrcmp(cur->name, (xmlChar *)"entry")) {
			priv->entries = g_slist_prepend(priv->entries, gdata_entry_new_from_xmlptr(doc,cur));
		}
		else {
			value = xmlNodeListGetString(doc, cur->xmlChildrenNode, 1);
			g_hash_table_insert(priv->field_table, g_strdup((gchar *)cur->name),
					g_strdup((gchar *)value));
			xmlFree(value);
		}
		cur = cur->next;
	}

	xmlDocDumpFormatMemory(doc, &value, &value_size, 1);
	priv->feedXML = g_strdup(feedXML);

	xmlFree(value);
	xmlFreeDoc(doc);

	return feed;
}

gchar *
gdata_feed_generate_xml(GDataFeed *feed)
{
	GDataFeedPrivate *priv;
	xmlDocPtr doc;
	xmlNodePtr cur;
	xmlNodePtr root;
	xmlNsPtr ns_os, ns_gd, ns_gcal;
	xmlChar *xmlString;
	gint xml_buffer_size;
	GSList *list;

	g_return_val_if_fail(feed != NULL, NULL);
	g_return_val_if_fail(GDATA_IS_FEED(feed), NULL);

	priv = GDATA_FEED_GET_PRIVATE(feed);

	if (!priv->feedXML) {
		doc = xmlNewDoc ((xmlChar *)"1.0");
		root = xmlNewDocNode (doc, NULL, (xmlChar *)"feed", NULL);

		xmlSetProp (root, (xmlChar *)"xmlns", (xmlChar *)"http://www.w3.org/2005/Atom");
		ns_os = xmlNewNs (root, (xmlChar *)"http://a9.com/-/spec/opensearchrss/1.0/", (xmlChar *)"openSearch");
		ns_gd = xmlNewNs (root, (xmlChar *)"http://schemas.google.com/g/2005", (xmlChar *)"gd");
		ns_gcal = xmlNewNs (root, (xmlChar *)"http://schemas.google.com/gcal/2005", (xmlChar *)"gCal");

		if (priv->id) {
			cur = xmlNewChild (root, NULL, (xmlChar *)"id", NULL);
			xmlNodeSetContent (cur, (xmlChar *)priv->id);
		}

		list = priv->categories;
		while (list) {
			cur = category_to_xmlnode (list->data);
			xmlAddChild (root, cur);
			list = g_slist_next (list);
		}

		list = priv->links;
		while (list) {
			cur = link_to_xmlnode (list->data);
			xmlAddChild (root, cur);
			list = g_slist_next (list);
		}

		list = priv->authors;
		while (list) {
			cur = author_to_xmlnode (list->data);
			xmlAddChild (root, cur);
			list = g_slist_next (list);
		}

		list = priv->entries;
		while (list) {
			cur = entry_to_xmlnode (list->data);
			xmlAddChild (root, cur);
			list = g_slist_next (list);
		}
		xmlDocDumpMemory (doc, &xmlString, &xml_buffer_size);
		priv->feedXML = g_strdup ((gchar *)xmlString);
		xmlFree (xmlString);
		xmlFreeDoc (doc);
		return priv->feedXML;
	}
	else {
		return priv->feedXML;
	}
}

gchar * gdata_feed_get_updated (GDataFeed *feed)
{
	GDataFeedPrivate *priv;
	priv = GDATA_FEED_GET_PRIVATE (feed);

	g_return_val_if_fail (feed !=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_FEED(feed), NULL);

	return priv->updated;
}

GSList *
gdata_feed_get_entries (GDataFeed *feed)
{
	GDataFeedPrivate *priv;
	priv = GDATA_FEED_GET_PRIVATE (feed);

	g_return_val_if_fail (feed !=NULL, NULL);
	g_return_val_if_fail (GDATA_IS_FEED(feed), NULL);

	return priv->entries;
}

/**
 * gdata_feed_get_timezone:
 * Returned pointer owns the feed, its value is like 'Indian/Christmas'
 **/
const gchar *
gdata_feed_get_timezone (GDataFeed *feed)
{
	GDataFeedPrivate *priv;

	g_return_val_if_fail (feed != NULL, NULL);
	g_return_val_if_fail (GDATA_IS_FEED (feed), NULL);

	priv = GDATA_FEED_GET_PRIVATE (feed);

	return priv->timezone;
}
