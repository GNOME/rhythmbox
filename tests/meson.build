check = dependency('check', required: false)

if check.found()
  test('test-rhythmdb',
    executable('test-rhythmdb',
      ['test-rhythmdb.c', 'test-utils.c'],
      dependencies: [rhythmbox_core_dep, check]))

  test('test-rhythmdb-query-model',
    executable('test-rhythmdb-query-model',
      ['test-rhythmdb-query-model.c', 'test-utils.c'],
      dependencies: [rhythmbox_core_dep, check]))

  test('test-rhythmdb-property-model',
    executable('test-rhythmdb-property-model',
      ['test-rhythmdb-property-model.c', 'test-utils.c'],
      dependencies: [rhythmbox_core_dep, check]))

  test('test-file-helpers',
    executable('test-file-helpers',
      ['test-file-helpers.c', 'test-utils.c'],
      dependencies: [rhythmbox_core_dep, check]))

  test('test-rb-lib',
    executable('test-rb-lib',
      ['test-rb-lib.c', 'test-utils.c'],
      dependencies: [rhythmbox_core_dep, check]))

  test('test-audioscrobbler',
    executable('test-audioscrobbler',
      ['test-audioscrobbler.c', 'test-utils.c'],
      dependencies: [rhythmbox_core_dep, audioscrobbler_test_dep, check]))

  test_widgets_resources = gnome.compile_resources('test-widgets-resources', 'test-widgets.gresource.xml',
    source_dir: ['../data'])
  test('test-widgets',
    executable('test-widgets',
      ['test-widgets.c', 'test-utils.c', test_widgets_resources],
      dependencies: [rhythmbox_core_dep, check]))

  executable('bench-rhythmdb-load',
    'bench-rhythmdb-load.c',
    dependencies: [rhythmbox_core_dep])
else
  message('Please install `check` so that unit tests can be executed.')
endif
