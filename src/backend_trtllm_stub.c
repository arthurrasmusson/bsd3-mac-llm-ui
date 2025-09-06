/*==============================================================================
 * src/backend_trtllm_stub.c
 * Stubbed TRT-LLM backend: returns 501 when TensorRT-LLM is not compiled in.
 * License: BSD3
 *============================================================================*/
#include "../include/llm_backend.h"
#include <string.h>
#include <stdlib.h>

static char *sdup(const char *s){ size_t n=strlen(s)+1; char *p=(char*)malloc(n); if(p) memcpy(p,s,n); return p; }

int llm_trtllm_complete(const struct llm_req *r, struct llm_resp *out){
	(void)r;
	if(out){
		memset(out,0,sizeof *out);
		out->status = 501;
		out->err = sdup("TRT-LLM backend not available. Build with HAVE_TRTLLM=1 and provide TRTLLM_CXXFLAGS / TRTLLM_LIBS.");
	}
	return -1;
}
