# Rhythmbox

Rhythmbox is your one-stop multimedia application, supporting
a music library, multiple playlists, internet radio, and more.

Rhythmbox is free software, licensed under the GNU GPL.

Our IRC channel is `#gnome-rhythmbox` on [Libera Chat](irc.libera.chat).

You can also post questions about Rhythmbox under the Applications
category on [GNOME Discourse](https://discourse.gnome.org/).

## Installation

Rhythmbox requires the following packages:

- A working GNOME platform including glib 2.66, gtk+ 3.16, and libsoup 3
- meson 0.59 or newer
- totem-plparser 3.2.0 or newer
- GStreamer 1.4.0. or newer and associated plugin packages
- libpeas 0.7.3 or newer
- json-glib
- libxml2 2.7.8 or newer
- tdb 1.2.6 or newer
- gettext 0.20 or newer

Rhythmbox can also make use of the following packages:

- pygobject 3.0.0 or newer (for python plugin support)
- GUdev 143 or newer (for iPod and generic audio player support)
- libgpod 0.7.92 or newer (for iPod support)
- libnotify 0.7 or newer
- libbrasero-media 2.31.5 or newer
- libdmapsharing 3.9.11 or newer
- libmtp 0.3.0 or newer
- libsecret 0.18 or newer
- grilo 0.3.1 or newer
- itstool (for documentation)
- gi-docgen (for API documentation)

Many package managers provide a way to install all the build dependencies for
a package, such as `dnf builddep` or `apt-get build-dep`, which will install
almost all of the above.  The requirements may have changed slightly between
the version packaged in the distribution and the current development version,
so you might have to install some additional packages.


## Simple install procedure

```
  % meson setup _build -Dprefix=$PWD/_install    # prepare the build
  % ninja -C _build                              # build Rhythmbox
  % ninja -C _build install                      # install Rhythmbox
```

This installs Rhythmbox to the `_install` directory under the source tree.
The executable to run will then be `_install/bin/rhythmbox`.
Before you can run it from there, you will also need to set an environment
variable to load schemas from the install location:

```
  % export GSETTINGS_SCHEMA_DIR=$PWD/_install/share/glib-2.0/schemas
  % _install/bin/rhythmbox
```

By default, the build will enable optional features if the packages they require
are installed.  To check which options are available and which are enabled,
run `meson configure _build`.

More detailed installation instructions can be found in [meson's
website](https://mesonbuild.com/Quick-guide.html).

## How to report bugs

Bugs should be reported to the GitLab repository.
(https://gitlab.gnome.org/GNOME/rhythmbox/issues) You will need to
create an account for yourself.

Please read the GNOME bug reporting guidelines, documented in the
[GNOME Project Handbook](https://handbook.gnome.org/issues.html)

