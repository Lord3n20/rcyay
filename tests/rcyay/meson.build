# Einheitstests für libaur (rcyay-e1); Mock-AUR-Tests: tests/rcyay/aur/run-tests
foreach t : ['json', 'srcinfo', 'rpc', 'resolve']
  exe = executable(
    'test-rcyay-' + t,
    files('unit/test_' + t + '.c'),
    include_directories : [includes, libaur_includes],
    link_with : [libaur, libalpm],
    dependencies : [libarchive, libcurl],
    build_by_default : false)
  test('rcyay-' + t, exe, protocol : 'tap', suite : 'rcyay')
endforeach

# Unit-Übersetzer (rcyay-dienste) mit echten Units aus dienste/units
test_unit = executable(
  'test-rcyay-unit',
  files('unit/test_unit.c', '../../src/rcyay/unit.c'),
  include_directories : [includes, libaur_includes, include_directories('../../src/rcyay')],
  link_with : [libalpm],
  build_by_default : false)
test('rcyay-unit', test_unit, args : [meson.current_source_dir() / 'dienste/units'],
  protocol : 'tap', suite : 'rcyay')
