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
