

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <camel/camel-object.h>
#include <camel/camel-mime-utils.h>

#include "camel-test.h"

struct {
	char *header;
	char *values[5];
} test1[] = {
	{ "<test@camel.host>", { "test@camel.host" } },
	{ "(this is a comment) <test@camel.host>", { "test@camel.host" } },
	{ "<test@camel.host> (this is a comment)", { "test@camel.host" } },
	{ "<test@camel.host> This is total rubbish!", { "test@camel.host" } },
	{ "<<test.groupwise@bug.novell>@novell>", { "test.groupwise@bug.novell" } },
	{ "<<test.groupwise@bug.novell>@novell> <test@camel.host>",
	  { "test@camel.host", "test.groupwise@bug.novell" } },
	{ "<test@camel.host> <<test.groupwise@bug.novell>@novell> <test@camel.host>",
	  { "test@camel.host", "test.groupwise@bug.novell", "test@camel.host" } },
	{ "<<test.groupwise@bug.novell>@novell> <test@camel.host> <<test.groupwise@bug.novell>@novell>",
	  { "test.groupwise@bug.novell", "test@camel.host", "test.groupwise@bug.novell" } },
};

int
main (int argc, char **argv)
{
	int i, j;

	camel_test_init(argc, argv);

	camel_test_start("references decoding");

	for (i=0;i<sizeof(test1)/sizeof(test1[0]);i++) {
		struct _camel_header_references *head, *node;

		camel_test_push("references decoding[%d] '%s'", i, test1[i].header);
		head = camel_header_references_decode(test1[i].header);
		node = head;
		for (j=0;test1[i].values[j];j++) {
			check_msg(node != NULL, "didn't find all references");
			check(strcmp(test1[i].values[j], node->id) == 0);
			node = node->next;
		}
		check_msg(node == NULL, "found more references than should have");
		camel_header_references_list_clear(&head);
		camel_test_pull();
	}

	camel_test_end();

	return 0;
}
