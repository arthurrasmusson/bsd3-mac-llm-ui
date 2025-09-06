/*==============================================================================
 * src/util.c
 * License: BSD3
 *============================================================================*/
#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <time.h>
#include <errno.h>
#include <sys/time.h>
#include <unistd.h>

static void sb_grow(struct sbuf *b, size_t need){
	if (b->len+need+1 <= b->cap) return;
	size_t ncap = b->cap? b->cap*2 : 256;
	while(ncap < b->len+need+1) ncap*=2;
	b->s = xrealloc(b->s, ncap);
	b->cap = ncap;
}

void die(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr);
	exit(1);
}
void warnx(const char *fmt, ...) {
	va_list ap; va_start(ap, fmt);
	vfprintf(stderr, fmt, ap); va_end(ap);
	fputc('\n', stderr);
}

void *xmalloc(size_t n){ void *p=malloc(n?n:1); if(!p) die("oom"); return p; }
void *xrealloc(void *p,size_t n){ void *q=realloc(p, n?n:1); if(!q) die("oom"); return q; }
char *xstrdup(const char *s){ if(!s) return NULL; size_t n=strlen(s)+1; char *p=xmalloc(n); memcpy(p,s,n); return p; }

char *html_escape(const char *s){
	struct sbuf b; sb_init(&b);
	for(const unsigned char *p=(const unsigned char*)s; s && *p; ++p){
		switch(*p){
			case '&': sb_puts(&b,"&amp;"); break;
			case '<': sb_puts(&b,"&lt;"); break;
			case '>': sb_puts(&b,"&gt;"); break;
			case '"': sb_puts(&b,"&quot;"); break;
			case '\'': sb_puts(&b,"&#39;"); break;
			default: sb_putc(&b,*p);
		}
	}
	return sb_steal(&b);
}
void str_trim(char *s){
	size_t n=strlen(s);
	while(n && isspace((unsigned char)s[n-1])) s[--n]=0;
	size_t i=0; while(s[i] && isspace((unsigned char)s[i])) i++;
	if(i) memmove(s, s+i, n-i+1);
}

static int hexv(int c){
	if(c>='0'&&c<='9') return c-'0';
	if(c>='a'&&c<='f') return c-'a'+10;
	if(c>='A'&&c<='F') return c-'A'+10;
	return -1;
}
void urldecode_inplace(char *s){
	char *w=s;
	for(; *s; s++){
		if(*s=='%'){
			int a=hexv(s[1]); int b=hexv(s[2]);
			if(a>=0&&b>=0){ *w++=(char)((a<<4)|b); s+=2; }
		}else if(*s=='+'){ *w++=' '; }
		else *w++=*s;
	}
	*w=0;
}

char *form_get(const char *body, const char *key){
	size_t klen=strlen(key);
	const char *p=body;
	while(p && *p){
		const char *eq=strchr(p,'=');
		const char *amp=strchr(p,'&');
		if(!eq) break;
		size_t nk=eq-p;
		if(nk==klen && strncmp(p,key,klen)==0){
			size_t nv = (amp? (size_t)(amp-eq-1) : strlen(eq+1));
			char *v = xmalloc(nv+1);
			memcpy(v, eq+1, nv); v[nv]=0;
			urldecode_inplace(v);
			return v;
		}
		p = amp ? amp+1 : NULL;
	}
	return NULL;
}

void sb_init(struct sbuf *b){ b->s=NULL; b->len=0; b->cap=0; }
void sb_free(struct sbuf *b){ free(b->s); b->s=NULL; b->len=b->cap=0; }
void sb_puts(struct sbuf *b, const char *s){ size_t n=strlen(s); sb_grow(b,n); memcpy(b->s+b->len,s,n); b->len+=n; b->s[b->len]=0; }
void sb_putc(struct sbuf *b, char c){ sb_grow(b,1); b->s[b->len++]=c; b->s[b->len]=0; }
void sb_printf(struct sbuf *b, const char *fmt, ...){
	va_list ap; va_start(ap,fmt);
	char tmp[4096]; int n=vsnprintf(tmp,sizeof tmp,fmt,ap);
	va_end(ap);
	if(n<0) return;
	if((size_t)n >= sizeof tmp){
		char *buf=xmalloc(n+1);
		va_start(ap,fmt); vsnprintf(buf,n+1,fmt,ap); va_end(ap);
		sb_puts(b, buf); free(buf);
	}else sb_puts(b,tmp);
}
char *sb_steal(struct sbuf *b){ char *s=b->s; b->s=NULL; b->len=b->cap=0; return s; }

char *read_file(const char *path, size_t *outlen){
	FILE *f=fopen(path,"rb");
	if(!f) return NULL;
	char *buf=NULL; size_t cap=0, len=0;
	char tmp[4096];
	for(;;){
		size_t r=fread(tmp,1,sizeof tmp,f);
		if(!r) break;
		if(len+r>cap){ cap = cap? cap*2 : 8192; while(cap<len+r) cap*=2; buf=xrealloc(buf,cap); }
		memcpy(buf+len,tmp,r); len+=r;
	}
	fclose(f);
	if(outlen) *outlen=len;
	if(!buf){ buf=xstrdup(""); if(outlen)*outlen=0; }
	else buf[len]=0;
	return buf;
}
uint64_t now_ms(void){
	struct timeval tv; gettimeofday(&tv,NULL);
	return (uint64_t)tv.tv_sec*1000ULL + tv.tv_usec/1000;
}

/* split "host:port" (ipv6: "[::1]:8080") into host/port */
int split_host_port(const char *hp, char *host, size_t hsz, char *port, size_t psz){
	if(!hp) return -1;
	if(hp[0]=='['){ /* [v6]:port */
		const char *rb=strchr(hp,']'); if(!rb) return -1;
		size_t hn = (size_t)(rb-hp-1); if(hn+1>hsz) return -1;
		memcpy(host, hp+1, hn); host[hn]=0;
		if(rb[1]!=':'||!rb[2]) return -1;
		strncpy(port, rb+2, psz-1); port[psz-1]=0;
		return 0;
	}else{
		const char *col=strrchr(hp,':');
		if(!col) return -1;
		size_t hn = (size_t)(col-hp); if(hn+1>hsz) return -1;
		memcpy(host,hp,hn); host[hn]=0;
		strncpy(port,col+1,psz-1); port[psz-1]=0;
		return 0;
	}
}
