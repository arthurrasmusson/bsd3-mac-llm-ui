/*==============================================================================
 * src/main.c  —  entry point; CLI parsing; selects mode (web/gtk/qt)
 * License: BSD3
 *============================================================================*/
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include "util.h"
#include "httpd.h"
#include "sandbox.h"
#include "../include/llm_backend.h"
#include "../config.h"

/* GUI launchers provided by GUI files if built; otherwise stubs below */
int run_gtk_gui(llm_fn);
int run_qt_gui(llm_fn);

#ifndef WITH_GTK
int run_gtk_gui(llm_fn fn){ (void)fn; fprintf(stderr,"GTK disabled at build\n"); return 1; }
#endif
#ifndef WITH_QT
int run_qt_gui(llm_fn fn){ (void)fn; fprintf(stderr,"Qt disabled at build\n"); return 1; }
#endif

static void usage(const char *prog){
	fprintf(stderr,
"usage: %s [--bind HOST:PORT] [--backend openai|trtllm]\n"
"          [--api-base URL] [--api-key-file FILE] [--model NAME]\n"
"          [--temp N] [--max-tokens N] [--trtllm-engine PATH]\n"
"          [--hme-command CMD ... --] [--no-network]\n"
"          [--local-gui gtk|qt] [-v]\n", prog);
	exit(2);
}

int main(int argc, char **argv){
	struct server_cfg cfg={0};
	cfg.bind_addr=DEF_BIND_ADDR;
	cfg.backend=DEF_BACKEND;
	cfg.api_base=DEF_API_BASE;
	cfg.model=DEF_MODEL;
	cfg.temperature=DEF_TEMPERATURE;
	cfg.max_tokens=DEF_MAX_TOKENS;

	const char *gui=NULL;
	char *api_key_mem=NULL;

	for(int i=1;i<argc;i++){
		if(!strcmp(argv[i],"--bind") && i+1<argc){ cfg.bind_addr=argv[++i]; continue; }
		if(!strcmp(argv[i],"--backend") && i+1<argc){ cfg.backend=argv[++i]; continue; }
		if(!strcmp(argv[i],"--api-base") && i+1<argc){ cfg.api_base=argv[++i]; continue; }
		if(!strcmp(argv[i],"--api-key-file") && i+1<argc){
			size_t n=0; char *k=read_file(argv[++i], &n);
			if(!k) die("cannot read key file");
			str_trim(k); cfg.api_key=k; api_key_mem=k; continue;
		}
		if(!strcmp(argv[i],"--model") && i+1<argc){ cfg.model=argv[++i]; continue; }
		if(!strcmp(argv[i],"--temp") && i+1<argc){ cfg.temperature=atof(argv[++i]); continue; }
		if(!strcmp(argv[i],"--max-tokens") && i+1<argc){ cfg.max_tokens=atoi(argv[++i]); continue; }
		if(!strcmp(argv[i],"--trtllm-engine") && i+1<argc){ cfg.trt_engine=argv[++i]; continue; }
		if(!strcmp(argv[i],"--hme-command") && i+1<argc){
			cfg.hme_argv = (const char**)&argv[i+1];
			cfg.hme_argc = argc-(i+1);
			/* scan for `--` to terminate argv explicitly if present */
			for(int j=i+1;j<argc;j++){ if(!strcmp(argv[j],"--")){ argv[j]=NULL; cfg.hme_argc = j-(i+1); break; } }
			break;
		}
		if(!strcmp(argv[i],"--no-network")){ cfg.no_network=1; continue; }
		if(!strcmp(argv[i],"--local-gui") && i+1<argc){ gui=argv[++i]; continue; }
		if(!strcmp(argv[i],"-v")){ cfg.verbose++; continue; }
		usage(argv[0]);
	}

	llm_fn fn = !strcmp(cfg.backend,"trtllm") ? llm_trtllm_complete : llm_openai_complete;

	/* sandbox: allow inbound sockets; on Linux optionally block connect() when --no-network */
	sandbox_init_web(!cfg.no_network);
#ifdef __linux__
	if(cfg.no_network) sandbox_block_connect_linux();
#endif

	if(gui){
		if(!strcmp(gui,"gtk")) return run_gtk_gui(fn);
		if(!strcmp(gui,"qt"))  return run_qt_gui(fn);
		die("unknown gui: %s", gui);
	}

	/* Fallback to env API key if not provided */
	if(!cfg.api_key) cfg.api_key = getenv("OPENAI_API_KEY");
	if(!strcmp(cfg.backend,"openai") && !cfg.hme_argc && cfg.no_network)
		die("openai backend with --no-network requires --hme-command");

	int rc = run_http_server(&cfg, fn);
	free(api_key_mem);
	return rc;
}
