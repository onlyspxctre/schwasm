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

SRCS := cli.c schwasm.c ir.c
OBJS := $(SRCS:%.c=%.o)
HEADERS := schwasm.h ir.h

SPLEXER_VERSION := 44b5485
SPLEXER_FLAGS := GRANULAR_TOK_UNKNOWN=y NO_MULTICOMMENT=y
SPLEXER_DIR := $(DEPSDIR)/splexer-$(SPLEXER_VERSION)

SPTL_VERSION := dc90d34
SPTL_DIR := $(DEPSDIR)/sptl.h-$(SPTL_VERSION)

ifneq ($(RELEASE),)
CFLAGS += -O2 -flto -static -DNDEBUG
SPLEXER_FLAGS += RELEASE=y
CONFIG := release
else
CFLAGS += -g3
CONFIG := debug
endif

ifneq ($(WINDOWS),)
OBJDIR := $(abspath ./obj/win)

CC += --target=x86_64-w64-mingw32 -DSP_STATIC
LIBS := -l:libsplexer-win.a
SPLEXER_FLAGS += WINDOWS=y
CONFIG := win-$(CONFIG)
else
LIBS := -lsplexer
endif

BUILDDIR := $(BUILDROOT)/$(CONFIG)
OBJDIR := $(OBJROOT)/$(CONFIG)
LIBDIR := $(LIBROOT)/$(CONFIG)

ifeq ($(RELEASE),)
LDFLAGS += -Wl,-rpath,$(LIBDIR)
endif

ifneq ($(WINDOWS),)
all: $(BUILDDIR)/schwasm.exe

$(BUILDDIR)/schwasm.exe: $(OBJS:%=$(OBJDIR)/%) $(LIBDIR)/libsplexer-win.a
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS:%=$(OBJDIR)/%) -L$(LIBDIR) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS:%=$(SRCDIR)/%) $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ -DSP_STATIC -c $<

$(LIBDIR)/libsplexer-win.a: $(SPLEXER_DIR).tar.gz
	$(MAKE) -C $(SPLEXER_DIR) clean
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(SPLEXER_DIR) $(SPLEXER_FLAGS) all
	cp -f $(SPLEXER_DIR)/build/libsplexer-win.a $@

else
all: $(BUILDDIR)/schwasm

$(BUILDDIR)/schwasm: $(OBJS:%=$(OBJDIR)/%) $(LIBDIR)/libsplexer.so $(LIBDIR)/libsplexer.a
	mkdir -p $(BUILDDIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS:%=$(OBJDIR)/%) -L$(LIBDIR) $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c $(HEADERS:%=$(SRCDIR)/%) $(INCLUDEDIR)/sptl.h $(INCLUDEDIR)/splexer.h
	mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ -c $<

$(LIBDIR)/libsplexer.a $(LIBDIR)/libsplexer.so &: $(SPLEXER_DIR).tar.gz
	$(MAKE) -C $(SPLEXER_DIR) clean
	mkdir -p $(LIBDIR)
	$(MAKE) -C $(SPLEXER_DIR) $(SPLEXER_FLAGS) all
	cp -f $(SPLEXER_DIR)/build/libsplexer.a $(LIBDIR)/libsplexer.a
	cp -f $(SPLEXER_DIR)/build/libsplexer.so $(LIBDIR)/libsplexer.so

endif

$(INCLUDEDIR)/splexer.h: $(SPLEXER_DIR).tar.gz
	mkdir -p $(INCLUDEDIR)
	cp $(SPLEXER_DIR)/splexer.h $@

$(INCLUDEDIR)/sptl.h: $(SPTL_DIR).tar.gz
	mkdir -p $(INCLUDEDIR)
	cp $(SPTL_DIR)/sptl.h $@

$(SPLEXER_DIR).tar.gz:
	mkdir -p $(SPLEXER_DIR)
	curl -fsSL -o $(SPLEXER_DIR).tar.gz https://github.com/onlyspxctre/splexer/archive/$(SPLEXER_VERSION).tar.gz
	tar xf $(SPLEXER_DIR).tar.gz -C $(SPLEXER_DIR) --strip-components=1

$(SPTL_DIR).tar.gz:
	mkdir -p $(SPTL_DIR)
	curl -fsSL -o $(SPTL_DIR).tar.gz https://github.com/onlyspxctre/sptl.h/archive/$(SPTL_VERSION).tar.gz
	tar xf $(SPTL_DIR).tar.gz -C $(SPTL_DIR) --strip-components=1

clean:
	rm -rf $(BUILDROOT)
	rm -rf $(OBJROOT)
	rm -rf $(LIBROOT)
	rm -rf $(INCLUDEDIR)
	rm -rf *.mif

depsclean:
	rm -rf $(DEPSDIR)
