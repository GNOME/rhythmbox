#ifndef __EGG_INTL_H__
#define __EGG_INTL_H__

#include <config.h>

#include <libintl.h>
#define _(String) gettext (String)
#ifdef gettext_noop
#   define N_(String) gettext_noop (String)
#else
#   define N_(String) (String)
#endif

#endif /* __EGG_INTL_H__ */
