# Release-Tarball statt git; Tests (googletest/effcee/re2) entfallen ohnehin (--nocheck)
usrside_tarball "spirv-tools-$pkgver.tar.gz::https://github.com/KhronosGroup/SPIRV-Tools/archive/refs/tags/vulkan-sdk-$pkgver.tar.gz" \
	e6c83a215538fbfd265ccf398d3877985055df1273dbed73912bd05307664688 SPIRV-Tools "SPIRV-Tools-vulkan-sdk-$pkgver"
unset b2sums
usrside_ohne git
# CMAKE_INSTALL_LIBDIR=lib: GNUInstallDirs wählt lib64, wenn /etc/arch-release fehlt (Arch-PKGBUILDs verlassen sich
# darauf); pacman ersetzte damit den Symlink /usr/lib64 → lib durch einen Ordner
build() {
	local cmake_options=(
		-D BUILD_SHARED_LIBS=ON
		-D CMAKE_BUILD_TYPE=Release
		-D CMAKE_INSTALL_PREFIX=/usr
		-D CMAKE_INSTALL_LIBDIR=lib
		-D CMAKE_INSTALL_SYSCONFDIR=/etc
		-D CMAKE_SKIP_INSTALL_RPATH=ON
		-D SPIRV-Headers_SOURCE_DIR=/usr
		-D SPIRV_TOOLS_BUILD_STATIC=OFF
		-D SPIRV_WERROR=OFF
	)
	cmake -S SPIRV-Tools -B build -G Ninja "${cmake_options[@]}"
	cmake --build build
}
