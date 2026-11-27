/* x11-fenster.c (rcyay-e2, 27.11.2026): kleiner X11-Client für grafik-test. Verbindet sich mit $DISPLAY
 * (niri startet dafür xwayland-satellite + Xwayland bei Bedarf), meldet die Bildschirmgröße, zeigt ein Fenster
 * mit Titel „x11-fenster“ und wartet ARGV[1] Sekunden (Standard 20), damit niri msg windows es sieht.
 *   cc x11-fenster.c -o x11-fenster -lxcb */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

int main(int argc, char **argv)
{
	int n = argc > 1 ? atoi(argv[1]) : 20;
	xcb_connection_t *c = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(c)) {
		fprintf(stderr, "x11-fenster: keine Verbindung zu DISPLAY=%s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "");
		return 1;
	}
	xcb_screen_t *s = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
	printf("X11 %ux%u, Hersteller %.*s\n", s->width_in_pixels, s->height_in_pixels,
	       xcb_setup_vendor_length(xcb_get_setup(c)), xcb_setup_vendor(xcb_get_setup(c)));
	xcb_window_t w = xcb_generate_id(c);
	uint32_t werte[] = { s->white_pixel };
	xcb_create_window(c, XCB_COPY_FROM_PARENT, w, s->root, 0, 0, 400, 300, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
			  s->root_visual, XCB_CW_BACK_PIXEL, werte);
	const char *titel = "x11-fenster";
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(titel), titel);
	/* WM_CLASS „Instanz\0Klasse\0“ → App ID bei niri */
	xcb_change_property(c, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8, 24,
			    "x11-fenster\0x11-fenster\0");
	xcb_map_window(c, w);
	xcb_flush(c);
	fflush(stdout);
	sleep(n);
	xcb_disconnect(c);
	return 0;
}
