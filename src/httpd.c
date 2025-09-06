/*==============================================================================
 * src/httpd.c  —  minimal HTTP/1.1 server with just enough routing
 * License: BSD3
 *============================================================================*/
#define _POSIX_C_SOURCE 200809L
#include "httpd.h"
#include "../config.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static int open_listen(const char *bindaddr){
	char host[256], port[16];
	if(split_host_port(bindaddr, host, sizeof host, port, sizeof port)<0)
		die("invalid bind address: %s", bindaddr);

	struct addrinfo hints, *res=0, *rp;
	memset(&hints,0,sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;
	int e = getaddrinfo(*host?host:NULL, port, &hints, &res);
	if(e) die("getaddrinfo: %s", gai_strerror(e));

	int fd=-1, on=1;
	for(rp=res; rp; rp=rp->ai_next){
		fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
		if(fd<0) continue;
		setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&on,sizeof on);
		if(bind(fd, rp->ai_addr, rp->ai_addrlen)==0){
			if(listen(fd, 16)==0) break;
		}
		close(fd); fd=-1;
	}
	freeaddrinfo(res);
	if(fd<0) die("cannot bind %s", bindaddr);
	return fd;
}

static ssize_t read_full(int fd, void *buf, size_t cap, int timeout_sec){
	(void)timeout_sec; /* keep it simple: blocking read */
	return read(fd, buf, cap);
}

static void write_all(int fd, const void *buf, size_t n){
	const char *p=(const char*)buf;
	while(n){
		ssize_t w=write(fd,p,n);
		if(w<0){ if(errno==EINTR) continue; break; }
		p+=w; n-=w;
	}
}

struct server_state { const struct server_cfg *cfg; llm_fn fn; };

static char *route_index(const struct server_cfg *cfg){
	return render_page(APP_TITLE, CSS_INLINE, cfg->model, cfg->temperature, "", "", NULL);
}

/* History format (stateless):
 *  a series of entries separated by "\n\n===\n\n"
 *  each entry starts with "U: " or "A: " followed by content.
 */
static void history_append(struct sbuf *h, const char prefix, const char *content){
	if(h->len) sb_puts(h, "\n\n===\n\n");
	sb_printf(h, "%c: %s", prefix, content);
}

static void messages_from_history(struct sbuf *transcript_pre,
                                  struct llm_msg *msgs, int *nmsgs,
                                  const char *system_prompt,
                                  const char *history_raw)
{
	int n=0;
	if(system_prompt && *system_prompt){
		msgs[n++] = (struct llm_msg){ "system", system_prompt };
	}
	if(history_raw && *history_raw){
		/* iterate records */
		const char *p=history_raw;
		while(*p){
			const char *sep = strstr(p, "\n\n===\n\n");
			size_t len = sep? (size_t)(sep-p): strlen(p);
			if(len>=3 && (p[0]=='U'||p[0]=='A') && p[1]==':' && p[2]==' '){
				const char *role = (p[0]=='U')? "user":"assistant";
				const char *content = p+3;
				char *frag = xmalloc(len-3+1); memcpy(frag,content,len-3); frag[len-3]=0;
				/* transcript for rendering */
				char *esc = html_escape(frag);
				sb_printf(transcript_pre, "%s: %s\n\n", role, esc);
				free(esc);
				msgs[n++] = (struct llm_msg){ role, frag };
				if(n >= MAX_TURNS*2+2) break; /* cap turns */
			}
			if(!sep) break;
			p = sep + 6;
		}
	}
	*nmsgs = n;
}

static char *handle_chat(struct server_state *st, const char *body){
	const struct server_cfg *cfg = st->cfg;
	char *prompt  = form_get(body, "prompt");
	char *model   = form_get(body, "model");
	char *tempstr = form_get(body, "temp");
	char *history = form_get(body, "history");
	double temp = tempstr? atof(tempstr) : cfg->temperature;
	if(!model||!*model) { free(model); model=xstrdup(cfg->model); }

	struct sbuf transcript; sb_init(&transcript);
	struct llm_msg msgs[1 + MAX_TURNS*2 + 1]; int nmsgs=0;
	messages_from_history(&transcript, msgs, &nmsgs, "", history);

	/* Append current user prompt */
	if(prompt && *prompt){
		char *pdup = xstrdup(prompt);
		msgs[nmsgs++] = (struct llm_msg){ "user", pdup };
		char *esc = html_escape(prompt);
		sb_printf(&transcript, "user: %s\n\n", esc);
		free(esc);
	}else{
		/* If no prompt, just render existing state */
		char *html = render_page(APP_TITLE, CSS_INLINE, model, temp,
		                         transcript.s, history?history:"", NULL);
		/* free allocated message contents from history */
		for(int i=0;i<nmsgs;i++){ if(msgs[i].content) free((void*)msgs[i].content); }
		free(prompt); free(model); free(tempstr); free(history); sb_free(&transcript);
		return html;
	}

	struct llm_req req = {
		.msgs = msgs, .nmsgs = nmsgs,
		.model = model, .temperature = temp, .max_tokens = cfg->max_tokens,
		.api_base = cfg->api_base, .api_key = cfg->api_key,
		.no_network = cfg->no_network, .hme_argv = cfg->hme_argv, .hme_argc = cfg->hme_argc,
		.trt_engine_path = cfg->trt_engine
	};
	struct llm_resp resp = {0};
	int rc = st->fn(&req, &resp);

	char *err_html=NULL;
	if(rc!=0 || resp.status!=0){
		struct sbuf e; sb_init(&e);
		sb_printf(&e, "Error (%d/%d): ", rc, resp.status);
		if(resp.err){ char *eh=html_escape(resp.err); sb_puts(&e, eh); free(eh); }
		err_html = sb_steal(&e);
	}

	/* Append assistant answer into transcript and history */
	struct sbuf h; sb_init(&h);
	if(history && *history) sb_puts(&h, history);
	if(prompt && *prompt)   history_append(&h, 'U', prompt);
	if(resp.content && *resp.content) history_append(&h, 'A', resp.content);

	char *ans_esc = resp.content? html_escape(resp.content): xstrdup("(no content)");
	sb_printf(&transcript, "assistant: %s\n\n", ans_esc);
	free(ans_esc);

	char *html = render_page(APP_TITLE, CSS_INLINE, model, temp,
	                         transcript.s, h.s, err_html);

	free(err_html);
	free(resp.content); free(resp.err);
	for(int i=0;i<nmsgs;i++){
		if(msgs[i].content) free((void*)msgs[i].content);
	}
	free(prompt); free(model); free(tempstr); free(history);
	sb_free(&h); sb_free(&transcript);
	return html;
}

static void handle_conn(struct server_state *st, int cfd){
	char buf[8192];
	ssize_t r = read_full(cfd, buf, sizeof buf - 1, IO_TIMEOUT_SEC);
	if(r<=0) return;
	buf[r]=0;

	/* crude parse */
	char *method = buf;
	char *sp1=strchr(method,' ');
	if(!sp1) return;
	*sp1=0;
	char *path = sp1+1;
	char *sp2=strchr(path,' ');
	if(!sp2) return;
	*sp2=0;

	char *headers = sp2+1;
	char *body = strstr(headers, "\r\n\r\n");
	size_t bodylen=0;
	if(body){ *body=0; body+=4; bodylen = (size_t)(buf + r - body); }

	if(strcmp(method,"GET")==0 && strcmp(path,"/")==0){
		char *html = route_index(st->cfg);
		write_all(cfd, html, strlen(html));
		free(html);
		return;
	}
	if(strcmp(method,"GET")==0 && strcmp(path,"/health")==0){
		const char *resp = "HTTP/1.1 200 OK\r\nContent-Type:text/plain\r\n"
		                   "Connection: close\r\n\r\nok\n";
		write_all(cfd, resp, strlen(resp));
		return;
	}
	if(strcmp(method,"POST")==0 && strcmp(path,"/chat")==0){
		/* ensure body not huge */
		if(bodylen > MAX_REQ_BODY){
			const char *resp = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\n\r\n";
			write_all(cfd, resp, strlen(resp)); return;
		}
		/* copy body to owned buffer and handle */
		char *b = xmalloc(bodylen+1); memcpy(b, body, bodylen); b[bodylen]=0;
		char *html = handle_chat(st, b);
		write_all(cfd, html, strlen(html));
		free(html); free(b);
		return;
	}
	const char *nf = "HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n";
	write_all(cfd, nf, strlen(nf));
}

int run_http_server(const struct server_cfg *cfg, llm_fn fn){
	int lfd = open_listen(cfg->bind_addr);
	struct server_state st = { cfg, fn };
	for(;;){
		int cfd = accept(lfd, NULL, NULL);
		if(cfd<0){ if(errno==EINTR) continue; break; }
		handle_conn(&st, cfd);
		close(cfd);
	}
	close(lfd);
	return 0;
}
