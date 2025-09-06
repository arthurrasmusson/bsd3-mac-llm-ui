/*==============================================================================
 * src/backend_openai.c
 * OpenAI-compatible backend using libtls (if TLS_BACKEND_LIBTLS) or curl(1)
 * License: BSD3
 *============================================================================*/
#define _POSIX_C_SOURCE 200809L
#include "../include/llm_backend.h"
#include "util.h"
#include "../config.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#if defined(TLS_BACKEND_LIBTLS)
#include <tls.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

/* --- Minimal JSON builder & string escaper --- */
static void json_escape_into(struct sbuf *b, const char *s){
	sb_putc(b,'"');
	if (s) for(const unsigned char *p=(const unsigned char*)s; *p; ++p){
		switch(*p){
			case '\\': sb_puts(b,"\\\\"); break;
			case '"':  sb_puts(b,"\\\""); break;
			case '\n': sb_puts(b,"\\n"); break;
			case '\r': sb_puts(b,"\\r"); break;
			case '\t': sb_puts(b,"\\t"); break;
			default:
				if(*p < 0x20) sb_printf(b,"\\u%04x", (unsigned)*p);
				else sb_putc(b,*p);
		}
	}
	sb_putc(b,'"');
}

static char *build_openai_json(const struct llm_req *r){
	struct sbuf b; sb_init(&b);
	sb_puts(&b, "{");
	sb_puts(&b, "\"model\":"); json_escape_into(&b, r->model ? r->model : DEF_MODEL);
	sb_printf(&b, ",\"temperature\":%.3f", r->temperature);
	if(r->max_tokens>0) sb_printf(&b, ",\"max_tokens\":%d", r->max_tokens);
	sb_puts(&b, ",\"messages\":[");
	for(int i=0;i<r->nmsgs;i++){
		if(i) sb_putc(&b, ',');
		sb_puts(&b, "{\"role\":"); json_escape_into(&b, r->msgs[i].role);
		sb_puts(&b, ",\"content\":"); json_escape_into(&b, r->msgs[i].content);
		sb_puts(&b, "}");
	}
	sb_puts(&b, "]}");
	return sb_steal(&b);
}

/* Extract first choices[0].message.content via a simple matcher (no full JSON) */
static char *extract_content(const char *json){
	if(!json) return NULL;
	const char *p = strstr(json, "\"content\"");
	if(!p) return NULL;

	/* Find the first quote after the colon: "content": " ... " */
	const char *colon = strchr(p, ':'); if(!colon) return NULL;
	const char *start = strchr(colon, '"'); if(!start) return NULL;
	start++;

	struct sbuf b; sb_init(&b);
	for(const char *s=start; *s; ++s){
		if(*s=='"'){ /* if not escaped, it's the end */
			const char *prev=s-1; int esc=0;
			while(prev>=start && *prev=='\\'){ esc^=1; prev--; }
			if(!esc){
				return sb_steal(&b);
			}
		}
		if(*s=='\\'){
			++s;
			if(!*s){ break; }
			switch(*s){
				case 'n': sb_putc(&b,'\n'); break;
				case 'r': /* ignore */ break;
				case 't': sb_putc(&b,'\t'); break;
				case '"': sb_putc(&b,'"'); break;
				case '\\': sb_putc(&b,'\\'); break;
				default: sb_putc(&b,'\\'); sb_putc(&b,*s); break;
			}
		}else{
			sb_putc(&b,*s);
		}
	}
	sb_free(&b);
	return NULL;
}

/* ---------- HME transport: exec argv[0..] and speak JSON on stdio ---------- */
static int call_hme(const struct llm_req *r, struct llm_resp *out){
	char *json = build_openai_json(r);
	int p_in[2], p_out[2];
	if(pipe(p_in)||pipe(p_out)){ free(json); return -1; }
	pid_t pid=fork();
	if(pid<0){ free(json); return -1; }
	if(pid==0){
		dup2(p_in[0],0); dup2(p_out[1],1);
		close(p_in[0]); close(p_in[1]); close(p_out[0]); close(p_out[1]);
		execvp(r->hme_argv[0], (char *const*)r->hme_argv);
		_exit(127);
	}
	close(p_in[0]); close(p_out[1]);
	/* write full JSON to child stdin */
	const char *wp = json; size_t wn = strlen(json);
	while(wn){
		ssize_t w = write(p_in[1], wp, wn);
		if(w < 0){ if(errno==EINTR) continue; break; }
		wp += (size_t)w; wn -= (size_t)w;
	}
	close(p_in[1]);
	free(json);

	struct sbuf b; sb_init(&b);
	char tmp[4096]; ssize_t rd;
	while((rd=read(p_out[0], tmp, sizeof tmp - 1))>0){
		tmp[rd]=0;
		sb_puts(&b, tmp);
	}
	close(p_out[0]);
	int status=0; waitpid(pid,&status,0);

	char *resp = sb_steal(&b);
	char *content = extract_content(resp);
	free(resp);
	if(!content){
		out->status=2; out->err=xstrdup("HME: bad JSON or missing content");
		return -1;
	}
	out->content=content; out->status=0; out->http_status=0;
	return 0;
}

#if defined(TLS_BACKEND_LIBTLS)
/* --- tiny host:port extractor for api_base like "https://host[:port][/...]" --- */
static void extract_host_port(const char *api_base, char *host, size_t hsz, char *port, size_t psz){
	/* Default to 443 */
	if(psz){ port[0]=0; strncat(port, "443", psz-1); }
	if(!api_base){ if(hsz) host[0]=0; return; }

	const char *p = strstr(api_base, "://");
	const char *h = p? p+3 : api_base;
	/* host[:port] until '/', '?' or '#' */
	const char *end = strpbrk(h, "/?#");
	size_t n = end? (size_t)(end - h) : strlen(h);

	/* Handle [ipv6]:port */
	if(n>0 && h[0]=='['){
		const char *rb = memchr(h, ']', n);
		if(rb && rb+1 < h+n && rb[1]==':'){
			size_t hn = (size_t)(rb - (h+1));
			if(hsz){ size_t c = (hn<hsz-1)? hn : hsz-1; memcpy(host, h+1, c); host[c]=0; }
			const char *ps = rb+2;
			size_t pn = (size_t)((h+n)-ps);
			if(psz){ size_t c = (pn<psz-1)? pn : psz-1; memcpy(port, ps, c); port[c]=0; }
			return;
		}
		/* no port given: copy inside brackets */
		size_t hn = rb? (size_t)(rb - (h+1)) : (n>2? n-2:0);
		if(hsz){ size_t c = (hn<hsz-1)? hn : hsz-1; memcpy(host, h+1, c); host[c]=0; }
		return;
	}

	/* Find ':' for port if present */
	const char *col = memchr(h, ':', n);
	if(col){
		size_t hn = (size_t)(col - h);
		if(hsz){ size_t c = (hn<hsz-1)? hn : hsz-1; memcpy(host, h, c); host[c]=0; }
		const char *ps = col+1; size_t pn = (size_t)((h+n) - ps);
		if(psz){ size_t c = (pn<psz-1)? pn : psz-1; memcpy(port, ps, c); port[c]=0; }
	}else{
		/* no port */
		if(hsz){ size_t c = (n<hsz-1)? n : hsz-1; memcpy(host, h, c); host[c]=0; }
		/* port already "443" */
	}
}

/* -------------------------- libtls HTTPS client ---------------------------- */
static int https_post_libtls(const char *host, const char *port,
                             const char *auth_hdr_value,
                             const char *path, const char *payload,
                             struct sbuf *out)
{
	int rc=-1;
	struct addrinfo hints, *res=0, *rp;
	memset(&hints,0,sizeof hints);
	hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
	if(getaddrinfo(host, port, &hints, &res)) return -1;

	int fd=-1; for(rp=res; rp; rp=rp->ai_next){
		fd=socket(rp->ai_family,rp->ai_socktype,rp->ai_protocol);
		if(fd<0) continue;
		if(connect(fd, rp->ai_addr, rp->ai_addrlen)==0) break;
		close(fd); fd=-1;
	}
	freeaddrinfo(res);
	if(fd<0) return -1;

	struct tls_config *cfg=tls_config_new();
	if(!cfg){ close(fd); return -1; }
	/* For compactness. In production, configure CA roots or pin certs. */
	tls_config_insecure_noverifycert(cfg);

	struct tls *ctx=tls_client();
	if(!ctx){ tls_config_free(cfg); close(fd); return -1; }
	if(tls_configure(ctx,cfg)){ tls_free(ctx); tls_config_free(cfg); close(fd); return -1; }
	if(tls_connect_socket(ctx, fd, host)){ tls_free(ctx); tls_config_free(cfg); close(fd); return -1; }

	struct sbuf req; sb_init(&req);
	sb_printf(&req,
"POST %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n"
"Content-Type: application/json\r\nAccept: application/json\r\nAccept-Encoding: identity\r\n"
"Authorization: %s\r\nContent-Length: %zu\r\n\r\n",
	          path, host, auth_hdr_value?auth_hdr_value:"", strlen(payload));
	sb_puts(&req, payload);

	size_t off=0; while(off<req.len){
		ssize_t w=tls_write(ctx, req.s+off, req.len-off);
		if(w<0){ goto out; } off+=(size_t)w;
	}
	char buf[4096]; ssize_t rdsz;
	while((rdsz=tls_read(ctx, buf, sizeof buf - 1))>0){
		buf[rdsz]=0;
		sb_puts(out, buf);
	}
	rc=0;
out:
	sb_free(&req);
	tls_close(ctx); tls_free(ctx); tls_config_free(cfg); close(fd);
	return rc;
}
#endif /* TLS_BACKEND_LIBTLS */

/* Build full URL for curl fallback.
   If api_base ends with "/v1" or "/v1/", append "/chat/completions".
   Else append "/v1/chat/completions".
   This keeps defaults simple and covers common OpenAI-compatible servers. */
static void build_full_url(const char *api_base, struct sbuf *url){
	size_t n = strlen(api_base);
	int ends_v1 = 0;
	if(n>=3 && (strcmp(api_base+n-3, "/v1")==0)) ends_v1 = 1;
	if(n>=4 && (strcmp(api_base+n-4, "/v1/")==0)) ends_v1 = 2;

	sb_puts(url, api_base);
	if(ends_v1==1)      sb_puts(url, "/chat/completions");
	else if(ends_v1==2) sb_puts(url, "chat/completions");
	else                sb_puts(url, "/v1/chat/completions");
}

/* ------------------------------ main entry -------------------------------- */
int llm_openai_complete(const struct llm_req *r, struct llm_resp *out){
	memset(out,0,sizeof *out);

	/* HME transport if provided */
	if(r->hme_argc>0 && r->hme_argv && r->hme_argv[0]){
		return call_hme(r,out);
	}

	if(r->no_network){
		out->status=1; out->err=xstrdup("no-network: OpenAI backend disabled");
		return -1;
	}
	if(!r->api_base || !r->api_key){
		out->status=1; out->err=xstrdup("api_base/api_key missing"); return -1;
	}

	char *json = build_openai_json(r);

	/* Prepare pieces common to both transports */
	struct sbuf auth; sb_init(&auth);
	sb_puts(&auth, "Bearer ");
	sb_puts(&auth, r->api_key);

	int rc=-1;

#if defined(TLS_BACKEND_LIBTLS)
	/* libtls path builds a raw HTTP/1.1 request and reads HTTP response */
	char host[256], port[16];
	extract_host_port(r->api_base, host, sizeof host, port, sizeof port);

	const char *path = "/v1/chat/completions";
	struct sbuf resp; sb_init(&resp);

	rc = https_post_libtls(host, port, auth.s, path, json, &resp);

	/* split headers/body (HTTP/1.1) */
	if(rc==0){
		char *p = strstr(resp.s, "\r\n\r\n");
		const char *body = p? p+4 : resp.s;
		char *content = extract_content(body);
		if(!content){ out->status=2; out->err=xstrdup("bad JSON or missing content"); sb_free(&resp); sb_free(&auth); free(json); return -1; }
		out->content = content; out->status=0; out->http_status=200;
		sb_free(&resp);
	}
#else
	/* Fallback via execvp("curl") with fixed argv (no shell) */
	struct sbuf url; sb_init(&url);
	build_full_url(r->api_base, &url);

	int in[2], outp[2];
	if(pipe(in)||pipe(outp)){ sb_free(&url); sb_free(&auth); free(json); return -1; }
	pid_t pid=fork();
	if(pid==0){
		dup2(in[0],0); dup2(outp[1],1);
		close(in[0]); close(in[1]); close(outp[0]); close(outp[1]);
		char *const argv_curl[] = {
			"curl",
			"-sS",
			"--http1.1",
			"-X","POST",
			"-H","Content-Type: application/json",
			"-H","Accept: application/json",
			"-H","Accept-Encoding: identity",
			"-H", auth.s,         /* e.g. "Bearer sk-...." */
			"--data-binary","@-",
			"--url", url.s,
			NULL
		};
		execvp("curl", argv_curl);
		_exit(127);
	}
	close(in[0]); close(outp[1]);
	/* write full JSON request to curl stdin */
	const char *wp = json; size_t wn = strlen(json);
	while(wn){
		ssize_t w = write(in[1], wp, wn);
		if(w < 0){ if(errno==EINTR) continue; break; }
		wp += (size_t)w; wn -= (size_t)w;
	}
	close(in[1]);

	struct sbuf resp; sb_init(&resp);
	char tmp[4096]; ssize_t rd;
	while((rd=read(outp[0], tmp, sizeof tmp - 1))>0){
		tmp[rd]=0;
		sb_puts(&resp, tmp);
	}
	close(outp[0]);
	int status=0; waitpid(pid,&status,0);
	rc= (WIFEXITED(status) && WEXITSTATUS(status)==0)? 0 : -1;

	if(rc==0){
		/* curl returns body only (no headers) */
		char *content = extract_content(resp.s);
		if(!content){ out->status=2; out->err=xstrdup("bad JSON or missing content"); sb_free(&resp); sb_free(&url); sb_free(&auth); free(json); return -1; }
		out->content = content; out->status=0; out->http_status=200;
	}
	sb_free(&resp);
	sb_free(&url);
#endif

	sb_free(&auth);
	free(json);

	if(rc!=0){ out->status=1; out->err=xstrdup("HTTPS request failed"); return -1; }
	return 0;
}
