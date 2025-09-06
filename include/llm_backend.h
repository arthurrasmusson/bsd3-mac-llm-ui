/*==============================================================================
 * include/llm_backend.h  —  tiny C ABI for backends
 * License: BSD3
 *============================================================================*/
#ifndef LLM_BACKEND_H
#define LLM_BACKEND_H
#ifdef __cplusplus
extern "C" {
#endif

struct llm_msg {
	const char *role;   /* "system" | "user" | "assistant" */
	const char *content;
};

struct llm_req {
	/* messages: system (optional) + history + current user prompt */
	const struct llm_msg *msgs;
	int nmsgs;

	/* knobs */
	const char *model;
	double temperature;
	int max_tokens;

	/* networking (OpenAI-compatible) */
	const char *api_base;  /* e.g. https://api.openai.com */
	const char *api_key;   /* may be NULL when using HME/TRT */
	int no_network;        /* strong intent: do not use outbound nets */

	/* HME/qrexec-style transport (optional):
	   If hme_argc>0, write request JSON to argv[0].. stdin and read JSON
	   reply from stdout in OpenAI /v1/chat/completions shape. */
	const char **hme_argv;
	int hme_argc;

	/* TRT-LLM options */
	const char *trt_engine_path;
};

struct llm_resp {
	char *content;     /* malloc'd; caller frees */
	int status;        /* 0 ok, >0 error */
	int http_status;   /* 200..; 0 if not HTTP */
	char *err;         /* malloc'd error string (nullable) */
};

typedef int (*llm_fn)(const struct llm_req*, struct llm_resp*);

/* Implemented by src/backend_openai.c */
int llm_openai_complete(const struct llm_req*, struct llm_resp*);

/* Implemented by src/backend_trtllm.cpp (HAVE_TRTLLM=1)
   or src/backend_trtllm_stub.c (HAVE_TRTLLM=0) */
int llm_trtllm_complete(const struct llm_req*, struct llm_resp*);

#ifdef __cplusplus
}
#endif
#endif