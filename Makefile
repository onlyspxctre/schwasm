.PHONY: all clean


export BUILDDIR := $(abspath ./build)
export OBJDIR := $(abspath ./obj)
export INCLUDEDIR := $(abspath ./include)
export LIBDIR := $(abspath ./lib)

export SPLEXER_VERSION := 7e5b9f8

WINDOWS ?= n
ifneq ($(WINDOWS),n)
export CC := x86_64-w64-mingw32-gcc
export CFLAGS := -Wall -Wextra -std=c11 -I$(INCLUDEDIR)
export LIBS := -l:libsplexer.dll -lm

all: $(BUILDDIR)/schwasm.exe

$(BUILDDIR)/schwasm.exe: schwasm.c $(LIBDIR)/libsplexer.dll $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -ggdb -o $@ $< -L$(LIBDIR) $(LIBS)

$(LIBDIR)/libsplexer.dll: $(BUILDDIR)/splexer
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(BUILDDIR)/splexer WINDOWS=y main.exe
	cp -f $(BUILDDIR)/splexer/build/libsplexer.dll $@

else
export CC := clang
export CFLAGS := -Wall -Wextra -std=c11 -fcolor-diagnostics -I$(INCLUDEDIR)
export LIBS := -l:libsplexer.so -lm

all: $(BUILDDIR)/schwasm

$(BUILDDIR)/schwasm: schwasm.c $(LIBDIR)/libsplexer.so $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -ggdb -o $@ $< -L$(LIBDIR) $(LIBS)

$(LIBDIR)/libsplexer.so: $(BUILDDIR)/splexer
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(BUILDDIR)/splexer
	cp -f $(BUILDDIR)/splexer/build/libsplexer.so $@
endif

$(INCLUDEDIR)/sptl.h:
	mkdir -p $(INCLUDEDIR)
	cd $(INCLUDEDIR) && curl -O https://raw.githubusercontent.com/onlyspxctre/sptl.h/refs/heads/master/sptl.h

$(INCLUDEDIR)/splexer.h: $(BUILDDIR)/splexer
	mkdir -p $(INCLUDEDIR)
	cp -f $(BUILDDIR)/splexer/splexer.h $@

$(BUILDDIR)/splexer:
	mkdir -p $(BUILDDIR)
	git clone https://github.com/onlyspxctre/splexer.git $(BUILDDIR)/splexer
	cd $(BUILDDIR)/splexer && git checkout $(SPLEXER_VERSION)

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(OBJDIR)
	rm -rf $(INCLUDEDIR)
	rm -rf $(LIBDIR)
