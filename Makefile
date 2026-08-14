.PHONY: all clean


export BUILDDIR := $(abspath ./build)
export DEPSDIR := $(abspath ./deps)
export INCLUDEDIR := $(abspath ./include)
export LIBDIR := $(abspath ./lib)

export SPLEXER_VERSION := 00a65a6

RELEASE ?= n

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
export LIBS := -lsplexer -lm

ifneq ($(RELEASE),n)
CFLAGS += -O2 -static
BUILDDIR := $(abspath ./build/release)
else
CFLAGS += -g
endif

all: $(BUILDDIR)/schwasm

$(BUILDDIR)/schwasm: schwasm.c $(LIBDIR)/libsplexer.so $(LIBDIR)/libsplexer.a $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $< -L$(LIBDIR) $(LIBS)

$(LIBDIR)/libsplexer.a: $(DEPSDIR)/splexer
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer GRANULAR_TOK_UNKNOWN=y all
	cp -f $(DEPSDIR)/splexer/build/libsplexer.a $@

$(LIBDIR)/libsplexer.so: $(DEPSDIR)/splexer
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer GRANULAR_TOK_UNKNOWN=y all
	cp -f $(DEPSDIR)/splexer/build/libsplexer.so $@
endif

$(INCLUDEDIR)/sptl.h:
	mkdir -p $(INCLUDEDIR)
	cd $(INCLUDEDIR) && curl -O https://raw.githubusercontent.com/onlyspxctre/sptl.h/refs/heads/master/sptl.h

$(INCLUDEDIR)/splexer.h: $(DEPSDIR)/splexer
	mkdir -p $(INCLUDEDIR)
	cp -f $(DEPSDIR)/splexer/splexer.h $@

$(DEPSDIR)/splexer:
	mkdir -p $(DEPSDIR)
	git clone https://github.com/onlyspxctre/splexer.git $(DEPSDIR)/splexer
	cd $(DEPSDIR)/splexer && git checkout $(SPLEXER_VERSION)

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(DEPSDIR)
	rm -rf $(INCLUDEDIR)
	rm -rf $(LIBDIR)
