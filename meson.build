#GRILO_REQS=0.3.0

project('rhythmbox', 'c',
  version: '3.4.1')

gnome = import('gnome')

# plugin API versioning
# Increment on interface addition, reset on removal
RHYTHMBOX_CORE_AGE=0
# Increment on interface add, remove, or change
RHYTHMBOX_CORE_CURRENT=10
# Increment on source change, reset when CURRENT changes.
RHYTHMBOX_CORE_REVISION=0

cdata = configuration_data()
cc = meson.get_compiler('c')

libmath = cc.find_library('m')

glib_required = '2.38.0'
gstreamer_required = '1.4.0'

gdk_pixbuf = dependency('gdk-pixbuf-2.0', version: '>= 2.18.0')
gio = dependency('gio-2.0', version: '>=' + glib_required, required: true)
gio_unix = dependency('gio-unix-2.0', version: '>=' + glib_required, required: true)
glib = dependency('glib-2.0', version: '>=' + glib_required, required: true)
gobject = dependency('gobject-2.0', version: '>=' + glib_required, required: true)
gobject_introspection = dependency('gobject-introspection-1.0', version: '>= 0.10.0', required: true)
gstreamer = dependency('gstreamer-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_audio = dependency('gstreamer-audio-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_base = dependency('gstreamer-base-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_controller = dependency('gstreamer-controller-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_plugins_base = dependency('gstreamer-plugins-base-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_pbutils = dependency('gstreamer-pbutils-1.0', version: '>= ' + gstreamer_required, required: true)
gstreamer_tag = dependency('gstreamer-tag-1.0', version: '>= ' + gstreamer_required, required: true)
gtk = dependency('gtk+-3.0', version: '>= 3.16.0', required: true)
json_glib = dependency('json-glib-1.0', required: true)
libpeas = dependency('libpeas-1.0', version: '>= 0.7.3', required: true)
libpeas_gtk = dependency('libpeas-gtk-1.0', version: '>= 0.7.3', required: true)
libsoup = dependency('libsoup-2.4', version: '>= 2.42.0', required: true)
libxml = dependency('libxml-2.0', version: '>= 2.7.8', required: true)
tdb = dependency('tdb', version: '>= 1.2.6', required: true)
totem_plparser = dependency('totem-plparser', version: '>= 3.2.0', required: true)
zlib = dependency('zlib', required: true)

intltool_merge = find_program('intltool-merge')

use_gudev = false
if get_option('gudev') != 'no'
  gudev = dependency('gudev-1.0', version: '>= 143', required: false)
  if gudev.found()
    use_gudev = true
    cdata.set('HAVE_GUDEV', 1)
  elif get_option('gudev') == 'yes'
    error('GUdev explicitly requested, but is not available')
  endif
endif

use_hal = false
if get_option('hal') != 'no'
  hal = dependency('hal', version: '>= 0.5 < 0.6', required: false)
  dbus_glib = dependency('dbus-glib-1', required: false)
  if hal.found() and dbus_glib.found()
    cdata.set('HAVE_HAL', 1)
    use_hal = true
  elif get_option('hal') == 'yes'
    error('HAL explicitly requested, but is not available')
  endif
endif

use_ipod = false
if get_option('ipod') != 'no'
  libgpod = dependency('libgpod-1.0', version: '>= 0.6', required: false)
  if libgpod.found()
    use_ipod = true
    cdata.set('HAVE_ITDB_START_STOP_SYNC', libgpod.version() >= '0.7.92')
  elif get_option('ipod') == 'yes'
    error('iPod explicitly requested but libgpod couldn\'t be found')
  endif
endif

use_mtp = false
if get_option('mtp') != 'no'
  libmtp = dependency('libmtp', version: '>= 0.3.0', required: false)

  if libmtp.found()
    if use_gudev or use_hal
      use_mtp = true
    else
      error('MTP explicitly requested but GUdev and HAL are not available')
    endif
  elif get_option('mtp') == 'yes'
    error('MTP explicitly requested but libmtp is not available')
  endif
endif

use_notify = false
if get_option('libnotify') != 'no'
  libnotify = dependency('libnotify', version: '>= 0.7.0', required: false)

  if libnotify.found()
    use_notify = true
    # In configure.ac the version of libnotify is passed to the C compiler
    # like -DLIBNOTIFY_VERSION_MAJOR=... but nothing seems to actually use
    # that, so I've not implemented that in the Meson build system.
  elif get_option('libnotify') == 'yes'
    error('libnotify support explicitly requested, but libnotify couldn\'t be found')
  endif
endif

use_libsecret = false
if get_option('libsecret') != 'no'
  libsecret = dependency('libsecret-1', version: '>= 0.18', required: false)

  if libsecret.found()
    use_libsecret = true
  elif get_option('libsecret') == 'yes'
    error('keyring support explicitly requested but libsecret could not be found')
  endif
endif

use_lirc = false
if get_option('lirc') != 'no'
  lirc = dependency('lirc', required: false)

  if lirc.found()
    use_lirc = true
  elif get_option('lirc') == 'yes'
    error('lirc explicitly requested but no support found')
  endif
endif

enable_mmkeys = false
if get_option('mmkeys') != 'no'
  if cc.has_header('X11/XF86keysym.h')
    enable_mmkeys = true
  elif get_option('mmkeys') == 'yes'
    error('Multimedia keys explicitly requested but no support found')
  endif
endif

have_libbrasero_media = false
if get_option('brasero') != 'no'
  brasero_media = dependency('libbrasero-media3', version: '>= 2.31.5', required: false)

  if brasero_media.found()
    cdata.set('HAVE_LIBBRASERO_MEDIA', 1)
    have_libbrasero_media = true
  elif get_option('brasero') == 'yes'
    error('CD burning support explicitly requested but libbrasero-media could not be found')
  endif
endif

cdata.set10('WITH_RHYTHMDB_TREE', 1)

cdata.set10('RHYTHMDB_ENABLE_SANITY_CHECK', get_option('rhythmdb-debug'))

have_gnu_fwrite_unlocked = cc.has_function('fwrite_unlocked')

deps_rb_client = [glib, gio, gtk, gio_unix]

gdk_targets = run_command(['pkg-config', '--variable', 'targets', 'gdk-3.0']).stdout().strip()

totem_plparser_uselibcamel = run_command(['pkg-config', '--variable', 'uselibcamel', 'totem-plparser']).stdout().strip()

if gdk_targets.contains('x11')
  x11 = dependency('x11', required: true)
endif

if totem_plparser_uselibcamel == 'no'
  error('totem playlist parsing library has Podcast parsing disabled')
endif

#RHYTHMBOX_CFLAGS="$RHYTHMBOX_CFLAGS $GSTREAMER_CFLAGS -DGST_USE_UNSTABLE_API"
#RHYTHMBOX_LIBS="$RHYTHMBOX_LIBS $GSTREAMER_LIBS"

test_strftime_src = '''#include <time.h>
int main ()
{
  char buf[100];
  struct tm tm = {0};
  tm.tm_year = 99;
  if (strftime(buf, 100, "%EY", &tm) == 4 &&
      strcmp (buf, "1999")==0)
    return 0;
  return 1;
}'''

if cc.compiles(test_strftime_src, name: 'whether strftime supports %E and %O modifiers')
  cdata.set('HAVE_STRFTIME_EXTENSION', 1)
endif

cdata.set('GETTEXT_PACKAGE', '"rhythmbox"')
cdata.set('PACKAGE', '"rhythmbox"')
cdata.set('VERSION', '"@0@"'.format(meson.project_version()))

cdata.set10('USE_UNINSTALLED_DIRS', get_option('uninstalled-build'))
cdata.set('SHARE_UNINSTALLED_DIR', '"@0@"'.format(meson.source_root() + '/data'))
cdata.set('SHARE_UNINSTALLED_BUILDDIR', '"@0@"'.format(meson.build_root() + '/data'))
cdata.set('METADATA_UNINSTALLED_DIR', '"@0@"'.format(meson.build_root() + '/metadata'))

# Needed so that we can build static libraries and later link them into
# shared libraries.
add_global_arguments('-fPIC', language: 'c')

set_more_warnings = false
if get_option('more-warnings') == 'yes'
  set_more_warnings = true
elif get_option('more-warnings') == 'auto'
  if run_command('[', '-d', '.git', ']').returncode() == 0
    set_more_warnings = true
  endif
endif

if set_more_warnings == true and cc.get_id() == 'gcc'
  warning_flags = [
    '-Wcomment', '-Wformat', '-Wnonnull', '-Wimplicit-int', '-Wimplicit',
    '-Wmain', '-Wmissing-braces', '-Wparentheses', '-Wsequence-point',
    '-Wreturn-type', '-Wswitch', '-Wtrigraphs', '-Wunused-function',
    '-Wunused-label', '-Wunused-value', '-Wchar-subscripts',
    '-Wmissing-declarations', '-Wmissing-prototypes', '-Wnested-externs',
    '-Wpointer-arith', '-Wcast-align', '-Wformat-security', '-Wall',
    '-Wno-deprecated-declarations', '-std=gnu89']

  # in configure.ac this option is only added if '-O0' is not present in the CFLAGS
  # I'm not sure how to do it in Meson and not sure if it matters; the configure.ac
  # stuff dates from 2005 in commit 8a76a0a72abad7ae24b895910d93791b90dfe005.
  warning_flags += ['-Wuninitialized']

  foreach option : warning_flags
    add_global_arguments(option, language: 'c')
    # In configure.ac we would check if each compiler argument was actually
    # supported by the compiler. That's quite slow and here we just assume
    # they all are. If this causes problems, `if cc.has_argument(option)` is
    # the way to implement such a check here.
  endforeach
endif

use_no_strict_aliasing = cc.has_argument('-fno-strict-aliasing')

#################
# Plugin support

#PLUGIN_LIBTOOL_FLAGS="-module -avoid-version"
#AC_SUBST(PLUGIN_LIBTOOL_FLAGS)
#
plugindir = get_option('libdir') + '/rhythmbox/plugins'
cdata.set('PLUGINDIR', get_option('prefix') + '/' + plugindir)

plugindatadir = get_option('datadir') + '/rhythmbox/plugins'
cdata.set('PLUGINDATADIR', get_option('prefix') + '/' + plugindatadir)
cdata.set('SAMPLEPLUGINDIR', get_option('libdir') + '/rhythmbox/sample-plugins')

enable_python = false
if get_option('python-plugins') != 'no'
  python = find_program('python3')
  if not python.found() and get_option('python-plugins') == 'yes'
    error('Python support explicitly requested, but no suitable Python interpreter could be found')
  endif

  pygobject = dependency('pygobject-3.0', version: '>= 3.0.0')
  if not pygobject.found() and get_option('python-plugins') == 'yes'
    error('Python support explicitly requested, but pygobject-3.0 could not be found')
  endif

  pyoverridesdir = run_command([python, '-c', '''import gi; print(gi._overridesdir)''']).stdout().strip()
  enable_python = true
endif

enable_vala = false
if get_option('vala-plugins') != 'no'
  vala_found = add_languages('vala')

  if not vala_found and get_option('vala-plugins') == 'yes'
    error('Vala plugin support explicitly requested, but not found')
  endif

  enable_vala = true
endif

##########
# Plugins

enable_fm_radio = false
if get_option('fm-radio') != 'no'
  if cc.has_header('linux/videodev2.h')
    enable_fm_radio = true
  elif get_option('fm-radio') == 'yes'
    error('FM radio plugin explicitly requested, but cannot be built')
  endif
endif

enable_browser_plugin = false
if get_option('browser-plugin')
  # This is configurable with configure.ac by setting MOZILLA_PLUGINDIR in the
  # environment. Here it's hardcoded, mainly because Meson doesn't yet support
  # reading environment vars: https://github.com/mesonbuild/meson/issues/9
  mozilla_plugindir = get_option('libdir') + '/mozilla/plugins'

  cdata.set('ITMS_HANDLER', 'x-scheme-handler/itms;x-scheme-handler/itmss;')
endif

have_libdmapsharing = false
if get_option('daap') != 'no'
  libdmapsharing = dependency('libdmapsharing-3.0', version: '>= 2.9.19', required: false)

  if libdmapsharing.found()
    cdata.set('HAVE_LIBDMAPSHARING', 1)
    have_libdmapsharing = true
  elif get_option('brasero') == 'yes'
    error('DAAP support explicitly requested, but libdmapsharing couldn\'t be found')
  endif
endif

if get_option('grilo') != 'no'
  grilo = dependency('grilo-0.3', version: '>= 0.3.0', required: false)

  if grilo.found()
    enable_grilo = true
    cdata.set('HAVE_GRILO', 1)
  elif get_option('grilo') == 'yes'
    error('Grilo support explicitly requested, but grilo could not be found')
  endif
endif

configure_file(
  input: 'rhythmbox.pc.in',
  install: true,
  install_dir: get_option('libdir') + '/pkgconfig',
  output: 'rhythmbox.pc',
  configuration: cdata
)

configure_file(input : 'config.h.meson',
  output : 'config.h',
configuration : cdata)

message('Rhythmbox was configured with the following options:')
if get_option('database') == 'tree'
  message('** Tree database is enabled')
else
  error('Unknown database!')
endif
if get_option('rhythmdb-debug') == '0'
  message('** RhythmDB sanity checking enabled (may be slow!)')
endif

if enable_mmkeys
  message('   Multimedia keys support is disabled')
else
  message('** Multimedia keys support is enabled')
endif

if use_ipod
  message('** iPod integration enabled')
else
  message('   iPod integration disabled')
endif
if use_mtp
  message('** MTP integration enabled')
else
  message('   MTP integration disabled')
endif
if have_libbrasero_media
  message('** CD burning support enabled')
else
  message('   CD burning support disabled')
endif
if have_libdmapsharing
  message('** DAAP (music sharing) support is enabled')
else
  message('   DAAP (music sharing) support is disabled')
endif
if use_notify
  message('** libnotify support is enabled')
else
  message('   libnotify support is disabled')
endif
if use_gudev
  message('** GUdev support enabled')
elif use_hal
  message('** HAL support enabled')
else
  message('   GUdev support disabled')
endif
if enable_python
  message('** Python plugin support enabled')
else
  message('   Python plugin support disabled')
endif
if enable_vala
  message('** Vala plugin support enabled')
else
  message('   Vala plugin support disabled')
endif
if use_libsecret
  message('** Libsecret keyring support enabled')
else
  message('   Libsecret keyring support disabled')
endif
if enable_fm_radio
  message('** FM radio support enabled')
else
  message('   FM radio support disabled')
endif

if enable_browser_plugin
  message('** iTunes detection browser plugin (for podcasts) enabled')
else
  message('   iTunes detection browser plugin (for podcasts) disabled')
endif

message('End options')

configinc = include_directories('.')
backendsinc = include_directories('backends')
gstreamerbackendinc = include_directories('backends/gstreamer')
libinc = include_directories('lib')
libmediaplayeridinc = include_directories('lib/libmediaplayerid')
metadatainc = include_directories('metadata')
pluginsinc = include_directories('plugins')
podcastinc = include_directories('podcast')
rhythmdbinc = include_directories('rhythmdb')
shellinc = include_directories('shell')
sourcesinc = include_directories('sources')
sourcessyncinc = include_directories('sources/sync')
widgetsinc = include_directories('widgets')

rpath = '@0@/@1@'.format(get_option('prefix'), get_option('libdir'))

subdir('data')

subdir('lib')
subdir('metadata')
subdir('rhythmdb')
subdir('backends')
subdir('podcast')
subdir('sources')
subdir('widgets')
subdir('shell')
subdir('plugins')
subdir('remote')

subdir('tests')

subdir('po')


# Some tests that can't be defined in the subdirs because rhythmbox_core_dep
# must be defined first.

test_metadata = executable('test-metadata',
  'metadata/test-metadata.c',
  dependencies: rhythmbox_core_dep)
test('test-metadata', test_metadata)

# These are interactive UI tests.

test_rb_segmented_bar = executable('widgets/test-rb-segmented-bar', 'widgets/test-rb-segmented-bar.c',
  dependencies: [rhythmbox_core_dep])

test_uri_dialog = executable('widgets/test-uri-dialog', 'widgets/test-uri-dialog.c',
  dependencies: [rhythmbox_core_dep])
