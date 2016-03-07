/*
 * html_parser.c
 *
 *  Created on: 08.09.2014
 *      Author: drozdov
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "html_parser.h"

void html_parser_init(html_parser *hp, char *filename) {
	memset(hp, 0, sizeof(html_parser));
	hp->file_name = filename;
}

void html_parser_close(html_parser *hp) {
	if (hp->is_opened) {
		fclose(hp->f);
	}
	hp->is_opened = 0;
}

void html_parse(html_parser *hp, char d) {
	if (!hp->is_opened) {
		hp->f = fopen(hp->file_name, "wb");
		hp->is_opened = 1;
	}
	fwrite(&d,1,1, hp->f);
}


