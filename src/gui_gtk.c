/*==============================================================================
 * src/gui_gtk.c  —  local GUI (GTK3), optional
 * License: BSD3
 *============================================================================*/
#ifdef WITH_GTK
#include <gtk/gtk.h>
#include "../include/llm_backend.h"
#include "util.h"
#include "../config.h"

struct gui_state {
	GtkWidget *win, *view, *entry, *model, *temp;
	llm_fn fn;
};
static void append_text(GtkTextBuffer *buf, const char *role, const char *txt){
	GtkTextIter end; gtk_text_buffer_get_end_iter(buf,&end);
	gtk_text_buffer_insert(buf,&end, role, -1);
	gtk_text_buffer_insert(buf,&end, ": ", -1);
	gtk_text_buffer_insert(buf,&end, txt?txt:"", -1);
	gtk_text_buffer_insert(buf,&end, "\n\n", -1);
}
static void on_send(GtkButton *btn, gpointer data){
	(void)btn;
	struct gui_state *gs=(struct gui_state*)data;
	const char *prompt = gtk_entry_get_text(GTK_ENTRY(gs->entry));
	const char *model  = gtk_entry_get_text(GTK_ENTRY(gs->model));
	double temp = atof(gtk_entry_get_text(GTK_ENTRY(gs->temp)));

	GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(gs->view));
	append_text(buf, "user", prompt);

	struct llm_msg msgs[1]={{"user",prompt}};
	struct llm_req req = {
		.msgs=msgs, .nmsgs=1, .model=model, .temperature=temp, .max_tokens=DEF_MAX_TOKENS,
		.api_base=DEF_API_BASE, .api_key=getenv("OPENAI_API_KEY"),
		.no_network=0, .hme_argv=NULL, .hme_argc=0, .trt_engine_path=NULL
	};
	struct llm_resp resp={0};
	gs->fn(&req,&resp);
	append_text(buf, "assistant", resp.content?resp.content:"(error)");
	free(resp.content); free(resp.err);
	gtk_entry_set_text(GTK_ENTRY(gs->entry), "");
}
int run_gtk_gui(llm_fn fn){
	gtk_init(NULL,NULL);
	struct gui_state gs={0}; gs.fn=fn;
	GtkWidget *w = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(w), APP_TITLE);
	gtk_window_set_default_size(GTK_WINDOW(w), 700, 600);
	g_signal_connect(w, "destroy", G_CALLBACK(gtk_main_quit), NULL);
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6); gtk_container_add(GTK_CONTAINER(w), box);
	GtkWidget *view = gtk_text_view_new(); gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
	gtk_box_pack_start(GTK_BOX(box), view, TRUE, TRUE, 0);
	GtkWidget *entry = gtk_entry_new(); gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
	GtkWidget *model = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(model), DEF_MODEL); gtk_box_pack_start(GTK_BOX(row), model, TRUE, TRUE, 0);
	GtkWidget *temp  = gtk_entry_new(); gtk_entry_set_text(GTK_ENTRY(temp), "0.6"); gtk_box_pack_start(GTK_BOX(row), temp, FALSE, FALSE, 0);
	GtkWidget *btn = gtk_button_new_with_label("Send"); gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
	gs.win=w; gs.view=view; gs.entry=entry; gs.model=model; gs.temp=temp;
	g_signal_connect(btn,"clicked",G_CALLBACK(on_send),&gs);
	gtk_widget_show_all(w); gtk_main(); return 0;
}
#endif
