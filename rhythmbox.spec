Name:		rhythmbox
Summary:	Music Management Application 
Version:	0.3.0
Release:	1
License:	GPL
Group:		Development/Libraries
Source:		%{name}-%{version}.tar.gz
BuildRoot: 	%{_tmppath}/%{name}-%{version}-root
Requires:   	gtk2 >= 1.3.12
Requires:	libgnomeui >= 1.111.0
Requires:       eel2 >= 1.1.5
Requires:	monkey-media >= 0.5.0
Requires:	gstreamer-gnomevfs >= 0.3.0

%description
Music Management application with support for ripping audio-cd's,
playback of Ogg Vorbis and Mp3 and burning of cdroms

%prep
%setup -q

%build
%configure

%makeinstall
%find_lang %name

%clean
rm -rf %{buildroot}

%post -p /sbin/ldconfig

%postun -p /sbin/ldconfig

%files -f %name.lang
%defattr(-, root, root)
%doc AUTHORS COPYING ChangeLog INSTALL README NEWS
%{_bindir}/rhythmbox
%{_datadir}/rhythmbox/art/*
%{_datadir}/rhythmbox/node-views/*
# %{_datadir}/rhythmbox/glade/*
%{_datadir}/locale/*/*/*
%{_datadir}/gnome-2.0/ui/*
%{_libdir}/bonobo/servers/GNOME_Rhythmbox_Shell.server
%{_datadir}/applications/rhythmbox.desktop
%{_datadir}/pixmaps/rhythmbox.png

%changelog
* Wed Jun 12 2002 Christian Schaller <Uraeus@linuxrising.org>
- Changed to work with 0.3.0 rewrite of Rhythmbox

* Thu Jun 07 2002 Christian Schaller <Uraeus@linuxrising.org>
- Updated to work with latest CVS
- Added GConf scheme stuff
- Fixed eel dependency

* Mon Mar 18 2002 Jorn Baayen <jorn@nl.linux.org>
- removed bonobo dependency
* Sat Mar 02 2002 Christian Schaller <Uraeus@linuxrising.org>
- created new spec file
