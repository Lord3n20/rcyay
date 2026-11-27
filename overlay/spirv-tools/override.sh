# Release-Tarball statt git; Tests (googletest/effcee/re2) entfallen ohnehin (--nocheck)
usrside_tarball "spirv-tools-$pkgver.tar.gz::https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/vulkan-sdk-$pkgver.tar.gz" \
	e6c83a215538fbfd265ccf398d3877985055df1273dbed73912bd05307664688 SPIRV-Tools "SPIRV-Tools-vulkan-sdk-$pkgver"
unset b2sums
usrside_ohne git
