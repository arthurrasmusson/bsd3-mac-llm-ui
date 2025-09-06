/*==============================================================================
 * src/tmpl.c
 * License: BSD3
 *============================================================================*/
#include "tmpl.h"
#include "util.h"
#include "../config.h"

char *render_page(const char *app_title,
                  const char *css,
                  const char *model,
                  double temperature,
                  const char *transcript_pre,
                  const char *history_raw,
                  const char *error_html)
{
	struct sbuf b; sb_init(&b);
	sb_printf(&b,
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html; charset=utf-8\r\n"
CSP_HEADER XFO_HEADER REF_HEADER CACHECTL
"Connection: close\r\n"
"\r\n"
"<!doctype html><html lang=en><meta charset=utf-8>"
"<title>%s</title><style>%s</style><h1>%s</h1>",
		app_title, css, app_title);

	if (error_html && *error_html)
		sb_printf(&b, "<p class=warn>%s</p>", error_html);

	sb_puts(&b, "<form method=POST action=/chat>");
	sb_puts(&b, "<label for=prompt>Prompt</label>");
	sb_puts(&b, "<textarea name=prompt id=prompt required></textarea>");

	sb_puts(&b, "<div class=row>");
	sb_puts(&b, "<div class=col>");
	sb_puts(&b, "<label for=model>Model</label>");
	sb_printf(&b, "<input type=text id=model name=model value=\"%s\">", model);
	sb_puts(&b, "</div>");

	sb_puts(&b, "<div class=col>");
	sb_puts(&b, "<label for=temp>Temperature</label>");
	sb_printf(&b, "<input type=number id=temp name=temp step=0.1 min=0 max=2 value=\"%.2f\">", temperature);
	sb_puts(&b, "</div>");
	sb_puts(&b, "</div>");

	/* stateless history */
	sb_puts(&b, "<textarea name=history style=\"display:none\">");
	if(history_raw) {
		/* history_raw is raw (not HTML-escaped). It's inside <textarea> so fine. */
		sb_puts(&b, history_raw);
	}
	sb_puts(&b, "</textarea>");

	sb_puts(&b, "<p><button type=submit>Send</button></p></form>");

	sb_puts(&b, "<h2>Transcript</h2><pre>");
	if (transcript_pre) sb_puts(&b, transcript_pre);
	sb_puts(&b, "</pre><p class=footer>"
		"This UI uses no JavaScript. Responses render on full-page reload.</p></html>");

	return sb_steal(&b);
}
