/*==============================================================================
 * file: config.def.h
 * minimal, suckless-style compile-time configuration (copy to config.h)
 * License: BSD3
 *============================================================================*/
#ifndef CONFIG_H
#define CONFIG_H

/* Default bind address (host:port) */
#define DEF_BIND_ADDR     "127.0.0.1:8080"

/* Default backend: "openai" or "trtllm" */
#define DEF_BACKEND       "openai"

/* OpenAI-compatible settings */
#define DEF_API_BASE      "https://api.openai.com"
#define DEF_MODEL         "gpt-4o-mini"
#define DEF_TEMPERATURE   0.6
#define DEF_MAX_TOKENS    1024

/* HTML theme bits */
#define APP_TITLE         "llmserv"
#define CSS_INLINE \
"body{max-width:52rem;margin:2rem auto;font:16px/1.35 system-ui,Arial,Helvetica,sans-serif}" \
"h1{font-weight:600;font-size:1.25rem;margin:0 0 1rem}" \
"form{margin:0 0 1rem}" \
"textarea{width:100%;min-height:9rem}" \
"pre{white-space:pre-wrap;background:#f7f7f7;padding:1rem;border-radius:.25rem}" \
"label{display:block;margin:.5rem 0 .25rem;color:#333}" \
"input[type=text],input[type=number]{width:100%;}" \
".row{display:flex;gap:1rem}" \
".col{flex:1}" \
".warn{color:#a00}" \
".footer{margin-top:1rem;color:#777;font-size:.9rem}"

/* Limits and timeouts */
#define MAX_REQ_BODY      (256*1024)       /* 256 KiB form body cap        */
#define MAX_RESP_BODY     (4*1024*1024)    /* 4 MiB upstream HTTP cap      */
#define MAX_RENDER        (4*1024*1024)    /* 4 MiB HTML render cap        */
#define MAX_TRANSCRIPT    (128*1024)       /* cap stateless transcript     */
#define MAX_TURNS         12               /* last N turns kept            */
#define IO_TIMEOUT_SEC    60

/* Security headers */
#define CSP_HEADER "Content-Security-Policy: default-src 'none'; form-action 'self'; style-src 'self' 'unsafe-inline'\r\n"
#define XFO_HEADER "X-Frame-Options: DENY\r\n"
#define REF_HEADER "Referrer-Policy: no-referrer\r\n"
#define CACHECTL   "Cache-Control: no-store\r\n"

/* Feature toggles (compile-time) */
#define WITH_CSRF     0   /* keep 0 to avoid pulling crypto */
#define WITH_PLEDGE   1   /* auto ignored on Linux */
#define WITH_UNVEIL   1   /* auto ignored on Linux */

#endif
