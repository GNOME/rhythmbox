## arch-tag: Automake rules for English help files

figdir = figures
docname = rhythmbox
lang = C
omffile = rhythmbox-C.omf
entities = fdl-appendix.xml legal.xml authors.xml quick-reference.xml user-guide.xml

authors.xml: $(top_srcdir)/AUTHORS $(top_srcdir)/MAINTAINERS $(top_srcdir)/DOCUMENTERS
	(echo '<?xml version="1.0" encoding="UTF-8"?>'; \
	 echo '<sect2 id="authors">'; \
	 echo '<title>Authors</title>'; \
	 echo '<sect3>'; \
         echo '<title>Lead Developers:</title>'; \
	 echo '<para><simplelist>') > $@.tmp
	 sed -e 's/>/<\/email><\/member>/' -e 's/ </ <email>/' -e 's/^/<member>/'  < $(top_srcdir)/MAINTAINERS >> $@.tmp
	(echo '</simplelist></para></sect3><sect3>'; \
	 echo '<title>Contributors</title>'; \
	 echo '<para><simplelist>') >> $@.tmp
	 sed -e 's/>/<\/email><\/member>/' -e 's/ </ <email>/' -e 's/^/<member>/'  < $(top_srcdir)/AUTHORS >> $@.tmp
	(echo '</simplelist></para></sect3>'; \
	 echo '<sect3><title>Documentation</title>'; \
	 echo '<para><simplelist>') >> $@.tmp
	 sed -e 's/>/<\/email><\/member>/' -e 's/ </ <email>/' -e 's/^/<member>/'  < $(top_srcdir)/DOCUMENTERS >> $@.tmp
	(echo '</simplelist></para></sect3>'; \
	 echo '</sect2>') >> $@.tmp
	mv $@.tmp $@

all-am: authors.xml

include $(top_srcdir)/xmldocs.make

CLEANFILES += authors.xml

dist-hook: app-dist-hook


