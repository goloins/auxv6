/*
 * 6wm/atoms.h — atom initialization
 */

#ifndef WM6_ATOMS_H
#define WM6_ATOMS_H

#include "6wm.h"

/* Intern all atoms into g_wm. Call once after XOpenDisplay. */
void atoms_init(void);

#endif /* WM6_ATOMS_H */
