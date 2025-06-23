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
 * Authors: Michael Zucchi <notzed@ximian.com>
 */

#include "evolution-data-server-config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "camel-folder-thread.h"

#define d(x)
#define m(x)

/*#define TIMEIT*/

#ifdef TIMEIT
#include <sys/time.h>
#endif

struct _CamelFolderThreadNode {
	struct _CamelFolderThreadNode *next, *parent, *child;
	gpointer item; /* CamelMessageInfo * or item from items */
	gchar *root_subject;	/* cached root equivalent subject */
	guint32 order : 31;
	guint32 re:1;			/* re version of subject? */
};

/**
 * camel_folder_thread_node_get_next:
 * @self: a #CamelFolderThreadNode
 *
 * Gets the next node in the tree structure from the @self.
 *
 * Returns: (nullable) (transfer none): the next node in the tree structure from the @self, or %NULL
 *
 * Since: 3.58
 **/
CamelFolderThreadNode *
camel_folder_thread_node_get_next (CamelFolderThreadNode *self)
{
	return self ? self->next : NULL;
}

/**
 * camel_folder_thread_node_get_parent:
 * @self: a #CamelFolderThreadNode
 *
 * Gets the parent node in the tree structure from the @self.
 *
 * Returns: (nullable) (transfer none): the parent node in the tree structure from the @self, or %NULL
 *
 * Since: 3.58
 **/
CamelFolderThreadNode *
camel_folder_thread_node_get_parent (CamelFolderThreadNode *self)
{
	return self ? self->parent : NULL;
}

/**
 * camel_folder_thread_node_get_child:
 * @self: a #CamelFolderThreadNode
 *
 * Gets the first child node in the tree structure from the @self.
 *
 * Returns: (nullable) (transfer none): the first child node in the tree structure from the @self, or %NULL
 *
 * Since: 3.58
 **/
CamelFolderThreadNode *
camel_folder_thread_node_get_child (CamelFolderThreadNode *self)
{
	return self ? self->child : NULL;
}

/**
 * camel_folder_thread_node_get_item:
 * @self: a #CamelFolderThreadNode
 *
 * Gets associated data with the @self. The actual type of the item depends
 * on the way the @self was created. For the camel_folder_thread_new() it's
 * a #CamelMessageInfo, for the camel_folder_thread_new_items() it's the member
 * of the used items array.
 *
 * Returns: (nullable) (transfer none): associated data with the @self
 *
 * Since: 3.58
 **/
gpointer
camel_folder_thread_node_get_item (CamelFolderThreadNode *self)
{
	return self ? self->item : NULL;
}

typedef struct _ItemFunctions {
	CamelFolderThreadStrFunc get_uid_func;
	CamelFolderThreadStrFunc get_subject_func;
	CamelFolderThreadUint64Func get_message_id_func;
	CamelFolderThreadArrayFunc get_references_func;
	CamelFolderThreadInt64Func get_date_sent_func;
	CamelFolderThreadInt64Func get_date_received_func;
	CamelFolderThreadVoidFunc lock_func;
	CamelFolderThreadVoidFunc unlock_func;
} ItemFunctions;

struct _CamelFolderThread {
	GObject parent_object;

	CamelFolderThreadFlags flags;
	CamelFolderThreadNode *tree;
	CamelMemChunk *node_chunks;
	CamelFolder *folder;
	GPtrArray *items; /* either CamelMessageInfo * or items from camel_folder_thread_new_items() */
	ItemFunctions functions;
};

G_DEFINE_TYPE (CamelFolderThread, camel_folder_thread, G_TYPE_OBJECT)

static void
camel_folder_thread_finalize (GObject *object)
{
	CamelFolderThread *self = CAMEL_FOLDER_THREAD (object);

	g_clear_object (&self->folder);
	g_clear_pointer (&self->items, g_ptr_array_unref);
	g_clear_pointer (&self->node_chunks, camel_memchunk_destroy);

	G_OBJECT_CLASS (camel_folder_thread_parent_class)->finalize (object);
}

static void
camel_folder_thread_class_init (CamelFolderThreadClass *klass)
{
	GObjectClass *object_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = camel_folder_thread_finalize;
}

static void
camel_folder_thread_init (CamelFolderThread *self)
{
	self->node_chunks = camel_memchunk_new (32, sizeof (CamelFolderThreadNode));
}

static void
container_add_child (CamelFolderThreadNode *node,
                     CamelFolderThreadNode *child)
{
	d (printf ("\nAdding child %p to parent %p \n", child, node));
	child->next = node->child;
	node->child = child;
	child->parent = node;
}

static void
container_parent_child (CamelFolderThreadNode *parent,
                        CamelFolderThreadNode *child)
{
	CamelFolderThreadNode *c, *node;

	/* are we already the right parent? */
	if (child->parent == parent)
		return;

	/* would this create a loop? */
	node = parent->parent;
	while (node) {
		if (node == child)
			return;
		node = node->parent;
	}

	/* are we unparented? */
	if (child->parent == NULL) {
		container_add_child (parent, child);
		return;
	}

	/* else remove child from its existing parent, and reparent */
	node = child->parent;
	c = (CamelFolderThreadNode *) &node->child;
	d (printf ("scanning children:\n"));
	while (c->next) {
		d (printf (" %p\n", c));
		if (c->next == child) {
			d (printf ("found node %p\n", child));
			c->next = c->next->next;
			child->parent = NULL;
			container_add_child (parent, child);
			return;
		}
		c = c->next;
	}

	printf ("DAMN, we shouldn't  be here!\n");
}

static void
prune_empty (CamelFolderThread *thread,
             CamelFolderThreadNode **cp)
{
	CamelFolderThreadNode *child, *next, *c, *lastc;

	/* yes, this is intentional */
	lastc = (CamelFolderThreadNode *) cp;
	while (lastc->next) {
		d(CamelSummaryMessageID dbg_message_id);

		c = lastc->next;
		prune_empty (thread, &c->child);

		d (dbg_message_id.id.id = c->item ? thread->functions.get_message_id_func (c->item) : 0);
		d (printf (
			"checking item %p %p (%08x%08x)\n", c,
			c->item,
			dbg_message_id.id.part.hi,
			dbg_message_id.id.part.lo));
		if (c->item == NULL) {
			if (c->child == NULL) {
				d (printf ("removing empty node\n"));
				lastc->next = c->next;
				m (memset (c, 0xfe, sizeof (*c)));
				camel_memchunk_free (thread->node_chunks, c);
				continue;
			}
			if (c->parent || c->child->next == NULL) {
				d (printf ("promoting child\n"));
				lastc->next = c->next; /* remove us */
				child = c->child;
				while (child) {
					next = child->next;

					child->parent = c->parent;
					child->next = lastc->next;
					lastc->next = child;

					child = next;
				}
				continue;
			}
		}
		lastc = c;
	}
}

static void
hashloop (gpointer key,
          gpointer value,
          gpointer data)
{
	CamelFolderThreadNode *c = value;
	CamelFolderThreadNode *tail = data;

	if (c->parent == NULL) {
		c->next = tail->next;
		tail->next = c;
	}
}

static gchar *
skip_list_ids (gchar *s)
{
	gchar *p;

	while (isspace (*s))
		s++;

	while (*s == '[') {
		p = s + 1;

		while (*p && *p != ']' && !isspace (*p))
			p++;

		if (*p != ']')
			break;

		s = p + 1;

		while (isspace (*s))
			s++;

		if (*s == '-' && isspace (s[1]))
			s += 2;

		while (isspace (*s))
			s++;
	}

	return s;
}

static gchar *
get_root_subject (CamelFolderThread *self,
		  CamelFolderThreadNode *c)
{
	gchar *s, *p;
	CamelFolderThreadNode *scan;

	s = NULL;
	c->re = FALSE;
	if (c->item)
		s = (gchar *) self->functions.get_subject_func (c->item);
	else {
		/* one of the children will always have a message */
		scan = c->child;
		while (scan) {
			if (scan->item) {
				s = (gchar *) self->functions.get_subject_func (scan->item);
				break;
			}
			scan = scan->next;
		}
	}
	if (s != NULL) {
		s = skip_list_ids (s);

		while (*s) {
			while (isspace (*s))
				s++;
			if (s[0] == 0)
				break;
			if ((s[0] == 'r' || s[0]=='R')
			    && (s[1] == 'e' || s[1]=='E')) {
				p = s + 2;
				while (isdigit (*p) || (ispunct (*p) && (*p != ':')))
					p++;
				if (*p == ':') {
					c->re = TRUE;
					s = skip_list_ids (p + 1);
				} else
					break;
			} else
				break;
		}
		if (*s)
			return s;
	}
	return NULL;
}

/* this can be pretty slow, but not used often */
/* clast cannot be null */
static void
remove_node (CamelFolderThreadNode **list,
             CamelFolderThreadNode *node,
             CamelFolderThreadNode **clast)
{
	CamelFolderThreadNode *c;

	/* this is intentional, even if it looks funny */
	/* if we have a parent, then we should remove it from the parent list,
	 * otherwise we remove it from the root list */
	if (node->parent) {
		c = (CamelFolderThreadNode *) &node->parent->child;
	} else {
		c = (CamelFolderThreadNode *) list;
	}
	while (c->next) {
		if (c->next == node) {
			if (*clast == c->next)
				*clast = c;
			c->next = c->next->next;
			return;
		}
		c = c->next;
	}

	printf ("ERROR: removing node %p failed\n", (gpointer) node);
}

static void
group_root_set (CamelFolderThread *thread,
                CamelFolderThreadNode **cp)
{
	GHashTable *subject_table = g_hash_table_new (g_str_hash, g_str_equal);
	CamelFolderThreadNode *c, *clast, *scan, *container;

	/* gather subject lines */
	d (printf ("gathering subject lines\n"));
	clast = (CamelFolderThreadNode *) cp;
	c = clast->next;
	while (c) {
		c->root_subject = get_root_subject (thread, c);
		if (c->root_subject) {
			container = g_hash_table_lookup (subject_table, c->root_subject);
			if (container == NULL
			    || (container->item == NULL && c->item)
			    || (container->re == TRUE && !c->re)) {
				g_hash_table_insert (subject_table, c->root_subject, c);
			}
		}
		c = c->next;
	}

	/* merge common subjects? */
	clast = (CamelFolderThreadNode *) cp;
	while (clast->next) {
		c = clast->next;
		d (printf ("checking %p %s\n", c, c->root_subject));
		if (c->root_subject
		    && (container = g_hash_table_lookup (subject_table, c->root_subject))
		    && (container != c)) {
			d (printf (" matching %p %s\n", container, container->root_subject));
			if (c->item == NULL && container->item == NULL) {
				d (printf ("merge containers children\n"));
				/* steal the children from c onto container, and unlink c */
				scan = (CamelFolderThreadNode *) &container->child;
				while (scan->next)
					scan = scan->next;
				scan->next = c->child;
				clast->next = c->next;
				m (memset (c, 0xee, sizeof (*c)));
				camel_memchunk_free (thread->node_chunks, c);
				continue;
			} if (c->item == NULL && container->item != NULL) {
				d (printf ("container is non-empty parent\n"));
				remove_node (cp, container, &clast);
				container_add_child (c, container);
			} else if (c->item != NULL && container->item == NULL) {
				d (printf ("container is empty child\n"));
				clast->next = c->next;
				container_add_child (container, c);
				continue;
			} else if (c->re && !container->re) {
				d (printf ("container is re\n"));
				clast->next = c->next;
				container_add_child (container, c);
				continue;
			} else if (!c->re && container->re) {
				d (printf ("container is not re\n"));
				remove_node (cp, container, &clast);
				container_add_child (c, container);
			} else {
				d (printf ("subjects are common %p and %p\n", c, container));

				/* build a phantom node */
				remove_node (cp, container, &clast);
				remove_node (cp, c, &clast);

				scan = camel_memchunk_alloc0 (thread->node_chunks);

				scan->root_subject = c->root_subject;
				scan->re = c->re && container->re;
				scan->next = c->next;
				clast->next = scan;
				container_add_child (scan, c);
				container_add_child (scan, container);
				clast = scan;
				g_hash_table_insert (subject_table, scan->root_subject, scan);
				continue;
			}
		}
		clast = c;
	}
	g_hash_table_destroy (subject_table);
}

struct _tree_info {
	GHashTable *visited;
};

static gint
dump_tree_rec (CamelFolderThread *self,
	       struct _tree_info *info,
               CamelFolderThreadNode *c,
               gint depth)
{
	gint count = 0, indent = depth * 2;

	while (c) {
		if (g_hash_table_lookup (info->visited, c)) {
			printf ("WARNING: NODE REVISITED: %p\n", (gpointer) c);
		} else {
			g_hash_table_insert (info->visited, c, c);
		}
		if (c->item) {
			CamelSummaryMessageID message_id;

			message_id.id.id = self->functions.get_message_id_func (c->item);

			printf (
				"%*s %p uid:'%s' <%08x%08x> subject:'%s' order:%d\n",
				indent, "", (gpointer) c,
				self->functions.get_uid_func (c->item),
				message_id.id.part.hi,
				message_id.id.part.lo,
				self->functions.get_subject_func (c->item),
				c->order);
			count += 1;
		} else {
			printf ("%*s %p <empty>\n", indent, "", (gpointer) c);
		}
		if (c->child)
			count += dump_tree_rec (self, info, c->child, depth + 1);
		c = c->next;
	}
	return count;
}

guint
camel_folder_thread_dump (CamelFolderThread *self)
{
	guint count;
	struct _tree_info info;

	g_return_val_if_fail (CAMEL_IS_FOLDER_THREAD (self), 0);

	info.visited = g_hash_table_new (g_direct_hash, g_direct_equal);
	count = dump_tree_rec (self, &info, self->tree, 0);
	g_hash_table_destroy (info.visited);

	return count;
}

typedef struct _SortCompareData {
	CamelFolderThread *self;
	GHashTable *times_cache;
} SortCompareData;

static gint
sort_node_cb (gconstpointer a,
	      gconstpointer b,
	      gpointer user_data)
{
	SortCompareData *scd = user_data;
	const CamelFolderThreadNode *a1 = ((CamelFolderThreadNode **) a)[0];
	const CamelFolderThreadNode *b1 = ((CamelFolderThreadNode **) b)[0];

	/* if we have no message, it must be a dummy node, which
	 * also means it must have a child, just use that as the
	 * sort data (close enough?) */
	if (a1->item == NULL)
		a1 = a1->child;
	if (b1->item == NULL)
		b1 = b1->child;

	/* Sort by sent or received time */
	if (scd->times_cache) {
		gint64 *ptime1, *ptime2;

		ptime1 = g_hash_table_lookup (scd->times_cache, a1);
		if (!ptime1 && a1->item) {
			ptime1 = g_new (gint64, 1);
			*ptime1 = scd->self->functions.get_date_sent_func (a1->item);
			if (*ptime1 <= 0)
				*ptime1 = scd->self->functions.get_date_received_func (a1->item);
			g_hash_table_insert (scd->times_cache, (gpointer) a1, ptime1);
		}

		ptime2 = g_hash_table_lookup (scd->times_cache, b1);
		if (!ptime2 && b1->item) {
			ptime2 = g_new (gint64, 1);
			*ptime2 = scd->self->functions.get_date_sent_func (b1->item);
			if (*ptime2 <= 0)
				*ptime2 = scd->self->functions.get_date_received_func (b1->item);
			g_hash_table_insert (scd->times_cache, (gpointer) b1, ptime2);
		}

		if (ptime1 && ptime2 && *ptime1 != *ptime2)
			return *ptime1 < *ptime2 ? -1 : 1;
	}

	if (a1->order == b1->order)
		return 0;
	if (a1->order < b1->order)
		return -1;
	else
		return 1;
}

static void
sort_thread (CamelFolderThread *self,
	     CamelFolderThreadNode **cp)
{
	CamelFolderThreadNode *c, *head, **carray;
	SortCompareData scd;
	gint size = 0;

	c = *cp;
	while (c) {
		/* sort the children while we're at it */
		if (c->child)
			sort_thread (self, &c->child);
		size++;
		c = c->next;
	}
	if (size < 2)
		return;

	carray = g_new (CamelFolderThreadNode *, size);

	c = *cp;
	size = 0;
	while (c) {
		carray[size] = c;
		c = c->next;
		size++;
	}

	scd.self = self;
	scd.times_cache = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, g_free);
	g_qsort_with_data (carray, size, sizeof (CamelFolderThreadNode *), sort_node_cb, &scd);
	g_hash_table_destroy (scd.times_cache);

	size--;
	head = carray[size];
	head->next = NULL;
	size--;
	do {
		c = carray[size];
		c->next = head;
		head = c;
		size--;
	} while (size >= 0);
	*cp = head;

	g_free (carray);
}

static guint
id_hash (gconstpointer key)
{
	const CamelSummaryMessageID *id = key;

	return id->id.part.lo;
}

static gboolean
id_equal (gconstpointer a,
          gconstpointer b)
{
	return ((const CamelSummaryMessageID *) a)->id.id == ((const CamelSummaryMessageID *) b)->id.id;
}

/* perform actual threading */
static void
thread_items (CamelFolderThread *self)
{
	GHashTable *id_table, *no_id_table;
	guint i;
	CamelFolderThreadNode *c, *child, *head;
	GPtrArray *items = self->items;
#ifdef TIMEIT
	struct timeval start, end;
	gulong diff;

	gettimeofday (&start, NULL);
#endif

	id_table = g_hash_table_new_full (id_hash, id_equal, g_free, NULL);
	no_id_table = g_hash_table_new (NULL, NULL);
	for (i = 0; i < items->len; i++) {
		gpointer item = items->pdata[i];
		CamelSummaryMessageID *message_id_copy, message_id;
		const GArray *references;

		if (self->functions.lock_func && self->functions.unlock_func)
			self->functions.lock_func (item);

		message_id.id.id = self->functions.get_message_id_func (item);
		references = self->functions.get_references_func (item);

		if (message_id.id.id) {
			c = g_hash_table_lookup (id_table, &message_id);
			/* check for duplicate messages */
			if (c && c->order) {
				/* if duplicate, just make out it is a no-id message,  but try and insert it
				 * into the right spot in the tree */
				d (printf ("doing: (duplicate message id)\n"));
				c = camel_memchunk_alloc0 (self->node_chunks);
				g_hash_table_insert (no_id_table, item, c);
			} else if (!c) {
				d (printf ("doing : %08x%08x (%s)\n", message_id.id.part.hi, message_id.id.part.lo, self->functions.get_subject_func (item)));
				c = camel_memchunk_alloc0 (self->node_chunks);
				message_id_copy = g_new0 (CamelSummaryMessageID, 1);
				message_id_copy->id.id = message_id.id.id;
				g_hash_table_insert (id_table, message_id_copy, c);
			}
		} else {
			d (printf ("doing : (no message id)\n"));
			c = camel_memchunk_alloc0 (self->node_chunks);
			g_hash_table_insert (no_id_table, item, c);
		}

		c->item = item;
		c->order = i + 1;
		child = c;
		if (references) {
			guint jj;

			d (printf ("%s (%s) references:\n", G_STRLOC, G_STRFUNC); )

			for (jj = 0; jj < references->len; jj++) {
				gboolean found = FALSE;

				message_id.id.id = g_array_index (references, guint64, jj);

				/* should never be empty, but just incase */
				if (!message_id.id.id)
					continue;

				c = g_hash_table_lookup (id_table, &message_id);
				if (c == NULL) {
					d (printf ("%s (%s) not found\n", G_STRLOC, G_STRFUNC));
					c = camel_memchunk_alloc0 (self->node_chunks);
					message_id_copy = g_new0 (CamelSummaryMessageID, 1);
					message_id_copy->id.id = message_id.id.id;
					g_hash_table_insert (id_table, message_id_copy, c);
				} else
					found = TRUE;
				if (c != child) {
					container_parent_child (c, child);
					/* Stop on the first parent found, no need to reparent
					 * it once it's placed in. Also, references are from
					 * parent to root, thus this should do the right thing. */
					if (found)
						break;
				}
				child = c;
			}
		}

		if (self->functions.lock_func && self->functions.unlock_func)
			self->functions.unlock_func (item);
	}

	d (printf ("\n\n"));
	/* build a list of root messages (no parent) */
	head = NULL;
	g_hash_table_foreach (id_table, hashloop, &head);
	g_hash_table_foreach (no_id_table, hashloop, &head);

	g_hash_table_destroy (id_table);
	g_hash_table_destroy (no_id_table);

	/* remove empty parent nodes */
	prune_empty (self, &head);

	/* find any siblings which missed out - but only if we are allowing threading by subject */
	if ((self->flags & CAMEL_FOLDER_THREAD_FLAG_SUBJECT) != 0)
		group_root_set (self, &head);

#if 0
	printf ("finished\n");
	i = camel_folder_thread_dump (self);
	printf ("%d count, %d items in tree\n", items->len, i);
#endif

	if ((self->flags & CAMEL_FOLDER_THREAD_FLAG_SORT) != 0)
		sort_thread (self, &head);

	/* remove any phantom nodes, this could possibly be put in group_root_set()? */
	c = (CamelFolderThreadNode *) &head;
	while (c && c->next) {
		CamelFolderThreadNode *scan, *newtop;

		child = c->next;
		if (child->item == NULL) {
			newtop = child->child;
			newtop->parent = NULL;
			/* unlink pseudo node */
			c->next = newtop;

			if (!(self->flags & CAMEL_FOLDER_THREAD_FLAG_SORT) && self->functions.get_date_sent_func &&
			    self->functions.get_date_received_func) {
				CamelFolderThreadNode *node;
				gint64 curr_sent_received;

				if (newtop->item) {
					curr_sent_received = self->functions.get_date_sent_func (newtop->item);
					if (curr_sent_received == 0 || curr_sent_received == -1)
						curr_sent_received = self->functions.get_date_received_func (newtop->item);
				} else {
					curr_sent_received = 0;
				}

				/* pick the oldest item as the new top item */
				for (node = newtop->next; node; node = node->next) {
					if (node->item) {
						gint64 sent_received;

						sent_received = self->functions.get_date_sent_func (node->item);
						if (sent_received == 0 || sent_received == -1)
							sent_received = self->functions.get_date_received_func (node->item);

						if (sent_received != 0 && sent_received != -1 && (sent_received < curr_sent_received ||
						    curr_sent_received == 0 || curr_sent_received == -1)) {
							gpointer ptr;
							guint32 val;

							curr_sent_received = sent_received;

							#define swap(_member, _var) \
								_var = newtop->_member; \
								newtop->_member = node->_member; \
								node->_member = _var;

							swap (item, ptr);
							swap (root_subject, ptr);
							swap (order, val);
							swap (re, val);

							#undef swap
						}
					}
				}
			}

			/* link its siblings onto the end of its children, fix all parent pointers */
			scan = (CamelFolderThreadNode *) &newtop->child;
			while (scan->next) {
				scan = scan->next;
			}
			scan->next = newtop->next;
			while (scan->next) {
				scan = scan->next;
				scan->parent = newtop;
			}

			/* and link the now 'real' node into the list */
			newtop->next = child->next;
			c = newtop;
			m (memset (child, 0xde, sizeof (*child)));
			camel_memchunk_free (self->node_chunks, child);
		} else {
			c = child;
		}
	}

#if d(1)+0
	/* this is only debug assertion stuff */
	c = (CamelFolderThreadNode *) &head;
	while (c->next) {
		c = c->next;
		if (c->item == NULL)
			g_warning ("threading missed removing a pseudo node: %s\n", c->root_subject);
		if (c->parent != NULL)
			g_warning ("base node has a non-null parent: %s\n", c->root_subject);
	}
#endif

	self->tree = head;

#ifdef TIMEIT
	gettimeofday (&end, NULL);
	diff = end.tv_sec * 1000 + end.tv_usec / 1000;
	diff -= start.tv_sec * 1000 + start.tv_usec / 1000;
	printf (
		"Message threading %d messages took %ld.%03ld seconds\n",
		items->len, diff / 1000, diff % 1000);
#endif
}

/**
 * camel_folder_thread_new:
 * @folder: a #CamelFolder
 * @uids: (element-type utf8) (nullable): The subset of uid's to thread. If %NULL, then thread
 *    all UID-s in the @folder
 * @flags: bit-or of #CamelFolderThreadFlags
 *
 * Thread a (subset) of the messages in a folder.
 *
 * If @flags contain %CAMEL_FOLDER_THREAD_FLAG_SUBJECT, messages with
 * related subjects will also be threaded. The default behaviour is to
 * only thread based on message-id.
 *
 * Returns: (transfer full): a new #CamelFolderThread containing a tree of #CamelFolderThreadNode-s
 *    which represent the threaded structure of the messages
 *
 * Since: 3.58
 **/
CamelFolderThread *
camel_folder_thread_new (CamelFolder *folder,
			 GPtrArray *uids,
			 CamelFolderThreadFlags flags)
{
	CamelFolderThread *self;
	GPtrArray *fsummary = NULL;
	guint ii;

	g_return_val_if_fail (CAMEL_IS_FOLDER (folder), NULL);

	self = g_object_new (CAMEL_TYPE_FOLDER_THREAD, NULL);
	self->flags = flags;
	self->folder = g_object_ref (folder);
	self->functions.get_uid_func = (CamelFolderThreadStrFunc) camel_message_info_get_uid;
	self->functions.get_subject_func = (CamelFolderThreadStrFunc) camel_message_info_get_subject;
	self->functions.get_message_id_func = (CamelFolderThreadUint64Func) camel_message_info_get_message_id;
	self->functions.get_references_func = (CamelFolderThreadArrayFunc) camel_message_info_get_references;
	self->functions.get_date_sent_func = (CamelFolderThreadInt64Func) camel_message_info_get_date_sent;
	self->functions.get_date_received_func = (CamelFolderThreadInt64Func) camel_message_info_get_date_received;
	self->functions.lock_func = (CamelFolderThreadVoidFunc) camel_message_info_property_lock;
	self->functions.unlock_func = (CamelFolderThreadVoidFunc) camel_message_info_property_unlock;

	camel_folder_summary_prepare_fetch_all (camel_folder_get_folder_summary (folder), NULL);

	/* prefer given order from the summary order */
	if (!uids) {
		fsummary = camel_folder_summary_dup_uids (camel_folder_get_folder_summary (folder));
		uids = fsummary;
	}

	self->items = g_ptr_array_new_full (uids->len, g_object_unref);

	for (ii = 0; ii < uids->len; ii++) {
		const gchar *uid = g_ptr_array_index (uids, ii);
		CamelMessageInfo *info;

		info = camel_folder_get_message_info (folder, uid);
		if (info)
			g_ptr_array_add (self->items, info);
	}

	g_clear_pointer (&fsummary, g_ptr_array_unref);

	thread_items (self);

	return self;
}

/**
 * camel_folder_thread_new_items:
 * @items: (transfer none) (element-type gpointer): items to thread
 * @flags: bit-or of #CamelFolderThreadFlags
 * @get_uid_func: (scope forever): an item get function, to get UID
 * @get_subject_func: (scope forever): an item get function, to get subject
 * @get_message_id_func: (scope forever): an item get function, to get encoded message ID
 * @get_references_func: (scope forever): an item get function, to get references
 * @get_date_sent_func: (scope forever) (nullable): an item get function, to get sent date, or %NULL
 * @get_date_received_func: (scope forever) (nullable): an item get function, to get received date, or %NULL
 * @lock_func: (scope forever) (nullable): an item get function, to lock for changes, or %NULL
 * @unlock_func: (scope forever) (nullable): an item get function, to unlock for changes, or %NULL
 *
 * Creates a folder tree of the provided @items, which can be accessed only
 * by the provided functions. The @get_date_sent_func and the @get_date_received_func
 * can be %NULL only when the @flags does not contain %CAMEL_FOLDER_THREAD_FLAG_SORT.
 *
 * The @lock_func and thed @unlock_func can be %NULL, but both at the same time
 * can be set or unset.
 *
 * The @items array is referenced and should not be manipulated
 * for the life time of the returned #CamelFolderThread.
 *
 * Returns: (transfer full): a new #CamelFolderThread, containing a tree of #CamelFolderThreadNode-s
 *    which represent the threaded structure of the @items
 *
 * Since: 3.58
 **/
CamelFolderThread *
camel_folder_thread_new_items (GPtrArray *items, /* caller data with items understood by the below functions */
			       CamelFolderThreadFlags flags,
			       CamelFolderThreadStrFunc get_uid_func,
			       CamelFolderThreadStrFunc get_subject_func,
			       CamelFolderThreadUint64Func get_message_id_func,
			       CamelFolderThreadArrayFunc get_references_func,
			       CamelFolderThreadInt64Func get_date_sent_func,
			       CamelFolderThreadInt64Func get_date_received_func,
			       CamelFolderThreadVoidFunc lock_func,
			       CamelFolderThreadVoidFunc unlock_func)
{
	CamelFolderThread *self;

	g_return_val_if_fail (items != NULL, NULL);
	g_return_val_if_fail (get_uid_func != NULL, NULL);
	g_return_val_if_fail (get_subject_func != NULL, NULL);
	g_return_val_if_fail (get_message_id_func != NULL, NULL);
	g_return_val_if_fail (get_references_func != NULL, NULL);

	if ((flags & CAMEL_FOLDER_THREAD_FLAG_SORT) != 0) {
		g_return_val_if_fail (get_date_sent_func != NULL, NULL);
		g_return_val_if_fail (get_date_received_func != NULL, NULL);
	}

	if (lock_func || unlock_func) {
		g_return_val_if_fail (lock_func != NULL, NULL);
		g_return_val_if_fail (unlock_func != NULL, NULL);
	} else {
		g_return_val_if_fail (lock_func == NULL, NULL);
		g_return_val_if_fail (unlock_func == NULL, NULL);
	}

	self = g_object_new (CAMEL_TYPE_FOLDER_THREAD, NULL);
	self->flags = flags;
	self->items = g_ptr_array_ref (items);
	self->functions.get_uid_func = get_uid_func;
	self->functions.get_subject_func = get_subject_func;
	self->functions.get_message_id_func = get_message_id_func;
	self->functions.get_references_func = get_references_func;
	self->functions.get_date_sent_func = get_date_sent_func;
	self->functions.get_date_received_func = get_date_received_func;
	self->functions.lock_func = lock_func;
	self->functions.unlock_func = unlock_func;

	thread_items (self);

	return self;
}

/**
 * camel_folder_thread_get_tree:
 * @self: a #CamelFolderThread
 *
 * Gets the root node of the threaded tree of the items.
 *
 * Returns: (transfer none): the root node of the threaded tree
 *
 * Since: 3.58
 **/
CamelFolderThreadNode *
camel_folder_thread_get_tree (CamelFolderThread *self)
{
	g_return_val_if_fail (CAMEL_IS_FOLDER_THREAD (self), NULL);

	return self->tree;
}
