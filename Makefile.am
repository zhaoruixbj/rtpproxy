ACLOCAL_AMFLAGS=	-I m4

AUTOMAKE_OPTIONS=	foreign

if BUILD_MODULES
SUBD_MOD=	modules
endif
if BUILD_ELPERIODIC
SUBD_ELP=       libelperiodic/src
endif
if BUILD_EXTRACTAUDIO
SUBD_EAUD=       extractaudio
endif
if BUILD_UDP_CONTENTION
SUBD_UDCNT=	pertools
endif

SUBDIRS=	libexecinfo makeann $(SUBD_MOD) $(SUBD_ELP) src $(SUBD_UDCNT) tests \
  $(SUBD_EAUD)

dist_man_MANS=	rtpproxy.8
EXTRA_DIST=	README.remote manpage.xml debian/README.Debian debian/changelog \
  debian/compat debian/conffiles.ex debian/control debian/copyright \
  debian/cron.d.ex debian/dirs debian/docs debian/manpage.1.ex \
  debian/manpage.sgml.ex debian/manpage.xml.ex debian/menu.ex \
  debian/postinst.ex debian/postrm.ex debian/preinst.ex debian/prerm.ex \
  debian/rtpproxy-default.ex debian/rtpproxy.doc-base.EX \
  debian/rtpproxy.init debian/rules debian/watch.ex  rpm/rtpproxy.init \
  rpm/rtpproxy.spec freebsd/rtpproxy.in LICENSE README.md

rtpproxy.8: manpage.xml
	xsltproc /usr/local/share/xsl/docbook/manpages/docbook.xsl manpage.xml
