//=============================================================================
// src/backend_trtllm.cpp
// Minimal TensorRT-LLM backend wrapper (skeleton).
// Build only when HAVE_TRTLLM=1 with proper includes/libs.
// License: BSD3
//=============================================================================
#include "../include/llm_backend.h"
#include <string.h>
#include <stdlib.h>

static char *dupstr(const char *s){ if(!s) return NULL; size_t n=strlen(s)+1; char *p=(char*)malloc(n); if(p) memcpy(p,s,n); return p; }

#ifdef HAVE_TRTLLM
// TODO: include your TRT-LLM headers and wire a real engine here.
// #include <tensorrt_llm/...>

// Example placeholders:
// static YourEngineType *g_engine = nullptr;
// static int ensure_engine(const char *path) { ... return 0 on success ... }
// static char* trt_generate(const struct llm_req *r) { ... return dupstr(generated); }

static int ensure_engine(const char *path){
	(void)path;
	/* Replace with real engine initialization. */
	return 0;
}
static char* trt_generate(const struct llm_req *r){
	(void)r;
	/* Replace with real generation. */
	return dupstr("<trtllm: integrate your generation here>");
}

extern "C" int llm_trtllm_complete(const struct llm_req *r, struct llm_resp *out){
	memset(out,0,sizeof *out);
	if(ensure_engine(r->trt_engine_path? r->trt_engine_path : "engine.plan")<0){
		out->status=2; out->err=dupstr("failed to init TRT-LLM engine"); return -1;
	}
	char *txt = trt_generate(r);
	if(!txt){ out->status=3; out->err=dupstr("TRT-LLM generation failed"); return -1; }
	out->content = txt; out->status=0; out->http_status=0;
	return 0;
}
#else
extern "C" int llm_trtllm_complete(const struct llm_req *r, struct llm_resp *out){
	(void)r; (void)out;
	/* If this file is compiled without HAVE_TRTLLM=1, it's a build config error. */
	return -1;
}
#endif
