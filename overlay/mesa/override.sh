# Stufe 2b: virgl (VM mit virtio-gpu gl=on) und softpipe; Stufe 2d: + llvmpipe (LLVM aus bootstrap/pakete/llvm);
# Stufe 2e: + iris/crocus (Intel, CLC aus clang + SPIRV-LLVM-Translator + SPIRV-Tools + libclc). Wayland/EGL/GBM/GLES2,
# kein GLX/X11, kein libglvnd (mesa liefert libEGL/libGLESv2 selbst), kein Vulkan (anv folgt), kein VA.
# Python-Module für den Bau (mako, markupsafe, packaging, pyyaml) als Quell-Tarballs über PYTHONPATH,
# damit sie nicht als Pakete für das Vorbau-python nötig sind.
source=("https://archive.mesa3d.org/mesa-$_pkgver.tar.xz"
	https://files.pythonhosted.org/packages/source/M/Mako/mako-1.3.10.tar.gz
	https://files.pythonhosted.org/packages/source/M/MarkupSafe/markupsafe-3.0.2.tar.gz
	https://files.pythonhosted.org/packages/source/p/packaging/packaging-25.0.tar.gz
	https://files.pythonhosted.org/packages/source/P/PyYAML/pyyaml-6.0.2.tar.gz)
sha256sums=(4e4e438eac7b840a9383988cbb7c08dad8041da544b9319fc26d319fa7348961
	99579a6f39583fa7e5630a28c3c1f440e4e97a414b80372649c0ce338da2ea28
	ee55d3edf80167e48ea11a923c7386f4669df67d7994554387f84e7d8b0a2bf0
	d443872c98d677bf60f6a1f2f8c1cb748e8fe762d2bf9d3148b5599295b0fc4f
	d584d9ec91ad65861cc08d42e834324ef890a082e591037abe114850ff7bbc3e)
for _a in b2sums sha512sums md5sums; do unset "$_a"; done
unset validpgpkeys
usrside_ohne clang-libs directx-headers libelf libglvnd libva libx11 libxcb libxext libxshmfence libxxf86vm \
	lm_sensors glslang cbindgen rust rust-bindgen \
	python-mako python-packaging python-ply python-yaml python-pycparser python-sphinx python-sphinx-hawkmoth \
	valgrind cmake elfutils clang libxrandr vulkan-icd-loader xcb-util-keysyms venus-protocol python-lxml xorgproto
usrside_nur mesa
prepare() { :; }
build() {
	export PYTHONPATH="$srcdir/mako-1.3.10:$srcdir/markupsafe-3.0.2/src:$srcdir/packaging-25.0/src:$srcdir/pyyaml-6.0.2/lib"
	CFLAGS+=" -ffat-lto-objects" CXXFLAGS+=" -ffat-lto-objects"
	arch-meson "mesa-$_pkgver" build -D platforms=wayland -D gallium-drivers=virgl,softpipe,llvmpipe,iris,crocus -D vulkan-drivers= \
		-D glvnd=disabled -D llvm=enabled -D shared-llvm=enabled -D gbm=enabled -D egl=enabled -D gles1=disabled -D gles2=enabled \
		-D glx=disabled -D opengl=true -D valgrind=disabled -D libunwind=disabled -D lmsensors=disabled \
		-D gallium-va=disabled -D video-codecs= -D android-libbacktrace=disabled -D xmlconfig=enabled \
		-D zstd=enabled -D expat=enabled -D teflon=false -D precomp-compiler=auto -D spirv-tools=enabled \
		-D gallium-rusticl=false -D microsoft-clc=disabled -D intel-rt=disabled -D tools= -D build-tests=false
	meson compile -C build
}
package_mesa() {
	depends=(expat musl libdrm gcc wayland zlib zstd llvm spirv-tools)
	optdepends=()
	provides=("mesa-libgl=$epoch:$pkgver-$pkgrel" opengl-driver libegl libgles libgbm libEGL.so libGLESv2.so libgbm.so)
	meson install -C build --destdir "$pkgdir" --no-rebuild
	rm -rf "$pkgdir/usr/share/doc"
	install -Dm644 "mesa-$_pkgver/docs/license.rst" -t "$pkgdir/usr/share/licenses/$pkgname"
}
