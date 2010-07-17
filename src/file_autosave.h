/* Bluefish HTML Editor
 * file_autosave.h - autosave 
 *
 * Copyright (C) 2009 Olivier Sessink
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __FILE_AUTOSAVE_H_
#define __FILE_AUTOSAVE_H_
GList *register_autosave_journal(GFile *autosave_file, GFile *document_uri, GFile *project_uri);
void remove_autosave(Tdocument *doc);
void need_autosave(Tdocument *doc);
void autosave_init(gboolean recover, Tbfwin *bfwin);
void autosave_cleanup(void);
#endif 