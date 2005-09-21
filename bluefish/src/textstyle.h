/* Bluefish HTML Editor
 *
 * textstyle - color styles used for syntax highlighting
 * 
 * Copyright (C) 2005 Olivier Sessink
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <gtk/gtk.h>
#ifndef __TEXTSTYLE_H__
#define __TEXTSTYLE_H__

void textstyle_rebuild(void);
GtkTextTagTable *textstyle_return_tagtable(void);

/* this function is *not* designed to run very often. It is designed 
to run *once* for each style, on start, or after 'OK' or 'apply' in the
preferences panel.
*/
GtkTextTag *textstyle_get(const gchar *name);


/* returns a widget that can be used in the preferences panel */
GtkWidget* textstyle_combo_box(GList *configstyles);

#endif /* __TEXTSTYLE_H__ */
