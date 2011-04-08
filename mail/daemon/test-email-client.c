
#include <config.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>

#include <libedataserver/e-data-server-util.h>
#include <camel/camel.h>
#include "e-account-utils.h"
#include "e-gdbus-emailsession.h"
#include "e-gdbus-emailstore.h"
#include "e-gdbus-emailfolder.c"

#define E_MAIL_DATA_FACTORY_SERVICE_NAME \
	"org.gnome.evolution.dataserver.Mail"

EGdbusSessionCS *session_proxy;

static void
message_info_dump (CamelMessageInfoBase *mi)
{
	CamelFlag *flag;
	CamelTag *tag;
	
	if (mi == NULL) {
		printf("No message?\n");
		return;
	}

	printf("Subject: %s\n", camel_message_info_subject(mi));
	printf("To: %s\n", camel_message_info_to(mi));
	printf("Cc: %s\n", camel_message_info_cc(mi));
	printf("mailing list: %s\n", camel_message_info_mlist(mi));
	printf("From: %s\n", camel_message_info_from(mi));
	printf("UID: %s\n", camel_message_info_uid(mi));
	printf("PREVIEW: %s\n", mi->preview);	
	printf("Flags: %04x\n", camel_message_info_flags(mi));

	printf("User flags: \t");
	flag = mi->user_flags;
	while (flag) {
		printf ("%s\t", flag->name);
		flag = flag->next;
	}
	printf("\n");

	printf("User tags: \t");
	tag = mi->user_tags;
	while (tag) {
		printf ("%s:%s\t", tag->name, tag->value);
		tag = tag->next;
	}
	printf("\n");

}

static void
test_folder_basic (EGdbusFolderCF *folder_proxy, char *folder_path)
{
	char *data = NULL;

	egdbus_folder_cf_call_get_name_sync (folder_proxy, &data, NULL, NULL);
	printf("\n Folder Name: %s\n", data);

	egdbus_folder_cf_call_get_full_name_sync (folder_proxy, &data, NULL, NULL);
	printf("\n Full Name: %s\n", data);

	egdbus_folder_cf_call_get_description_sync (folder_proxy, &data, NULL, NULL);
	printf("\n Description %s\n", data);

	printf("\n Prepare Summary %d\n", egdbus_folder_cf_call_prepare_summary_sync (folder_proxy, NULL, NULL));

}


static CamelMessageInfoBase *
info_from_variant (CamelFolder *folder, GVariant *vinfo) 
{
	CamelMessageInfoBase *info;
	GVariantIter iter, aiter;
	GVariant *item, *aitem;
	int count, i;

	info = (CamelMessageInfoBase *) camel_message_info_new (folder ? folder->summary : NULL);

         /* Structure of CamelMessageInfoBase
         ssssss - uid, sub, from, to, cc, mlist
	 uu - flags, size
	 tt - date_sent, date_received
	 t  - message_id
	 iat - references
	 as - userflags
	 a(ss) - usertags
	 a(ss) - header 
         NOTE: We aren't now sending content_info*/

	g_variant_iter_init (&iter, vinfo);

	/* Uid, Subject, From, To, CC, mlist */
	item = g_variant_iter_next_value (&iter);
	info->uid = camel_pstring_strdup (g_variant_get_string(item, NULL));

	item = g_variant_iter_next_value (&iter);
	info->subject = camel_pstring_strdup (g_variant_get_string(item, NULL));

	item = g_variant_iter_next_value (&iter);
	info->from = camel_pstring_strdup (g_variant_get_string(item, NULL));

	item = g_variant_iter_next_value (&iter);
	info->to = camel_pstring_strdup (g_variant_get_string(item, NULL));

	item = g_variant_iter_next_value (&iter);
	info->cc = camel_pstring_strdup (g_variant_get_string(item, NULL));

	item = g_variant_iter_next_value (&iter);
	info->mlist = camel_pstring_strdup (g_variant_get_string(item, NULL));

	item = g_variant_iter_next_value (&iter);
	info->preview = g_strdup (g_variant_get_string(item, NULL));

	/* Flags & size */
	item = g_variant_iter_next_value (&iter);
	info->flags = g_variant_get_uint32 (item);

	item = g_variant_iter_next_value (&iter);
	info->size = g_variant_get_uint32 (item);

	/* Date: Sent/Received */
	item = g_variant_iter_next_value (&iter);
	info->date_sent = g_variant_get_uint64 (item);

	item = g_variant_iter_next_value (&iter);
	info->date_received = g_variant_get_uint64 (item);

	/* Message Id */
	item = g_variant_iter_next_value (&iter);	
	info->message_id.id.id = g_variant_get_uint64 (item);

	/* References */
	item = g_variant_iter_next_value (&iter);	
	count = g_variant_get_int32 (item);
	if (count) {
		item = g_variant_iter_next_value (&iter);	
      		g_variant_iter_init (&aiter, item);
	
		info->references = g_malloc(sizeof(*info->references) + ((count-1) * sizeof(info->references->references[0])));
		i=0;
      		while ((aitem = g_variant_iter_next_value (&aiter))) {
			info->references->references[i].id.id = g_variant_get_uint64 (aitem);
			i++;
        	}
		info->references->size = count;
	} else {
		item = g_variant_iter_next_value (&iter);	
	}

	/* UserFlags */
	item = g_variant_iter_next_value (&iter);	
      	g_variant_iter_init (&aiter, item);
	
      	while ((aitem = g_variant_iter_next_value (&aiter))) {
		char *str = g_variant_get_string (aitem, NULL);
		if (str && *str)	
			camel_flag_set (&info->user_flags, str, TRUE);
		else
			printf("Empty User Flags\n");
        }
	
	/* User Tags */
	item = g_variant_iter_next_value (&iter);	
      	g_variant_iter_init (&aiter, item);
	
      	while ((aitem = g_variant_iter_next_value (&aiter))) {
		GVariantIter siter;
		GVariant *sitem;
		char *tagname, *tagvalue;
		
		g_variant_iter_init (&siter, aitem);
		sitem = g_variant_iter_next_value (&siter);
		tagname = g_strdup (g_variant_get_string (sitem, NULL));
		sitem = g_variant_iter_next_value (&siter);
		tagvalue = g_strdup (g_variant_get_string (sitem, NULL));
		if (tagname && *tagname && tagvalue && *tagvalue)
			camel_tag_set (&info->user_tags, tagname, tagvalue);
		g_free (tagname);
		g_free (tagvalue);
        }

	return info;
}

#define VALUE_OR_NULL(x) x?x:""
static GVariant *
variant_from_info (CamelMessageInfoBase *info)
{
	GVariant *v, *v1;
	GVariantBuilder *builder, *b1, *b2;
	int i;
	CamelFlag *flags;
	CamelTag *tags;

	builder = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	
	g_variant_builder_add (builder, "s", info->uid);
	g_variant_builder_add (builder, "s", VALUE_OR_NULL(info->subject));
	g_variant_builder_add (builder, "s", VALUE_OR_NULL(info->from));
	g_variant_builder_add (builder, "s", VALUE_OR_NULL(info->to));
	g_variant_builder_add (builder, "s", VALUE_OR_NULL(info->cc));
	g_variant_builder_add (builder, "s", VALUE_OR_NULL(info->mlist));
	g_variant_builder_add (builder, "s", VALUE_OR_NULL(info->preview));


	g_variant_builder_add (builder, "u", info->flags);
	g_variant_builder_add (builder, "u", info->size);

	g_variant_builder_add (builder, "t", info->date_sent);
	g_variant_builder_add (builder, "t", info->date_received);

	g_variant_builder_add (builder, "t", info->message_id.id.id);


	
	/* references */

	if (info->references) {
		g_variant_builder_add (builder, "i", info->references->size);

		b1 = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
		for (i=0; i<info->references->size; i++) {
			g_variant_builder_add (b1, "t", info->references->references[i].id.id);
		}
		v1 = g_variant_builder_end (b1);
		g_variant_builder_unref (b1);
	
		g_variant_builder_add_value (builder, v1);
		g_variant_unref (v1);
	} else {
		g_variant_builder_add (builder, "i", 0);
		b1 = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
		g_variant_builder_add (b1, "t", 0);
		v1 = g_variant_builder_end (b1);
		g_variant_builder_unref (b1);
	
		g_variant_builder_add_value (builder, v1);
		

	}

	/* User Flags */
	b1 = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
	flags = info->user_flags;
	while (flags) {
		g_variant_builder_add (b1, "s", flags->name);
		flags = flags->next;
	}
	g_variant_builder_add (b1, "s", "");
	v1 = g_variant_builder_end (b1);
	g_variant_builder_unref (b1);
	
	g_variant_builder_add_value (builder, v1);

	/* User Tags */
	b1 = g_variant_builder_new (G_VARIANT_TYPE_ARRAY);
	tags = info->user_tags;
	while (tags) {
		b2 = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
		g_variant_builder_add (b2, "s", tags->name);
		g_variant_builder_add (b2, "s", tags->value);
		
		v1 = g_variant_builder_end (b2);
		g_variant_builder_unref (b2);

		/* FIXME: Should we handle empty tags? Can it be empty? If it potential crasher ahead*/
		g_variant_builder_add_value (b1, v1);

		tags = tags->next;
	}

	b2 = g_variant_builder_new (G_VARIANT_TYPE_TUPLE);
	g_variant_builder_add (b2, "s", "");
	g_variant_builder_add (b2, "s", "");	
	v1 = g_variant_builder_end (b2);
	g_variant_builder_unref (b2);
	g_variant_builder_add_value (b1, v1);

	v1 = g_variant_builder_end (b1);
	g_variant_builder_unref (b1);
	
	g_variant_builder_add_value (builder, v1);

	v = g_variant_builder_end (builder);
	g_variant_builder_unref (builder);

	return v;
}


static void
test_message_basics (char *folder_path, EGdbusFolderCF *folder_proxy)
{
	char *data = NULL;
	char **uids;
	GError *error = NULL;
	GVariant *variant=NULL;

	egdbus_folder_cf_call_get_uids_sync (folder_proxy, &uids, NULL, &error);

	if (error) {
		printf("Error while getting uids: %s\n", error->message);
		g_error_free (error);
		error = NULL;
	} else {
		gboolean ret, fg;
		int i=0;
		guint32 flags=0;
		char *msg = NULL;
		char *name, *val=NULL;
		CamelMessageInfoBase *info;

		printf("UIDS received: \t");
		while (uids[i]) {
			printf("%s\t", uids[i]);
			i++;
		}
		printf("\n");

		ret = egdbus_folder_cf_call_get_message_info_sync (folder_proxy, uids[0], &variant, NULL, &error);
		if (!ret || error) {
			printf("Error while getting messageinfo: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("Message info at beginning\n\n");	
			info = info_from_variant (NULL, variant);
			message_info_dump (info);
			camel_message_info_free (info);
			printf("\n\n");
		}

		/* Message flags */
		ret = egdbus_folder_cf_call_get_message_flags_sync(folder_proxy, uids[0], &flags, NULL, &error);
		if (!ret || error) {
			printf("Error while getting message flags: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nFlags of the message : %u\n", flags);
		}

		ret = egdbus_folder_cf_call_set_message_flags_sync(folder_proxy, uids[0], CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED, CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while setting message flags: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("Set the flags of the message : %u : success:%d\n", CAMEL_MESSAGE_SEEN | CAMEL_MESSAGE_DELETED, fg);
		}


		/* User flags */
		ret = egdbus_folder_cf_call_get_message_user_flag_sync(folder_proxy, uids[0], "bold", &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while getting message user flag: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nUser Flags of the message for 'bold': %d\n", fg);
		}
	
		ret = egdbus_folder_cf_call_set_message_user_flag_sync(folder_proxy, uids[0], "bold", TRUE, NULL, &error);
		if (!ret || error) {
			printf("Error while setting message user flag: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nSet UserFlags of the message to 'bold': success\n");
		}

		ret = egdbus_folder_cf_call_get_message_user_flag_sync(folder_proxy, uids[0], "bold", &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while getting message user flag: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nUser Flags of the message for 'bold' : %d\n", fg);
		}
		
		/* User Tag */
		ret = egdbus_folder_cf_call_get_message_user_tag_sync(folder_proxy, uids[0], "", &val, NULL, &error);
		if (!ret || error) {
			printf("Error while getting message user tag: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("User Tag of the message : %s\n", val ? val : "<empty>");
		}
	
		ret = egdbus_folder_cf_call_set_message_user_tag_sync(folder_proxy, uids[0], "bold", "strong", NULL, &error);
		if (!ret || error) {
			printf("Error while setting message user tag: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nSet UserTag of the message 'bold' to 'strong': success\n");
		}

		ret = egdbus_folder_cf_call_get_message_user_tag_sync(folder_proxy, uids[0], "bold", &val, NULL, &error);
		if (!ret || error) {
			printf("Error while getting message user tag: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n User Tag of the message for 'bold' : %s\n", val ? val : "<empty>");
		}


		printf("\n\nMessage Info at the end\n");

		ret = egdbus_folder_cf_call_get_message_info_sync (folder_proxy, uids[0], &variant, NULL, &error);
		if (!ret || error) {
			printf("Error while getting messageinfo: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			
			info = info_from_variant (NULL, variant);
			message_info_dump (info);
			/* camel_message_info_free (info); */
		}

		/* Get Message */
		ret = egdbus_folder_cf_call_get_message_sync (folder_proxy, uids[0], &msg, NULL, &error);
		if (!ret || error) {
			printf("Error while getting message: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n\n%s\n\n", msg);
			/* g_free(msg); */
		}

		/* Folder sync */
		ret = egdbus_folder_cf_call_sync_sync (folder_proxy, FALSE, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while syncing folder: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nSyncing folder success: %d\n", fg);
		}

		/* getPermanentFlags */
		ret = egdbus_folder_cf_call_get_permanent_flags_sync (folder_proxy, &flags, NULL, &error);
		if (!ret || error) {
			printf("Error while getting folder permanent flags : %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Permanent Flags: %u\n", flags);
		}

		/* hasSummaryCapability */
		ret = egdbus_folder_cf_call_has_summary_capability_sync (folder_proxy, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while checking has summary capability: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Has summary capability : %d\n", fg);
		}

		/* hasSearchCapability */
		ret = egdbus_folder_cf_call_has_search_capability_sync (folder_proxy, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while checking has search capability: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Has search capability : %d\n", fg);
		}

		/* Total count */
		ret = egdbus_folder_cf_call_total_message_count_sync (folder_proxy, &i, NULL, &error);
		if (!ret || error) {
			printf("Error while getting total msg count: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Total message count : %d\n", i);
		}

		/* Unread count */
		ret = egdbus_folder_cf_call_unread_message_count_sync (folder_proxy, &i, NULL, &error);
		if (!ret || error) {
			printf("Error while getting unread msg count: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Unread message count : %d\n", i);
		}

		/* Deleted count*/
		ret = egdbus_folder_cf_call_deleted_message_count_sync (folder_proxy, &i, NULL, &error);
		if (!ret || error) {
			printf("Error while getting deleted msg count: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Deleted message count : %d\n", i);
		}

		/* Expunge */
		ret = egdbus_folder_cf_call_expunge_sync (folder_proxy, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while expunging folder: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nExpunging folder success: %d\n", fg);
		}

		/* Refresh */
		ret = egdbus_folder_cf_call_refresh_info_sync (folder_proxy, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while refreshing folder: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Refreshing folder success: %d\n", fg);
		}

		/* Get UIDS */
		egdbus_folder_cf_call_get_uids_sync (folder_proxy, &uids, NULL, &error);
		if (error) {
			printf("Error while getting uids: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			int i=0;
			guint32 flags=0;
			char *name, *val=NULL;

			printf("UIDS at END received: \t");
			while (uids[i]) {
				printf("%s\t", uids[i]);
				i++;
			}
			printf("\n");
		}

		/* get parent store */
		ret = egdbus_folder_cf_call_get_parent_store_sync (folder_proxy, &name, NULL, &error);
		if (error) {
			printf("Error while getting parent store: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Parent Store OBJ: %s\n", name);
			g_free (name);
		}

		/* Local Store*/
		EGdbusFolderCF *local_folder_proxy;
		char *local_folder_proxy_path;
		EGdbusStoreMS *local_store_proxy;
		char *local_store;

		ret = egdbus_session_cs_call_get_local_store_sync (session_proxy, &local_store, NULL, &error);
		if (error) {
			printf("Error while getting local store: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Local store path: %s\n", local_store);
			local_store_proxy = egdbus_store_ms_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (session_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							local_store,
							NULL, &error);
			if (error)
				printf("Failed to create Local store proxy %s\n", error->message);
			else 
				printf("Created Local Store proxy\n");
		}
		/* Local Folder */
		ret = egdbus_session_cs_call_get_local_folder_sync (session_proxy, "drafts", &local_folder_proxy_path, NULL, &error);
		if (error) {
			printf("Error while getting local folder: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\n Got Local Folder Drafts %s\n", local_folder_proxy_path);
		
			local_folder_proxy = egdbus_folder_cf_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (session_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							local_folder_proxy_path,
							NULL, &error);
			if (error)
				printf("failed to get local folder drafts: %s\n", error->message);
			else
				printf("Got Local Folder Drafts\n");
		}
		
		/* append message */
		char *retuid;
		GVariant *gv = variant_from_info (info);
		ret = egdbus_folder_cf_call_append_message_sync (local_folder_proxy, gv, msg, &retuid, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while getting appending msg: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("Successfully appended msg: %s\n", retuid);
		}

		/* Sync */
		ret = egdbus_folder_cf_call_sync_sync (local_folder_proxy , FALSE, &fg, NULL, &error);
		if (!ret || error) {
			printf("Error while syncing folder: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			printf("\nSyncing folder success: %d\n", fg);
		}
		
		/* Search by expression */
		/* serach by uids */
		
	}
}

static void
folder_changed_cb (EGdbusFolderCF *folder_proxy, 
		   const gchar *const *added,
		   const gchar *const *removed,
		   const gchar *const *changed,
		   const gchar *const *recent)
{
	int i=0;

	printf("Received FOLDER CHANGED event\n");

	printf("Folder: Added uids:\t");
	while (added[i]) {
		printf("%s\t", added[i]);
		i++;
	}
	printf("\n");

	i=0;
	printf("Folder: Removed uids:\t");
	while (removed[i]) {
		printf("%s\t", removed[i]);
		i++;
	}
	printf("\n");

	printf("Folder: changed uids:\t");
	while (changed[i]) {
		printf("%s\t", changed[i]);
		i++;
	}
	printf("\n");

	printf("Folder: recent uids:\t");
	while (recent[i]) {
		printf("%s\t", recent[i]);
		i++;
	}
	printf("\n");
	

}

static GList *
parse_infos (EGdbusStoreMS *store_proxy, GVariant *var_changes)
{
	GList *l = NULL;
	GVariantIter iter;
	guint32 u1;
	gint32 i1, i2;
	gchar *str1, *str2, *str3;
	EGdbusFolderCF *inbox_proxy;
	char *inbox_path;
	EGdbusFolderCF *folder_proxy;
	char *folder_proxy_path;
	GError *error = NULL;
	GVariant *cf_info;
	gboolean success = FALSE;
	char *new_folder_uri;

	if (var_changes == NULL)
		return NULL;

	g_variant_iter_init (&iter, var_changes);
	while (g_variant_iter_next (&iter, "(sssuii)", &str1, &str2, &str3, &u1, &i1, &i2)) {
		printf("uri: %s Folder Name:%s Full Name:%s Flags:%u, UnreadCount%d TotalCount%d\n", str1, str2, str3, u1, i1, i2);
	}

	/* Get Inbox folder */
	if (!egdbus_store_ms_call_get_folder_sync (
		store_proxy, 
		"INBOX", /* Pass the full name */
		&folder_proxy_path,
		NULL, 
		&error))
		printf("Error while getting folder INBOX: %s\n", error->message);
	
	printf("Folder path for %s\n", folder_proxy_path);
		
	folder_proxy = egdbus_folder_cf_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (store_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							folder_proxy_path,
							NULL, &error);
	g_signal_connect (folder_proxy , "folder-changed", G_CALLBACK (folder_changed_cb), NULL);

	printf("Success in getting FolderProxy? %p %s\n", folder_proxy, error ? error->message : "Yahoo");
	inbox_proxy = folder_proxy;
	inbox_path = g_strdup (folder_proxy_path);
	
#if 1
	/* Get Inbox API */
	/* Most providers don't implement Get Inbox */
	if (!egdbus_store_ms_call_get_inbox_sync (
		store_proxy, 
		&folder_proxy_path,
		NULL, 
		&error))
		printf("Error while getting GET INBOX: %s\n", error->message);
	
	printf("INBOX path for %s\n", folder_proxy_path);
	if (!error)
	folder_proxy = egdbus_folder_cf_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (store_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							folder_proxy_path,
							NULL, &error);
	else
		g_error_free (error);

	error = NULL;
	printf("Success in getting FolderProxy for INBOX ? %p %s\n", folder_proxy, error ? error->message : "Yahoo");
#endif
#if 1	
	/* Get Trash */
	if (!egdbus_store_ms_call_get_trash_sync (
		store_proxy, 
		&folder_proxy_path,
		NULL, 
		&error))
		printf("Error while getting GET Trash: %s\n", error->message);
	
	printf("Trash path for %s\n", folder_proxy_path);
		
	if (!error)
	folder_proxy = egdbus_folder_cf_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (store_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							folder_proxy_path,
							NULL, &error);
	else
		g_error_free (error);
	error = NULL;
	printf("Success in getting FolderProxy for TRASH ? %p %s\n", folder_proxy, error ? error->message : "Yahoo");
	
	/* Get Junk*/
	if (!egdbus_store_ms_call_get_junk_sync (
		store_proxy, 
		&folder_proxy_path,
		NULL, 
		&error))
		printf("Error while getting GET Junk: %s\n", error->message);
	
	printf("Junk path for %s\n", folder_proxy_path);
	
	if (!error)
	folder_proxy = egdbus_folder_cf_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (store_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							folder_proxy_path,
							NULL, &error);
	else
		g_error_free (error);
	error = NULL;
	printf("Success in getting FolderProxy for JUNK ? %p %s\n", folder_proxy, error ? error->message : "Yahoo");

#endif	

	/* Create Folder */
	if (!egdbus_store_ms_call_create_folder_sync (store_proxy, "", "ATestEmailServer", &cf_info, NULL, &error))
		printf("Failed to create folder: %s \n", error->message);

	if (error && error->message) 
		g_error_free (error);
	else {
		g_variant_iter_init (&iter, cf_info);
		while (g_variant_iter_next (&iter, "(sssuii)", &str1, &str2, &str3, &u1, &i1, &i2)) {
			new_folder_uri = str1;
			printf("NEW FOLDER: uri: %s Folder Name:%s Full Name:%s Flags:%u, UnreadCount%d TotalCount%d\n", str1, str2, str3, u1, i1, i2);
			/* */
			/* Get the folder */
			if (!egdbus_store_ms_call_get_folder_sync (
				store_proxy, 
				str3, /* Pass the full name */
				&folder_proxy_path,
				NULL, 
				&error))
				printf("Error while getting folder : %s\n", error->message);
	
			printf("Folder path for %s\n", folder_proxy_path);
		
			folder_proxy = egdbus_folder_cf_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (store_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							folder_proxy_path,
							NULL, &error);

			printf("Success in getting FolderProxy? %p %s\n", folder_proxy, error ? error->message : "Yahoo");
			test_folder_basic (folder_proxy, folder_proxy_path);
			break;

		}
		
	}
	error = NULL;

	/* supports subscription ?*/ 
	if (!egdbus_store_ms_call_supports_subscriptions_sync(store_proxy, &success, NULL, &error)) {
		printf("Unable to check sub: %s\n", error->message);
		g_error_free (error);
		error = NULL;
	} else {
		printf("Store supports subscription: %d\n", success);
		/* Subscribe Folder */
		if (!egdbus_store_ms_call_subscribe_folder_sync (store_proxy, "ATestEmailServer", &success, NULL, &error)) {
			printf("Unable to subscribe: %s\n", error->message);
			g_error_free (error);
			error = NULL;
		} else {
			/* Can Refresh Folder */
			if (!egdbus_store_ms_call_can_refresh_folder_sync (store_proxy, cf_info, &success, NULL, &error)) {
				printf("Unable to check if can refresh: %s\n", error->message);
				g_error_free (error);
				error = NULL;

			} else {
				printf("Can Refresh Folder\n");
			}

			/* Transfer one msg */
			char *uids[2], **retuids;
			uids[0] = "13989";
			uids[1] = "13942";
			uids[2] = NULL;

			if (!egdbus_folder_cf_call_transfer_messages_to_sync (inbox_proxy,  &uids, folder_proxy_path,  FALSE, &retuids, NULL, NULL))
				printf("\n Unable to copy \n");
			else 
				printf("\n COPIED %s\n", retuids[0] ? retuids[0] : "nil");
			
			test_message_basics (folder_proxy_path, folder_proxy);

#if 0
			/* Unsubscribe Folder */
			printf("Folder successfully subscribed: %d\n", success);
			if (!egdbus_store_ms_call_unsubscribe_folder_sync (store_proxy, "ATestEmailServer", &success, NULL, &error)) {
			printf("Unable to unsubscribe: %s\n", error->message);
			g_error_free (error);
			error = NULL;
			} else {
				printf("Folder successfully unsubscribed: %d\n", success);
			}
#endif
		}
	}
	



	/* Rename Folder */
	if (!egdbus_store_ms_call_rename_folder_sync (store_proxy, "ATestEmailServer", "ANOTHERTestEmailServer", &success, NULL, &error))
		printf("Failed to rename folder: %s \n", error->message);

	if (error && error->message) 
		g_error_free (error);
	else {
		printf("SUCCESS, renamed folder to ANOTHERTestEmailServer\n");
	}
	error = NULL;


#if 1
	/* Delete folder */
	if (!egdbus_store_ms_call_delete_folder_sync (store_proxy, "ANOTHERTestEmailServer", &success, NULL, &error))
		printf("Failed to delete folder: %s \n", error->message);

	if (error && error->message) 
		g_error_free (error);
	else {
		printf("SUCCESS, delete folder to ANOTHERTestEmailServer\n");
	}
	error = NULL;
#endif

	/* Sync */
	if (!egdbus_store_ms_call_sync_sync (store_proxy, FALSE, &success, NULL, &error)) {
		printf("Unable to sync: %s\n", error->message);
		g_error_free (error);
		error = NULL;
	} else {
		printf("Sync store success\n");
	}

	/* Noop */
	if (!egdbus_store_ms_call_noop_sync (store_proxy, &success, NULL, &error)) {
		printf("Unable to noop : %s\n", error->message);
		g_error_free (error);
		error = NULL;
	} else {
		printf("Noop store success\n");
	}


	return g_list_reverse (l);
}

static void
print_info (GVariant *v, const char *operation)
{
	GVariantIter iter;
	char *str1, *str2, *str3;
	int i1, i2;
	guint32 u1;

	g_variant_iter_init (&iter, v);
	while (g_variant_iter_next (&iter, "(sssuii)", &str1, &str2, &str3, &u1, &i1, &i2)) {
		if (!str1 || !*str1|| !str2 || !*str2|| !str3 || !*str3) {
			break;
		}
		printf("\n\nSIGNAL: \n%s:::::::  uri: %s Folder Name:%s Full Name:%s Flags:%u, UnreadCount%d TotalCount%d\n\n\n", operation, str1, str2, str3, u1, i1, i2);
	}

}

static void
folder_opened_cb (EGdbusStoreMS *object, GVariant *v, gpointer data)
{
	print_info (v, "Folder Opened");
}
static void
folder_created_cb (EGdbusStoreMS *object, GVariant *v, gpointer data)
{
	print_info (v, "Folder Created");	
}
static void
folder_deleted_cb (EGdbusStoreMS *object, GVariant *v, gpointer data)
{
	print_info (v, "Folder Deleted");
	
}
static void
folder_renamed_cb (EGdbusStoreMS *object, const char *oldname, GVariant *v, gpointer data)
{
	print_info (v, "Folder Renamed");
	printf("Old folder name: %s\n\n\n", oldname);
	
}
static void
folder_subscribed_cb (EGdbusStoreMS *object, GVariant *v, gpointer data)
{
	print_info (v, "Folder Subscribed");
}
static void
folder_unsubscribed_cb (EGdbusStoreMS *object, GVariant *v, gpointer data)
{
	print_info (v, "Folder UnSubscribed");	
}

static gboolean
start_test_client (gpointer foo)
{
	EAccount *account = e_get_default_account ();
	const char *uri = e_account_get_string (account, E_ACCOUNT_SOURCE_URL);
	GError *error = NULL;
	EGdbusStoreMS *store_proxy;
	char *path;
	GVariant *infos = NULL;

	/* Get Session */
	session_proxy = egdbus_session_cs_proxy_new_for_bus_sync (
		G_BUS_TYPE_SESSION,
		G_DBUS_PROXY_FLAGS_NONE,
		E_MAIL_DATA_FACTORY_SERVICE_NAME,
		"/org/gnome/evolution/dataserver/Mail/Session",
		NULL,
		&error);
	if (error) 
		printf("ERROR %s\n", error->message);
	else 
		printf("Success\n");

	/* Get Store */
	if (!egdbus_session_cs_call_get_store_sync (session_proxy, uri, &path, NULL, &error)) {
		printf("Get store %s\n", error->message);
	}

	printf("PATH %s\n", path);

	store_proxy = egdbus_store_ms_proxy_new_sync (g_dbus_proxy_get_connection (G_DBUS_PROXY (session_proxy)),
							G_DBUS_PROXY_FLAGS_NONE,
							E_MAIL_DATA_FACTORY_SERVICE_NAME,
							path,
							NULL, &error);
	if (error)
		printf("Failed to create store proxy %s\n", error->message);

	/* Get Folder Info */
	if (!egdbus_store_ms_call_get_folder_info_sync(store_proxy, "", CAMEL_STORE_FOLDER_INFO_RECURSIVE|CAMEL_STORE_FOLDER_INFO_FAST | CAMEL_STORE_FOLDER_INFO_SUBSCRIBED, &infos, NULL, &error))
		printf("Error %s\n", error->message);
	
	printf("Registering signalhandlers\n\n");
	g_signal_connect (store_proxy, "folder-opened", G_CALLBACK (folder_opened_cb), NULL);
	g_signal_connect (store_proxy, "folder-created", G_CALLBACK (folder_created_cb), NULL);
	g_signal_connect (store_proxy, "folder-deleted", G_CALLBACK (folder_deleted_cb), NULL);
	g_signal_connect (store_proxy, "folder-renamed", G_CALLBACK (folder_renamed_cb), NULL);
	g_signal_connect (store_proxy, "folder-subscribed", G_CALLBACK (folder_subscribed_cb), NULL);
	g_signal_connect (store_proxy, "folder-unsubscribed", G_CALLBACK (folder_unsubscribed_cb), NULL);

	parse_infos (store_proxy, infos);
	


	return FALSE;
}

int 
main(int argc, char* argv[])
{
	gtk_init_with_args (
		&argc, &argv,
		_("- The Evolution Mail Data Server"),
		NULL, (gchar *) GETTEXT_PACKAGE, NULL);

	g_type_init ();
	g_set_prgname ("e-mail-test-client");
	if (!g_thread_supported ()) g_thread_init (NULL);


	g_idle_add ((GSourceFunc) start_test_client, NULL);
	gtk_main ();

   	return 0;
}
