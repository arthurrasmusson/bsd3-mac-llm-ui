#===============================================================================
# Makefile — Suckless-style with build knobs
# License: BSD3
#===============================================================================
# Knobs (override on command line): e.g. make WITH_WEB=1 BACKEND=openai
WITH_WEB ?= 1
WITH_GTK ?= 0
WITH_QT  ?= 0
BACKEND  ?= openai        # runtime default; both backends are available
TLS_BACKEND ?= libtls     # libtls or anything else (curl fallback)
WITH_SECCOMP ?= 1

PREFIX  ?= /usr/local
CC      ?= cc
CXX     ?= c++
CFLAGS  ?= -std=c99 -O2 -Wall -Wextra -pedantic -D_POSIX_C_SOURCE=200809L
CXXFLAGS?= -O2 -Wall -Wextra -pedantic
LDFLAGS ?=
LIBS    :=

UNAME_S := $(shell uname -s)

# libtls selection (OpenBSD ships -ltls; Linux can use libretls)
# If TLS_BACKEND!=libtls, the backend will use curl(1) fallback.
ifeq ($(TLS_BACKEND),libtls)
  CFLAGS  += -DTLS_BACKEND_LIBTLS
  LIBS    += -ltls
endif

# Linux seccomp
ifeq ($(UNAME_S),Linux)
  ifeq ($(WITH_SECCOMP),1)
    CFLAGS += -DWITH_SECCOMP
    LIBS   += -lseccomp
  endif
endif

# GTK/Qt toggles
ifeq ($(WITH_GTK),1)
  CFLAGS  += -DWITH_GTK `pkg-config --cflags gtk+-3.0`
  LIBS    += `pkg-config --libs gtk+-3.0`
endif
ifeq ($(WITH_QT),1)
  CXXFLAGS += -DWITH_QT `pkg-config --cflags Qt5Widgets`
  LIBS     += `pkg-config --libs Qt5Widgets`
endif

# TensorRT-LLM selection:
#   - If HAVE_TRTLLM=1, compile the real C++ backend (you supply includes/libs).
#   - Else, compile a small C stub that returns 501 (keeps link simple).
HAVE_TRTLLM ?= 0
ifeq ($(HAVE_TRTLLM),1)
  CXXFLAGS += -DHAVE_TRTLLM $(TRTLLM_CXXFLAGS)
  LIBS     += $(TRTLLM_LIBS)
endif

# Sources
SRC_C := src/util.c src/tmpl.c src/httpd.c src/sandbox.c src/backend_openai.c src/main.c
SRC_CPP :=
ifeq ($(HAVE_TRTLLM),1)
  SRC_CPP  += src/backend_trtllm.cpp
else
  SRC_C    += src/backend_trtllm_stub.c
endif
ifeq ($(WITH_GTK),1)
  SRC_C    += src/gui_gtk.c
endif
ifeq ($(WITH_QT),1)
  SRC_CPP  += src/gui_qt.cpp
endif

OBJ := $(SRC_C:.c=.o) $(SRC_CPP:.cpp=.o)

# Choose linker: use C++ only when we actually have C++ objects to link.
LINKER = $(CC)
ifneq ($(strip $(SRC_CPP)),)
  LINKER = $(CXX)
endif

all: config.h llmserv

config.h:
	@test -f config.h || cp config.def.h config.h

llmserv: $(OBJ)
	$(LINKER) -o $@ $(OBJ) $(LDFLAGS) $(LIBS)

%.o: %.c include/llm_backend.h src/util.h src/httpd.h src/tmpl.h src/sandbox.h config.h
	$(CC) $(CFLAGS) -Iinclude -Isrc -c $< -o $@

%.o: %.cpp include/llm_backend.h config.h
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

install: llmserv
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f llmserv $(DESTDIR)$(PREFIX)/bin/
	chmod 755 $(DESTDIR)$(PREFIX)/bin/llmserv

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/llmserv

clean:
	rm -f $(OBJ) llmserv

.PHONY: all install uninstall clean
