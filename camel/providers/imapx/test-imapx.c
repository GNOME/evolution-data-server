#include <glib.h>
#include "camel-imapx-store.h"
#include "camel-imapx-folder.h"
#include <camel/camel-session.h>

#define URL "imapx://pchenthill@prv1-3.novell.com/;check_lsub;basic_headers=1;imap_custom_headers;command=ssh%20-C%20-l%20%25u%20%25h%20exec%20/usr/sbin/imapd;use_ssl=always"

int 
main (int argc, char *argv [])
{
	CamelSession *session;
	CamelException *ex;
	gchar *uri = NULL;
	CamelService *service;

	g_thread_init (NULL);
	system ("rm -rf /tmp/test-camel-imapx");
	camel_init ("/tmp/test-camel-imapx");
	camel_provider_init ();
	ex = camel_exception_new ();
	
	session = CAMEL_SESSION (camel_object_new (CAMEL_SESSION_TYPE));
	camel_session_construct (session, "/tmp/test-camel-imapx");

	service = camel_session_get_service (session, URL, CAMEL_PROVIDER_STORE, ex);
	camel_service_connect (service, ex);

	exit (1);
}
