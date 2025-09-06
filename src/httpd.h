/*==============================================================================
 * src/httpd.h
 * License: BSD3
 *============================================================================*/
#ifndef HTTPD_H
#define HTTPD_H
#include "util.h"
#include "tmpl.h"
#include "../include/llm_backend.h"

struct server_cfg {
	const char *bind_addr;
	const char *backend;      /* "openai" or "trtllm" */
	const char *api_base;
	const char *api_key;
	const char **hme_argv;
	int hme_argc;
	int no_network;
	const char *trt_engine;
	const char *model;
	double temperature;
	int max_tokens;
	int verbose;
	int sessioned; /* reserved for future */
};

int run_http_server(const struct server_cfg *cfg, llm_fn fn);

#endif
