# just some maintainer stuff for me ...
########################################################################

make-sync-dir = $(HOME)/projects/gnu-makefiles

.PHONY: sync
sync:: distclean
	test -d $(make-sync-dir)
	rm -f $(srcdir)/INSTALL $(srcdir)/mk/*.mk
	cp -v $(make-sync-dir)/INSTALL $(srcdir)/.
	cp -v $(make-sync-dir)/*.mk $(srcdir)/mk
	chmod 444 $(srcdir)/INSTALL $(srcdir)/mk/*.mk


repository = $(shell cat CVS/Repository)
release-dir ?= $(HOME)/projects/Releases
release-pub ?= goldbach@me.in-berlin.de:dl.bytesex.org/releases/$(repository)
tarball = $(release-dir)/$(repository)-$(VERSION).tar.gz

.PHONY: release
release:
	cvs tag $(RELTAG)
	cvs export -r $(RELTAG) -d "$(repository)-$(VERSION)" "$(repository)"
	find "$(repository)-$(VERSION)" -name .cvsignore -exec rm -fv "{}" ";"
	tar -c -z -f "$(tarball)" "$(repository)-$(VERSION)"
	rm -rf "$(repository)-$(VERSION)"
	scp $(tarball) $(release-pub)

