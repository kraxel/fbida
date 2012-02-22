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


repository := $(shell basename $(PWD))
release-dir ?= $(HOME)/projects/Releases
release-pub ?= bigendian.kraxel.org:/public/vhosts/www.kraxel.org/releases/$(repository)
tarball = $(release-dir)/$(repository)-$(VERSION).tar

$(tarball).gz:
	git tag -m "release $(VERSION)" "$(VERSION)"
	git push --tags
	git archive --format=tar --prefix=$(repository)-$(VERSION)/ \
		-o $(tarball) $(VERSION)
	gzip $(tarball)

.PHONY: release
release: $(tarball).gz
	gpg --detach-sign --armor $(tarball).gz
	scp $(tarball).gz* $(release-pub)
