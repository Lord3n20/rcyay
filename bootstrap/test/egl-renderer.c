/* egl-renderer (grafik-test): GL_RENDERER über EGL surfaceless (ohne Compositor), z. B. „llvmpipe (LLVM …)“ */
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <stdio.h>

int main(void)
{
	EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
	EGLint a[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
	EGLContext c;

	if (!eglInitialize(d, NULL, NULL)) {
		puts("eglInitialize fehlgeschlagen");
		return 1;
	}
	eglBindAPI(EGL_OPENGL_ES_API);
	c = eglCreateContext(d, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, a);
	if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c)) {
		puts("Kontext fehlgeschlagen");
		return 1;
	}
	printf("GL_RENDERER=%s GL_VERSION=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));
	return 0;
}
