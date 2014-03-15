# This file is part of aMIDIcat.

VERSION := 1.2

# Trivial Makefile: Compiling this program is a one-liner
amidicat: amidicat.c
	gcc -pthread -O2 -ggdb -Wall -DVERSION_STR=\"$(VERSION)\" amidicat.c -lasound -o amidicat

doc: amidicat.docbook
	xmllint --valid --noout --noent --debug --dtdvalid /usr/share/sgml/docbook/xml-dtd-4.5/docbookx.dtd amidicat.docbook
	sed s/@VERSION@/$(VERSION)/g <amidicat.docbook >amidicat.docbook.temp
	xsltproc --output amidicat.1 /usr/share/sgml/docbook/xsl-stylesheets/manpages/docbook.xsl amidicat.docbook.temp
	xsltproc --output amidicat.html /usr/share/sgml/docbook/xsl-stylesheets/html/docbook.xsl amidicat.docbook.temp
	rm -f amidicat.docbook.temp
			
clean:
	rm -f amidicat

pristine: clean
	rm -f amidicat.1
	rm -f amidicat.html
	
# Convenience for making releases
release: pristine doc
	rm -rf ../amidicat-$(VERSION) ../amidicat-$(VERSION).tar ../amidicat-$(VERSION).tar.gz
	mkdir -m 0755 -p ../amidicat-$(VERSION)
	cp -a . ../amidicat-$(VERSION)
	cd ../amidicat-$(VERSION) && find . -depth -name '.svn' -exec rm -rfv '{}' ';'
	cd ../amidicat-$(VERSION) && rm -rf .kdev4 && rm -f amidicat.kdev4
	cd ../amidicat-$(VERSION) && find . -name '*~' -delete
	cd .. && tar -cvf amidicat-$(VERSION).tar amidicat-$(VERSION)
	cd .. && gzip -9 amidicat-$(VERSION).tar
