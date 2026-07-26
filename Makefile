.PHONY: all clean

export BUILDDIR := $(abspath ./build)
export OBJDIR := $(abspath ./obj)
export INCLUDEDIR := $(abspath ./include)
export LIBDIR := $(abspath ./lib)

export CC := clang
export CFLAGS := -Wall -Wextra -std=c11 -fcolor-diagnostics -I$(INCLUDEDIR)
export LIBS := -l:libsplexer.so -lm

all: $(BUILDDIR)/schwasm

$(BUILDDIR)/schwasm: schwasm.c $(LIBDIR)/libsplexer.so $(INCLUDEDIR)/sptl.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -ggdb -o $@ $< -L$(LIBDIR) $(LIBS)

$(INCLUDEDIR)/sptl.h:
	mkdir -p $(INCLUDEDIR)
	cd $(INCLUDEDIR) && curl -O https://raw.githubusercontent.com/onlyspxctre/sptl.h/refs/heads/master/sptl.h
