//=============================================================================
// src/gui_qt.cpp  —  local GUI (Qt Widgets), optional
// License: BSD3
//=============================================================================
#ifdef WITH_QT
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include "../include/llm_backend.h"
#include "../config.h"
#include <cstdlib>

static void append(QTextEdit *te, const char *role, const char *txt){
	te->append(QString("%1: %2\n").arg(role).arg(txt?txt:""));
}
int run_qt_gui(llm_fn fn){
	int argc=0; char **argv=nullptr; QApplication app(argc,argv);
	QWidget w; w.setWindowTitle(APP_TITLE);
	auto *v = new QVBoxLayout(&w);
	auto *txt = new QTextEdit; txt->setReadOnly(false); v->addWidget(txt);
	auto *entry = new QLineEdit; v->addWidget(entry);
	auto *row = new QHBoxLayout; v->addLayout(row);
	auto *model = new QLineEdit(DEF_MODEL); row->addWidget(model);
	auto *temp  = new QLineEdit("0.6"); row->addWidget(temp);
	auto *btn   = new QPushButton("Send"); row->addWidget(btn);
	QObject::connect(btn,&QPushButton::clicked,[=](){
		QString p = entry->text(); if(p.isEmpty()) return;
		append(txt,"user", p.toUtf8().constData());
		struct llm_msg msgs[1]={{"user", p.toUtf8().constData()}};
		struct llm_req req = { msgs,1, model->text().toUtf8().constData(), atof(temp->text().toUtf8().constData()), DEF_MAX_TOKENS,
		                       DEF_API_BASE, getenv("OPENAI_API_KEY"), 0, nullptr,0, nullptr };
		struct llm_resp resp={0};
		fn(&req,&resp);
		append(txt,"assistant", resp.content);
		free(resp.content); free(resp.err);
		entry->clear();
	});
	w.resize(700,600); w.show();
	return app.exec();
}
#endif
