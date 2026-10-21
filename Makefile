# Build robust_triangle_wl without GLFW.
#
# Needs: libwayland-client, libwayland-egl, EGL + GL (Mesa), and
#        wayland-protocols + wayland-scanner (for the xdg-shell codegen).
# Debian/Ubuntu/KDE Neon:  sudo apt install libwayland-dev wayland-protocols libegl-dev libgl-dev
# Arch:                    sudo pacman -S wayland wayland-protocols mesa
# Fedora:                  sudo dnf install wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGL-devel

PKGS          := wayland-client wayland-egl egl gl
CFLAGS        := -O2 -Wall -Wextra $(shell pkg-config --cflags $(PKGS))
LDLIBS        := $(shell pkg-config --libs $(PKGS)) -lm

SCANNER       := $(shell pkg-config --variable=wayland_scanner wayland-scanner)
PROTO_DIR     := $(shell pkg-config --variable=pkgdatadir wayland-protocols)
XDG_XML       := $(PROTO_DIR)/stable/xdg-shell/xdg-shell.xml

TARGET        := robust_triangle_wl
GEN           := xdg-shell-client-protocol.h xdg-shell-protocol.c

$(TARGET): robust_triangle_wl.c xdg-shell-protocol.c xdg-shell-client-protocol.h
	$(CC) $(CFLAGS) robust_triangle_wl.c xdg-shell-protocol.c -o $@ $(LDLIBS)

xdg-shell-client-protocol.h:
	$(SCANNER) client-header $(XDG_XML) $@

xdg-shell-protocol.c:
	$(SCANNER) private-code  $(XDG_XML) $@

clean:
	rm -f $(TARGET) $(GEN)

.PHONY: clean

