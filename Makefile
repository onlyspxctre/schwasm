.PHONY: all clean

BUILDDIR := $(abspath ./build)
DEPSDIR := $(abspath ./deps)
SRCDIR := $(abspath ./src)
OBJDIR := $(abspath ./obj)
INCLUDEDIR := $(abspath ./include)
LIBDIR := $(abspath ./lib)

CC := clang
CFLAGS := -Wall -Wextra -Wshadow -std=c11 -fcolor-diagnostics -I$(INCLUDEDIR)
LDFLAGS := -fuse-ld=lld

SPLEXER_VERSION := 1aeb2f8
SPLEXER_FLAGS := GRANULAR_TOK_UNKNOWN=y NO_MULTICOMMENT=y
SPLEXER_STAMP := $(DEPSDIR)/.splexer-$(SPLEXER_VERSION).stamp

ifneq ($(RELEASE),)
CFLAGS += -O2 -flto -static
SPLEXER_FLAGS += RELEASE=y
BUILDDIR := $(abspath ./build/release)
else
CFLAGS += -g
endif

ifneq ($(WINDOWS),)
OBJDIR := $(abspath ./obj/win)

CC +=  --target=x86_64-w64-mingw32 --sysroot=/usr/x86_64-w64-mingw32
LDFLAGS += -L/usr/lib/gcc/x86_64-w64-mingw32/16.1.0 -DSP_STATIC
LIBS := -l:libsplexer-win.a
SPLEXER_FLAGS += WINDOWS=y

all: $(BUILDDIR)/schwasm.exe

$(BUILDDIR)/schwasm.exe: $(OBJDIR)/schwasm.o $(SRCDIR)/schwasm.c $(SRCDIR)/schwasm.h $(LIBDIR)/libsplexer-win.a $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCDIR)/cli.c $< -L$(LIBDIR) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ -DSP_STATIC -c $<

$(LIBDIR)/libsplexer-win.a: $(SPLEXER_STAMP)
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
	cp -f $(DEPSDIR)/splexer/build/libsplexer-win.a $@

else

LIBS := -lsplexer

all: $(BUILDDIR)/schwasm

$(BUILDDIR)/schwasm: $(OBJDIR)/schwasm.o $(SRCDIR)/schwasm.c $(SRCDIR)/schwasm.h $(LIBDIR)/libsplexer.so $(LIBDIR)/libsplexer.a $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCDIR)/cli.c $< -L$(LIBDIR) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ -c $<

$(LIBDIR)/libsplexer.a: $(SPLEXER_STAMP)
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
	cp -f $(DEPSDIR)/splexer/build/libsplexer.a $@

$(LIBDIR)/libsplexer.so: $(SPLEXER_STAMP)
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
	cp -f $(DEPSDIR)/splexer/build/libsplexer.so $@
endif

$(INCLUDEDIR)/sptl.h:
	mkdir -p $(INCLUDEDIR)
	cd $(INCLUDEDIR) && curl -O https://raw.githubusercontent.com/onlyspxctre/sptl.h/refs/heads/master/sptl.h

$(INCLUDEDIR)/splexer.h: $(SPLEXER_STAMP)
	mkdir -p $(INCLUDEDIR)
	cp -f $(DEPSDIR)/splexer/splexer.h $@

$(SPLEXER_STAMP):
	mkdir -p $(DEPSDIR)
	test -d $(DEPSDIR)/splexer/.git || git clone https://github.com/onlyspxctre/splexer.git $(DEPSDIR)/splexer
	-git -C $(DEPSDIR)/splexer fetch --tags
	cd $(DEPSDIR)/splexer && git checkout $(SPLEXER_VERSION)
	touch $@

clean:
	rm -rf $(BUILDDIR)
	rm -rf $(DEPSDIR)
	rm -rf $(OBJDIR)
	rm -rf $(INCLUDEDIR)
	rm -rf $(LIBDIR)
	rm -rf *.mif
