/* Bluefish HTML Editor - bookmarks
 *
 * Copyright (C) 2003 Oskar Swida
 * modifications (C) 2004 Olivier Sessink
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
/* #define DEBUG */

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
#include "stringlist.h"
#include "menu.h"				/* menu_translate() */

#ifdef USE_SCANNER
#include "bf-textview.h"
#endif

/*
bookmarks will be loaded and saved to an arraylist (see stringlist.c). This 
is a double linked list (GList *) with pointers to string arrays (gchar **).

To have the GUI work with them, we convert those arrays (gchar **) into a 
Tbmark struct. This struct will ahve a pointer to the array (gchar **) so 
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
	GnomeVFSURI *filepath;
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
	Tdocument *bevent_doc;		/* last button event document */
	gint bevent_charoffset;		/* last button event location */
	GtkTreeStore *bookmarkstore; /* the global bookmarks from the global session */
	GHashTable *bmarkfiles; /* the global bookmarks from the global session */
} Tbmarkdata;
#define BMARKDATA(var) ((Tbmarkdata *)(var))

enum {
	BM_FMODE_FULL,
	BM_FMODE_HOME, /* not implemented, defaults to full */
	BM_FMODE_FILE,
	BM_FMODE_PATH
};

static void gnome_vfs_uri_hash_destroy(gpointer data) {
	gnome_vfs_uri_unref((GnomeVFSURI *)data);
}

static gchar *bmark_display_text(gchar *name, gchar *text) {
	if (name && strlen(name) > 0) {
		return g_strconcat(name, " - ", text,NULL);
	} 
	return g_strdup(text);
}

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
	gnome_vfs_uri_unref(m->filepath);
	g_free(m->text);
	g_free(m->name);
	g_free(m->description);
	g_free(m);
}

static void bmark_update_offset_from_textmark(Tbmark *b) {
	if (b->doc && b->mark) {
		GtkTextIter it;
		gtk_text_buffer_get_iter_at_mark(b->doc->buffer, &it, b->mark);
		b->offset = gtk_text_iter_get_offset(&it);
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
		DEBUG_MSG("name=%s, description=%s, filepath=%s, text=%s\n", b->name, b->description, gnome_vfs_uri_get_path(b->filepath), b->text);
		strarr[2] = gnome_vfs_uri_to_string(b->filepath,GNOME_VFS_URI_HIDE_PASSWORD);
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

	if (b->doc) b->len = b->doc->fileinfo->size;
	
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

/* get value from pointer column */
static gpointer get_current_bmark(Tbfwin * bfwin)
{
	if (bfwin->bmark) {
		GtkTreePath *path;
		GtkTreeViewColumn *col;
		gtk_tree_view_get_cursor(bfwin->bmark, &path, &col);
		if (path != NULL) {
			gpointer retval = NULL;
			GtkTreeIter iter;
			gtk_tree_model_get_iter(gtk_tree_view_get_model(bfwin->bmark), &iter, path);
			gtk_tree_model_get(gtk_tree_view_get_model(bfwin->bmark),&iter,1, &retval, -1);
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
	GTK_WIDGET_SET_FLAGS(button, GTK_CAN_DEFAULT);
	gtk_dialog_add_action_widget(GTK_DIALOG(dlg), button, GTK_RESPONSE_OK);

	table = gtk_table_new(2, 3, FALSE);
	gtk_table_set_col_spacings(GTK_TABLE(table), 12);
	gtk_table_set_row_spacings(GTK_TABLE(table), 6);
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), table, FALSE, FALSE, 12);

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
		m->is_temp = GTK_TOGGLE_BUTTON(istemp)->active;
		if (m->name && strlen(m->name) > 0) {
			tmpstr = g_strconcat(m->name, " - ", m->text,NULL);
		} else {
			tmpstr = g_strdup(m->text);
		}
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

static void bmark_popup_menu_goto(Tbfwin *bfwin) {
	Tbmark *b;
	b = get_current_bmark(bfwin);
	if (b) {
		DEBUG_MSG("bmark_popup_menu_goto_lcb, calling doc_new_from_uri for %s with goto_offset %d\n",gnome_vfs_uri_get_path(b->filepath),b->offset);
		doc_new_from_uri(bfwin, b->filepath, NULL, FALSE, FALSE, -1, b->offset);
	}
}
/* 
 * removes the bookmark from the treestore, and if it is the last remaining bookmark
 * for the document, it will remove the parent iter (the filename) from the treestore as well
 */
static void bmark_check_remove(Tbfwin *bfwin,Tbmark *b) {
	GtkTreeIter parent;
#ifdef USE_SCANNER
	GtkTextIter it;
#endif	
	if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&parent,&b->iter)) {
		gint numchild = gtk_tree_model_iter_n_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &parent);
		DEBUG_MSG("bmark_check_remove, the parent of this bookmark has %d children\n", numchild);
		gtk_tree_store_remove(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &(b->iter));
#ifdef USE_SCANNER
				if (b->doc) {
				 	gtk_text_buffer_get_iter_at_mark(b->doc->buffer,&it,b->mark);
				 	bf_textview_set_symbol(BF_TEXTVIEW(b->doc->view),"bmark",gtk_text_iter_get_line(&it)+1,FALSE);
				} 
#endif		
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
		}
	}
#ifdef DEVELOPMENT	
	 else {
		gchar *name;
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &b->iter, NAME_COLUMN,&name, -1);
		g_print("bmark_check_remove, very weird, bookmark %s for %s does not have a parent ?????\n",name,gnome_vfs_uri_get_path(b->filepath));
		g_free(name);
		exit(123);
	}
#endif
  	DEBUG_MSG("bmark_check_remove, finished\n");
}

static void bmark_del_children_backend(Tbfwin *bfwin, GtkTreeIter *parent) {
	GtkTreeIter tmpiter;
	while (gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, parent)) {
		Tbmark *b;
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN,&b,-1);
		if (b) {
			DEBUG_MSG("bmark_del_children_backend, found b=%p\n",b);
			bmark_check_remove(bfwin,b);
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
			GtkTreeIter iter;
			gint depth, retval;
			depth = gtk_tree_path_get_depth(path);
			if (depth == 2) {
				/* go up to parent */
				gtk_tree_path_up(path);
			} else if (depth != 1) {
				g_print("a bug in function bmark_popup_menu_deldoc_lcb()\n");
#ifdef DEVELOPMENT
				exit(124);
#endif
				return;
			}
			gtk_tree_model_get_iter(gtk_tree_view_get_model(bfwin->bmark), &iter, path);
			gtk_tree_path_free(path);
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &iter, NAME_COLUMN,&name, -1);
		
			pstr = g_strdup_printf(_("Do you really want to delete all bookmarks for %s?"), name);
			retval = message_dialog_new_multi(bfwin->main_window,
														 GTK_MESSAGE_QUESTION,
														 buttons,
														 _("Delete bookmarks?"),
														 pstr);
			g_free(pstr);
			if (retval == 0)
				return;
			bmark_del_children_backend(bfwin, &iter);
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
		case 1:
			bmark_popup_menu_goto(bfwin);
		break;
		case 2: {
			Tbmark *m = get_current_bmark(bfwin);
			if (!m) return;
			bmark_add_rename_dialog(bfwin, _("Edit bookmark"));
		} break;
		case 3:
			bmark_popup_menu_del(bfwin);
		break;
		case 10:
			bmark_del_all(bfwin,TRUE);
		break;
		case 11:
			bmark_popup_menu_deldoc(bfwin);
		break;
		case 20:
			main_v->globses.bookmarks_default_store = gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget));
		break;
		case 30:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_filename_mode = BM_FMODE_FILE;
			}
		break;
		case 31:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_filename_mode = BM_FMODE_PATH;
			}
		break;
		case 32:
			if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(widget))) {
				bfwin->session->bookmarks_filename_mode = BM_FMODE_FULL;
			}
		break;
		default:
			g_print("not implemented yet\n");
		break;
	}
}

static GtkItemFactoryEntry bmark_rpopup_menu_entries[] = {
	{ N_("/Goto"),		NULL,	bmark_rpopup_action_lcb,		1,	"<Item>" },
	{ N_("/Edit"),NULL,	bmark_rpopup_action_lcb,	2,	"<Item>" },
	{ N_("/Delete"),NULL,	bmark_rpopup_action_lcb,	3,	"<Item>" },
	{ "/sep1",						NULL,	NULL,									0,	"<Separator>" },
	{ N_("/Delete All in document"),NULL,	bmark_rpopup_action_lcb,	10,	"<Item>" },
	{ N_("/Delete All"),NULL,	bmark_rpopup_action_lcb,	11,	"<Item>" },
	{ "/sep2",						NULL,	NULL,									0,	"<Separator>" },
	{ N_("/Permanent by default"),	NULL,	bmark_rpopup_action_lcb,	20,	"<ToggleItem>" },
	{ "/sep3",						NULL,	NULL,									0,	"<Separator>" },
	{ N_("/Show by file name"),	NULL,	bmark_rpopup_action_lcb,	30,	"<RadioItem>" },
	{ N_("/Show by full path"),	NULL,	bmark_rpopup_action_lcb,	31,	"/Show by file name" },
	{ N_("/Show by full uri"),	NULL,	bmark_rpopup_action_lcb,	32,	"/Show by file name" }
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
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Goto"), FALSE);
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Edit"), FALSE);
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Delete"), FALSE);
	}
	if (!show_file_specific) {
		gtk_widget_set_sensitive(gtk_item_factory_get_widget(menumaker, "/Delete All in document"), FALSE);
	}
	setup_toggle_item(menumaker, "/Permanent by default", main_v->globses.bookmarks_default_store);
	setup_toggle_item(menumaker, "/Show by file name", bfwin->session->bookmarks_filename_mode == BM_FMODE_FILE);
	setup_toggle_item(menumaker, "/Show by full path", bfwin->session->bookmarks_filename_mode == BM_FMODE_PATH);
	setup_toggle_item(menumaker, "/Show by full uri", bfwin->session->bookmarks_filename_mode != BM_FMODE_FILE && bfwin->session->bookmarks_filename_mode != BM_FMODE_PATH);
	gtk_widget_show_all(menu);
	g_signal_connect_after(G_OBJECT(menu), "destroy", G_CALLBACK(destroy_disposable_menu_cb), menu);
	return menu;
}

/* right mouse click */
static gboolean bmark_event_mouseclick(GtkWidget * widget, GdkEventButton * event, Tbfwin * bfwin) {
	GtkTreePath *path;
	gboolean show_bmark_specific = FALSE, show_file_specific = FALSE;
	if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(bfwin->bmark), event->x, event->y, &path, NULL, NULL, NULL)) {
		if (path) {
			gint depth = gtk_tree_path_get_depth(path);
			gtk_tree_path_free(path);
			if (depth==2) {
				show_bmark_specific = TRUE;
				show_file_specific = TRUE;
				if (event->button == 1 && event->type == GDK_2BUTTON_PRESS) {	/* double click  */
					bmark_popup_menu_goto(bfwin);
					return TRUE;
				}
			} else if (depth == 1) {
				show_file_specific = TRUE;
			}
		}
	}
	if (event->button == 3 && event->type == GDK_BUTTON_PRESS) {	/* right mouse click */
		gtk_menu_popup(GTK_MENU(bmark_popup_menu(bfwin, show_bmark_specific, show_file_specific)), NULL, NULL, NULL, NULL,
				   event->button, event->time);
	}
	return FALSE;
}

/* Initialize bookmarks gui for window */

GtkWidget *bmark_gui(Tbfwin * bfwin)
{
	GtkWidget *vbox, *scroll;
	GtkCellRenderer *cell;
	GtkTreeViewColumn *column;
	DEBUG_MSG("bmark_gui, building gui for bfwin=%p\n",bfwin);
	/* Tree Store is in BMARKDATA(bfwin->bmarkdata)->bookmarkstore 
	   Tree View is in bfwin->bmark 
	 */
	vbox = gtk_vbox_new(FALSE, 1);
	bfwin->bmark = gtk_tree_view_new_with_model(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore));
	cell = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("", cell, "text", NAME_COLUMN, NULL);
	gtk_tree_view_column_set_sizing(column, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_append_column(GTK_TREE_VIEW(bfwin->bmark), column);
	gtk_widget_show_all(bfwin->bmark);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(bfwin->bmark), FALSE);
	/*gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(bfwin->bmark), TRUE);*/
	scroll = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), bfwin->bmark);
	gtk_box_pack_start(GTK_BOX(vbox), scroll, TRUE, TRUE, 0);
	g_signal_connect(G_OBJECT(bfwin->bmark), "button-press-event",
					 G_CALLBACK(bmark_event_mouseclick), bfwin);
	gtk_tree_view_expand_all(bfwin->bmark);
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
	DEBUG_MSG("bmark_get_iter_at_tree_position, started for filepath=%s\n",gnome_vfs_uri_get_path(m->filepath));
	ptr = g_hash_table_lookup(BMARKDATA(bfwin->bmarkdata)->bmarkfiles, m->filepath);
	DEBUG_MSG("bmark_get_iter_at_tree_position, found %p for filepath %s in hashtable %p\n",ptr,gnome_vfs_uri_get_path(m->filepath),BMARKDATA(bfwin->bmarkdata)->bmarkfiles);
	if (ptr == NULL) {			/* closed document or bookmarks never set */
		gchar *title, *rawtitle = NULL;
		parent = g_new0(GtkTreeIter, 1);
		gtk_tree_store_append(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, parent, NULL);
		switch (bfwin->session->bookmarks_filename_mode) {
		/*case BM_FMODE_HOME:
			if (bfwin->project != NULL && bfwin->project->basedir && strlen(bfwin->project->basedir)) {
				gint baselen = strlen(bfwin->project->basedir);
				gchar *tmp;
				tmp = gnome_vfs_uri_to_string(m->filepath,GNOME_VFS_URI_HIDE_PASSWORD);
				if (tmp[baselen] == '/') baselen++;
				if (strncmp(tmp, bfwin->project->basedir, baselen)==0) {
					title = g_strdup(tmp + baselen);
				}
				g_free(tmp);
			}
			break;*/
		case BM_FMODE_PATH:
			title = g_strdup(gnome_vfs_uri_get_path(m->filepath));
			break;
		case BM_FMODE_FILE:
			title = gnome_vfs_uri_extract_short_name(m->filepath);
			break;
		case BM_FMODE_FULL:
		default:
			rawtitle = gnome_vfs_uri_to_string(m->filepath,GNOME_VFS_URI_HIDE_PASSWORD);
			title = gnome_vfs_format_uri_for_display(rawtitle);
			g_free(rawtitle);
			break;
		}
		
		gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, parent, NAME_COLUMN, title, PTR_COLUMN, m->doc, -1);
		g_free(title);
		  
		if (m->doc != NULL) {
			DEBUG_MSG("bmark_get_iter_at_tree_position, setting parent iter %p for doc%p\n",parent,m->doc);
			m->doc->bmark_parent = parent;
		}
		DEBUG_MSG("bmark_get_iter_at_tree_position, appending parent %p in hashtable %p for filepath=%s\n",parent,BMARKDATA(bfwin->bmarkdata)->bmarkfiles,gnome_vfs_uri_get_path(m->filepath));
		/* the hash table frees the key, but not the value, on destroy */
		gnome_vfs_uri_ref(m->filepath);
		g_hash_table_insert(BMARKDATA(bfwin->bmarkdata)->bmarkfiles, m->filepath, parent);
	} else
		parent = (GtkTreeIter *) ptr;

	{
		Tbmark *bef = bmark_find_bookmark_before_offset(bfwin, m->offset, parent);
		if (bef == NULL) {
			gtk_tree_store_prepend(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &m->iter, parent);
			return;
		} else {
			gtk_tree_store_insert_after(GTK_TREE_STORE(BMARKDATA(bfwin->bmarkdata)->bookmarkstore),&m->iter,parent,&bef->iter);
			return;
		}
	}
}


/*
 * this function should create the global
 * main_v->bookmarkstore
 */
gpointer bookmark_data_new(void) {
	Tbmarkdata *bmd;
	bmd = g_new0(Tbmarkdata, 1);
	bmd->bookmarkstore = gtk_tree_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_POINTER);
	bmd->bmarkfiles = g_hash_table_new_full(gnome_vfs_uri_hash, gnome_vfs_uri_hequal,gnome_vfs_uri_hash_destroy,NULL);
	DEBUG_MSG("bookmark_data_new, created bookmarkstore at %p\n", bmd->bookmarkstore);
	return bmd;
}
void bookmark_data_cleanup(gpointer *data) {
	Tbmarkdata *bmd = BMARKDATA(data);
	g_object_unref(bmd->bookmarkstore);
	g_hash_table_destroy(bmd->bmarkfiles);
	g_free(bmd);
}

/* this function will load the bookmarks
 * from bfwin->session->bmarks and parse
 * them into treestore BMARKDATA(bfwin->bmarkdata)->bookmarkstore
 *
 * it is called from bluefish.c for the first window (global bookmarks)
 * and from project.c after opening a project
 *
 * this function should ALSO check all douments that are
 * opened (bfwin->documentlist) if they have bookmarks !!
 */
void bmark_reload(Tbfwin * bfwin) {
	GnomeVFSURI *cacheduri=NULL;
	GList *tmplist;

	DEBUG_MSG("bmark_reload for bfwin %p\n",bfwin);
	bmark_store_all(bfwin);

	tmplist = g_list_first(bfwin->session->bmarks);
	while (tmplist) {
		gchar **items = (gchar **) tmplist->data;
		if (items && count_array(items) == 6) {
			gchar *ptr;
			Tbmark *b;
			b = g_new0(Tbmark, 1);
			b->name = g_strdup(items[0]);
			b->description = g_strdup(items[1]);
			/* convert old (Bf 1.0) bookmarks to new bookmarks with uri's */
			if (strchr(items[2], ':')==NULL) {
				gchar *tmp;
				tmp = g_strconcat("file://", items[2], NULL);
				b->filepath = gnome_vfs_uri_new(tmp);
				g_free(tmp);
			} else {
				b->filepath = gnome_vfs_uri_new(items[2]);
			}
			/* because the bookmark list is usually sorted, we try to cache the uri's and consume less memory */
			if (cacheduri && (cacheduri == b->filepath || gnome_vfs_uri_equal(cacheduri,b->filepath))) {
				gnome_vfs_uri_unref(b->filepath);
				gnome_vfs_uri_ref(cacheduri);
				b->filepath = cacheduri;
			} else {
				cacheduri = b->filepath;
			}
			b->offset = atoi(items[3]);
			b->text = g_strdup(items[4]);
			b->len = atoi(items[5]);
			b->strarr = items;
			bmark_get_iter_at_tree_position(bfwin, b);
			if (b->name && strlen(b->name)>0) {
				ptr = g_strconcat(b->name, " - ", b->text, NULL);
			} else {
				ptr = g_strdup(b->text);
			}
			gtk_tree_store_set(BMARKDATA(bfwin->bmarkdata)->bookmarkstore, &(b->iter), NAME_COLUMN, ptr, PTR_COLUMN, b,
							   -1);
			g_free(ptr);
		}
		tmplist = g_list_next(tmplist);
	}
	
	tmplist = g_list_first(bfwin->documentlist);
	while (tmplist) {
		DEBUG_MSG("bmark_reload, calling bmark_set_for_doc for doc=%p\n",tmplist->data);
		bmark_set_for_doc(DOCUMENT(tmplist->data));
		bmark_check_length(bfwin, DOCUMENT(tmplist->data));
		tmplist = g_list_next(tmplist);
	}
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
		gtk_tree_view_set_model(bfwin->bmark, GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore));
	}
}

void bmark_clean_for_doc(Tdocument * doc) {
	GtkTreeIter tmpiter;
	gboolean cont;

	if (doc->bmark_parent == NULL)
		return;

	if (!doc->uri) 
		return;
	DEBUG_MSG("bmark_clean_for_doc, getting children for parent_iter=%p\n",doc->bmark_parent);
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
			if (doc->fileinfo) b->len = doc->fileinfo->size;
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
	/* remove the pointer from the hastable */
	g_hash_table_remove(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bmarkfiles,doc->uri);
	g_free(doc->bmark_parent);
	doc->bmark_parent = NULL;
}

/*
 * this function will check is this document needs any bookmarks, and set the
 * doc->bmark_parent if needed
 */
void bmark_set_for_doc(Tdocument * doc) {
	GtkTreeIter tmpiter;
	GtkTextIter it;
	gboolean cont;
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
	
	cont =
		gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter,
									 NULL);
	while (cont) {
		GtkTreeIter child;
		if (gtk_tree_model_iter_children
			(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &child, &tmpiter)) {
			Tbmark *mark = NULL;
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &child, PTR_COLUMN,
							   &mark, -1);
			if (mark) {
				if (mark->filepath && (mark->filepath == doc->uri || gnome_vfs_uri_equal(mark->filepath, doc->uri))) {	/* this is it */
					gboolean cont2;
					DEBUG_MSG("bmark_set_for_doc, we found a bookmark for document %s at offset=%d!\n",gtk_label_get_text(GTK_LABEL(doc->tab_menu)),mark->offset);
					/* we will now first set the Tdocument * into the second column of the parent */
					gtk_tree_store_set(GTK_TREE_STORE(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN, doc, -1);
					
					mark->doc = doc;
					gtk_text_buffer_get_iter_at_offset(doc->buffer, &it, mark->offset);
					mark->mark = gtk_text_buffer_create_mark(doc->buffer, NULL, &it, TRUE);
#ifdef USE_SCANNER
					bf_textview_set_symbol(BF_TEXTVIEW(doc->view),"bmark",gtk_text_iter_get_line(&it)+1,TRUE);
#endif					
					cont2 =
						gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore),
												 &child);
					while (cont2) {
						mark = NULL;
						gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &child,
										   PTR_COLUMN, &mark, -1);
						if (mark) {
							mark->doc = doc;
							DEBUG_MSG("bmark_set_for_doc, next bookmark at offset=%d!\n",mark->offset);
							gtk_text_buffer_get_iter_at_offset(doc->buffer, &it, mark->offset);
							mark->mark =
								gtk_text_buffer_create_mark(doc->buffer, NULL, &it, TRUE);
#ifdef USE_SCANNER
					bf_textview_set_symbol(BF_TEXTVIEW(doc->view),"bmark",gtk_text_iter_get_line(&it)+1,TRUE);
#endif													
						}
						cont2 =
							gtk_tree_model_iter_next(GTK_TREE_MODEL
													 (BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &child);
					}
					doc->bmark_parent = g_memdup(&tmpiter, sizeof(GtkTreeIter));
					/* now we add that to the hastbale */
					gnome_vfs_uri_ref(doc->uri);
					g_hash_table_insert(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bmarkfiles, doc->uri, doc->bmark_parent);
					DEBUG_MSG("bmark_set_for_doc, added parent_iter %p to doc %p, and adding that to the hashtable %p\n",doc->bmark_parent,doc,BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bmarkfiles);
					return;
				}
			}
		}
		cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter);
	}							/* cont */
	DEBUG_MSG("bmark_set_for_doc, no bookmarks found for document %s\n", gtk_label_get_text(GTK_LABEL(doc->tab_menu)));
}

/**
 * bmark_get_bookmarked_lines:
 * @doc: Tdocument *
 * @ fromit: GtkTextIter *
 * @ toit: GtkTextIter *
 *
 * this function returns a hash table with all bookmarks between fromit and toit
 *
 * this function is called VERY OFTEN (might be 20X per second!!!!) by document.c
 * to redraw the bookmarks at the sides
 * so we obviously need to keep this function VERY FAST 
 *
 * the function will return NULL if no bookmarks for this document are 
 * known (this is faster then looking in an empty hash table)
 *
 * Return value: #GHashTable * pointer or NULL
 */
GHashTable *bmark_get_bookmarked_lines(Tdocument * doc, GtkTextIter *fromit, GtkTextIter *toit) {
	if (doc->bmark_parent) {
		gboolean cont = TRUE;
		guint offset;
		Tbmark *mark;
		GtkTreeIter tmpiter;

		GHashTable *ret = g_hash_table_new_full(g_int_hash, g_int_equal, g_free, g_free);

		/* because the bookmarks are sorted by line number, we don't have to scan trough all 
		bookmarks, we can start at the bookmark *before* fromit, and continue until the 
		first bookmark > toit */
		offset = gtk_text_iter_get_offset(fromit);
		mark = bmark_find_bookmark_before_offset(BFWIN(doc->bfwin), offset, doc->bmark_parent);
		if (mark) {
			tmpiter = mark->iter;
		} else {
			cont = gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), 
									&tmpiter, doc->bmark_parent);
		}
		
		while (cont) {
			Tbmark *mark;
			gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN,
							   &mark, -1);
			if (mark && mark->mark) {
				GtkTextIter it;
				gint *iaux;
				gtk_text_buffer_get_iter_at_mark(doc->buffer, &it, mark->mark);
				if (gtk_text_iter_compare(toit,&it) < 0) {
					break;
				} else if (gtk_text_iter_compare(fromit,&it) < 0) {
					iaux = g_new(gint, 1);
					*iaux = gtk_text_iter_get_line(&it);
					g_hash_table_insert(ret, iaux, g_strdup(mark->is_temp ? "1" : "0"));
				}
			}
			cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter);
		} /* cont */
		return ret;
	}
	return NULL;
}

/* this function will simply add the bookmark as defined in the arguments
 *
 * will use offset if itoffset is NULL
 * will use itoffset if not NULL
 */
static void bmark_add_backend(Tdocument *doc, GtkTextIter *itoffset, gint offset, const gchar *name, const gchar *text, gboolean is_temp) {
	Tbmark *m;
	gchar *displaytext = NULL;
	GtkTextIter it;
	m = g_new0(Tbmark, 1);
	m->doc = doc;
	
	if (itoffset) {
		it = *itoffset;
		m->offset = gtk_text_iter_get_offset(&it);
	} else {
		gtk_text_buffer_get_iter_at_offset(doc->buffer,&it,offset);
		m->offset = offset;
	}
	
	m->mark = gtk_text_buffer_create_mark(doc->buffer, NULL, &it, TRUE);
	gnome_vfs_uri_ref(doc->uri);
	m->filepath = doc->uri;
	m->is_temp = is_temp;
	m->text = g_strdup(text);
	m->name = (name) ? g_strdup(name) : g_strdup("");
	m->description = g_strdup("");
	
	/* insert into tree */
	bmark_get_iter_at_tree_position(doc->bfwin, m);
	displaytext = bmark_display_text(m->name, m->text);
	gtk_tree_store_set(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore, &m->iter, NAME_COLUMN, displaytext, PTR_COLUMN, m, -1);
	g_free (displaytext);
	
	/* and store */
	if (!m->is_temp) {
		bmark_store(BFWIN(doc->bfwin), m);
	}
#ifdef USE_SCANNER
	bf_textview_set_symbol(BF_TEXTVIEW(doc->view),"bmark",gtk_text_iter_get_line(&it)+1,TRUE);
	gtk_widget_queue_draw(doc->view);
#endif	
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
	DEBUG_MSG("bmark_add_backend, adding bookmark at offset=%d for bfwin=%p\n",offset,bfwin);
	/* create bookmark */
	gtk_text_buffer_get_iter_at_offset(DOCUMENT(bfwin->current_document)->buffer,&it,offset);
	/* if there is a selection, and the offset is within the selection, we'll use it as text content */
	if (gtk_text_buffer_get_selection_bounds(DOCUMENT(bfwin->current_document)->buffer,&sit,&eit) 
				&& gtk_text_iter_in_range(&it,&sit,&eit)) {
		gchar *text = gtk_text_iter_get_text(&sit, &eit);
		bmark_add_backend(DOCUMENT(bfwin->current_document), &sit, offset, name, text, is_temp);
		g_free(text);
		
	} else {
		gchar *text;
		text = bmark_text_for_offset(DOCUMENT(bfwin->current_document), &it, offset);
		bmark_add_backend(DOCUMENT(bfwin->current_document), &it, offset, name, text, is_temp);
		g_free(text);
	}
	if (bfwin->bmark) {
		/* only if there is a left panel we should do this */
		gtk_tree_view_expand_all(bfwin->bmark);
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
#ifdef DEBUG
	{
		gchar *tmp = gtk_text_buffer_get_text(doc->buffer, &sit,&eit,FALSE);
		DEBUG_MSG("bmark_get_bmark_at_line, searching bookmarks at line %d between offsets %d - %d --> '%s'\n",linenum,gtk_text_iter_get_offset(&sit),gtk_text_iter_get_offset(&eit),tmp);
		g_free(tmp);
	}
#endif
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

void bmark_add(Tbfwin * bfwin) {
	GtkTextMark *im;
	GtkTextIter it;
	gint offset;
	/* check for unnamed document */
	if (!DOCUMENT(bfwin->current_document)->uri) {
		message_dialog_new(bfwin->main_window, 
								 GTK_MESSAGE_ERROR, 
								 GTK_BUTTONS_CLOSE, 
								 _("Error adding bookmark"), 
								 _("Cannot add bookmarks to unsaved files."));
		/*Please save the file first. A Save button in this dialog would be cool -- Alastair*/
		return;
	}
	/* if the left panel is disabled, we simply should add the bookmark to the list, and do nothing else */
/*	if (bfwin->bmark == NULL) {
		DEBUG_MSG("no left panel, this is not implemented yet\n");
	} else */ {
		gboolean has_mark;
		im = gtk_text_buffer_get_insert(DOCUMENT(bfwin->current_document)->buffer);
		gtk_text_buffer_get_iter_at_mark(DOCUMENT(bfwin->current_document)->buffer, &it, im);
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
}

gboolean bmark_have_bookmark_at_stored_bevent(Tdocument * doc) {
	if (BMARKDATA(main_v->bmarkdata)->bevent_doc == doc) {
		return (bmark_get_bmark_at_line(doc, BMARKDATA(main_v->bmarkdata)->bevent_charoffset) != NULL);
	}
	return FALSE;
}

void bmark_store_bevent_location(Tdocument * doc, gint charoffset) {
	DEBUG_MSG("bmark_store_bevent_location, storing offset=%d for doc=%p\n",charoffset,doc);
	BMARKDATA(main_v->bmarkdata)->bevent_doc = doc;
	BMARKDATA(main_v->bmarkdata)->bevent_charoffset = charoffset;
}

void bmark_del_at_bevent(Tdocument *doc) {
	if (BMARKDATA(main_v->bmarkdata)->bevent_doc == doc) {
		Tbmark *b = bmark_get_bmark_at_line(doc, BMARKDATA(main_v->bmarkdata)->bevent_charoffset);
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
	if (BMARKDATA(main_v->bmarkdata)->bevent_doc == doc) {
		gint offset = BMARKDATA(main_v->bmarkdata)->bevent_charoffset;
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
#ifdef DEBUG
		gchar *name;
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter, NAME_COLUMN,&name, -1);
		DEBUG_MSG("bmark_del_all, the toplevel has child '%s'\n", name);
#endif
		bmark_del_children_backend(bfwin, &tmpiter);
	}
	gtk_widget_grab_focus(bfwin->current_document->view);
}	

void bmark_check_length(Tbfwin * bfwin, Tdocument * doc) {
	GtkTreeIter tmpiter;
	gboolean cont;
	if (!doc || !doc->bmark_parent) {
		DEBUG_MSG("bmark_check_length, no bmark_parent iter => no bookmarks, returning\n");
		return;
	}
	DEBUG_MSG("bmark_check_length, doc %p, filename %s\n\n", doc, gtk_label_get_text(GTK_LABEL(doc->tab_menu)));

	cont =
		gtk_tree_model_iter_children(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter,
									 doc->bmark_parent);
	while (cont) {
		Tbmark *mark = NULL;
		gtk_tree_model_get(GTK_TREE_MODEL(BMARKDATA(BFWIN(doc->bfwin)->bmarkdata)->bookmarkstore), &tmpiter, PTR_COLUMN,
						   &mark, -1);
		if (mark) {
			glong size;
			size = doc->fileinfo->size;
			DEBUG_MSG("bmark_check_length, bmark has %d, file has %ld\n",mark->len, size);
			if (mark->len != size) {
				gint retval;
				const gchar *buttons[] = {GTK_STOCK_NO, GTK_STOCK_YES, NULL};
				gchar *str;
				str = g_strconcat(_("File size changed in file \n"),gtk_label_get_text(GTK_LABEL(doc->tab_menu)),NULL);
				retval = message_dialog_new_multi(bfwin->main_window,
															 GTK_MESSAGE_QUESTION,
															 buttons,
															 _("Bookmarks positions could be incorrect. Delete bookmarks?"),
															 str);
				if (retval==1) {
					bmark_del_for_document(bfwin, doc);
				} else {
					mark->len = size;
				}
				return;
			}
		} else {
			DEBUG_MSG("bmark_check_length, NOT GOOD no mark in the treestore??\n");
		}
		cont = gtk_tree_model_iter_next(GTK_TREE_MODEL(BMARKDATA(bfwin->bmarkdata)->bookmarkstore), &tmpiter);
	}
	DEBUG_MSG("bmark_check_length, all bookmarks OK, returning\n");
}

void bmark_cleanup(Tbfwin * bfwin) {
	DEBUG_MSG("bmark_cleanup, cleanup for bfwin=%p\n",bfwin);
	bfwin->bmark = NULL;
}
