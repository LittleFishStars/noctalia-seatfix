CC      ?= cc
CFLAGS  ?= -O2 -fPIC -Wall -Wextra
LDFLAGS ?= -shared
LDLIBS  ?= -ldl

PREFIX  ?= $(HOME)/.local
LIBDIR  ?= $(PREFIX)/lib
BINDIR  ?= $(PREFIX)/bin
LIB     := noctalia-seat-filter.so
WRAPPER := noctalia-seatfix

.PHONY: all install uninstall clean test

all: $(LIB)

$(LIB): src/noctalia-seat-filter.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< $(LDLIBS)

# 安装过滤库与启动包装脚本（不修改任何系统文件）
install: $(LIB)
	install -d $(LIBDIR) $(BINDIR)
	install -m 0644 $(LIB) $(LIBDIR)/$(LIB)
	install -m 0755 bin/$(WRAPPER) $(BINDIR)/$(WRAPPER)

uninstall:
	rm -f $(LIBDIR)/$(LIB) $(BINDIR)/$(WRAPPER)

# 自检：装好后跑一下，应只看到一个 wl_seat（seat0）
test:
	LD_PRELOAD=$(PWD)/$(LIB) wayland-info 2>/dev/null | grep -c 'interface: wl_seat' || \
	LD_PRELOAD=$(PWD)/$(LIB) WAYLAND_DEBUG=1 wl-paste -l 2>&1 | grep -c 'wl_seat.*\.name'

clean:
	rm -f $(LIB)
