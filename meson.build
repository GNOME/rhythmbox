project('rhythmbox', 'c',
  version: '3.4.5',
  meson_version: '>= 0.59.0',
  default_options: ['c_std=gnu89'])

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')
python = import('python')

# plugin API versioning
# Increment on interface addition, reset on removal
RHYTHMBOX_CORE_AGE=0
# Increment on interface add, remove, or change
RHYTHMBOX_CORE_CURRENT=10
# Increment on source change, reset when CURRENT changes.
RHYTHMBOX_CORE_REVISION=0

cdata = configuration_data()
cc = meson.get_compiler('c')

common_cflags = [
  '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
]
test_cflags = [
  '-Wno-deprecated-declarations',
  '-Wformat=2',
  '-Wno-format-nonliteral',
  '-Wmissing-declarations',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wpointer-arith',
  '-Wcast-align',
  '-Wstrict-prototypes',
]
common_cflags += cc.get_supported_arguments (test_cflags)
add_project_arguments(common_cflags, language: 'c')

libmath = cc.find_library('m')

glib_required = '2.56.0'
cdata.set('GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_56')
cdata.set('GLIB_VERSION_MAX_ALLOWED', 'GLIB_VERSION_2_56')

gstreamer_required = '1.4.0'

intl = dependency('intl', required: true)
cairo = dependency('cairo', required: true)
gdk_pixbuf = dependency('gdk-pixbuf-2.0', version: '>= 2.18.0')
gio = dependency('gio-2.0', version: '>=' + glib_required, required: true)
gio_unix = dependency('gio-unix-2.0', version: '>=' + glib_required, required: true)
glib = dependency('glib-2.0', version: '>=' + glib_required, required: true)
gmodule_export = dependency('gmodule-export-2.0', version: '>=' + glib_required, required: true)
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
pango = dependency('pango', required: true)
tdb = dependency('tdb', version: '>= 1.2.6', required: true)
totem_plparser = dependency('totem-plparser', version: '>= 3.2.0', required: true)

check = dependency('check', required: get_option('tests'))

use_gudev = false
gudev = dependency('gudev-1.0', version: '>= 143', required: get_option('gudev'))
if gudev.found()
  use_gudev = true
  cdata.set('HAVE_GUDEV', 1)
endif

use_libsecret = false
libsecret = dependency('libsecret-1', version: '>= 0.18', required: get_option('libsecret'))
if libsecret.found()
  use_libsecret = true
endif
cdata.set('PY_LIBSECRET_ENABLED', use_libsecret)
cdata.set('WITH_LIBSECRET', use_libsecret)

have_gnu_fwrite_unlocked = false
if cc.has_function('fwrite_unlocked')
  have_gnu_fwrite_unlocked = cc.has_header_symbol('stdio.h', 'fwrite_unlocked')
endif
cdata.set('HAVE_GNU_FWRITE_UNLOCKED', have_gnu_fwrite_unlocked)

# rhythmbox-client interactive mode uses this non-standard function
have_cfmakeraw = cc.has_function('cfmakeraw', prefix: '#include <termios.h>')
cdata.set('HAVE_CFMAKERAW', have_cfmakeraw)

gdk_targets = run_command(['pkg-config', '--variable', 'targets', 'gdk-3.0'], check: true).stdout().strip()

have_prctl = cc.has_function('prctl', prefix: '#include <sys/prctl.h>')
cdata.set('HAVE_PRCTL', have_prctl)

have_pthread_getname_np = cc.links('''
  #include <pthread.h>
  int main() {
    char nm[17];
    pthread_getname_np(pthread_self(), nm, sizeof(nm));
  }''', name: 'pthread_getname_np')
cdata.set('HAVE_PTHREAD_GETNAME_NP', have_pthread_getname_np)

if gdk_targets.contains('x11')
  x11 = dependency('x11', required: true)
endif

cdata.set('GETTEXT_PACKAGE', '"rhythmbox"')
cdata.set('PACKAGE', '"rhythmbox"')
cdata.set('VERSION', '@0@'.format(meson.project_version()))

# Needed so that we can build static libraries and later link them into
# shared libraries.
add_global_arguments('-fPIC', language: 'c')

use_no_strict_aliasing = cc.has_argument('-fno-strict-aliasing')

datadir = get_option('prefix') / get_option('datadir')
libdir = get_option('prefix') / get_option('libdir')
pkgdatadir = datadir / 'rhythmbox'

#################
# Plugin support

plugindir = libdir / 'rhythmbox' / 'plugins'
plugindatadir = datadir / 'rhythmbox' / 'plugins'

sampleplugindir = libdir / 'rhythmbox' / 'sample-plugins'
sampleplugindatadir = datadir / 'rhythmbox' / 'sample-plugins'

enable_python = false
python_install = python.find_installation('python3', required: get_option('plugins_python'))
pygobject = dependency('pygobject-3.0', version: '>= 3.0.0', required: get_option('plugins_python'))
if python_install.found() and pygobject.found()
  enable_python = true
endif

enable_vala = false
vala_found = add_languages('vala', required: get_option('plugins_vala'), native: false)
if vala_found
  enable_vala = true
  vapi_dir = meson.current_source_dir() / 'bindings' / 'vala'
  add_project_arguments(['--vapidir', vapi_dir], language: 'vala')
endif

plugin_headers = ['plugins/rb-plugin-macros.h']
install_headers(plugin_headers, subdir: 'rhythmbox/plugins')

po_dir = meson.project_source_root() / 'po'
# FIXME: Move custom_target to i18n.merge_file when gettext acquires plugin support.
#        http://lists.gnu.org/archive/html/bug-gettext/2017-06/msg00001.html
msgfmt_plugin_cmd = [
  find_program('msgfmt'),
  '--desktop',
  '--keyword=Name',
  '--keyword=Description',
  '--template=@INPUT@',
  '-d', po_dir,
  '--output=@OUTPUT@'
]

##########
# Plugins

plugins = [
  'audiocd',
  'audioscrobbler',
  'dbus-media-server',
  'generic-player',
  'iradio',
  'mpris',
  'power-manager'
]

if enable_python
  plugins += [
    'rb',

    'artsearch',
    'im-status',
    'listenbrainz',
    'lyrics',
    'magnatune',
    'pythonconsole',
    'rbzeitgeist',
    'replaygain',
    'webremote',

    # 'context'
  ]
endif

libgpod = dependency('libgpod-1.0', version: '>= 0.7.92', required: get_option('ipod'))
if libgpod.found()
  plugins += 'ipod'
endif

libmtp = dependency('libmtp', version: '>= 0.3.0', required: get_option('mtp'))
if libmtp.found()
  if use_gudev
    plugins += 'mtpdevice'
  elif get_option('mtp').enabled()
    error('MTP explicitly requested but GUdev is not available')
  endif
endif
if use_gudev
  plugins += 'android'
endif

libnotify = dependency('libnotify', version: '>= 0.7.0', required: get_option('libnotify'))
if libnotify.found()
  plugins += 'notification'
endif

lirc = dependency('lirc', required: get_option('lirc'))
if lirc.found()
  plugins += 'lirc'
endif

brasero_media = dependency('libbrasero-media3', version: '>= 2.31.5', required: get_option('brasero'))
if brasero_media.found()
  plugins += 'brasero-disc-recorder'
endif

if get_option('fm_radio').allowed()
  if cc.has_header('linux/videodev2.h')
    plugins += 'fmradio'
  elif get_option('fm_radio').enabled()
    error('FM radio plugin explicitly requested, but cannot be built')
  endif
endif

if get_option('daap').allowed()
  libdmapsharing4 = dependency('libdmapsharing-4.0', version: '>= 3.9.4', required: false)

  if libdmapsharing4.found()
    plugins += 'daap'
  else
    libdmapsharing = dependency('libdmapsharing-3.0', version: '>= 2.9.19', required: get_option('daap'))
    if libdmapsharing.found()
      plugins += 'daap'
    endif
  endif
endif

grilo = dependency('grilo-0.3', version: '>= 0.3.1', required: get_option('grilo'))
if grilo.found()
  plugins += 'grilo'
endif

pkg.generate(
  name: 'rhythmbox',
  description: 'plugin API for rhythmbox',
  requires: [glib, gobject, gio, gtk, libsoup, totem_plparser, gstreamer],
  requires_private: libxml,
  variables: [
    'plugin_dir=' + plugindir,
  ]
)

gnome.post_install(
  glib_compile_schemas: true,
  gtk_update_icon_cache: true,
  update_desktop_database: true
)

configure_file(input : 'config.h.meson',
  output : 'config.h',
configuration : cdata)

summary({'Tests': check.found(),
         'Help': get_option('help'),
         'API documentation': get_option('gtk_doc'),
        },
        section: 'General',
        bool_yn: true)
summary({'iPod integration': plugins.contains('ipod'),
         'MTP integration': plugins.contains('mtpdevice'),
         'CD burning support': plugins.contains('brasero-disc-recorder'),
         'DAAP (music sharing) support': plugins.contains('daap'),
         'libnotify support': plugins.contains('notification'),
         'GUdev support': use_gudev,
         'Python plugin support': enable_python,
         'Vala plugin support': enable_vala,
         'Libsecret keyring support': use_libsecret,
         'FM radio support': plugins.contains('fmradio'),
         'Grilo support': plugins.contains('grilo'),
         'Sample plugins': get_option('sample-plugins'),
        },
        section: 'Plugins',
        bool_yn: true)

configinc = include_directories('.')
libinc = include_directories('lib')
mpidinc = include_directories('lib/libmediaplayerid')
metadatainc = include_directories('metadata')
rhythmdbinc = include_directories('rhythmdb')
backendsinc = include_directories('backends', 'backends/gstreamer')
pluginsinc = include_directories('plugins')
podcastinc = include_directories('podcast')
shellinc = include_directories('shell')
sourcesinc = include_directories('sources', 'sources/sync')
widgetsinc = include_directories('widgets')

subdir('data')

subdir('lib')
subdir('metadata')
subdir('rhythmdb')
subdir('backends')
subdir('podcast')
subdir('sources')
subdir('widgets')
subdir('shell')

foreach p : plugins
  subdir('plugins' / p)
endforeach
if get_option('sample-plugins')

  subdir('sample-plugins/sample')
  if enable_python
    subdir('sample-plugins/sample-python')
  endif
  if enable_vala
    subdir('sample-plugins/sample-vala')
  endif
endif
subdir('remote')

subdir('bindings')
if check.found()
  subdir('tests')
endif
subdir('po')
if get_option('gtk_doc')
  subdir('doc/reference')
endif
if get_option('help')
  subdir('help')
endif

