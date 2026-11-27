# Release-Tarball statt git (kein git im Bau-chroot); eigener Dateiname, weil SPIRV-Tools denselben Tag-Namen hat
usrside_tarball "spirv-headers-$pkgver.tar.gz::https://github.com/KhronosGroup/SPIRV-Headers/archive/refs/tags/vulkan-sdk-$pkgver.tar.gz" \
	5b3a0d099f5c7649e29c0938476c462e71d66ad89a0bde42fa1ba3eb476956c9 SPIRV-Headers "SPIRV-Headers-vulkan-sdk-$pkgver"
unset b2sums
usrside_ohne git
