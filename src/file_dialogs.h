/* Bluefish HTML Editor
 * file_dialogs.h
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
#ifndef __FILEDIALOGS_H_
#define __FILEDIALOGS_H_

void files_advanced_win(Tbfwin *bfwin, gchar *basedir);
void file_open_advanced_cb(GtkWidget * widget, Tbfwin *bfwin);
void file_open_cb(GtkWidget * widget, Tbfwin *bfwin);
void file_open_url_cb(GtkWidget * widget, Tbfwin *bfwin);

void doc_save_backend(Tdocument *doc, gboolean do_save_as, gboolean do_move, gboolean close_doc, gboolean close_window);
void file_save_cb(GtkWidget * widget, Tbfwin *bfwin);
void file_save_as_cb(GtkWidget * widget, Tbfwin *bfwin);
void file_move_to_cb(GtkWidget * widget, Tbfwin *bfwin);
void file_save_all_cb(GtkWidget * widget, Tbfwin *bfwin);

void doc_close_single_backend(Tdocument *doc, gboolean close_window);
void file_close_cb(GtkWidget * widget, Tbfwin *bfwin);
void doc_close_multiple_backend(Tbfwin *bfwin, gboolean close_window);
void file_close_all_cb(GtkWidget * widget, Tbfwin *bfwin);

#endif /* __FILEDIALOGS_H_ */
