/*
 * run_opts.c
 *
 *  Created on: 07.09.2014
 *      Author: drozdov
 */

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "run_opts.h"

void show_help(const char *cmd_name) {
	printf("Usage: %s [flags] URL\r\n", cmd_name);
	printf("Flags:\r\n");
	printf("\t-h        - show help\r\n");
	printf("\t-r        - allow redirect\r\n");
	printf("\t-p <port> - alternate port (default is 80)\r\n");
	printf("\t-f <file> - file name to store web page (default is index.html)");
	printf("\r\n");
	printf("URL can start either with http:// or without in and can contain only ASCII symbols. Spaces and special symbols should be converted in %%XX form\r\n");
}

int run_options_parse(int argc, const char *argv[]) {
	run_opts.url = "http://mail.ru";
	int opt = 0;
	memset(&run_opts, 0, sizeof(run_options));
	run_opts.file_name = "./index.html";
	run_opts.port = 80;
	while ((opt = getopt(argc, (char**)argv, "hrp:f:")) != -1) {
		switch (opt) {
		case 'h':
			run_opts.show_help = 1;
			break;
		case 'r':
			run_opts.allow_redirect = 1;
			break;
		case 'p':
			run_opts.port = atoi(optarg);
			break;
		case 'f':
			run_opts.file_name = optarg;
			break;
		default:
			break;
		}
	}
	run_opts.url = (char*)argv[argc-1];
	return 0;
}

run_options run_opts;


