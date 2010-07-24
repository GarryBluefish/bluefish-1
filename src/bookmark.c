/* Bluefish HTML Editor
 * bookmark.c - bookmarks
 *
 * Copyright (C) 2003 Oskar Swida
 * modifications (C) 2004-2010 Olivier Sessink
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
/*#define DEBUG*/

#include <gtk/gtk.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>

#include "bluefish.h"
#include "bookmark.h"
#include "dialog_utils.h"
#include "document.h"
#include "gtk_easy.h"
#include "gui.h"
#include "bf_lib.h"
#include "stringlist.h"
#include "menu.h"				/* menu_translate() */


/*
bookmarks will be loaded and saved to an arraylist (see stringlist.c). This
is a double linked list (GList *) with pointers to string arrays (gchar **).

To have the GUI work with them, we convert those arrays (gchar **) into a
Tbmark struct. This struct will have a pointer to the array (gchar **) so
on change it can directly change the array as well, without any need to
look it up in the list.

For the GUI, we store everything in a Gtktreestore. The treestore will have a
pointer to a string with the name, and it will also have a pointer to the
Tbmark. When the user clicks in the Gtktreeview widget, we can get
immediately a pointer to the Tbmark, and that has the Gtktextmark, so that
is very easy, and very fast!

But now we have one problem: all normal windows do share the same bookmarks list.
So it is probably the most logical to have them store the same Gtktreestore as
well. The best way is to have the project functions create/destroy the
gtktreestore when they convert a window (Tbfwin) into a project window.
*/

#define BMARK_SHOW_NUM_TEXT_CHARS 20
#define BMARK_STORE_TEXT_NUM_CHARS 15
enum {
	NAME_COLUMN,				/* bookmark name */
	PTR_COLUMN,					/* bookmark pointer */
	N_COLUMNS
};

enum {
	BMARK_ADD_PERM_DIALOG,
	BMARK_RENAME_TEMP_DIALOG,
	BMARK_RENAME_PERM_DIALOG
};

typedef struct {
	GtkTextMark *mark;
	GFile *filepath;
	gint offset;
	Tdocument *doc;
	GtkTreeIter iter;			/* for tree view */
	gchar *description;
	gchar *text;
	gchar *name;
	gint len;					/* file length for integrity check - perhaps some hash code is needed */
	gboolean is_temp;
	gchar **strarr;				/* this is a pointer to the location where this bookmark is stored in the sessionlist,
									   so we can immediately change it _in_ the list */
} Tbmark;
#define BMARK(var) ((Tbmark *)(var))

typedef struct {
	GtkTreeStore *bookmarkstore; /* the treestore with the name and the pointer to the Tbmark */
	GHashTable *bmarkfiles; /* a hash table with the GFile as key, and the iter in the treestore as value */
} Tbmarkdata;
#define BMARKDATA(var) ((Tbmarkdata *)(var))

enum {
	BM_FMODE_FULL,
	BM_FMODE_HOME, /* not implemented, defaults to full */
	BM_FMODE_FILE,
	BM_FMODE_PATH
};

enum {
	BM_SMODE_BOTH,
	BM_SMODE_NAME,
	BM_SMODE_CONTENT
};

enum {
	BM_SEARCH_BOTH,
	BM_SEARCH_NAME,
	BM_SEARCH_CONTENT
};

/* Free bookmark structure */
static void bmark_free(gpointer ptr)
{
	Tbmark *m;
	if (ptr == NULL)
		return;
	m = BMARK(ptr);
	if (m->doc && m->mark) {
		DEBUG_MSG("bmark_free, deleting mark %p\n",m->mark);
		gtk_text_buffer_delete_mark(m->doc->buffer, m->mark);
		m->doc = NULL;
	}
#ifdef DEBUG
	if (m->strarr) {
		DEBUG_MSG("bmark_free, NOT GOOD, strarr should be NULL here...\n");
	}
#endif
	g_object_unref(m->filepath);
	g_free(m->text);
	g_free(m->name);
	g_free(m->description);
	/*g_print("free bmark %p\n",m);*/
	g_slice_free(Tbmark,m);
}

static gchar *bmark_showname(Tbfwin *bfwin, Tbmark *b) {
	if (b->name && strlen(b->name)>0 && bfwin->session->bookmarks_show_mode == BM_SMODE_BOTH) {
		return g_strconcat(b->name, " - ", b->text, NULL);
	} else if ((b->name && bfwin->session->bookmarks_show_mode == BM_SMODE_NAME) || !b->text) {
		return g_strdup(b->name);
	} else {
		return g_strdup(b->text);
	}
}

static gchar *bmark_filename(Tbfwin *bfwin, GFile *filepath) {
	gchar *title;
	switch (bfwin->session->bookmarks_filename_mode) {
	case BM_FMODE_PATH:
		title = g_file_get_uri(filepath);
		break;
	case BM_FMODE_FILE:
		title = g_file_get_basename(filepath);
		break;
	case BM_FMODE_FULL:
	default:
		title = g_file_get_uri(filepath);
		break;
	}
	return title;
}

static void bmark_update_treestore_name(Tbfwin *bfwin) {
	GtkTreeIter piter, citer;
	gboolean cont1, cont2;
	cont1 = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&piter);

	while (cont1) {
		Tbmark *b=NULL;
		gchar *name;
		cont2 = gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&citer,&piter);
		/* first handle the filename of the parent */
		if (cont2) {
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &citer, PTR_COLUMN, &b,-1);
			name = bmark_filename(bfwin, b->filepath);
			gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &piter, NAME_COLUMN, name, -1);
			g_free(name);
		}
		while (cont2) {
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &citer, PTR_COLUMN, &b,-1);
			name = bmark_showname(bfwin, b);
			gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &citer, NAME_COLUMN, name, -1);
			g_free(name);
			cont2 = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&citer);
		}
		cont1 = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&piter);
	}
}

static void bmark_update_offset_from_textmark(Tbmark *b) {
	if (b->doc && b->mark) {
		GtkTextIter it, it2;
		int len;
		gtk_text_buffer_get_iter_at_mark(b->doc->buffer, &it, b->mark);
		b->offset = gtk_text_iter_get_offset(&it);
		len = strlen(b->text);
		/* to aid future repositioning (if the file has changed) update the text as well */
		gtk_text_buffer_get_iter_at_offset(b->doc->buffer,&it2,b->offset+len);
		g_free(b->text);
		b->text = gtk_text_buffer_get_text(b->doc->buffer,&it,&it2,FALSE);
		DEBUG_MSG("bmark_update_offset_from_textmark, text=%s\n",b->text);
	}
}

/*
 * this function should use a smart sorting algorithm to find
 * the GtkTreeIter of the bookmark *before* the place where this
 * bookmark should be added, but the same function can be used to
 * find the bookmarks we have to check to detect double bookmarks
 * at the same line.
 *
 * returns the bookmark closest before 'offset', or the bookmark exactly at 'offset'
 *
 * returns NULL if we have to append this as first child to the parent
 *
 */
static Tbmark *bmark_find_bookmark_before_offset(Tbfwin *bfwin, guint offset, GtkTreeIter *parent) {
	gint jumpsize, num_children, child;
	GtkTreeIter iter;

	num_children = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), parent);
	if (num_children == 0) {
		return NULL;
	}

	if (num_children == 1) {
		gint compare;
		Tbmark *b;
		gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &iter, parent, 0);
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&iter,PTR_COLUMN,&b, -1);

		bmark_update_offset_from_textmark(b);
		DEBUG_MSG("bmark_find_bookmark_before_offset, num_children=%d\n",num_children);
		compare = (offset - b->offset);

		if (compare <= 0) {
			return NULL;
		} else {
			return b;
		}
	}
	jumpsize = (num_children+2)/2;
	child = num_children + 1 - jumpsize;
	DEBUG_MSG("bmark_find_bookmark_before_offset, num_children=%d,jumpsize=%d,child=%d\n",num_children,jumpsize,child);
	while (jumpsize > 0) {
		gint compare;
		Tbmark *b;

		if (child > num_children) child = num_children;
		if (child < 1) child = 1;
		/* we request child-1, NOT child*/
		gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &iter, parent, child-1);
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&iter,PTR_COLUMN,&b, -1);

		bmark_update_offset_from_textmark(b);
		compare = (offset - b->offset);
		DEBUG_MSG("in_loop: jumpsize=%2d, child=%2d, child offset=%3d, compare=%3d\n",jumpsize,child,b->offset,compare);
		if (compare == 0) {
			return b;
		} else if (compare < 0) {
			jumpsize = (jumpsize > 3) ? (jumpsize+1)/2 : jumpsize-1;
			if (jumpsize <= 0) {
				child--;
				/* we request child-1, NOT child*/
				if (child >= 1 && gtk_tree_model_iter_nth_child(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &iter, parent, child-1)) {
					gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&iter,PTR_COLUMN,&b, -1);
					bmark_update_offset_from_textmark(b);
					DEBUG_MSG("in_loop: returning bookmark (offset %d) for previous child %d\n",b->offset, child);
					return b;
				} else{
					DEBUG_MSG("in_loop: no previous child, return NULL\n");
					return NULL;
				}
			}
			child = child - jumpsize;
		} else { /* compare > 0 */
			jumpsize = (jumpsize > 3) ? (jumpsize+1)/2 : jumpsize-1;
			if (jumpsize <= 0) {
				DEBUG_MSG("in_loop: return bookmark (offset %d) from child %d\n",b->offset, child);
				return b;
			}
			child = child + jumpsize;
		}
	}
	DEBUG_MSG("bmark_find_bookmark_before_offset, end-of-function, return NULL\n");
	return NULL;
}

void bmark_rename_uri(Tbfwin * bfwin, Tbmark * b, GFile *newuri) {
	g_object_unref(b->filepath);
	b->filepath = newuri;
	if (newuri)
		g_object_ref(b->filepath);
	if (b->strarr != NULL) {
		g_free(b->strarr[2]);
		if (newuri)
			b->strarr[2] = g_file_get_parse_name(b->filepath);
		else
			b->strarr[2] = g_strdup("");
	}
}

void bmark_doc_renamed(Tbfwin * bfwin, Tdocument *doc) {
	if (doc->uri && doc->bmark_parent) {
		GtkTreeIter tmpiter;
		gboolean cont;
		gchar *name;
		name = bmark_filename(bfwin, doc->uri);
		gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, doc->bmark_parent, NAME_COLUMN, name, -1);
		g_free(name);
		cont = gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, doc->bmark_parent);
		while (cont) {
			Tbmark *b;
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN,&b,-1);
			if (b) {
				bmark_rename_uri(bfwin, b, doc->uri);
			}
			cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&tmpiter);
		}
	}
}


/* this function re-uses the b->strarr if possible, otherwise it will create a new one and
append it to the list */
static void bmark_store(Tbfwin * bfwin, Tbmark * b) {
	gchar **strarr;
	if (b->is_temp) {
		DEBUG_MSG("bmark_store, called for temp bookmark %p ?!?! weird!!!! returning\n", b);
		return;
	}

	/* if there is a strarr already, we only update the fields, else we append a new one */
	if (b->strarr == NULL) {
		DEBUG_MSG("bmark_store, creating new strarr for bookmark %p\n",b);
		strarr = g_malloc0(sizeof(gchar *) * 7);
		DEBUG_MSG("name=%s, description=%s, text=%s\n", b->name, b->description, b->text);
		strarr[2] = g_file_get_parse_name(b->filepath);
		strarr[4] = g_strdup(b->text);
	} else {
		DEBUG_MSG("bmark_store, bookmark %p has strarr at %p\n",b,b->strarr);
		strarr = b->strarr;
		/* free the ones we are going to update */
		g_free(strarr[0]);
		g_free(strarr[1]);
		g_free(strarr[3]);
		g_free(strarr[5]);
	}
	strarr[0] = g_strdup(b->name);
	strarr[1] = g_strdup(b->description);

	if (b->doc) b->len = gtk_text_buffer_get_char_count(b->doc->buffer);

	strarr[3] = g_strdup_printf("%d", b->offset);
	DEBUG_MSG("bmark_store, offset string=%s, offset int=%d\n",strarr[3],b->offset);
	strarr[5] = g_strdup_printf("%d", b->len);
	DEBUG_MSG("bmark_store, stored size=%d\n",b->len);
	if (b->strarr == NULL) {
		bfwin->session->bmarks = g_list_append(bfwin->session->bmarks, strarr);
		DEBUG_MSG("added new (previously unstored) bookmark to session list, list length=%d\n",
				  g_list_length(bfwin->session->bmarks));
		b->strarr = strarr;
	}
}

/* when a users want to save the project, it's good to have updated bookmarks
so this function will update all arrays (strarr**)
 */
void bmark_store_all(Tbfwin *bfwin) {
	/* we loop over all filename iters, and only for the ones that are opened
	 we loop over the children (the ones that are not open cannot be changed) */
	GtkTreeIter fileit;
	gboolean cont;

	cont = gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &fileit,NULL);
	while (cont) {
		Tdocument *doc = NULL;
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &fileit, PTR_COLUMN,&doc, -1);
		if (doc) {
			/* the document is open, so the offsets could be changed, store all permanent */
			GtkTreeIter bmit;
			gboolean cont2 = gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &bmit,&fileit);
			DEBUG_MSG("bmark_store_all, storing bookmarks for %s\n",gtk_label_get_text(GTK_LABEL(doc->tab_menu)));
			while (cont2) {
				Tbmark *bmark;
				gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &bmit, PTR_COLUMN,&bmark, -1);
				if (!bmark->is_temp) {
					bmark_update_offset_from_textmark(bmark);
					bmark_store(bfwin, bmark);
				}
				cont2 = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &bmit);
			}
		} else {
			DEBUG_MSG("doc not set, so not open...\n");
		}
		cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &fileit);
	} /* cont */
}

/* removes the bookmark from the session, removed the b->strarr pointer and frees it */
static void bmark_unstore(Tbfwin * bfwin, Tbmark * b)
{
	if (bfwin->session->bmarks == NULL || b->strarr == NULL)
		return;
	DEBUG_MSG("bmark_remove, removing bookmark %p from sessionlist\n",b);
	bfwin->session->bmarks = g_list_remove(bfwin->session->bmarks, b->strarr);
	g_strfreev(b->strarr);
	b->strarr = NULL;
}

static Tbmark *get_bmark_at_iter(GtkTreeModel *model, GtkTreeIter *iter) {
	Tbmark *retval = NULL;
	gtk_tree_model_get(model,iter,PTR_COLUMN, &retval, -1);
	return retval;
}

/* get value from pointer column */
static Tbmark *get_current_bmark(Tbfwin * bfwin)
{
	if (bfwin->bmark) {
		GtkTreePath *path;
		GtkTreeViewColumn *col;
		gtk_tree_view_get_cursor(bfwin->bmark, &path, &col);
		if (path != NULL) {
			Tbmark *retval = NULL;
			if (gtk_tree_path_get_depth(path)==2) {
				GtkTreeIter iter;
				GtkTreeModel *model = gtk_tree_view_get_model(bfwin->bmark);
				gtk_tree_model_get_iter(model, &iter, path);
				retval = get_bmark_at_iter(model, &iter);
			} else {
				DEBUG_MSG("get_current_bmark, error, depth=%d\n",gtk_tree_path_get_depth(path));
			}
			gtk_tree_path_free(path);
			DEBUG_MSG("get_current_bmark, returning %p\n",retval);
			return retval;
		}
	}
	return NULL;
}

void bmark_add_rename_dialog(Tbfwin * bfwin, gchar * dialogtitle)
{
	GtkWidget *dlg, *name, *desc, *button, *table, *istemp;
	gint result;
	Tbmark *m = get_current_bmark(bfwin);
	if (!m) return;

	dlg =
		gtk_dialog_new_with_buttons(dialogtitle, GTK_WINDOW(bfwin->main_window), GTK_DIALOG_MODAL,
									GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
	button = gtk_button_new_from_stock(GTK_STOCK_OK);
	gtk_widget_set_can_default(button, TRUE);
	gtk_dialog_add_action_widget(GTK_DIALOG(dlg), button, GTK_RESPONSE_OK);

	table = gtk_table_new(2, 3, FALSE);
	gtk_table_set_col_spacings(GTK_TABLE(table), 12);
	gtk_table_set_row_spacings(GTK_TABLE(table), 6);
	gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dlg))),
			table, FALSE, FALSE, 12);

	name = entry_with_text(m->name, 200);
	gtk_entry_set_activates_default(GTK_ENTRY(name), TRUE);
	bf_mnemonic_label_tad_with_alignment(_("_Name:"), name, 0, 0.5, table, 0, 1, 0, 1);
	gtk_table_attach_defaults(GTK_TABLE(table), name, 1, 2, 0, 1);

	desc = entry_with_text(m->description, 200);
	gtk_entry_set_activates_default(GTK_ENTRY(desc), TRUE);
	bf_mnemonic_label_tad_with_alignment(_("_Description:"), desc, 0, 0.5, table, 0, 1, 1, 2);
	gtk_table_attach_defaults(GTK_TABLE(table), desc, 1, 2, 1, 2);
	istemp = checkbut_with_value(_("Temporary"), m->is_temp);
	gtk_table_attach_defaults(GTK_TABLE(table), istemp, 0, 2, 2, 3);

	gtk_window_set_default(GTK_WINDOW(dlg), button);

	gtk_widget_show_all(dlg);
	result = gtk_dialog_run(GTK_DIALOG(dlg));

	if (result == GTK_RESPONSE_OK) {
		gchar *tmpstr;
		g_free(m->name);
		m->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(name)));
		g_free(m->description);
		m->description = g_strdup(gtk_entry_get_text(GTK_ENTRY(desc)));
		m->is_temp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(istemp));
		tmpstr = bmark_showname(bfwin, m);
		gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &m->iter, NAME_COLUMN,
						   tmpstr,-1);
		g_free(tmpstr);
		if (m->is_temp) {
			if (m->strarr) {
				/* hmm previously this was not a temporary bookmark */
				bmark_unstore(bfwin, m);
			}
		} else {
			bmark_store(bfwin, m);
		}
	}
	gtk_widget_destroy(dlg);
}

static void bmark_activate(Tbfwin *bfwin, Tbmark *b, gboolean select_bmark) {
	GtkTextIter it;

	if (!b)
		return;

	if ( b->doc && b->mark ) {
		/* recalculate offset */
		gtk_text_buffer_get_iter_at_mark(b->doc->buffer,&it,b->mark);
		b->offset = gtk_text_iter_get_offset(&it);
	}
	DEBUG_MSG("bmark_activate, bmark at %p, filepath at %p\n",b, b->filepath);
	DEBUG_MSG("bmark_activate, calling doc_new_from_uri with goto_offset %d\n",b->offset);
	doc_new_from_uri(bfwin, b->filepath, NULL, FALSE, FALSE, -1, b->offset);
	/* remove selection */
	if ( b->doc ) {
		gtk_text_buffer_get_iter_at_mark(b->doc->buffer,&it,gtk_text_buffer_get_insert(b->doc->buffer));
		gtk_text_buffer_move_mark_by_name(b->doc->buffer, "selection_bound", &it);
		gtk_widget_grab_focus(b->doc->view);
	} else {
		gtk_widget_grab_focus(bfwin->current_document->view);
	}
	if (select_bmark) {
		GtkTreeIter fiter;
		gtk_tree_model_filter_convert_child_iter_to_iter(bfwin->bmarkfilter,&fiter,&b->iter);
		gtk_tree_selection_select_iter(gtk_tree_view_get_selection(bfwin->bmark), &fiter);
	}
	
}

/*static void bmark_goto_selected(Tbfwin *bfwin) {
	Tbmark *b = get_current_bmark(bfwin);
	if (b) {
		bmark_activate(bfwin, b, FALSE);
	}
}*/
/*
 * removes the bookmark from the treestore, and if it is the last remaining bookmark
 * for the document, it will remove the parent iter (the filename) from the treestore as well
 *
 * if the parent is not removed it will return TRUE
 * if the parent is removed, it will return FALSE
 */
static gboolean bmark_check_remove(Tbfwin *bfwin,Tbmark *b) {
	GtkTreeIter parent;
	GtkTextIter it;

	if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&parent,&b->iter)) {
		gint numchild = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &parent);
		DEBUG_MSG("bmark_check_remove, the parent of this bookmark has %d children\n", numchild);
		gtk_tree_store_remove(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &(b->iter));

		if (b->doc) {
		 	gtk_text_buffer_get_iter_at_mark(b->doc->buffer,&it,b->mark);
		}

		if (numchild == 1) {
			gpointer ptr;
			DEBUG_MSG("bmark_check_remove, we removed the last child, now remove the parent\n");
			gtk_tree_store_remove(BMARKDATA(bfwin->bmarkdata)->bookmarkstore,&parent);
			/* if the document is open, it should be removed from the hastable as well */
			ptr = g_hash_table_lookup(BMARKDATA(bfwin->bmarkdata)->bmarkfiles,b->filepath);
			if (ptr) {
				DEBUG_MSG("bmark_check_remove, removing iter from hashtable\n");
				g_hash_table_remove(BMARKDATA(bfwin->bmarkdata)->bmarkfiles,b->filepath);
				g_free(ptr);
				if (b->doc) b->doc->bmark_parent = NULL;
			}
			return FALSE;
		}
	}
  	DEBUG_MSG("bmark_check_remove, finished\n");
  	return TRUE;
}

/* *parent should be a valid GtkTreeIter pointing to a filename.  */
static void bmark_del_children_backend(Tbfwin *bfwin, GtkTreeIter *parent) {
	GtkTreeIter tmpiter;
	gboolean have_parent = TRUE;
	while (have_parent && gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, parent)) {
		Tbmark *b;
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN,&b,-1);
		if (b) {
			DEBUG_MSG("bmark_del_children_backend, found b=%p\n",b);
			have_parent = bmark_check_remove(bfwin,b);
			if (!b->is_temp)
				bmark_unstore(bfwin, b);
			bmark_free(b);
		} else {
			DEBUG_MSG("bmark_del_children_backend, iter without bookmark ??? LOOP WARNING!\n");
		}
	}
}

static void bmark_popup_menu_deldoc(Tbfwin *bfwin) {
	if (bfwin->bmark) {
		GtkTreePath *path;
		GtkTreeViewColumn *col;
		gtk_tree_view_get_cursor(bfwin->bmark, &path, &col);
		if (path != NULL) {
			gchar *name;
			gchar *pstr;
			const gchar *buttons[] = { GTK_STOCK_NO, GTK_STOCK_YES, NULL };
			GtkTreeIter iter, realiter;
			GtkTreeModel *model = gtk_tree_view_get_model(bfwin->bmark);
			gint depth, retval;
			depth = gtk_tree_path_get_depth(path);
			if (depth == 2) {
				/* go up to parent */
				gtk_tree_path_up(path);
			}
			gtk_tree_model_get_iter(model, &iter, path);
				/* iter is now an iter in the filter model, not in the real backend model !!!! */
			gtk_tree_path_free(path);
			gtk_tree_model_get(model, &iter, NAME_COLUMN,&name, -1);

			pstr = g_strdup_printf(_("Do you really want to delete all bookmarks for %s?"), name);
			retval = message_dialog_new_multi(bfwin->main_window,
														 GTK_MESSAGE_QUESTION,
														 buttons,
														 _("Delete bookmarks?"),
														 pstr);
			g_free(pstr);
			if (retval == 0)
				return;
			gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model),&realiter,&iter);
			bmark_del_children_backend(bfwin, &realiter);
		}
	}
	gtk_widget_grab_focus(bfwin->current_document->view);
}

static void bmark_popup_menu_del(Tbfwin *bfwin) {
	Tbmark *b;
	gint retval;
	gchar *pstr;
	const gchar *buttons[] = { GTK_STOCK_NO, GTK_STOCK_YES, NULL };

	b = get_current_bmark(bfwin);
	if (!b)
		return;
	/* check if it is temp mark */
	if (b->is_temp) {
		bmark_check_remove(bfwin,b); /* check  if we should remove a filename too */
		bmark_free(b);
	} else {
		pstr = g_strdup_printf(_("Do you really want to delete %s?"), b->name);
		retval = message_dialog_new_multi(bfwin->main_window,
													 GTK_MESSAGE_QUESTION,
													 buttons,
													 _("Delete permanent bookmark."),
													 pstr);
		g_free(pstr);
		if (retval == 0)
			return;
		bmark_check_remove(bfwin,b); /* check  if we should remove a filename too */
		bmark_unstore(bfwin, b);
		bmark_free(b);
	}
	gtk_widget_grab_focus(bfwin->current_document->view);
}

static void bmark_rpopup_action_lcb(gpointer data, guint action, GtkWidget *widget) {
	Tbfwin *bfwin = BFWIN(data);
	switch (action) {
		/*case 1:
			bmark_goto_selected(bfwin);
		break;*/
		case 2: {
			Tbmark *m = get_current_bmark(bfwin);
			if (!m) return;
			bmark_add_rename_dialog(bfwin, _("Edit bookmark"));
		} break;
		case 3:
			bmark_popup_menu_del(bfwin);
		break;
		case 10:
			bmark_popup_menu_deldoc(bfwin);
		break;
		case 11:
			bmark_del_all(bfwin,TRUE);
		break;
		case 20:
			main_v->globses.bookmarks_default_store = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
		break;
		case 30:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_filename_mode = BM_FMODE_FILE;
				bmark_update_treestore_name(bfwin);
			}
		break;
		case 31:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_filename_mode = BM_FMODE_PATH;
				bmark_update_treestore_name(bfwin);
			}
		break;
		case 32:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_filename_mode = BM_FMODE_FULL;
				bmark_update_treestore_name(bfwin);
			}
		break;
		case 40:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_show_mode = BM_SMODE_BOTH;
				bmark_update_treestore_name(bfwin);
			}
		break;
		case 41:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_show_mode = BM_SMODE_CONTENT;
				bmark_update_treestore_name(bfwin);
			}
		break;
		case 42:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_show_mode = BM_SMODE_NAME;
				bmark_update_treestore_name(bfwin);
			}
		break;
		default:
			g_print("bmark_rpopup_action_lcb: action %d not implemented yet\n",action);
		break;
	}
}

static GtkItemFactoryEntry bmark_rpopup_menu_entries[] = {
	/*{ N_("/Goto"),		NULL,	bmark_rpopup_action_lcb,		1,	"<Item>" },*/
	{ N_("/Edit"),NULL,	bmark_rpopup_action_lcb,	2,	"<Item>" },
	{ N_("/Delete"),NULL,	bmark_rpopup_action_lcb,	3,	"<Item>" },
	{ "/sep1",						NULL,	NULL,									0,	"<Separator>" },
	{ N_("/Delete All in document"),NULL,	bmark_rpopup_action_lcb,	10,	"<Item>" },
	{ N_("/Delete All"),NULL,	bmark_rpopup_action_lcb,	11,	"<Item>" },
	{ "/sep2",						NULL,	NULL,									0,	"<Separator>" },
	{ N_("/Permanent by default"),	NULL,	bmark_rpopup_action_lcb,	20,	"<ToggleItem>" },
	{ "/sep3",						NULL,	NULL,									0,	"<Separator>" },
	{ N_("/Show file"),	NULL,	NULL,	0,	"<Branch>" },
	{ N_("/Show file/By name"),	NULL,	bmark_rpopup_action_lcb,	30,	"<RadioItem>" },
	{ N_("/Show file/By full path"),	NULL,	bmark_rpopup_action_lcb,	31,	"/Show file/By name" },
	{ N_("/Show file/By full uri"),	NULL,	bmark_rpopup_action_lcb,	32,	"/Show file/By name" },
	{ N_("/Show bookmark"),	NULL,	NULL,	0,	"<Branch>" },
	{ N_("/Show bookmark/Name & Content"),	NULL,	bmark_rpopup_action_lcb,	40,	"<RadioItem>" },
	{ N_("/Show bookmark/Content"),	NULL,	bmark_rpopup_action_lcb,	41,	"/Show bookmark/Name & Content" },
	{ N_("/Show bookmark/Name"),	NULL,	bmark_rpopup_action_lcb,	42,	"/Show bookmark/Name & Content" }
};

static GtkWidget *bmark_popup_menu(Tbfwin * bfwin, gboolean show_bmark_specific, gboolean show_file_specific) {
	GtkWidget *menu;
	GtkItemFactory *menumaker;

	menumaker = gtk_item_factory_new(GTK_TYPE_MENU, "<Bookmarks>", NULL);
#ifdef ENABLE_NLS
	gtk_item_factory_set_translate_func(menumaker,menu_translate,"<Bookmarks>",NULL);
#endif
	gtk_item_factory_create_items(menumaker, sizeof(bmark_rpopup_menu_entries)/sizeof(GtkItemFactoryEntry), bmark_rpopup_menu_entries, bfwin);
	menu = gtk_item_factory_get_widget(menumaker, "<Bookmarks>");

	if (!show_bmark_specific) {
		/*gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Goto"), FALSE);*/
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Edit"), FALSE);
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Delete"), FALSE);
	}
	if (!show_file_specific) {
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Delete All in document"), FALSE);
	}
	setup_toggle_item(menumaker, "/Permanent by default", main_v->globses.bookmarks_default_store);
	setup_toggle_item(menumaker, "/Show file/By name", bfwin->session->bookmarks_filename_mode == BM_FMODE_FILE);
	setup_toggle_item(menumaker, "/Show file/By full path", bfwin->session->bookmarks_filename_mode == BM_FMODE_PATH);
	setup_toggle_item(menumaker, "/Show file/By full uri", bfwin->session->bookmarks_filename_mode != BM_FMODE_FILE && bfwin->session->bookmarks_filename_mode != BM_FMODE_PATH);
	setup_toggle_item(menumaker, "/Show bookmark/Name & Content", bfwin->session->bookmarks_show_mode == BM_SMODE_BOTH);
	setup_toggle_item(menumaker, "/Show bookmark/Content", bfwin->session->bookmarks_show_mode == BM_SMODE_CONTENT);
	setup_toggle_item(menumaker, "/Show bookmark/Name", bfwin->session->bookmarks_show_mode != BM_SMODE_BOTH && bfwin->session->bookmarks_show_mode != BM_SMODE_CONTENT);
	gtk_widget_show_all(menu);
	g_signal_connect_after(G_OBJECT(menu), "destroy", G_CALLBACK(destroy_disposable_menu_cb), menu);
	return menu;
}

static void bmark_row_activated(GtkTreeView * tree, GtkTreePath * path,
									GtkTreeViewColumn * column, Tbfwin *bfwin) {
	GtkTreeIter iter;
	gtk_tree_model_get_iter(GTK_TREE_MODEL(bfwin->bmarkfilter), &iter, path);
	bmark_activate(bfwin, get_bmark_at_iter(GTK_TREE_MODEL(bfwin->bmarkfilter), &iter), FALSE);	
}

/* mouse click */
static gboolean bmark_event_mouseclick(GtkWidget * widget, GdkEventButton * event, Tbfwin * bfwin) {
	GtkTreePath *path;
	gboolean show_bmark_specific = FALSE, show_file_specific = FALSE;
	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(bfwin->bmark), event->x, event->y, &path, NULL, NULL, NULL)) {
		if (path) {
			gint depth = gtk_tree_path_get_depth(path);
			
			if (depth==2) {
				show_bmark_specific = TRUE;
				show_file_specific = TRUE;
				if (event->button == 1) {
					GtkTreeIter iter;
					gtk_tree_model_get_iter(GTK_TREE_MODEL(bfwin->bmarkfilter), &iter, path);
					bmark_activate(bfwin, get_bmark_at_iter(GTK_TREE_MODEL(bfwin->bmarkfilter), &iter), FALSE);
				}
			} else if (depth == 1) {
				show_file_specific = TRUE;
			}
			gtk_tree_path_free(path);
		}
	}
	if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {	/* right mouse click */
		gtk_menu_popup(GTK_MENU(bmark_popup_menu(bfwin, show_bmark_specific, show_file_specific)), NULL, NULL, NULL, NULL,
				   event->button, event->time);
	}
	return FALSE;
}

/*static void bmark_selection_changed_lcb(GtkTreeSelection *treeselection,Tbfwin * bfwin) {
	/ * this is not the best way to activate bookmarks. according to the gtk documentation:
	Emitted whenever the selection has (possibly) changed. Please note that this signal is 
	mostly a hint. It may only be emitted once when a range of rows are selected, and it 
	may occasionally be emitted when nothing has happened.
	
	THUS: we should better use the mouse click event to find the correct bookmark to 
	activate.
	* /
	DEBUG_MSG("bmark_selection_changed_lcb, started\n");
	bmark_goto_selected(bfwin);
}*/

void bookmark_menu_cb(Tbfwin *bfwin,guint action,GtkWidget *widget) {
	GtkTreeModel *model = GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore);
	GtkTreeIter iter;
	Tdocument *doc = bfwin->current_document;
	GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(bfwin->bmark));
	
	if (!doc || !doc->bmark_parent)
		return;
	
	if (action == 1) /* first */ {
			if (gtk_tree_model_iter_children(model,&iter,doc->bmark_parent))
				bmark_activate(bfwin, get_bmark_at_iter(model, &iter), TRUE);
	} else if (action == 4) /* last */ {
			gint num = gtk_tree_model_iter_n_children(model,doc->bmark_parent);
			if (gtk_tree_model_iter_nth_child(model,&iter,doc->bmark_parent,num-1))
				bmark_activate(bfwin, get_bmark_at_iter(model, &iter), TRUE);
	} else {
		if (gtk_tree_selection_get_selected(selection,&model,&iter)) {
			if (action == 2) 	/* previous */ {
				GtkTreePath *path= gtk_tree_model_get_path(model,&iter);
				gtk_tree_path_prev(path);
				if (gtk_tree_model_get_iter(model,&iter,path))
					bmark_activate(bfwin, get_bmark_at_iter(model, &iter), TRUE);
				gtk_tree_path_free(path);
			} else if (action == 3)/* next */ {
				if (gtk_tree_model_iter_next(model,&iter))
					bmark_activate(bfwin, get_bmark_at_iter(model, &iter), TRUE);
			}
		}
	}
}

static void bmark_first_lcb(GtkWidget *widget, Tbfwin *bfwin) {
	bookmark_menu_cb(bfwin,1,widget);
}
static void bmark_previous_lcb(GtkWidget *widget, Tbfwin *bfwin) {
	bookmark_menu_cb(bfwin,2,widget);
}
static void bmark_next_lcb(GtkWidget *widget, Tbfwin *bfwin) {
	bookmark_menu_cb(bfwin,3,widget);
}
static void bmark_last_lcb(GtkWidget *widget, Tbfwin *bfwin) {
	bookmark_menu_cb(bfwin,4,widget);
}

static gboolean bmark_search_filter_func(GtkTreeModel *model, GtkTreeIter  *iter, gpointer data) {
	GtkTreeIter piter;
	Tbfwin *bfwin = data;
	if (bfwin->bmark_search_prefix == NULL || bfwin->bmark_search_prefix[0]=='\0')
		return TRUE;

	/* the parents have a Tdocument stored in PTR_COLUMN, the bookmarks themselves have a Tbmark */
	if (gtk_tree_model_iter_parent(model, &piter, iter)) {
		Tbmark *bmark;
		gtk_tree_model_get(model, iter, PTR_COLUMN,&bmark, -1);
		if (bmark) {
			switch(/*bfwin->session->bmark_search_mode*/BM_SEARCH_BOTH) {
			case BM_SEARCH_NAME:
				return (bmark->name && strstr(bmark->name, bfwin->bmark_search_prefix));
			break;
			case BM_SEARCH_CONTENT:
				return (bmark->text && strstr(bmark->text, bfwin->bmark_search_prefix));
			break;
			case BM_SEARCH_BOTH:
				return ((bmark->text && strstr(bmark->text, bfwin->bmark_search_prefix)) || (bmark->name && g_str_has_prefix(bmark->name, bfwin->bmark_search_prefix)));
			break;
			}
		}
		return FALSE;
	}
	return TRUE;
}

static void bmark_search_changed(GtkEditable *editable,gpointer user_data) {
	Tbfwin *bfwin=user_data;
	/* call refilter on the bmarkfilter */
	g_free(bfwin->bmark_search_prefix);
	bfwin->bmark_search_prefix = gtk_editable_get_chars(editable, 0, -1);
	gtk_tree_model_filter_refilter(bfwin->bmarkfilter);
}

static void bmark_search_mode_changed(gpointer data, guint action, GtkWidget *widget) {
	Tbfwin *bfwin = BFWIN(data);
	bfwin->session->bmarksearchmode = action;
}

static void bmark_search_rpopup_menu(Tbfwin *bfwin, GdkEventButton * bevent) {
	GtkItemFactory *menumaker;
	GtkMenu *menu;

	static GtkItemFactoryEntry bmarksearch_rpopup_menu_entries[] = {
		{ N_("/Both Name & Content"),	NULL,	bmark_search_mode_changed,	BM_SEARCH_BOTH, "<RadioItem>" },
		{ N_("/Content"),	NULL,	bmark_search_mode_changed,	BM_SEARCH_CONTENT, "/Both Name & Content" },
		{ N_("/Name"),	NULL,	bmark_search_mode_changed,	BM_SEARCH_NAME, "/Both Name & Content" }
	};

	menumaker = gtk_item_factory_new(GTK_TYPE_MENU, "<Bookmarksearch>", NULL);
#ifdef ENABLE_NLS
	gtk_item_factory_set_translate_func(menumaker,menu_translate,"<Bookmarksearch>",NULL);
#endif
	gtk_item_factory_create_items(menumaker, sizeof(bmarksearch_rpopup_menu_entries)/sizeof(GtkItemFactoryEntry), bmarksearch_rpopup_menu_entries, bfwin);
	menu = (GtkMenu *) gtk_item_factory_get_widget(menumaker, "<Bookmarksearch>");
	setup_toggle_item(menumaker, "/Both Name & Content", bfwin->session->bmarksearchmode != BM_SEARCH_CONTENT && bfwin->session->bmarksearchmode != BM_SEARCH_NAME);
	setup_toggle_item(menumaker, "/Content", bfwin->session->bmarksearchmode == BM_SEARCH_CONTENT);
	setup_toggle_item(menumaker, "/Name", bfwin->session->bmarksearchmode == BM_SEARCH_NAME);
	gtk_widget_show_all(GTK_WIDGET(menu));
	gtk_menu_popup(GTK_MENU(menu), NULL, NULL, NULL, NULL,bevent->button, bevent->time);
	g_signal_connect_after(G_OBJECT(menu), "destroy", G_CALLBACK(destroy_disposable_menu_cb), menu);
}

#if GTK_CHECK_VERSION(2,16,0)
static void bmark_search_icon_press(GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEvent *event, gpointer user_data) {
	bmark_search_rpopup_menu(user_data, (GdkEventButton*)event);
}
#else
static gboolean bmark_search_button_press(GtkWidget *widget, GdkEventButton *event, gpointer user_data) {
	if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {
		bmark_search_rpopup_menu(user_data, event);
		return TRUE;
	}
	return FALSE;
}
#endif

/* Initialize bookmarks gui for window */
GtkWidget *bmark_gui(Tbfwin * bfwin)
{
	GtkWidget *vbox, *hbox, *scroll, *entry;
	GtkToolItem *but;
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	DEBUG_MSG("bmark_gui, building gui for bfwin=%p\n",bfwin);
	/* Tree Store is in BMARKDATA(bfwin->bmarkdata)->bookmarkstore
	   Tree View is in bfwin->bmark
	 */
	vbox = gtk_vbox_new(FALSE, 1);
	entry = gtk_entry_new();
#if GTK_CHECK_VERSION(2,16,0)
	gtk_entry_set_icon_from_stock(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY, GTK_STOCK_FIND);
	gtk_entry_set_icon_activatable(GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY, TRUE);
	g_signal_connect(G_OBJECT(entry), "icon-press", G_CALLBACK(bmark_search_icon_press), bfwin);
#else
	g_signal_connect(G_OBJECT(entry), "button-press-event", G_CALLBACK(bmark_search_button_press), bfwin);
#endif
	g_signal_connect(G_OBJECT(entry), "changed", G_CALLBACK(bmark_search_changed), bfwin);
	gtk_box_pack_start(GTK_BOX(vbox), entry, FALSE, TRUE, 0);
	hbox = gtk_toolbar_new();
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(hbox),GTK_ICON_SIZE_MENU);
	gtk_toolbar_set_style(GTK_TOOLBAR(hbox),GTK_TOOLBAR_ICONS);

	but = gtk_tool_button_new(gtk_image_new_from_stock(GTK_STOCK_GOTO_TOP,GTK_ICON_SIZE_MENU),"");
	g_signal_connect(G_OBJECT(but),"clicked",G_CALLBACK(bmark_first_lcb),bfwin);
	gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(but),_("First bookmark"));
	gtk_toolbar_insert(GTK_TOOLBAR(hbox),but,-1);
	but = gtk_tool_button_new(gtk_image_new_from_stock(GTK_STOCK_GO_UP,GTK_ICON_SIZE_MENU),"");
	g_signal_connect(G_OBJECT(but),"clicked",G_CALLBACK(bmark_previous_lcb),bfwin);
	gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(but),_("Previous bookmark"));
	gtk_toolbar_insert(GTK_TOOLBAR(hbox),but,-1);
	but = gtk_tool_button_new(gtk_image_new_from_stock(GTK_STOCK_GO_DOWN,GTK_ICON_SIZE_MENU),"");
	g_signal_connect(G_OBJECT(but),"clicked",G_CALLBACK(bmark_next_lcb),bfwin);
	gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(but),_("Next bookmark"));
	gtk_toolbar_insert(GTK_TOOLBAR(hbox),but,-1);
	but = gtk_tool_button_new(gtk_image_new_from_stock(GTK_STOCK_GOTO_BOTTOM,GTK_ICON_SIZE_MENU),"");
	g_signal_connect(G_OBJECT(but),"clicked",G_CALLBACK(bmark_last_lcb),bfwin);
	gtk_tool_item_set_tooltip_text(GTK_TOOL_ITEM(but),_("Last bookmark"));
	gtk_toolbar_insert(GTK_TOOLBAR(hbox),but,-1);

	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, TRUE, 0);

	bfwin->bmarkfilter = (GtkTreeModelFilter *)gtk_tree_model_filter_new(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),NULL);
	gtk_tree_model_filter_set_visible_func(bfwin->bmarkfilter, bmark_search_filter_func, bfwin, NULL);
	bfwin->bmark = (GtkTreeView *)gtk_tree_view_new_with_model(GTK_TREE_MODEL(bfwin->bmarkfilter));
	g_object_unref(bfwin->bmarkfilter);
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("", cell, "text", NAME_COLUMN, NULL);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(bfwin->bmark), column);
	gtk_widget_show_all(GTK_WIDGET(bfwin->bmark));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(bfwin->bmark), FALSE);
	/*gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(bfwin->bmark), TRUE);*/
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), GTK_WIDGET(bfwin->bmark));
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(bfwin->bmark), "button-press-event",
					 G_CALLBACK(bmark_event_mouseclick), bfwin);
	g_signal_connect(G_OBJECT(bfwin->bmark), "row-activated", G_CALLBACK(bmark_row_activated),bfwin);
	gtk_tree_view_expand_all(bfwin->bmark);
	/*{
		GtkTreeSelection* selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(bfwin->bmark));
		gtk_tree_selection_set_mode(selection,GTK_SELECTION_BROWSE);
		g_signal_connect(G_OBJECT(selection), "changed",G_CALLBACK(bmark_selection_changed_lcb), bfwin);
	}*/

	return vbox;
}

/**
 * bmark_get_iter_at_tree_position:
 *
 * determine bookmark's location in the tree and  insert - result GtkTreeIter is stored in m->iter
 */
static void bmark_get_iter_at_tree_position(Tbfwin * bfwin, Tbmark * m) {
	GtkTreeIter *parent;
	gpointer ptr;
	DEBUG_MSG("bmark_get_iter_at_tree_position, started\n");
	ptr = g_hash_table_lookup(BMARKDATA(bfwin->bmarkdata)->bmarkfiles, m->filepath);
	DEBUG_MSG("bmark_get_iter_at_tree_position, found %p in hashtable %p\n",ptr,BMARKDATA(bfwin->bmarkdata)->bmarkfiles);
	if (ptr == NULL) {			/* closed document or bookmarks never set */
		gchar *title;
		parent = g_new0(GtkTreeIter, 1);
		/* we should sort the document names in the treestore */
		title = bmark_filename(bfwin,m->filepath);
		DEBUG_MSG("insert parent with name %s and doc=%p in treestore %p\n",title, m->doc, BMARKDATA(bfwin->bmarkdata)->bookmarkstore);
		gtk_tree_store_insert_with_values(BMARKDATA(bfwin->bmarkdata)->bookmarkstore,parent,NULL,0, NAME_COLUMN, title, PTR_COLUMN, m->doc, -1);
/*		gtk_tree_store_append(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, parent, NULL);
		gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, parent, NAME_COLUMN, title, PTR_COLUMN, m->doc, -1);*/
		g_free(title);
		if (m->doc != NULL) {
			m->doc->bmark_parent = parent;
		}
		DEBUG_MSG("bmark_get_iter_at_tree_position, appending parent %p in hashtable %p\n",parent,BMARKDATA(bfwin->bmarkdata)->bmarkfiles);
		/* the hash table frees the key, but not the value, on destroy */
		g_object_ref(m->filepath);
		g_hash_table_insert(BMARKDATA(bfwin->bmarkdata)->bmarkfiles, m->filepath, parent);
	} else
		parent = (GtkTreeIter *) ptr;

	{
		/*Tbmark *bef = bmark_find_bookmark_before_offset(bfwin, m->offset, parent);
		if (bef == NULL) {*/
			gtk_tree_store_prepend(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &m->iter, parent);
			return;
/*		} else {
			gtk_tree_store_insert_after(GTK_TREE_STORE(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&m->iter,parent,&bef->iter);
			return;
		}*/
	}
}

static gint bmark_sort_func(GtkTreeModel *model,GtkTreeIter *a,GtkTreeIter *b,gpointer user_data) {
	GtkTreeIter tmp;
	if (gtk_tree_model_iter_parent(model,&tmp,a)==FALSE) {
		gint retval; 
		gchar *name_a, *name_b;
		gtk_tree_model_get(model, a, NAME_COLUMN,&name_a, -1);
		gtk_tree_model_get(model, b, NAME_COLUMN,&name_b, -1);
		retval = g_strcmp0(name_a,name_b);
		g_free(name_a);
		g_free(name_b);
		return retval;
	} else {
		Tbmark *bmark_a, *bmark_b;
		gtk_tree_model_get(model, a, PTR_COLUMN,&bmark_a, -1);
		gtk_tree_model_get(model, b, PTR_COLUMN,&bmark_b, -1);
		if (bmark_a && bmark_b) {
			return  bmark_a->offset - bmark_b->offset;
		} else {
			return (gint)(bmark_a-bmark_b);
		}
	}
}

/*
 * this function is used to create the global main_v->bookmarkstore
 * as well as the project bookmarkstores
 */
gpointer bookmark_data_new(void) {
	Tbmarkdata *bmd;
	bmd = g_new0(Tbmarkdata, 1);
	bmd->bookmarkstore = gtk_tree_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_POINTER);
	gtk_tree_sortable_set_default_sort_func(GTK_TREE_SORTABLE(bmd->bookmarkstore),bmark_sort_func,bmd,NULL);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(bmd->bookmarkstore),GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,GTK_SORT_ASCENDING);
	/* BUG: shouldn't we free the data in the hash table when we close a project? */
	bmd->bmarkfiles = g_hash_table_new_full(g_file_hash, (GEqualFunc)g_file_equal, NULL, NULL);
	DEBUG_MSG("bookmark_data_new, created bookmarkstore at %p\n", bmd->bookmarkstore);
	return bmd;
}
/* used to clean up the project bookmarkdata */
void bookmark_data_cleanup(gpointer *data) {
	Tbmarkdata *bmd = BMARKDATA(data);
	GtkTreeIter fileit;
	gboolean cont;
	/*walk the treestore and free all Tbmark's in the pointer columns */
	DEBUG_MSG("bookmark_data_cleanup bmarkdata %p\n",bmd);
	cont = gtk_tree_model_iter_children(GTK_TREE_MODEL(bmd->bookmarkstore), &fileit,NULL);
	while (cont) { /* walk the toplevel */
		GtkTreeIter bmit;
		gboolean cont2 = gtk_tree_model_iter_children(GTK_TREE_MODEL(bmd->bookmarkstore), &bmit,&fileit);
		while (cont2) {
			Tbmark *bmark;
			gtk_tree_model_get(GTK_TREE_MODEL(bmd->bookmarkstore), &bmit, PTR_COLUMN,&bmark, -1);
			bmark->strarr=NULL;
			if (bmark->doc)
				bmark->doc->bmark_parent = NULL;
			bmark_free(bmark);
			cont2 = gtk_tree_model_iter_next(GTK_TREE_MODEL(bmd->bookmarkstore), &bmit);
		}
		cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(bmd->bookmarkstore), &fileit);
	}
	g_object_unref(bmd->bookmarkstore);
	g_hash_table_destroy(bmd->bmarkfiles);
	g_free(bmd);
}

/* this function will load the bookmarks
 * from bfwin->session->bmarks and parse
 * them into treestore BMARKDATA(bfwin->bmarkdata)->bookmarkstore
 *
 * it is called from bluefish.c for the first window (global bookmarks)
 * and from project.c during opening a project
 *
 * this function should ALSO check all douments that are
 * opened (bfwin->documentlist) if they have bookmarks !!
 */
void bmark_reload(Tbfwin * bfwin) {
	GFile *cacheduri=NULL;
	GList *tmplist;

	DEBUG_MSG("bmark_reload for bfwin %p\n",bfwin);
	bmark_store_all(bfwin);

	tmplist = g_list_first(bfwin->session->bmarks);
	while (tmplist) {
		gchar **items = (gchar **) tmplist->data;
		if (items && count_array(items) == 6) {
			gchar *ptr;
			Tbmark *b;
			b = g_slice_new0(Tbmark);
			/*g_print("bmark_reload, alloc bmark %p\n",b);*/
			b->name = g_strdup(items[0]);
			b->description = g_strdup(items[1]);
			/* convert old (Bf 1.0) bookmarks to new bookmarks with uri's */
			if (strchr(items[2], ':')==NULL) {
				gchar *tmp;
				tmp = g_strconcat("file://", items[2], NULL);
				b->filepath = g_file_parse_name(tmp);
				g_free(tmp);
			} else {
				b->filepath = g_file_parse_name(items[2]);
			}
			/* because the bookmark list is usually sorted, we try to cache the uri's and consume less memory */
			if (cacheduri && (cacheduri == b->filepath || g_file_equal(cacheduri,b->filepath))) {
				g_object_unref(b->filepath);
				g_object_ref(cacheduri);
				b->filepath = cacheduri;
			} else {
				cacheduri = b->filepath;
			}

			b->offset = atoi(items[3]);
			b->text = g_strdup(items[4]);
			b->len = atoi(items[5]);
			b->strarr = items;
			DEBUG_MSG("bmark_reload, loaded bookmark %p for uri=%pat offset %d with text %s\n",b,b->filepath,b->offset,b->text);
			bmark_get_iter_at_tree_position(bfwin, b);
			ptr = bmark_showname(bfwin, b);
			gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &(b->iter), NAME_COLUMN, ptr, PTR_COLUMN, b,-1);
			g_free(ptr);
		}
		tmplist = g_list_next(tmplist);
	}
#ifdef WALKTREE
	/* walk over all bookmarks and print them to stdout */
	{
		GtkTreeIter iter;
		gboolean cont;
		cont = gtk_tree_model_get_iter_first(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&iter);
		while (cont) {
			gchar *name;
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&iter, NAME_COLUMN, &name, -1);
			g_print("walk bookmarks, got name %s\n",name);
			g_free(name);
			cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&iter);
		}
	}
#endif
}

/*
 * this function will simply call
 * gtk_tree_view_set_model() to connect the treeview
 * to the new treestore, used in unloading and
 * loading of projects
 */
void bmark_set_store(Tbfwin * bfwin) {
	DEBUG_MSG("bmark_set_store set store %p for bfwin %p\n",BMARKDATA(bfwin->bmarkdata)->bookmarkstore,bfwin);
	if (BMARKDATA(bfwin->bmarkdata)->bookmarkstore && bfwin->bmark) {
		bfwin->bmarkfilter = (GtkTreeModelFilter *)gtk_tree_model_filter_new(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), NULL);
		gtk_tree_model_filter_set_visible_func(bfwin->bmarkfilter, bmark_search_filter_func, bfwin, NULL);
		gtk_tree_view_set_model(bfwin->bmark, GTK_TREE_MODEL(bfwin->bmarkfilter));
		g_object_unref(bfwin->bmarkfilter);
	}
}

void bmark_clean_for_doc(Tdocument * doc) {
	GtkTreeIter tmpiter;
	gboolean cont;

	if (doc->bmark_parent == NULL)
		return;

	if (!doc->uri)
		return;
	DEBUG_MSG("bmark_clean_for_doc, doc=%p, bfwin=%p, bmarkdata=%p, getting children for parent_iter=%p\n",doc, doc->bfwin,BFWIN(doc->bfwin)->bmarkdata, doc->bmark_parent);
	cont =
		gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter,
									 doc->bmark_parent);
	while (cont) {
		Tbmark *b = NULL;
		DEBUG_MSG("bmark_clean_for_doc, getting bookmark for first child\n");
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN,
						   &b, -1);
		if (b) {
			bmark_update_offset_from_textmark(b);
			DEBUG_MSG("bmark_clean_for_doc, bookmark=%p, new offset=%d, now deleting GtkTextMark from TextBuffer\n",b,b->offset);
			gtk_text_buffer_delete_mark(doc->buffer, b->mark);
			if (doc->fileinfo) b->len = gtk_text_buffer_get_char_count(doc->buffer);
			b->mark = NULL;
			b->doc = NULL;
			if (!b->is_temp) {
				bmark_store(doc->bfwin, b);
			}
		}
		cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter);
	}							/* cont */
	/* now unset the Tdocument* in the second column */
	DEBUG_MSG("bmark_clean_for_doc, unsetting and freeing parent_iter %p for doc %p\n",doc->bmark_parent,doc);
	gtk_tree_store_set(GTK_TREE_STORE(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), doc->bmark_parent, PTR_COLUMN, NULL, -1);
	doc->bmark_parent = NULL;
}

static gboolean bookmark_reposition(Tbmark *mark, gint offset) {
	gint doclen = gtk_text_buffer_get_char_count(mark->doc->buffer);
	gint bandwidth = offset>0?2*offset:-2*offset;
	if (bandwidth < 10*strlen(mark->text)) bandwidth = 10*strlen(mark->text);
	/* search for the bookmark near the old positions */

	while (TRUE) {
		GtkTextIter its,ite,/*starr,end*/ itrs,itre;/* resultstart, resultend */
		gint startpos;
		startpos = mark->offset+offset-bandwidth/2;
		if (startpos <0) startpos=0;
		DEBUG_MSG("bookmark_reposition, searching from %d to %d for %s\n",startpos,startpos+bandwidth,mark->text);
		gtk_text_buffer_get_iter_at_offset(mark->doc->buffer,&its,startpos);
		gtk_text_buffer_get_iter_at_offset(mark->doc->buffer,&ite,startpos+bandwidth);
		if (gtk_text_iter_forward_search(&its,mark->text,GTK_TEXT_SEARCH_TEXT_ONLY,&itrs,&itre,&ite)) {
			/* found !!!!!!! */
			DEBUG_MSG("bookmark_reposition, found result! resposition from %d to %d\n",mark->offset,gtk_text_iter_get_offset(&itrs));
			mark->offset = gtk_text_iter_get_offset(&itrs);
			return TRUE;
		}
		if (bandwidth > doclen) return FALSE;
		bandwidth *= 2;
	}
}

static gboolean bookmark_needs_repositioning(Tbmark *mark, GtkTextIter *it) {
	GtkTextIter it2;
	gboolean retval;
	gchar *tmpstr;
	/* check the content at the bookmark */
	gtk_text_buffer_get_iter_at_offset(mark->doc->buffer, &it2, mark->offset + strlen(mark->text));
	tmpstr = gtk_text_buffer_get_text(mark->doc->buffer, it, &it2, FALSE);
	DEBUG_MSG("original offset %d, compare %s and %s\n",mark->offset, tmpstr, mark->text);
	retval = (strcmp(tmpstr, mark->text)!=0);
	DEBUG_MSG("bookmark_needs_repositioning, reposition=%d,text='%s', tmpstr='%s'\n",retval,mark->text, tmpstr);
	g_free(tmpstr);
	return retval;
}

/*
 * this function will check is this document needs any bookmarks, and set the
 * doc->bmark_parent if needed
 *
 * if there are bookmarks, the bookmark GtkTextMark's will be inserted
 *
 * if check_position is TRUE, the content of the bookmark will be checked, and if
 * changed, the offset will be re-positioned
 *
 */
void bmark_set_for_doc(Tdocument * doc, gboolean check_positions) {
	if (!doc->uri) {
		DEBUG_MSG("bmark_set_for_doc, document %p does not have a filename, returning\n", doc);
		return;
	}

	DEBUG_MSG("bmark_set_for_doc, doc=%p, filename=%s\n",doc,gtk_label_get_text(GTK_LABEL(doc->tab_menu)));
/*	if (!BFWIN(doc->bfwin)->bmark) {
		DEBUG_MSG("bmark_set_for_doc, no leftpanel, not implemented yet!!\n");
		return;
	}*/
	if (doc->bmark_parent) {
		DEBUG_MSG("this document (%p) already has a bmark_parent (%p) why is this function called?\n",doc,doc->bmark_parent);
		return;
	}

	doc->bmark_parent = g_hash_table_lookup(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bmarkfiles, doc->uri);
	if (doc->bmark_parent) {
		gboolean cont2;
		GtkTreeIter child;
		/*g_print("bmark_set_for_doc, we found a bookmark for document %s at offset=%d!\n",gtk_label_get_text(GTK_LABEL(doc->tab_menu)),mark->offset);*/
		gtk_tree_store_set(GTK_TREE_STORE(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), doc->bmark_parent, PTR_COLUMN, doc, -1);

		cont2 = gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &child, doc->bmark_parent);
		while (cont2) { /* loop the bookmarks for this document  */
			Tbmark *mark = NULL;
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &child,
							   PTR_COLUMN, &mark, -1);
			if (mark) {
				GtkTextIter it;
				mark->doc = doc;
				DEBUG_MSG("bmark_set_for_doc, next bookmark at offset=%d!\n",mark->offset);
				gtk_text_buffer_get_iter_at_offset(doc->buffer, &it, mark->offset);
				if (check_positions && bookmark_needs_repositioning(mark, &it)) { /* repositioning required ! */
					if (bookmark_reposition(mark, gtk_text_buffer_get_char_count(doc->buffer) - mark->len)) {
						gtk_text_buffer_get_iter_at_offset(doc->buffer, &it, mark->offset);
					} else {
						/* BUG: bookmark not restored, what to do now ???? - just put it where it was ??  */
					}
				}
				mark->mark = gtk_text_buffer_create_mark(doc->buffer, NULL, &it, TRUE);
			}
			cont2 =
				gtk_tree_model_iter_next(GTK_TREE_MODEL
										 (BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &child);
		}

	} else {
		DEBUG_MSG("no bookmarks for doc %p in hashtable\n",doc);
	}
}

/* this is called by the editor widget to show bookmarks in the left margin.
returns a line number for the Tbmark that bmark points to, or -1 if there is no bmark  */
gint bmark_margin_get_next_bookmark(Tdocument * doc, gpointer *bmark) {
	gboolean cont;
	GtkTextIter textit;
	GtkTreeIter treeit = ((Tbmark *)*bmark)->iter;
	cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &treeit);
	if (!cont) {
		return -1;
	}
	gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &treeit, PTR_COLUMN,bmark, -1);
	gtk_text_buffer_get_iter_at_mark(doc->buffer, &textit, ((Tbmark *)*bmark)->mark);
	return gtk_text_iter_get_line(&textit);
}
/* this is called by the editor widget to show bookmarks in the left margin.
 returns a line number for the Tbmark that bmark points to, or -1 if there is no bmark */
gint bmark_margin_get_initial_bookmark(Tdocument * doc, GtkTextIter *fromit, gpointer *bmark) {
	guint offset;
	GtkTextIter textit;
	if (!doc->bmark_parent) {
		return -1;
	}
	offset = gtk_text_iter_get_offset(fromit);
	*bmark = bmark_find_bookmark_before_offset(BFWIN(doc->bfwin), offset, doc->bmark_parent); /* returns NULL if there is no existing bookmark *before* offset */
	if (!*bmark) {
		GtkTreeIter treeit;
		gboolean retval = gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &treeit, doc->bmark_parent);
		if (!retval) {
			return -1;
		}
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &treeit, PTR_COLUMN,bmark, -1);
	}
	gtk_text_buffer_get_iter_at_mark(doc->buffer, &textit, ((Tbmark *)*bmark)->mark);
	return gtk_text_iter_get_line(&textit);
}

/* this function will simply add the bookmark as defined in the arguments
 *
 * will use offset if itoffset is NULL
 * will use itoffset if not NULL
 */
static Tbmark *bmark_add_backend(Tdocument *doc, GtkTextIter *itoffset, gint offset, const gchar *name, const gchar *text, gboolean is_temp) {
	Tbmark *m;
	gchar *displaytext = NULL;
	GtkTextIter it;
	m = g_slice_new0(Tbmark);
	/*g_print("bmark_add_backend, alloc bmark %p\n",m);*/
	m->doc = doc;

	if (itoffset) {
		it = *itoffset;
		m->offset = gtk_text_iter_get_offset(&it);
	} else {
		gtk_text_buffer_get_iter_at_offset(doc->buffer,&it,offset);
		m->offset = offset;
	}

	m->mark = gtk_text_buffer_create_mark(doc->buffer, NULL, &it, TRUE);
	g_object_ref(doc->uri);
	m->filepath = doc->uri;
	m->is_temp = is_temp;
	m->text = g_strdup(text);
	m->name = (name) ? g_strdup(name) : g_strdup("");
	m->description = g_strdup("");

	/* insert into tree */
	bmark_get_iter_at_tree_position(doc->bfwin, m);
	displaytext = bmark_showname(doc->bfwin, m);
	gtk_tree_store_set(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore, &m->iter, NAME_COLUMN, displaytext, PTR_COLUMN, m, -1);
	g_free (displaytext);

	/* and store */
	if (!m->is_temp) {
		bmark_store(BFWIN(doc->bfwin), m);
	}
	gtk_widget_queue_draw(doc->view);
	return m;
}

/**
 * bmark_text_for_offset:
 *
 * will use offset if itoffset is NULL
 * will use itoffset if not NULL
 */
static gchar *bmark_text_for_offset(Tdocument *doc, GtkTextIter *itoffset, gint offset) {
	GtkTextIter it, eit, sit;
	if (itoffset) {
		it = *itoffset;
	} else {
		gtk_text_buffer_get_iter_at_offset(doc->buffer,&it,offset);
	}
	sit = eit = it;
	gtk_text_iter_forward_to_line_end(&eit);
	gtk_text_iter_forward_chars(&sit, BMARK_SHOW_NUM_TEXT_CHARS);
	if (!gtk_text_iter_in_range(&sit, &it, &eit))
		sit = eit;
#ifdef DEBUG
	{
		gchar *tmp = gtk_text_iter_get_text(&it, &sit);
		DEBUG_MSG("bmark_text_for_offset, text=%s\n",tmp);
		g_free(tmp);
	}
#endif
	return gtk_text_iter_get_text(&it, &sit);
}

/* this function will add a bookmark to the current document at current cursor / selection */
static void bmark_add_current_doc_backend(Tbfwin *bfwin, const gchar *name, gint offset, gboolean is_temp) {
	GtkTextIter it, eit, sit;
	Tbmark *m;
	DEBUG_MSG("bmark_add_backend, adding bookmark at offset=%d for bfwin=%p\n",offset,bfwin);
	/* create bookmark */
	gtk_text_buffer_get_iter_at_offset(DOCUMENT(bfwin->current_document)->buffer,&it,offset);
	/* if there is a selection, and the offset is within the selection, we'll use it as text content */
	if (gtk_text_buffer_get_selection_bounds(DOCUMENT(bfwin->current_document)->buffer,&sit,&eit)
				&& gtk_text_iter_in_range(&it,&sit,&eit)) {
		gchar *text = gtk_text_iter_get_text(&sit, &eit);
		m = bmark_add_backend(DOCUMENT(bfwin->current_document), &sit, offset, name, text, is_temp);
		g_free(text);

	} else {
		gchar *text;
		text = bmark_text_for_offset(DOCUMENT(bfwin->current_document), &it, offset);
		m = bmark_add_backend(DOCUMENT(bfwin->current_document), &it, offset, name, text, is_temp);
		g_free(text);
	}
	if (bfwin->bmark) { /* only if there is a left panel we should do this */
		GtkTreePath *path;
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&m->iter);
		gtk_tree_view_expand_to_path(bfwin->bmark,path);
		gtk_tree_path_free(path);
		gtk_widget_grab_focus(bfwin->current_document->view);
	}
}

/*
can we make this function faster? when adding bookmarks from a search this function uses
a lot of time, perhaps that can be improved
*/
static Tbmark *bmark_get_bmark_at_line(Tdocument *doc, gint offset) {
	GtkTextIter sit, eit;
	GtkTreeIter tmpiter;
	gint linenum;
	gtk_text_buffer_get_iter_at_offset(doc->buffer,&sit,offset);
	linenum = gtk_text_iter_get_line(&sit);
	eit = sit;
	gtk_text_iter_set_line_offset(&sit, 0);
	gtk_text_iter_forward_to_line_end(&eit);
	/* check for existing bookmark in this place */
	if (DOCUMENT(doc)->bmark_parent) {
		GtkTextIter testit;
		Tbmark *m, *m2;
		m = bmark_find_bookmark_before_offset(BFWIN(doc->bfwin), offset, doc->bmark_parent);
		if (m == NULL) {
			DEBUG_MSG("bmark_get_bmark_at_line, m=NULL, get first child\n");
			if (gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter,doc->bmark_parent)) {
				gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN, &m2, -1);
				gtk_text_buffer_get_iter_at_mark(doc->buffer, &testit,m2->mark);
				if (gtk_text_iter_get_line(&testit) == linenum) {
					return m2;
				}
			}
		} else {
			gtk_text_buffer_get_iter_at_mark(doc->buffer, &testit,m->mark);
			DEBUG_MSG("bmark_get_bmark_at_line, m=%p, has linenum=%d\n",m,gtk_text_iter_get_line(&testit));
			if (gtk_text_iter_get_line(&testit) == linenum) {
				return m;
			}
			tmpiter = m->iter;
			if (gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore),&tmpiter)) {
				gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN, &m2, -1);
				gtk_text_buffer_get_iter_at_mark(doc->buffer, &testit,m2->mark);
				if (gtk_text_iter_get_line(&testit) == linenum) {
						return m2;
				}
			}
		}
		DEBUG_MSG("bmark_get_bmark_at_line, nothing found at this line, return NULL\n");
		return NULL;

	}
	DEBUG_MSG("bmark_get_bmark_at_line, no existing bookmark found, return NULL\n");
	return NULL;
}

static void bmark_warn_unsaved_file(Tbfwin *bfwin) {
	message_dialog_new(bfwin->main_window,
								 GTK_MESSAGE_ERROR,
								 GTK_BUTTONS_CLOSE,
								 _("Error adding bookmark"),
								 _("Cannot add bookmarks to unsaved files."));
}

/**
 * bmark_add_extern
 * @doc: a #Tdocument* with the document
 * @offset: the character position where to set the bookmark
 * @name: a name to set for the bookmark, or NULL for no name
 * @text: the text for the bookmark, or NULL to have it set automatically
 *
 * Code in bluefish that want to set a bookmark, not related to
 * the cursor location or a mouse position should use
 * this function.
 */
void bmark_add_extern(Tdocument *doc, gint offset, const gchar *name, const gchar *text, gboolean is_temp) {
	if (!doc)
		return;
	if (!doc->uri) {
		bmark_warn_unsaved_file(doc->bfwin);
		return;
	}
	DEBUG_MSG("adding bookmark at offset %d with name %s\n",offset,name); /* dummy */
	if (!bmark_get_bmark_at_line(doc, offset)) {
		if (text) {
			bmark_add_backend(doc, NULL, offset, (name) ? name : "", text, is_temp);
		} else {
			gchar *tmp = bmark_text_for_offset(doc, NULL, offset);
			bmark_add_backend(doc, NULL, offset, (name) ? name : "", tmp, is_temp);
			g_free(tmp);
		}
	}
}

void bmark_toggle(Tdocument *doc, gint offset, const gchar *name, const gchar *text) {
	Tbmark *bmark;
	if (!doc)
		return;
	if (!doc->uri) {
		bmark_warn_unsaved_file(doc->bfwin);
		return;
	}
	bmark = bmark_get_bmark_at_line(doc, offset);
	if (bmark) {
		bmark_check_remove(BFWIN(doc->bfwin),bmark); /* check  if we should remove a filename too */
		bmark_unstore(BFWIN(doc->bfwin), bmark);
		bmark_free(bmark);
	} else {
		bmark_add_extern(doc, offset, name, text, !main_v->globses.bookmarks_default_store);
	}
}

void bmark_add(Tbfwin * bfwin) {
	GtkTextIter it, it2;
	gint offset;
	gboolean has_mark;
	/* check for unnamed document */
	if (!DOCUMENT(bfwin->current_document)->uri) {
		bmark_warn_unsaved_file(bfwin);
		/*Please save the file first. A Save button in this dialog would be cool -- Alastair*/
		return;
	}
	/* if the left panel is disabled, we simply should add the bookmark to the list, and do nothing else */
	gtk_text_buffer_get_iter_at_mark(DOCUMENT(bfwin->current_document)->buffer, &it, gtk_text_buffer_get_insert(DOCUMENT(bfwin->current_document)->buffer));
	gtk_text_buffer_get_iter_at_mark(DOCUMENT(bfwin->current_document)->buffer, &it2, gtk_text_buffer_get_selection_bound(DOCUMENT(bfwin->current_document)->buffer));
	gtk_text_iter_order(&it,&it2);
	offset = gtk_text_iter_get_offset(&it);
	/* check for existing bookmark in this place */
	has_mark = (bmark_get_bmark_at_line(DOCUMENT(bfwin->current_document), offset) != NULL);
	if (has_mark) {
		message_dialog_new(bfwin->main_window,
							 	 GTK_MESSAGE_ERROR,
							 	 GTK_BUTTONS_CLOSE,
							 	 _("Can't add bookmark"),
								 _("You already have a bookmark here!"));
		return;
	}
	bmark_add_current_doc_backend(bfwin, "", offset, !main_v->globses.bookmarks_default_store);
}

gboolean bmark_have_bookmark_at_stored_bevent(Tdocument * doc) {
	if (main_v->bevent_doc == doc) {
		return (bmark_get_bmark_at_line(doc, main_v->bevent_charoffset) != NULL);
	}
	return FALSE;
}

void bmark_del_at_bevent(Tdocument *doc) {
	if (main_v->bevent_doc == doc) {
		Tbmark *b = bmark_get_bmark_at_line(doc, main_v->bevent_charoffset);
		if (b) {
			DEBUG_MSG("bmark_del_at_bevent, deleting bookmark %p\n",b);
			bmark_check_remove(BFWIN(doc->bfwin),b); /* check  if we should remove a filename too */
			bmark_unstore(BFWIN(doc->bfwin), b);
			bmark_free(b);
		}
	}
}

void bmark_add_at_bevent(Tdocument *doc) {
		/* check for unnamed document */
	if (!doc->uri) {
		message_dialog_new(BFWIN(doc->bfwin)->main_window,
								 GTK_MESSAGE_ERROR,
								 GTK_BUTTONS_CLOSE,
								 _("Error adding bookmark"),
								 _("Cannot add bookmarks to unsaved files."));
		return;
	}
	if (main_v->bevent_doc == doc) {
		gint offset = main_v->bevent_charoffset;
		/* we have the location */
		bmark_add_current_doc_backend(doc->bfwin, "", offset, !main_v->globses.bookmarks_default_store);
	}
}

/* not used yet
void bmark_del_for_filename(Tbfwin *bfwin, gchar *filename) {
	GtkTreeIter *parent = (GtkTreeIter *)g_hash_table_lookup(BMARKDATA(bfwin->bmarkdata)->bmarkfiles,filename);
	if (parent) {
		bmark_del_children_backend(bfwin, parent);
	}
}
*/

void bmark_del_for_document(Tbfwin *bfwin, Tdocument *doc) {
	if (doc->bmark_parent) {
		bmark_del_children_backend(bfwin, doc->bmark_parent);
	}
}

void bmark_del_all(Tbfwin *bfwin,gboolean ask) {
	gint retval;
	const gchar *buttons[] = {GTK_STOCK_NO, GTK_STOCK_YES, NULL};
	GtkTreeIter tmpiter;

	if (bfwin==NULL) return;

	if (ask)	{
	  retval = message_dialog_new_multi(bfwin->main_window,
													GTK_MESSAGE_QUESTION,
													buttons,
													_("Delete all bookmarks."),
													NULL);
	  if (retval==0) return;
	}
	DEBUG_MSG("bmark_del_all, deleting all bookmarks!\n");
	while (gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, NULL) ) {
		bmark_del_children_backend(bfwin, &tmpiter);
	}
	gtk_widget_grab_focus(bfwin->current_document->view);
}

void bmark_cleanup(Tbfwin * bfwin) {
	DEBUG_MSG("bmark_cleanup, cleanup for bfwin=%p\n",bfwin);
	bfwin->bmark = NULL;
	bfwin->bmarkfilter = NULL;
	g_free(bfwin->bmark_search_prefix);
	bfwin->bmark_search_prefix=NULL;
}