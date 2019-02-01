MESON	:= $(shell which meson 2>/dev/null)
NINJA	:= $(shell which ninja-build 2>/dev/null || which ninja 2>/dev/null)
HOST	:= $(shell hostname -s)
BDIR	:= build-meson-$(HOST)

build: $(BDIR)/build.ninja
	$(NINJA) -C $(BDIR)

$(BDIR)/build.ninja:
	$(MESON) $(BDIR)

