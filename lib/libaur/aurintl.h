/* aurintl.h - Übersetzung der libaur-Meldungen (Domäne "rcyay", nur intern) */
#ifndef AUR_AURINTL_H
#define AUR_AURINTL_H
#if defined(AUR_NLS)
#include <libintl.h>
#define _(s) dgettext("rcyay", s)
#else
#define _(s) (s)
#endif
#endif
