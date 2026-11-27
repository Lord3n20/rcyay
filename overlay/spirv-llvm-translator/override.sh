# Tarballs statt git (Translator-Tag + die von Arch festgelegte SPIRV-Headers-Version); ohne Tests (bräuchten lit)
source=("spirv-llvm-translator-$pkgver.tar.gz::https://github.com/KhronosGroup/SPIRV-LLVM-Translator/archive/refs/tags/v$pkgver.tar.gz"
	"spirv-headers-$_spirv_headers_commit.tar.gz::https://github.com/KhronosGroup/SPIRV-Headers/archive/$_spirv_headers_commit.tar.gz")
sha256sums=(9ed57de3f31c575c836bef8f05ce5dfa98a8d80e64d948a02aac83c2e124f842
	117010e525fe36b1ce006e0e58a6e0a3b415aabc9179d912b92737f41f3f994f)
unset b2sums
usrside_ohne git
prepare() {
	ln -sfn "SPIRV-LLVM-Translator-$pkgver" SPIRV-LLVM-Translator
	ln -sfn "SPIRV-Headers-$_spirv_headers_commit" SPIRV-Headers
}
build() {
	local cmake_options=(
		-D BUILD_SHARED_LIBS=ON
		-D CMAKE_BUILD_TYPE=Release
		-D CMAKE_INSTALL_PREFIX=/usr
		-D CMAKE_INSTALL_SYSCONFDIR=/etc
		-D CMAKE_POSITION_INDEPENDENT_CODE=ON
		-D CMAKE_SKIP_RPATH=ON
		-D LLVM_CONFIG=llvm-config
		-D LLVM_EXTERNAL_SPIRV_HEADERS_SOURCE_DIR="${srcdir}/SPIRV-Headers/include"
		-D LLVM_LIBDIR_SUFFIX=
		-D LLVM_SPIRV_ENABLE_LIBSPIRV_DIS=ON
		-D LLVM_SPIRV_INCLUDE_TESTS=OFF
		-W no-author
	)
	export CXXFLAGS+=" -isystem${srcdir}/SPIRV-Headers/include"
	cmake -S SPIRV-LLVM-Translator -B build -G Ninja "${cmake_options[@]}"
	cmake --build build
}
