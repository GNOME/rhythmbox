test_env = environment()
test_env.set('G_TEST_SRCDIR', meson.current_source_dir())
test_env.set('G_TEST_BUILDDIR', meson.current_build_dir())
test_env.set('GSETTINGS_SCHEMA_DIR', join_paths(meson.project_build_root(), 'data'))
test_env.set('GSETTINGS_BACKEND', 'memory')
test_env.set('CK_DEFAULT_TIMEOUT', '0')

test('test-rhythmdb',
  executable('test-rhythmdb',
    ['test-rhythmdb.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check],
    c_args: ['-DTEST_DIR="@0@"'.format(meson.current_source_dir())]),
  depends: gschemas_compiled,
  env: test_env,
)

test('test-rhythmdb-query-model',
  executable('test-rhythmdb-query-model',
    ['test-rhythmdb-query-model.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  depends: gschemas_compiled,
  env: test_env,
)

test('test-rhythmdb-property-model',
  executable('test-rhythmdb-property-model',
    ['test-rhythmdb-property-model.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  depends: gschemas_compiled,
  env: test_env,
)

test('test-file-helpers',
  executable('test-file-helpers',
    ['test-file-helpers.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  env: test_env,
)

test('test-rb-lib',
  executable('test-rb-lib',
    ['test-rb-lib.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, check]),
  env: test_env,
)

test('test-audioscrobbler',
  executable('test-audioscrobbler',
    ['test-audioscrobbler.c', 'test-utils.c'],
    dependencies: [rhythmbox_core_dep, audioscrobbler_test_dep, check]),
  env: test_env,
)

test_widgets_resources = gnome.compile_resources('test-widgets-resources', 'test-widgets.gresource.xml',
  source_dir: ['../data'])
test('test-widgets',
  executable('test-widgets',
    ['test-widgets.c', 'test-utils.c', test_widgets_resources],
    dependencies: [rhythmbox_core_dep, check]),
  depends: gschemas_compiled,
  env: test_env,
)

executable('bench-rhythmdb-load',
  'bench-rhythmdb-load.c',
  dependencies: [rhythmbox_core_dep])
