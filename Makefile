.PHONY: all clean

BUILDDIR := $(abspath ./build)
DEPSDIR := $(abspath ./deps)
INCLUDEDIR := $(abspath ./include)
LIBDIR := $(abspath ./lib)

CC := clang
CFLAGS := -Wall -Wextra -std=c11 -fcolor-diagnostics -I$(INCLUDEDIR)
LDFLAGS := -fuse-ld=lld

SPLEXER_VERSION := d8bab4d
SPLEXER_FLAGS := GRANULAR_TOK_UNKNOWN=y NO_MULTICOMMENT=y

ifneq ($(RELEASE),)
CFLAGS += -O2 -static
BUILDDIR := $(abspath ./build/release)
else
CFLAGS += -g
endif

ifneq ($(WINDOWS),)
CC +=  --target=x86_64-w64-mingw32 --sysroot=/usr/x86_64-w64-mingw32
LDFLAGS += -L/usr/lib/gcc/x86_64-w64-mingw32/16.1.0 -DSP_STATIC
LIBS := -l:libsplexer-win.a
SPLEXER_FLAGS += WINDOWS=y

all: $(BUILDDIR)/schwasm.exe

$(BUILDDIR)/schwasm.exe: schwasm.c $(LIBDIR)/libsplexer-win.a $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -ggdb -o $@ $< -L$(LIBDIR) $(LIBS)

$(LIBDIR)/libsplexer-win.a: $(DEPSDIR)/splexer
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
	cp -f $(DEPSDIR)/splexer/build/libsplexer-win.a $@

else

LIBS := -lsplexer

all: $(BUILDDIR)/schwasm

$(BUILDDIR)/schwasm: schwasm.c $(LIBDIR)/libsplexer.so $(LIBDIR)/libsplexer.a $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -L$(LIBDIR) $(LIBS)

$(LIBDIR)/libsplexer.a: $(DEPSDIR)/splexer
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
	cp -f $(DEPSDIR)/splexer/build/libsplexer.a $@

$(LIBDIR)/libsplexer.so: $(DEPSDIR)/splexer
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
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
