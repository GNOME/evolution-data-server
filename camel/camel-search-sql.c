/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2008 Novell, Inc. (www.novell.com)
 *
 *  Author: Srinivasa Ragavan  <sragavan@novell.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/* This is a helper class for folders to implement the search function.
   It implements enough to do basic searches on folders that can provide
   an in-memory summary and a body index. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include "camel-search-sql.h"

#define d(x) 

#ifdef TEST_MAIN
#include <sqlite3.h>

char * camel_db_get_column_name (const char *raw_name);

char * 
camel_db_sqlize_string (const char *string)
{
	return sqlite3_mprintf ("%Q", string);
}

void 
camel_db_free_sqlized_string (char *string)
{
	sqlite3_free (string);
	string = NULL;
}
#else
#include "camel-db.h"
#endif
char * escape_values (char *str);

static GScannerConfig config =
        {
                (
                        " \t\n\r"
                        )                       /* cset_skip_characters */,
                (
                        G_CSET_a_2_z
                        G_CSET_A_2_Z
                        )                       /* cset_identifier_first */,
                (
                        G_CSET_a_2_z
                        "_-0123456789"
                        G_CSET_A_2_Z
                        )                       /* cset_identifier_nth */,
                ( "#\n" )               /* cpair_comment_single */,
                FALSE                    /* case_sensitive */,
                TRUE                    /* skip_comment_multi */,
                TRUE                    /* skip_comment_single */,
                TRUE                    /* scan_comment_multi */,
                TRUE                    /* scan_identifier */,
                TRUE                    /* scan_identifier_1char */,
                FALSE                   /* scan_identifier_NULL */,
                TRUE                   /* scan_symbols */,
                FALSE                   /* scan_binary */,
                FALSE                   /* scan_octal */,
                FALSE                    /* scan_float */,
                FALSE                   /* scan_hex */,
                FALSE                   /* scan_hex_dollar */,
                TRUE                    /* scan_string_sq */,
                TRUE                    /* scan_string_dq */,
                FALSE                   /* numbers_2_int */,
                FALSE                   /* int_2_float */,
                FALSE                   /* identifier_2_string */,
                TRUE                    /* char_2_token */,
                FALSE                   /* symbol_2_token */,
                FALSE                   /* scope_0_fallback */,
        };

typedef struct Node {
	char *token; /* Token to search*/
	char *exact_token; /* Token to substitute */
	int nodes; /* Number of nodes to process */
	char pre_token; /* Pre token to prepend with value substitute*/
	char post_token; /* post token to apppend with substitute */
	char rval; /* rhs value for binary ops */
	int level; /* depth in the hier */
	int prefix:1 ; /* unary operator to be searched ?*/
	int sys_node:1 ; /* is it a predefined term ? */
	int ignore_lhs:1; /* ignore lhs value ?*/
	int swap :1;
	int prenode :1;
	int operator:1;
	int execute:1;
	int ref;
}Node;

/* 
 * Design of the sexp parser
 *
 * Every node is a operator/operand (sysnode operand[like known headers] or normal operand)
 * Every sysnode has a min nodes to operate and we operate at it. Every time a sysnode is encountered
 * the further nodes's min nodes are reduced from the prev nodes and will go till it becomes 1. Then the nodes
 * till the sysnode is popped from the stack executed and added to the stack.
 *
 * Once the entire parse is completed, we parse the stack, take operands, till you find a operator. If you find a operator
 * then operate that on all the popped operands and push it back to the stack. Repeat till you have just one node on the stack.
 * Sexp has at times single operand, in which case the operator is ignored.
 *
 * We use 3 stacks, all, operators and operand. 'All' is the stack that is used in the second iteration and operand/operators stack
 * is used for the first iteration.
 *
 * Rest of the code sucks and might not work for all statements. Just could be Evolution specific.
 *
 * */

/* Configuration of your sexp expression */
static Node elements[] =  { {"header-contains", "LIKE", 3, '%', '%', 0, 0, 0 , 1, 0, 0, 0, 0, 0}, 
			    {"system-flag", "=", 2, ' ', ' ', '1', 0, 1, 1, 0, 0, 0, 0, 0}, 
			    {"match-all", "", 0, ' ', ' ', 0, 0, 0, 1, 0, 0, 0, 0, 0},
			    {"cast-int", "", 0, ' ', ' ', 0, 0, 0, 1, 0, 0, 0, 0, 0}, 			    
			    { "header-matches", "LIKE", 3, ' ', ' ', 0, 0, 1, 1, 0, 0, 0, 0, 0}, 
			    { "header-ends-with", "LIKE", 3, '%', ' ', 0, 0, 0, 1, 0, 0, 0, 0, 0}, 
			    { "header-exists", "NOTNULL", 2, ' ', ' ', ' ', 0, 0, 1, 0, 0, 0, 0, 0},
			    { "user-tag", "usertags", 3, '%', '%', 0, 0, 1, 1, 1, 0, 0, 0, 0},
			    { "user-flag", "labels LIKE", 2, '%', '%', 0, 0, 0, 1, 0, 1, 0, 0, 0},
			    { "header-starts-with", "LIKE", 3, ' ', '%', 0, 0, 0, 1, 0, 0, 0, 0, 0},
			    { "get-sent-date", "dsent", 2, ' ', ' ', 0, 0, 1, 1, 0, 0, 0, 0, 1, 0 },
			    { "get-received-date", "dreceived", 2, ' ', ' ', 0, 0, 1, 1, 0, 0, 0, 0, 1, 0 },
			    { "get-size", "size", 2, ' ', ' ', ' ', 0, 1, 1, 0, 0, 0, 0, 1}
};
#if 0		 
	{ "get-sent-date", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_sent_date), 1 },
	{ "get-received-date", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_received_date), 1 },
	{ "get-current-date", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_current_date), 1 },
	{ "get-size", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, get_size), 1 },
	{ "uid", CAMEL_STRUCT_OFFSET(CamelFolderSearchClass, uid), 1 },

#endif

static void
free_node (Node *node)
{
	node->ref--;

	if (node->ref)
		return;
	
	d(printf("freeing %s %s %p\n", node->token, node->exact_token, node));
	g_free (node->token);
	g_free (node->exact_token);
	g_free (node);
}

static void
g_list_dump (GList *l)
{
	while (l) {
		 printf("%s\t", (char *)l->data);
		l = l->next;
	}
}

static void
g_node_dump (GList *l)
{
	Node *node;

	while (l) {
		node = l->data;
		printf("%p (%d '%s')\t", node, node->ref, node->exact_token);
		l = l->next;
	}
	printf("\n");

}

char *
escape_values (char *str)
{
	char *sql = camel_db_sqlize_string (g_strstrip(str));
	 char *ret = g_strdup (sql);

	 camel_db_free_sqlized_string (sql);
	 /* I dont want to manage sql strings here */
	 
	 return ret;
}

/**
 * camel_search_sexp_to_sql:
 * @txt: A valid sexp expression
 * 
 * Converts a valid sexp expression to a sql statement with table fields converted into it.
 * This is very specific to Evolution. It might crash is the sexp is invalid. The callers must ensure that the sexp is valid
 **/

char *
camel_sexp_to_sql (const char *txt) 
{
	GScanner *scanner = g_scanner_new (&config);
	char *sql;
	int level = 0;
	GList *tlist;
	GList *operators=NULL, *operands=NULL, *all=NULL;
	GList *tmp;
	Node *n1=NULL, *n2=NULL, *n3=NULL, *op=NULL;
	GList *res=NULL;
	gboolean last_sysnode = FALSE;
	
	d(printf("len = %d\n", strlen (txt)));
	
	g_scanner_input_text (scanner, txt, strlen(txt));
	while (!g_scanner_eof (scanner)) {
		Node *mnode;
		guint token = g_scanner_get_next_token (scanner);
		
		/* Extract and identify tokens */
		if (token == G_TOKEN_IDENTIFIER || token == G_TOKEN_STRING) {
			char *token = scanner->value.v_string;

			d(printf("token %s\n", token));
			if (g_ascii_strcasecmp (token, "and") == 0 ||
				g_ascii_strcasecmp (token, "or") == 0 ||
					g_ascii_strcasecmp (token, "not") == 0) {
				/* operator */
				Node *node = g_new0 (Node, 1);
				
				node->token = g_strdup (token);
				node->exact_token =  g_strdup (token);
				node->level = level;
				node->operator = 1;
				node->ref = 2;
				operators = g_list_prepend (operators, node);
				all = g_list_prepend (all, node);
			} else {
				/* Should be operand*/
				int i;
				Node *node;
				
				for (i=0; i < G_N_ELEMENTS(elements); i++) {
					if (g_ascii_strcasecmp (elements[i].token, token) == 0) {
						 
						if (!*elements[i].exact_token) /* Skip match-all */
							break;
						
						node = g_new0 (Node, 1);
						node->token = g_strdup (elements[i].token);
						node->exact_token = g_strdup (elements[i].exact_token);
						node->nodes = elements[i].nodes;
						node->prefix = elements[i].prefix;
						node->pre_token = elements[i].pre_token;
						node->post_token = elements[i].post_token;
						node->rval = elements[i].rval;
						node->sys_node = 1;
						node->level = level;
						node->ignore_lhs = elements[i].ignore_lhs;
						node->swap = elements[i].swap;
						node->prenode = elements[i].prenode;
						node->operator = 0;
						node->execute = elements[i].execute;
						node->ref = 2;
						operands = g_list_prepend (operands, node);
						all = g_list_prepend (all, node);
						last_sysnode = TRUE;
						
						break;
					}
				}

				/* These should be normal tokens */
				if (i >= G_N_ELEMENTS(elements)) {
					Node *pnode = operands->data;
					
					node = g_new0 (Node, 1);
					node->token = g_strdup (token);
					if (last_sysnode) {
						 last_sysnode = FALSE;
						 node->exact_token = camel_db_get_column_name (token);
					} else
						 node->exact_token = g_strdup (token);
					
					node->nodes = pnode->nodes > 0 ? pnode->nodes - 1:0;
					node->prefix = 0;
					node->rval = ' ';
					node->level = level;
					node->sys_node = 0;
					node->operator = 0;
					node->execute = 0;
					node->ref = 2;
					operands = g_list_prepend (operands, node);
					all = g_list_prepend (all, node);
				}
			}

		} else if (token == G_TOKEN_LEFT_PAREN)  {
			d(printf("(\n"));
			level++;
		} else if (token == G_TOKEN_RIGHT_PAREN) {
			d(printf(")\n"));
			level--;
		} else if (token == G_TOKEN_EQUAL_SIGN) {
			Node *node = g_new0 (Node, 1); 

			node->token = g_strdup ("=");
			node->exact_token =  g_strdup ("=");
			node->level = level;
			node->ref = 2;
			operators = g_list_prepend (operators, node);		
			all = g_list_prepend (all, node);
		} else if (token == '+') {
			char *astr=NULL, *bstr=NULL;
			Node *node, *pnode = operands->data;
			int lvl=0, lval=0;

			if (g_ascii_strcasecmp (pnode->token, "user-flag") == 0) {
				    /* Colloct all after '+' and append them to one token. Go till you find ')' */
				    token = g_scanner_get_next_token (scanner) ;
				    while (token != G_TOKEN_RIGHT_PAREN) {
					    astr = g_strdup_printf ("%s%s", bstr?bstr:"", scanner->value.v_string);
					    g_free (bstr); bstr = astr;
					    token = g_scanner_get_next_token (scanner);
				    }
			} else {
				/* should be the date fns*/
			/* Colloct all after '+' and append them to one token. Go till you find ')' */
				token = g_scanner_get_next_token (scanner) ;
				while (token >= 0 && !g_scanner_eof(scanner) && lvl >=0 ) {
					if (token == G_TOKEN_RIGHT_PAREN) {
						d(printf(")\n"));
						lvl--;
						token = g_scanner_get_next_token (scanner);
						continue;
					} else if (token == G_TOKEN_LEFT_PAREN) {
						d(printf("(\n"));
						lvl++;
						token = g_scanner_get_next_token (scanner);
						continue;
					} else if (token == G_TOKEN_INT) {
						d(printf("int %d\n", scanner->value.v_int));
						lval = lval + scanner->value.v_int;
					} else {
						d(printf("str %s\n", scanner->value.v_string));
						if (g_ascii_strcasecmp (scanner->value.v_string, "get-current-date") == 0) {
							lval = time(NULL); //Make this 12:00 am
						} else
							lval = atol (scanner->value.v_string);
						d(printf("str %d\n", lval));
					}
					token = g_scanner_get_next_token (scanner);
				}
				// (> (get-sent-date) (- (get-current-date) 100))) )
				d(printf("lvl = %d %ld\n", lvl, lval));
				bstr = g_strdup_printf ("%d", lval);
			}
			
			node = g_new0 (Node, 1);
			node->token = bstr;
			node->exact_token = g_strdup(bstr);
			node->nodes = pnode->nodes > 0 ? pnode->nodes - 1:0;
			node->prefix = 0;
			node->rval = ' ';
			node->level = level;
			node->sys_node = 0;
			node->ref = 2;
			operands = g_list_prepend (operands, node);	
			all = g_list_prepend (all, node);

			level--;
		} else if (token == '-') {
			char *bstr=NULL;
			Node *node, *pnode = operands->data;
			int lvl=0, lval=0;
			
			/* Colloct all after '+' and append them to one token. Go till you find ')' */
			token = g_scanner_get_next_token (scanner) ;
			while (token >= 0 && !g_scanner_eof(scanner) && lvl >=0 ) {
				if (token == G_TOKEN_RIGHT_PAREN) {
					lvl--;
					token = g_scanner_get_next_token (scanner);
					continue;
				} else if (token == G_TOKEN_LEFT_PAREN) {
					lvl++;
					token = g_scanner_get_next_token (scanner);
					continue;
				} else if (token == G_TOKEN_INT) {
					d(printf("int %d\n", scanner->value.v_int));
					lval = lval - scanner->value.v_int;
				} else {
					d(printf("str %s\n", scanner->value.v_string));
					if (g_ascii_strcasecmp (scanner->value.v_string, "get-current-date") == 0) {
						lval = time(NULL); //Make this 12:00 am
					} else
						lval = atol (scanner->value.v_string);
					d(printf("str %d\n", lval));
				}
				token = g_scanner_get_next_token (scanner);
			}
			// (> (get-sent-date) (- (get-current-date) 100))) )
			d(printf("lvl = %ld\n", lval));
			node = g_new0 (Node, 1);
			node->token = bstr;
			node->exact_token = g_strdup_printf("%ld", (long)lval);
			node->nodes = pnode->nodes > 0 ? pnode->nodes - 1:0;
			node->prefix = 0;
			node->rval = ' ';
			node->level = level;
			node->sys_node = 0;
			node->ref = 2;
			operands = g_list_prepend (operands, node);	
			all = g_list_prepend (all, node);
			level--;
				//g_node_dump (all);printf("\n\n");
				//g_node_dump (operands);printf("\n\n");
				//g_node_dump (operators);printf("\n\n");
			
		} else if (token == '>' || token == '<') {
			
				/* operator */
				Node *node = g_new0 (Node, 1);
				
				node->token = g_strdup_printf ("%c", token);
				node->exact_token =  g_strdup_printf ("%c", token);
				node->level = level;
				node->operator = 1;
				node->ref = 2;
				operators = g_list_prepend (operators, node);
				all = g_list_prepend (all, node);			
		} else if (token == G_TOKEN_INT) {
			Node *pnode = operands->data, *node;

			node = g_new0 (Node, 1);
			node->token = g_strdup_printf ("%ld", scanner->value.v_int) ;
			node->exact_token = g_strdup_printf ("%ld", scanner->value.v_int);
			node->nodes = pnode->nodes > 0 ? pnode->nodes - 1:0;
			node->prefix = 0;
			node->rval = ' ';
			node->level = level;
			node->sys_node = 0;
			node->operator = 0;
			node->execute = 0;
			node->ref = 2;
			operands = g_list_prepend (operands, node);
			all = g_list_prepend (all, node);
			
		}
				//g_node_dump (all);printf("\n\n");
				//g_node_dump (operands);printf("\n\n");
				//g_node_dump (operators);printf("\n\n");

		if (operands) {
			mnode = operands->data;
			d(printf("recalculating ? %d\n", mnode->nodes));
			
			/* If we reach the operating level, which is the exec min for last seen sys-header */
			if (mnode->nodes == 1) {
				/* lets evaluate */
				int len = 2;
				Node *pnode;

					
				n1=NULL; n2=NULL; n3=NULL;
				tmp = operands;
				n1 = operands->data;
				operands = g_list_delete_link(operands, operands);
				all = g_list_delete_link (all, all);
				tmp = operands;
				n2 = operands->data;
				operands = g_list_delete_link(operands, operands);		
				all = g_list_delete_link (all, all);
				
				/* If it is a sysnode, then it is double operand */
				if (!n2->sys_node) {
					/* This has to be a sysnode if not panic */
					n3 = operands->data;
					operands = g_list_delete_link(operands, operands);
					/* this is a triple operand */					
					len = 3;
					all = g_list_delete_link (all, all);

				}

				if (operands)
					pnode  = operands->data;
				else	
					pnode = NULL;
				
				if (len == 3) {
					char *prefix = NULL;
					char *str, *sqstr, *escstr;
					int dyn_lvl;
					Node *opnode = operators->data;
					char *temp_op="";
					
					if (n3->level < n2->level)
						dyn_lvl = n2->level;
					else
						dyn_lvl = n3->level;
					

					if (n3->prefix && g_ascii_strcasecmp (opnode->token, "=") == 0) {
						/* see if '=' was a last operator. if so take care of it */
						free_node(opnode);
						free_node(opnode);
						all = g_list_delete_link (all, all);
						operators = g_list_delete_link (operators, operators);
						opnode = operators->data;
						if ((g_ascii_strcasecmp (n2->exact_token,  "follow-up") == 0) ||
						    (g_ascii_strcasecmp (n2->exact_token,  "completed-on") == 0)) {
							/* swap */
							char *temp = n2->exact_token;
							n2->exact_token = n1->exact_token;
							n1->exact_token = temp;
							temp = n2->exact_token;
							n2->exact_token = n3->exact_token;
							n3->exact_token = temp;
							n3->ignore_lhs = 0;
							if (g_ascii_strcasecmp (opnode->token, "not") == 0)
								temp_op = "LIKE";
							else
								temp_op = "NOT LIKE";
							prefix="";	
						} else {
							/* user tags like important */
							g_free(n2->exact_token);
							n2->exact_token = n3->exact_token;
							n3->exact_token = g_strdup("");
							temp_op = "LIKE";
							n3->ignore_lhs = 0;
						}
						
							
					}
					if (n3->prefix && ((g_ascii_strcasecmp (opnode->token, ">") == 0) || (g_ascii_strcasecmp (opnode->token, ">") == 0) )) {
						/* see if '=' was a last operator. if so take care of it */
						free_node(opnode);
						free_node(opnode);
						all = g_list_delete_link (all, all);
						operators = g_list_delete_link (operators, operators);
						opnode = operators->data;
						if ((g_ascii_strcasecmp (n2->exact_token,  "follow-up") == 0) ||
						    (g_ascii_strcasecmp (n2->exact_token,  "completed-on") == 0)) {
							/* swap */
							char *temp = n2->exact_token;
							n2->exact_token = n1->exact_token;
							n1->exact_token = temp;
							temp = n2->exact_token;
							n2->exact_token = n3->exact_token;
							n3->exact_token = temp;
							n3->ignore_lhs = 0;
							if (g_ascii_strcasecmp (opnode->token, "not") == 0)
								temp_op = "LIKE";
							else
								temp_op = "NOT LIKE";
							prefix="";	
						} else {
							/* user tags like important */
							g_free(n2->exact_token);
							n2->exact_token = n3->exact_token;
							n3->exact_token = g_strdup("");
							temp_op = "LIKE";
							n3->ignore_lhs = 0;
						}
						
							
					} 					
					
					/* Handle if 'not' was a last sysnode, if so take care of it */
					if (n3->prefix && g_ascii_strcasecmp (opnode->token, "not") == 0) {
						if (!prefix)
							prefix = "NOT ";
						free_node(opnode);
						free_node(opnode);
						operators = g_list_delete_link (operators, operators);
						all = g_list_delete_link (all, all);
					} 
					
					/* n2 needs to be db specific */
					sqstr = g_strdup_printf("%c%s%c", n3->pre_token, n1->exact_token, n3->post_token);
					escstr = escape_values(sqstr);
					str = g_strdup_printf("(%s %s%s %s %s)", n3->ignore_lhs ? "":n2->exact_token, prefix ? prefix : " ", temp_op, n3->exact_token, escstr);
					//printf("str %s\n", str);

					g_free (n3->exact_token);
					g_free (sqstr);
					g_free (escstr);
					
					n3->exact_token = str;
					n3->prefix = 0;
					n3->nodes = (pnode ? pnode->nodes : 0 ) > 0 ? pnode->nodes -1 : 0;
					n3->level = dyn_lvl;
					operands = g_list_prepend (operands, n3);
					free_node (n2); free_node(n1);
					free_node (n2); free_node(n1);
					d(printf("Pushed %s\n", n3->exact_token));
					all = g_list_prepend (all, n3);
				} else {
					char prefix = 0;
					char *str, *estr;
					Node *opnode = operators->data;
					int dyn_lvl = n1->level;

					if (n2->prefix && g_ascii_strcasecmp (opnode->token, "not") == 0) {
						prefix = '!';
						dyn_lvl = opnode->level;
						free_node(opnode); free_node(opnode);
						operators = g_list_delete_link (operators, operators);
						all = g_list_delete_link (all, all);
						//g_node_dump (operators);
					}

					if (n2->execute) {
						Node *popnode=NULL;
						gboolean dbl = FALSE;
						
						//g_node_dump (operators);
						if (n2->prefix) {
							if (operators->next)
								popnode = operators->next->data;

							if (g_ascii_strcasecmp (popnode->token, "not") == 0) {
								prefix = '!';
								dbl = TRUE;
							}
						}
						str = g_strdup_printf("(%s %c%s %s)", n2->exact_token, prefix ? prefix : ' ', opnode->exact_token, n1->exact_token);
						
						free_node(opnode); free_node(opnode);
						operators = g_list_delete_link (operators, operators);
						all = g_list_delete_link (all, all);
						if (dbl) {
							free_node(opnode); free_node(opnode);
							operators = g_list_delete_link (operators, operators);
							all = g_list_delete_link (all, all);							
						}
						
					} else {
						if (!n2->swap) {
							str = g_strdup_printf("(%s %c%s %c)", n1->exact_token, prefix ? prefix : ' ', n2->exact_token, n2->rval);
						} else {
							str = g_strdup_printf("%c%c%s%c", prefix ? prefix : ' ', n2->pre_token, n1->exact_token, n2->post_token);
							estr = escape_values(str);
							g_free(str);
							str = g_strdup_printf("(%s %s %c)", n2->exact_token, estr, n2->rval ? n2->rval : ' ');
							g_free(estr);
						}
					}
					g_free (n2->exact_token);
					
					n2->exact_token = str;
					n2->prefix = 0;
					n2->nodes = (pnode ? pnode->nodes : 0 )> 0 ? pnode->nodes -1 : 0;
					n2->level = dyn_lvl;
					operands = g_list_prepend (operands, n2);
					d(printf("Pushed %s\n", n2->exact_token));					
					free_node(n1);
					free_node(n1);

					all = g_list_prepend (all, n2);
				}
					
			}
		}
	

	}
	

	tmp = operands;
	d(g_node_dump (operands));
	while (tmp) {
		 free_node(tmp->data); 
		 tmp = tmp->next;
	}
	d(g_node_dump (operands));

	g_list_free (operands);
	d(printf("\n\n\n"));
	d(g_node_dump (operators));
	tmp = operators;
	while (tmp) {
		 free_node(tmp->data); 
		 tmp = tmp->next;
	}	
	g_list_free (operators);
	d(printf("\n\n\n"));
	d(g_node_dump (all));
	d(printf("\n\n\n"));
	
	
	res=NULL;
	tmp = all;
	op=NULL; n1=NULL;

	/* Time to operate on the stack. */
	tmp = all;
	while (all->next) {
		 n1 = tmp->data;
		 all = g_list_delete_link (all, all);
		 tmp = all;

		 if (n1->operator) {
			  if (res->next) {
				   GList *ts=res;
				   Node *n = ts->data;
				   GString *s = g_string_new (NULL);
					
				   g_string_append_printf (s, "(%s", n->exact_token);
				   ts = ts->next;
				   while (ts) { /* should have atleast 2 nodes */
						free_node (n);
						n = ts->data;
						g_string_append_printf (s, " %s %s", n1->exact_token, n->exact_token);
						ts = ts->next;
				   }

				   g_string_append_printf (s, ")");
				   g_free (n->exact_token);
				   n->exact_token = s->str;
				   g_string_free (s, FALSE);
				   all = g_list_prepend (all, n); 
			  } else /* remove single operand nodes */
				   all = g_list_prepend (all, res->data);

			  free_node (n1);
			  g_list_free (res);
			  res = NULL;
			  tmp = all;
		 } else {
			  res = g_list_prepend (res, n1);
		 }
	}
		
	n1 = res->data;
	sql = g_strdup (n1->exact_token);
	free_node (n1);
	g_list_free (res);
	
	tlist = all;
	while (tlist) {
		free_node (tlist->data);
		tlist = tlist->next;
	}
	g_list_free (all);

	g_scanner_destroy(scanner);

	return sql;
}

#ifdef TEST_MAIN
/*


(and (match-all (and (not (system-flag "deleted")) (not (system-flag "junk"))))
 (and   (or
  
     (match-all (not (system-flag "Attachments")))
    
  )
 ))
 
"
replied INTEGER ,                (match-all (system-flag  "Answered"))
size INTEGER ,                   (match-all (< (get-size) 100))
dsent NUMERIC ,                  (match-all (< (get-sent-date) (- (get-current-date) 10)))
dreceived NUMERIC ,               (match-all (< (get-received-date) (- (get-current-date) 10)))
//mlist TEXT ,                      x-camel-mlist   (match-all (header-matches "x-camel-mlist"  "gnome.org"))
//attachment,                      system-flag "Attachments"   (match-all (system-flag "Attachments")) 
//followup_flag TEXT ,             (match-all (not (= (user-tag "follow-up") "")))
//followup_completed_on TEXT ,      (match-all (not (= (user-tag "completed-on") "")))
//followup_due_by TEXT ," //NOTREQD
*/

char * camel_db_get_column_name (const char *raw_name)
{
	d(g_print ("\n\aRAW name is : [%s] \n\a", raw_name));
	if (!g_ascii_strcasecmp (raw_name, "Subject"))
		return g_strdup ("subject");
	else if (!g_ascii_strcasecmp (raw_name, "from"))
		return g_strdup ("mail_from");
	else if (!g_ascii_strcasecmp (raw_name, "Cc"))
		return g_strdup ("mail_cc");
	else if (!g_ascii_strcasecmp (raw_name, "To"))
		return g_strdup ("mail_to");
	else if (!g_ascii_strcasecmp (raw_name, "Flagged"))
		return g_strdup ("important");
	else if (!g_ascii_strcasecmp (raw_name, "deleted"))
		return g_strdup ("deleted");
	else if (!g_ascii_strcasecmp (raw_name, "junk"))
		return g_strdup ("junk");
	else if (!g_ascii_strcasecmp (raw_name, "Answered"))
		return g_strdup ("replied");	
	else if (!g_ascii_strcasecmp (raw_name, "Seen"))
		return g_strdup ("read");
	else if (!g_ascii_strcasecmp (raw_name, "user-tag"))
		return g_strdup ("usertags");	
	else if (!g_ascii_strcasecmp (raw_name, "user-flag"))
		return g_strdup ("labels");	
	else if (!g_ascii_strcasecmp (raw_name, "Attachments"))
		return g_strdup ("attachment");
	else if (!g_ascii_strcasecmp (raw_name, "x-camel-mlist"))
		return g_strdup ("mlist");	
	else {
		/* Let it crash for all unknown columns for now. 
		We need to load the messages into memory and search etc. 
		We should extend this for camel-folder-search system flags search as well 
		otherwise, search-for-signed-messages will not work etc.*/

		return g_strdup (raw_name);
	}

}

int main ()
{

	int i=0;
	char *txt[] = {
	"(and  (and   (match-all (header-contains \"From\"  \"org\"))   )  (match-all (not (system-flag \"junk\"))))", 
	"(and  (and (match-all (header-contains \"From\"  \"org\"))) (and (match-all (not (system-flag \"junk\"))) (and   (or (match-all (header-contains \"Subject\"  \"test\")) (match-all (header-contains \"From\"  \"test\"))))))", 
	"(and  (and   (match-all (header-exists \"From\"))   )  (match-all (not (system-flag \"junk\"))))", 
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (header-contains \"Subject\"  \"org\")) (match-all (header-contains \"From\"  \"org\")) (match-all (system-flag  \"Flagged\")) (match-all (system-flag  \"Seen\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) (match-all (or (= (user-tag \"label\")  \"work\")  (user-flag  \"work\"))) )))", 
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) ((= (user-tag \"label\")  \"work\") ) )))", 
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) (user-flag  \"work\") )))", 
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (header-ends-with \"To\"  \"novell.com\") (header-ends-with \"Cc\"  \"novell.com\"))) (user-flag  (+ \"$Label\"  \"work\")) )))",

	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (not (= (user-tag \"follow-up\") \"\"))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (= (user-tag \"follow-up\") \"\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (not (= (user-tag \"completed-on\") \"\"))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (or (= (user-tag \"label\")  \"important\") (user-flag (+ \"$Label\"  \"important\")) (user-flag  \"important\"))) ))",		
	"(or (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")) (not (system-flag \"Attachments\")) (not (system-flag \"Answered\")))) (and   (or (match-all (= (user-tag \"completed-on\") \"\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (= (user-tag \"completed-on\") \"\")) (match-all (= (user-tag \"follow-up\") \"\")) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (> (get-sent-date) (- (get-current-date) 100))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (< (get-sent-date) (+ (get-current-date) 100))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (not (= (get-sent-date) 1216146600))) )))",
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (or (match-all (= (get-sent-date) 1216146600)) )))"	,
	"(and (match-all (and (not (system-flag \"deleted\")) (not (system-flag \"junk\")))) (and   (and (match-all (header-contains \"Subject\"  \"mysubject\")) (match-all (not (header-matches \"From\"  \"mysender\"))) (match-all (= (get-sent-date) (+ (get-current-date) 1))) (match-all (= (get-received-date) (- (get-current-date) 604800))) (match-all (or (= (user-tag \"label\")  \"important\") (user-flag (+ \"$Label\"  \"important\")) (match-all (< (get-size) 7000)) (match-all (not (= (get-sent-date) 1216146600)))  (match-all (> (cast-int (user-tag \"score\")) 3))  (user-flag  \"important\"))) (match-all (system-flag  \"Deleted\")) (match-all (not (= (user-tag \"follow-up\") \"\"))) (match-all (= (user-tag \"completed-on\") \"\")) (match-all (system-flag \"Attachments\")) (match-all (header-contains \"x-camel-mlist\"  \"evo-hackers\")) )))",

	};

	for (i=18; i < G_N_ELEMENTS(txt); i++) {
		char *sql;
		printf("Q: %s\n\n", txt[i]);		
		sql = camel_sexp_to_sql (txt[i]);
		printf("A: %s\n\n\n", sql);
		g_free (sql);
	}

}
#endif
