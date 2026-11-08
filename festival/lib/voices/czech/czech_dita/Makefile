# Configuration

festival_voices_path = /usr/share/festival/voices
estdir = /usr

# End of configuration

voice_name := dita
package := voice-czech-$(voice_name)
version := 1.0.0

destdir = $(festival_voices_path)/czech/czech_$(voice_name)

.PHONY: all install uninstall dist dist-src dist-bin lpc-files

all: festvox/czech_$(voice_name).scm lpc-files group/$(voice_name).group

lpc-files: $(patsubst wav/%.wav, lpc/%.lpc, $(wildcard wav/*.wav))
lpc/$(voice_name)0000.lpc: wav/$(voice_name)0000.wav pm/$(voice_name)0000.pm
	mkdir -p lpc
	ESTDIR="$(estdir)" ./tools/make_lpc $<
lpc/%.lpc: wav/%.wav pm/%.pm etc/powfacts
	ESTDIR="$(estdir)" ./tools/make_lpc $<

festvox/czech_$(voice_name).scm: festvox/czech_$(voice_name).scm.in
	sed 's/DESTDIR/$(subst /,\/,$(destdir))/' $< > $@

group/$(voice_name).group: lpc-files
	mkdir -p group

clean:
	rm -rf $(package)-$(version)
	rm -rf $(package)-bin-$(version) $(package)-bin-$(version).tar
	rm -f $(package)-$(version).tar.bz2 $(package)-bin-$(version).tar.bz2
	rm -f festvox/czech_$(voice_name).scm

distclean: clean
	rm -f lpc/* group/*

maintainer-clean: distclean

install: group/$(voice_name).group festvox/czech_$(voice_name).scm
	install -d $(destdir)
	install -d $(destdir)/festvox
	install -m 644 festvox/czech_$(voice_name).scm $(destdir)/festvox/
	install -d $(destdir)/group
	install -m 644 group/$(voice_name).group $(destdir)/group/

uninstall:
	rm -f $(destdir)/festvox/czech_$(voice_name).scm $(destdir)/group/$(voice_name).group
	-rmdir $(destdir)/festvox
	-rmdir $(destdir)/group

dist: clean dist-src dist-bin

dist-src:
	git archive --format=tar --prefix=$(package)-$(version)/ HEAD | bzip2 -9 -c > $(package)-$(version).tar.bz2

dist-bin: all
	rm -rf $(package)-bin-$(version) $(package)-bin-$(version).tar $(package)-bin-$(version).tar.bz2
	mkdir $(package)-bin-$(version)
	cp -a COPYING INSTALL README* festvox group $(package)-bin-$(version)/
	for d in `find $(package)-bin-$(version) -name .git`; do rm -r $$d; done
	tar cf $(package)-bin-$(version).tar $(package)-bin-$(version)
	bzip2 -9 $(package)-bin-$(version).tar
