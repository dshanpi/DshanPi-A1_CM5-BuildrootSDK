SDK_ROOT ?= $(abspath ../..)
HOST_DIR ?= $(SDK_ROOT)/buildroot/output/rockchip_rk3576/host
SYSROOT ?= $(HOST_DIR)/aarch64-buildroot-linux-gnu/sysroot
CC := $(HOST_DIR)/bin/aarch64-buildroot-linux-gnu-gcc
PKG_CONFIG := $(HOST_DIR)/bin/pkg-config
WAYLAND_SCANNER := $(HOST_DIR)/bin/wayland-scanner
XDG_SHELL_XML := $(SYSROOT)/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
BUILD_DIR := build
TARGET := $(BUILD_DIR)/cm5-factory-test
PACKAGES := wayland-client cairo pangocairo alsa
CPPFLAGS += -I$(BUILD_DIR) -Isrc $(shell $(PKG_CONFIG) --cflags $(PACKAGES))
CFLAGS ?= -O2 -g
CFLAGS += -std=gnu11 -Wall -Wextra -Wformat=2 -Wshadow -MMD -MP
LDLIBS += $(shell $(PKG_CONFIG) --libs $(PACKAGES)) -lm
OBJECTS := $(BUILD_DIR)/main.o $(BUILD_DIR)/factory.o $(BUILD_DIR)/nvme_probe.o $(BUILD_DIR)/checks.o $(BUILD_DIR)/hardware.o $(BUILD_DIR)/xdg-shell-protocol.o
.PHONY: all clean install
all: $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@
$(BUILD_DIR)/xdg-shell-client-protocol.h: $(XDG_SHELL_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) client-header $< $@
$(BUILD_DIR)/xdg-shell-protocol.c: $(XDG_SHELL_XML) | $(BUILD_DIR)
	$(WAYLAND_SCANNER) private-code $< $@
$(BUILD_DIR)/%.o: src/%.c $(BUILD_DIR)/xdg-shell-client-protocol.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD_DIR)/xdg-shell-protocol.o: $(BUILD_DIR)/xdg-shell-protocol.c $(BUILD_DIR)/xdg-shell-client-protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(BUILD_DIR):
	mkdir -p $@
install: $(TARGET)
	install -D -m 0755 $(TARGET) $(DESTDIR)/usr/bin/cm5-factory-test
	install -D -m 0755 cm5-factory-launch $(DESTDIR)/usr/bin/cm5-factory-launch
	install -D -m 0755 S98cm5-factory-test $(DESTDIR)/etc/init.d/S98cm5-factory-test
	@if test -s $(BUILD_DIR)/cm5_yt8521.ko; then \
	    release=$$(cat $(BUILD_DIR)/phy-kernel-release); \
	    install -D -m 0644 $(BUILD_DIR)/cm5_yt8521.ko $(DESTDIR)/usr/lib/modules/$$release/extra/cm5_yt8521.ko; \
	    install -D -m 0755 S19cm5-phy $(DESTDIR)/etc/init.d/S19cm5-phy; \
	fi
	install -D -m 0644 cm5-factory-test.conf $(DESTDIR)/etc/cm5-factory-test.conf
	install -D -m 0644 cm5-factory-test.desktop $(DESTDIR)/usr/share/applications/cm5-factory-test.desktop
	install -D -m 0644 assets/a1-factory-test.png $(DESTDIR)/usr/share/icons/hicolor/256x256/apps/cm5-factory-test.png
clean:
	rm -rf $(BUILD_DIR)
-include $(OBJECTS:.o=.d)
