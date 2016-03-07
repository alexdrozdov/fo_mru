/*
 * html_parser.h
 *
 *  Created on: 08.09.2014
 *      Author: drozdov
 */

#ifndef HTML_PARSER_H_
#define HTML_PARSER_H_

#include <stdlib.h>

typedef struct _html_parser {
	char* file_name;
	int is_opened;
	FILE* f;
} html_parser;

void html_parser_init(html_parser *hp, char *filename);
void html_parser_close(html_parser *hp);
void html_parse(html_parser *hp, char d);

#endif /* HTML_PARSER_H_ */
