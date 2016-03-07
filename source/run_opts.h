/*
 * run_opts.h
 *
 *  Created on: 07.09.2014
 *      Author: drozdov
 */

#ifndef RUN_OPTS_H_
#define RUN_OPTS_H_

typedef struct _run_options {
	char *url;
	char *file_name;
	unsigned short port;
	int show_help;
	int allow_redirect;
} run_options;

extern void show_help(const char *cmd_name);
extern int run_options_parse(int argc, const char *argv[]);

extern run_options run_opts;


#endif /* RUN_OPTS_H_ */
