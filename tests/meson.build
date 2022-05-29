test('test-rhythmdb',
  executable('test-rhythmdb',
    ['test-rhythmdb.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check],
    c_args: ['-DTEST_DIR="@0@"'.format(meson.current_source_dir())]),
  depends: gschemas_compiled,
  env: ['CK_DEFAULT_TIMEOUT=0'],
)

test('test-rhythmdb-query-model',
  executable('test-rhythmdb-query-model',
    ['test-rhythmdb-query-model.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  depends: gschemas_compiled,
  env: ['CK_DEFAULT_TIMEOUT=0'],
)

test('test-rhythmdb-property-model',
  executable('test-rhythmdb-property-model',
    ['test-rhythmdb-property-model.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  depends: gschemas_compiled,
  env: ['CK_DEFAULT_TIMEOUT=0'],
)

test('test-file-helpers',
  executable('test-file-helpers',
    ['test-file-helpers.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  env: ['CK_DEFAULT_TIMEOUT=0'],
)

test('test-rb-lib',
  executable('test-rb-lib',
    ['test-rb-lib.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  env: ['CK_DEFAULT_TIMEOUT=0'],
)

test('test-audioscrobbler',
  executable('test-audioscrobbler',
    ['test-audioscrobbler.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, audioscrobbler_test_dep, check]),
  env: ['CK_DEFAULT_TIMEOUT=0'],
)

test_widgets_resources = gnome.compile_resources('test-widgets-resources', 'test-widgets.gresource.xml',
  source_dir: ['../data'])
test('test-widgets',
  executable('test-widgets',
    ['test-widgets.c', 'test-utils.c', test_widgets_resources],
    dependencies: [rhythmbox_core_dep, check]),
  depends: gschemas_compiled,
  env: ['CK_DEFAULT_TIMEOUT=0'],
)

executable('bench-rhythmdb-load',
  'bench-rhythmdb-load.c',
  dependencies: [rhythmbox_core_dep])
