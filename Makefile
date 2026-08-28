.PHONY: all clean depsclean

BUILDROOT := $(abspath ./build)
LIBROOT := $(abspath ./lib)
OBJROOT := $(abspath ./obj)

DEPSDIR := $(abspath ./deps)
SRCDIR := $(abspath ./src)
INCLUDEDIR := $(abspath ./include)

CC := clang
CFLAGS := -Wall -Wextra -Wshadow -Winline -std=c11 -fcolor-diagnostics -I$(INCLUDEDIR)
LDFLAGS := -fuse-ld=lld

SPLEXER_VERSION := 3b06958
SPLEXER_FLAGS := GRANULAR_TOK_UNKNOWN=y NO_MULTICOMMENT=y

ifneq ($(RELEASE),)
CFLAGS += -O2 -flto -static -DNDEBUG
SPLEXER_FLAGS += RELEASE=y
CONFIG := release
else
CFLAGS += -g
CONFIG := debug
endif

ifneq ($(WINDOWS),)
OBJDIR := $(abspath ./obj/win)

CC +=  --target=x86_64-w64-mingw32 -DSP_STATIC
LIBS := -l:libsplexer-win.a
SPLEXER_FLAGS += WINDOWS=y
CONFIG := win-$(CONFIG)
else
LIBS := -lsplexer
endif

BUILDDIR := $(BUILDROOT)/$(CONFIG)
OBJDIR := $(OBJROOT)/$(CONFIG)
LIBDIR := $(LIBROOT)/$(CONFIG)
SPLEXER_STAMP := $(DEPSDIR)/.splexer-$(SPLEXER_VERSION)-$(CONFIG).stamp

ifneq ($(WINDOWS),)
all: $(BUILDDIR)/schwasm.exe

$(BUILDDIR)/schwasm.exe: $(OBJDIR)/schwasm.o $(SRCDIR)/cli.c $(SRCDIR)/schwasm.c $(SRCDIR)/schwasm.h $(LIBDIR)/libsplexer-win.a $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCDIR)/cli.c $< -L$(LIBDIR) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ -DSP_STATIC -c $<

$(LIBDIR)/libsplexer-win.a: $(SPLEXER_STAMP)
	$(MAKE) -C $(DEPSDIR)/splexer clean
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
	cp -f $(DEPSDIR)/splexer/build/libsplexer-win.a $@

else
all: $(BUILDDIR)/schwasm

$(BUILDDIR)/schwasm: $(OBJDIR)/schwasm.o $(OBJDIR)/ir.o $(SRCDIR)/cli.c $(SRCDIR)/schwasm.c $(SRCDIR)/schwasm.h $(SRCDIR)/ir.c $(SRCDIR)/ir.h $(LIBDIR)/libsplexer.so $(LIBDIR)/libsplexer.a $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCDIR)/cli.c $(OBJDIR)/schwasm.o $(OBJDIR)/ir.o -L$(LIBDIR) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ -c $<

$(LIBDIR)/libsplexer.a $(LIBDIR)/libsplexer.so &: $(SPLEXER_STAMP)
	$(MAKE) -C $(DEPSDIR)/splexer clean
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(DEPSDIR)/splexer $(SPLEXER_FLAGS) all
	cp -f $(DEPSDIR)/splexer/build/libsplexer.a $(LIBDIR)/libsplexer.a
	cp -f $(DEPSDIR)/splexer/build/libsplexer.so $(LIBDIR)/libsplexer.so

endif

$(INCLUDEDIR)/sptl.h:
	mkdir -p $(INCLUDEDIR)
	cd $(INCLUDEDIR) && curl -O https://raw.githubusercontent.com/onlyspxctre/sptl.h/refs/heads/master/sptl.h

$(INCLUDEDIR)/splexer.h: $(SPLEXER_STAMP)
	mkdir -p $(INCLUDEDIR)
	cp -f $(DEPSDIR)/splexer/splexer.h $@

# TODO: fetch halts the process, make it conditional
$(SPLEXER_STAMP):
	mkdir -p $(DEPSDIR)
	test -d $(DEPSDIR)/splexer/.git || git clone https://github.com/onlyspxctre/splexer.git $(DEPSDIR)/splexer
	-git -C $(DEPSDIR)/splexer fetch --tags
	cd $(DEPSDIR)/splexer && git checkout $(SPLEXER_VERSION)
	touch $@

clean:
	rm -rf $(BUILDROOT)
	rm -rf $(OBJROOT)
	rm -rf $(LIBROOT)
	rm -rf $(INCLUDEDIR)
	rm -rf *.mif

depsclean:
	rm -rf $(DEPSDIR)
