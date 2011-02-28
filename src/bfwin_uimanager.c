/*
 * Bluefish HTML Editor
 * bfwin_uimanager.c
 *
 * Copyright (C) 2011 James Hayward and Olivier Sessink
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */


#include "bfwin_uimanager.h"
#include "bfwin.h"
#include "bftextview2.h"
#include "blocksync.h"
#include "bookmark.h"
#include "document.h"
#include "doc_comments.h"
#include "doc_text_tools.h"
#include "encodings_dialog.h"
#include "file_dialogs.h"
#include "preferences.h"
#include "project.h"
#include "snr2.h"
#include "undo_redo.h"


#define MAIN_MENU_UI PKGDATADIR"/ui/bluefish_menu_ui.xml"
#define MAIN_TOOLBAR_UI PKGDATADIR"/ui/bluefish_toolbar_ui.xml"
/* For testing purposes */
/*#define MAIN_MENU_UI "ui/bluefish_menu_ui.xml"*/
/*#define MAIN_TOOLBAR_UI "ui/bluefish_toolbar_ui.xml"*/


/* document action callbacks */

static void
ui_autocompletion_popup_show(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_auto_complete(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
											 gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_bookmark_add(GtkAction * action, gpointer user_data)
{
	bmark_add(BFWIN(user_data));
}

static void
ui_encoding_add_remove(GtkAction * action, gpointer user_data)
{
	bluefish_encodings_dialog_new(BFWIN(user_data));
}

static void
ui_floating_window(GtkAction * action, gpointer user_data)
{
	doc_floating_view_new(BFWIN(user_data));
}

static void
ui_font_size_decrease(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_font_size(bfwin->current_document, -1);
}

static void
ui_font_size_increase(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_font_size(bfwin->current_document, 1);
}

static void
ui_font_size_reset(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_font_size(bfwin->current_document, 0);
}

static void
ui_highlighting_toggle(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_toggle_highlighting(bfwin, gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_highlighting_update(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_update_highlighting(bfwin, 0, NULL);
}

static void
ui_move_tab_left(GtkAction * action, gpointer user_data)
{
	bfwin_notebook_switch(BFWIN(user_data), 5);
}

static void
ui_move_tab_right(GtkAction * action, gpointer user_data)
{
	bfwin_notebook_switch(BFWIN(user_data), 6);
}

static void
ui_set_autoindent(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_auto_indent(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
										   gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_set_highlight_block_delimiters(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_show_mbhl(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
										 gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

#ifdef HAVE_LIBENCHANT
static void
ui_set_spell_check(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_spell_check(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
										   gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}
#endif

static void
ui_set_wrap_text(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document) {
		bfwin->current_document->wrapstate = gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action));
		doc_set_wrap(bfwin->current_document);
	}
}

static void
ui_show_blocks(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_show_blocks(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
										   gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_show_line_numbers(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_show_line_numbers(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
												 gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_show_right_margin(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_show_right_margin(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
												 gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_show_split_view(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_split_view(CURDOC(bfwin), gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_show_visible_spacing(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		bluefish_text_view_set_show_visible_spacing(BLUEFISH_TEXT_VIEW(bfwin->current_document->view),
													gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_tab_size_decrease(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_change_tabsize(bfwin->current_document, -1);
}

static void
ui_tab_size_increase(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_change_tabsize(bfwin->current_document, 1);
}

static void
ui_tab_size_reset(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_change_tabsize(bfwin->current_document, 0);
}

/* edit action callbacks */

static void
ui_redo(GtkAction * action, gpointer user_data)
{
	redo(BFWIN(user_data));
}

static void
ui_redo_all(GtkAction * action, gpointer user_data)
{
	redo_all(BFWIN(user_data));
}

static void
ui_undo(GtkAction * action, gpointer user_data)
{
	undo(BFWIN(user_data));
}

static void
ui_undo_all(GtkAction * action, gpointer user_data)
{
	undo_all(BFWIN(user_data));
}

static void
ui_copy(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_copy(bfwin);
}

static void
ui_cut(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_cut(bfwin);
}

static void
ui_paste(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_paste(bfwin);
}

static void
ui_find(GtkAction * action, gpointer user_data)
{
	snr_dialog_new(BFWIN(user_data), BF_FIND_DIALOG);
}

static void
ui_find_again(GtkAction * action, gpointer user_data)
{
	search_again(BFWIN(user_data));
}

static void
ui_find_from_selection(GtkAction * action, gpointer user_data)
{
	search_from_selection(BFWIN(user_data));
}

static void
ui_replace(GtkAction * action, gpointer user_data)
{
	snr_dialog_new(BFWIN(user_data), BF_REPLACE_DIALOG);
}

static void
ui_replace_again(GtkAction * action, gpointer user_data)
{
	replace_again(BFWIN(user_data));
}

static void
ui_indent(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_indent_selection(bfwin->current_document, FALSE);
}

static void
ui_unindent(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_indent_selection(bfwin->current_document, TRUE);
}

static void
ui_preferences(GtkAction * action, gpointer user_data)
{
	preferences_dialog_new();
}

static void
ui_select_all(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_select_all(bfwin);
}

/* file action callbacks */

static void
ui_file_close(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_close_single_backend(bfwin->current_document, FALSE, FALSE);
}

static void
ui_file_close_all(GtkAction * action, gpointer user_data)
{
	file_close_all(BFWIN(user_data));
}

static void
ui_file_reload(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_reload(bfwin->current_document, NULL, TRUE);
}

static void
ui_file_rename(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_save_backend(bfwin->current_document, TRUE, TRUE, FALSE, FALSE);
}

static void
ui_file_save(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_save_backend(bfwin->current_document, FALSE, FALSE, FALSE, FALSE);
}

static void
ui_file_save_all(GtkAction * action, gpointer user_data)
{
	file_save_all(BFWIN(user_data));
}

static void
ui_file_save_as(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_save_backend(bfwin->current_document, TRUE, FALSE, FALSE, FALSE);
}

static void
ui_insert_doc(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		file_insert_doc(bfwin);
}

static void
ui_new_doc(GtkAction * action, gpointer user_data)
{
	file_new_doc(BFWIN(user_data));
}

static void
ui_new_window(GtkAction * action, gpointer user_data)
{
	bfwin_window_new();
}

static void
ui_window_close(GtkAction * action, gpointer user_data)
{
	bfwin_window_close(BFWIN(user_data));
}

static void
ui_open_advanced(GtkAction * action, gpointer user_data)
{
	files_advanced_win(BFWIN(user_data), NULL);
}

static void
ui_open_doc(GtkAction * action, gpointer user_data)
{
	file_open_doc(BFWIN(user_data));
}

static void
ui_open_from_selection(GtkAction * action, gpointer user_data)
{
	file_open_from_selection(BFWIN(user_data));
}

static void
ui_quit(GtkAction * action, gpointer user_data)
{
	bluefish_exit_request();
}

/* Go action callbacks */
static void
ui_bookmark_first(GtkAction * action, gpointer user_data)
{
	bookmark_navigate(BFWIN(user_data), 1);
}

static void
ui_bookmark_last(GtkAction * action, gpointer user_data)
{
	bookmark_navigate(BFWIN(user_data), 4);
}

static void
ui_bookmark_next(GtkAction * action, gpointer user_data)
{
	bookmark_navigate(BFWIN(user_data), 3);
}

static void
ui_bookmark_previous(GtkAction * action, gpointer user_data)
{
	bookmark_navigate(BFWIN(user_data), 2);
}

static void
ui_doc_first(GtkAction * action, gpointer user_data)
{
	bfwin_notebook_switch(BFWIN(user_data), 3);
}

static void
ui_doc_last(GtkAction * action, gpointer user_data)
{
	bfwin_notebook_switch(BFWIN(user_data), 4);
}

static void
ui_doc_next(GtkAction * action, gpointer user_data)
{
	bfwin_notebook_switch(BFWIN(user_data), 2);
}

static void
ui_doc_previous(GtkAction * action, gpointer user_data)
{
	bfwin_notebook_switch(BFWIN(user_data), 1);
}

static void
ui_goto_line(GtkAction * action, gpointer user_data)
{
	bfwin_gotoline_frame_show(BFWIN(user_data));
}

static void
ui_jump_to_reference(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_jump(bfwin->current_document);
}

/* project action callbacks */

static void
ui_project_close(GtkAction * action, gpointer user_data)
{
	project_save_and_close(BFWIN(user_data));
}

static void
ui_project_edit_options(GtkAction * action, gpointer user_data)
{
	project_edit(BFWIN(user_data));
}

static void
ui_project_new(GtkAction * action, gpointer user_data)
{
	project_new(BFWIN(user_data));
}

static void
ui_project_open(GtkAction * action, gpointer user_data)
{
	project_open(BFWIN(user_data));
}

static void
ui_project_save(GtkAction * action, gpointer user_data)
{
	project_save(BFWIN(user_data), FALSE);
}

static void
ui_project_save_as(GtkAction * action, gpointer user_data)
{
	project_save(BFWIN(user_data), TRUE);
}

/* tools action callbacks */

static void
ui_indenting_to_spaces(GtkAction * action, gpointer user_data)
{
	convert_identing(BFWIN(user_data)->current_document, FALSE);
}

static void
ui_indenting_to_tabs(GtkAction * action, gpointer user_data)
{
	convert_identing(BFWIN(user_data)->current_document, TRUE);
}

static void
ui_join_lines(GtkAction * action, gpointer user_data)
{
	join_lines(BFWIN(user_data)->current_document);
}

static void
ui_merge_lines(GtkAction * action, gpointer user_data)
{
	convert_to_columns(BFWIN(user_data)->current_document);
}

static void
ui_rewrap_lines(GtkAction * action, gpointer user_data)
{
	rewrap_lines(BFWIN(user_data)->current_document);
}

static void
ui_split_lines(GtkAction * action, gpointer user_data)
{
	split_lines(BFWIN(user_data)->current_document);
}

static void
ui_strip_trailing_whitespace(GtkAction * action, gpointer user_data)
{
	strip_trailing_spaces(BFWIN(user_data)->current_document);
}

static void
ui_synch_text_block(GtkAction * action, gpointer user_data)
{
	blocksync_dialog(BFWIN(user_data));
}

static void
ui_toggle_comment(GtkAction * action, gpointer user_data)
{
	toggle_comment(BFWIN(user_data)->current_document);
}

static void
ui_word_count(GtkAction * action, gpointer user_data)
{
	Tbfwin *bfwin = BFWIN(user_data);

	if (bfwin->current_document)
		doc_word_count(bfwin);
}

/* view action callbacks */

static void
ui_fullscreen_toggle(GtkAction * action, gpointer user_data)
{
	gboolean active = gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action));

	/* TODO: This only works when "use-action-appearance" is TRUE. TRUE is default */
	gtk_action_set_stock_id(action, (active ? GTK_STOCK_LEAVE_FULLSCREEN : GTK_STOCK_FULLSCREEN));
	gtk_action_set_tooltip(action, (active ? _("Leave Fullscreen") : _("Fullscreen")));
	bfwin_fullscreen_toggle(BFWIN(user_data), active);
}

static void
ui_main_toolbar_show(GtkAction * action, gpointer user_data)
{
	bfwin_main_toolbar_show(BFWIN(user_data), gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_output_pane_show(GtkAction * action, gpointer user_data)
{
	bfwin_output_pane_show(BFWIN(user_data), gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_side_pane_show(GtkAction * action, gpointer user_data)
{
	bfwin_side_panel_show(BFWIN(user_data), gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static void
ui_statusbar_show(GtkAction * action, gpointer user_data)
{
	bfwin_statusbar_show(BFWIN(user_data), gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action)));
}

static const GtkActionEntry top_level_menus[] = {
	{"FileMenu", NULL, N_("_File")},
	{"NewFromTemplate", NULL, N_("New From Template")},
	{"FileOpenRecent", NULL, N_("Open _Recent")},
	{"EditMenu", NULL, N_("_Edit")},
	{"ViewMenu", NULL, N_("_View")},
	{"DocumentMenu", NULL, N_("_Document")},
	{"DocumentTabSize", NULL, N_("_Tab Size")},
	{"DocumentFontSize", NULL, N_("_Font Size")},
	{"DocumentLangMode", NULL, N_("Language _Mode")},
	{"DocumentEncoding", NULL, N_("Character _Encoding")},
	{"GoMenu", NULL, N_("_Go")},
	{"ProjectMenu", NULL, N_("_Project")},
	{"ProjectOpenRecent", NULL, N_("Open _Recent")},
	{"ToolsMenu", NULL, N_("_Tools")},
	{"ToolsCommands", NULL, N_("_Commands")},
	{"ToolsFilters", NULL, N_("_Filters")},
	{"ToolsOutputBox", NULL, N_("_Outputbox")}
};

static const GtkActionEntry global_actions[] = {
	{"FileNew", GTK_STOCK_NEW, N_("_New"), "<control>N", N_("New file"), G_CALLBACK(ui_new_doc)},
	{"FileNewWindow", NULL, N_("New _Window"), "<shift><control>N", N_("New window"),
	 G_CALLBACK(ui_new_window)},
	{"FileCloseWindow", NULL, N_("Close Win_dow"), NULL, N_("Close window"), G_CALLBACK(ui_window_close)},
	{"FileOpen", GTK_STOCK_OPEN, N_("_Open..."), "<control>O", N_("Open file"), G_CALLBACK(ui_open_doc)},
	{"FileOpenAdvanced", NULL, N_("Open Ad_vanced..."), "<shift><control>O", N_("Open advanced"),
	 G_CALLBACK(ui_open_advanced)},
#ifndef WIN32
#ifndef MAC_INTEGRATION
	{"FileOpenUrl", NULL, N_("Open _URL..."), NULL, N_("Open URL"), NULL},
#endif							/* MAC_INTEGRATION */
#endif							/* WIN32 */
	{"FileOpenSelection", NULL, N_("Open _From Selection"), NULL, N_("Open From Selection"),
	 G_CALLBACK(ui_open_from_selection)},
	{"FileInsert", NULL, N_("_Insert..."), NULL, N_("Insert file"), G_CALLBACK(ui_insert_doc)},
	{"FileQuit", GTK_STOCK_QUIT, N_("_Quit"), "<control>Q", N_("Quit Bluefish"), G_CALLBACK(ui_quit)},
	{"EditPreferences", GTK_STOCK_PREFERENCES, N_("Preference_s..."), NULL, N_("Edit Preferences"),
	 G_CALLBACK(ui_preferences)},
	{"EditSelectAll", NULL, N_("Select _All"), "<control>A", N_("Select all"), G_CALLBACK(ui_select_all)},
	{"DocAddBookmark", NULL, N_("Add Boo_kmark"), "<control>k", N_("Add bookmark"),
	 G_CALLBACK(ui_bookmark_add)},
	{"DocEncodingAddRemove", NULL, N_("_Add or Remove..."), NULL, N_("Add or remove character encoding"),
	 G_CALLBACK(ui_encoding_add_remove)},
	{"DocFloatingWindow", NULL, N_("Floati_ng Window"), NULL, N_("Open current document in floating window"),
	 G_CALLBACK(ui_floating_window)},
	{"DocFontSizeIncrease", NULL, N_("_Increase"), NULL, N_("Increase document font size"),
	 G_CALLBACK(ui_font_size_increase)},
	{"DocFontSizeDecrease", NULL, N_("_Decrease"), NULL, N_("Decrease document font size"),
	 G_CALLBACK(ui_font_size_decrease)},
	{"DocFontSizeReset", NULL, N_("_Reset"), NULL, N_("Reset document font size"),
	 G_CALLBACK(ui_font_size_reset)},
	{"DocTabSizeIncrease", NULL, N_("_Increase"), NULL, N_("Increase document tab size"),
	 G_CALLBACK(ui_tab_size_increase)},
	{"DocTabSizeDecrease", NULL, N_("_Decrease"), NULL, N_("Decrease document tab size"),
	 G_CALLBACK(ui_tab_size_decrease)},
	{"DocTabSizeReset", NULL, N_("_Reset"), NULL, N_("Reset document tab size"),
	 G_CALLBACK(ui_tab_size_reset)},
#ifdef HAVE_LIBENCHANT
	{"DocumentRescan", NULL, N_("_Rescan Syntax & Spelling"), "F5", N_("Rescan document highlighting"),
	 G_CALLBACK(ui_highlighting_update)},
#else
	{"DocumentRescan", NULL, N_("_Rescan Syntax"), "F5", N_("Rescan document highlighting"),
	 G_CALLBACK(ui_highlighting_update)},
#endif
	{"ProjectNew", NULL, N_("_New Project..."), NULL, N_("New project"), G_CALLBACK(ui_project_new)},
	{"ProjectOpen", NULL, N_("_Open Project..."), NULL, N_("Open project"), G_CALLBACK(ui_project_open)},
	{"SynchTextBlock", NULL, N_("_Synchronize Text Block"), NULL, N_("Synchronize text block"),
	 G_CALLBACK(ui_synch_text_block)},
	{"ToggleComment", NULL, N_("Toggle _Comment"), "<shift><control>C", N_("Toggle comment"),
	 G_CALLBACK(ui_toggle_comment)},
	{"WordCount", NULL, N_("_Word Count"), NULL, N_("Word count"), G_CALLBACK(ui_word_count)},
	{"IndentingToSpaces", NULL, N_("Indenting To S_paces"), NULL, N_("Indenting to spaces"),
	 G_CALLBACK(ui_indenting_to_spaces)},
	{"IndentingToTabs", NULL, N_("Indenting To T_abs"), NULL, N_("Indenting to tabs"),
	 G_CALLBACK(ui_indenting_to_tabs)},
	{"JoinLines", NULL, N_("_Join Lines Together"), NULL, N_("Join lines together"),
	 G_CALLBACK(ui_join_lines)},
	{"SplitLines", NULL, N_("Sp_lit Lines on Right Margin"), NULL, N_("Split lines on right margin"),
	 G_CALLBACK(ui_split_lines)},
	{"MergeLinesIntoColumns", NULL, N_("Merge Lines Into Col_umns"), NULL, N_("Merge lines into columns"),
	 G_CALLBACK(ui_merge_lines)},
	{"RewrapLines", NULL, N_("Rewrap _Lines"), NULL, N_("Rewrap lines"), G_CALLBACK(ui_rewrap_lines)},
	{"StripTrailingWhitespace", NULL, N_("Strip T_railing Whitespace"), NULL, N_("Strip trailing whitespace"),
	 G_CALLBACK(ui_strip_trailing_whitespace)}
};

static const GtkToggleActionEntry global_toggle_actions[] = {
	{"ViewFullScreen", GTK_STOCK_FULLSCREEN, N_("_Full Screen"), "F11", N_("Full screen"),
	 G_CALLBACK(ui_fullscreen_toggle), FALSE},
	{"ViewMainToolbar", NULL, N_("_Main Toolbar"), NULL, N_("Show main toolbar"),
	 G_CALLBACK(ui_main_toolbar_show), TRUE},
	{"ViewSidePane", NULL, N_("_Side Pane"), "F9", N_("Show side pane"), G_CALLBACK(ui_side_pane_show), TRUE},
	{"ViewOutputPane", NULL, N_("_Output Pane"), "<control>F9", N_("Show output pane"),
	 G_CALLBACK(ui_output_pane_show), FALSE},
	{"ViewStatusbar", NULL, N_("Status_bar"), NULL, N_("Show statusbar"), G_CALLBACK(ui_statusbar_show),
	 TRUE},
	{"AutoCompletion", NULL, N_("Auto _Completion Popup"), NULL, N_("Show auto completion popup"),
	 G_CALLBACK(ui_autocompletion_popup_show), TRUE},
	{"AutoIndent", NULL, N_("_Auto Indent"), NULL, N_("Auto indent"), G_CALLBACK(ui_set_autoindent), TRUE},
	{"HighlightBlockDelimiters", NULL, N_("Highlight Block Delimiters"), NULL,
	 N_("Highlight block delimiters"), G_CALLBACK(ui_set_highlight_block_delimiters), TRUE},
	{"HighlightSyntax", NULL, N_("_Highlight Syntax"), NULL, N_("Highlight syntax"),
	 G_CALLBACK(ui_highlighting_toggle), TRUE},
	{"ShowBlocks", NULL, N_("Show _Blocks"), NULL, N_("Show blocks"), G_CALLBACK(ui_show_blocks), TRUE},
	{"ShowLineNumbers", NULL, N_("Show _Line Numbers"), NULL, N_("Show line numbers"),
	 G_CALLBACK(ui_show_line_numbers), TRUE},
	{"ShowRightMargin", NULL, N_("Show Right Margin"), NULL, N_("Show right margin"),
	 G_CALLBACK(ui_show_right_margin), FALSE},
	{"ShowSplitView", NULL, N_("Show Split View"), NULL, N_("Show split view"),
	 G_CALLBACK(ui_show_split_view), FALSE},
	{"ShowVisibleSpacing", NULL, N_("Show _Visible Spacing"), NULL, N_("Show visible spacing"),
	 G_CALLBACK(ui_show_visible_spacing), FALSE},
#ifdef HAVE_LIBENCHANT
	{"SpellCheck", NULL, N_("_Spell Check"), NULL, N_("Spell check"), G_CALLBACK(ui_set_spell_check), TRUE},
#endif
	{"WrapText", NULL, N_("_Wrap Text"), NULL, N_("Wrap text"), G_CALLBACK(ui_set_wrap_text), FALSE}
};

static const GtkActionEntry document_actions[] = {
	{"FileClose", GTK_STOCK_CLOSE, N_("_Close"), "<control>W", N_("Close current file"),
	 G_CALLBACK(ui_file_close)},
	{"FileCloseAll", NULL, N_("Close A_ll"), "<shift><control>W", N_("Close all files"),
	 G_CALLBACK(ui_file_close_all)},
	{"FileSave", GTK_STOCK_SAVE, N_("_Save"), "<control>S", N_("Save current file"),
	 G_CALLBACK(ui_file_save)},
	{"FileSaveAs", GTK_STOCK_SAVE_AS, N_("Save _As..."), "<shift><control>S", N_("Save file as"),
	 G_CALLBACK(ui_file_save_as)},
	{"FileSaveAll", NULL, N_("Sav_e All"), NULL, N_("Save all files"), G_CALLBACK(ui_file_save_all)},
	{"FileRevert", GTK_STOCK_REVERT_TO_SAVED, N_("Rever_t to Saved"), NULL, N_("Reload current file"),
	 G_CALLBACK(ui_file_reload)},
	{"FileRename", NULL, N_("Rena_me..."), "F2", N_("Rename file"), G_CALLBACK(ui_file_rename)},
	{"EditIndent", GTK_STOCK_INDENT, N_("_Indent"), NULL, N_("Indent"), G_CALLBACK(ui_indent)},
	{"EditUnindent", GTK_STOCK_UNINDENT, N_("_Unindent"), NULL, N_("Unindent"), G_CALLBACK(ui_unindent)},
	{"BookmarkFirst", GTK_STOCK_GOTO_TOP, N_("F_irst Bookmark"), NULL, N_("Goto first bookmark"),
	 G_CALLBACK(ui_bookmark_first)},
	{"BookmarkLast", GTK_STOCK_GOTO_BOTTOM, N_("_Last Bookmark"), NULL, N_("Goto last bookmark"),
	 G_CALLBACK(ui_bookmark_last)},
	{"BookmarkNext", GTK_STOCK_GO_DOWN, N_("N_ext Bookmark"), "<shift><control>K", N_("Goto next bookmark"),
	 G_CALLBACK(ui_bookmark_next)},
	{"BookmarkPrevious", GTK_STOCK_GO_UP, N_("P_revious Bookmark"), "<shift><control>J",
	 N_("Goto previous document"), G_CALLBACK(ui_bookmark_previous)},
	{"DocMoveLeft", NULL, N_("Move Tab Left"), NULL, N_("Move current tab left"),
	 G_CALLBACK(ui_move_tab_left)},
	{"DocMoveRight", NULL, N_("Move Tab Right"), NULL, N_("Move current tab right"),
	 G_CALLBACK(ui_move_tab_right)},
	{"DocFirst", GTK_STOCK_GOTO_FIRST, N_("_First Document"), NULL, N_("Goto first document"),
	 G_CALLBACK(ui_doc_first)},
	{"DocLast", GTK_STOCK_GOTO_LAST, N_("L_ast Document"), NULL, N_("Goto last document"),
	 G_CALLBACK(ui_doc_last)},
	{"DocNext", GTK_STOCK_GO_FORWARD, N_("_Next Document"), NULL, N_("Goto next document"),
	 G_CALLBACK(ui_doc_next)},
	{"DocPrevious", GTK_STOCK_GO_BACK, N_("_Previous Document"), NULL, N_("Goto previous document"),
	 G_CALLBACK(ui_doc_previous)},
	{"GotoLine", NULL, N_("Goto _Line"), "<control>L", N_("Goto line"), G_CALLBACK(ui_goto_line)},
	{"GotoLineSelection", NULL, N_("Goto Line Number in _Selection"), "<shift><control>L",
	 N_("Goto line number in selection"), NULL},
	{"JumpToReference", NULL, N_("Jump to Reference"), "<control>J", N_("Jump to reference"),
	 G_CALLBACK(ui_jump_to_reference)}
};

static const GtkActionEntry edit_actions[] = {
	{"Cut", GTK_STOCK_CUT, N_("Cu_t"), "<control>X", N_("Cut"), G_CALLBACK(ui_cut)},
	{"Copy", GTK_STOCK_COPY, N_("_Copy"), "<control>C", N_("Copy"), G_CALLBACK(ui_copy)},
	{"Paste", GTK_STOCK_PASTE, N_("_Paste"), "<control>V", N_("Paste"), G_CALLBACK(ui_paste)}
};

static const GtkActionEntry find_replace_actions[] = {
	{"Find", GTK_STOCK_FIND, N_("_Find..."), "<control>F", N_("Find"), G_CALLBACK(ui_find)},
	{"FindAgain", NULL, N_("Find A_gain"), "<control>G", N_("Find again"), G_CALLBACK(ui_find_again)},
	{"FindSelection", NULL, N_("Find From Selection"), "<shift><control>F", N_("Find from selection"),
	 G_CALLBACK(ui_find_from_selection)},
	{"Replace", GTK_STOCK_FIND_AND_REPLACE, N_("R_eplace..."), "<control>H", N_("Replace"),
	 G_CALLBACK(ui_replace)},
	{"ReplaceAgain", NULL, N_("Replace Agai_n"), "<shift><control>H", N_("Replace again"),
	 G_CALLBACK(ui_replace_again)}
};

static const GtkActionEntry project_actions[] = {
	{"ProjectClose", NULL, N_("Save & _close"), NULL, N_("Save & close project"),
	 G_CALLBACK(ui_project_close)},
	{"ProjectSave", NULL, N_("_Save"), NULL, N_("Save project"), G_CALLBACK(ui_project_save)},
	{"ProjectSaveAs", NULL, N_("Save _as..."), NULL, N_("Save project as"), G_CALLBACK(ui_project_save_as)},
	{"ProjectEditOptions", NULL, N_("E_dit Project Options..."), NULL, N_("Edit project options"),
	 G_CALLBACK(ui_project_edit_options)},
};

static const GtkActionEntry undo_redo_actions[] = {
	{"Undo", GTK_STOCK_UNDO, N_("_Undo"), "<control>Z", N_("Undo"), G_CALLBACK(ui_undo)},
	{"UndoAll", NULL, N_("Undo All"), NULL, N_("Undo All"), G_CALLBACK(ui_undo_all)},
	{"Redo", GTK_STOCK_REDO, N_("_Redo"), "<shift><control>Z", N_("Redo"), G_CALLBACK(ui_redo)},
	{"RedoAll", NULL, N_("Redo All"), NULL, N_("Redo All"), G_CALLBACK(ui_redo_all)}
};

void
bfwin_main_menu_init(Tbfwin * bfwin, GtkWidget * vbox)
{
	GtkActionGroup *action_group;
	GtkUIManager *manager;
	GtkWidget *menubar;

	GError *error = NULL;

	manager = bfwin->uimanager;

	gtk_window_add_accel_group(GTK_WINDOW(bfwin->main_window), gtk_ui_manager_get_accel_group(manager));

	action_group = gtk_action_group_new("topLevelMenus");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, top_level_menus, G_N_ELEMENTS(top_level_menus), bfwin);
	gtk_ui_manager_insert_action_group(manager, action_group, 0);
	g_object_unref(action_group);

	action_group = gtk_action_group_new("GlobalActions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, global_actions, G_N_ELEMENTS(global_actions), bfwin);
	gtk_action_group_add_toggle_actions(action_group, global_toggle_actions,
										G_N_ELEMENTS(global_toggle_actions), bfwin);
	gtk_ui_manager_insert_action_group(manager, action_group, 0);
	g_object_unref(action_group);
	bfwin->globalGroup = action_group;

	action_group = gtk_action_group_new("DocumentActions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, document_actions, G_N_ELEMENTS(document_actions), bfwin);
	gtk_ui_manager_insert_action_group(manager, action_group, 0);
	g_object_unref(action_group);
	bfwin->documentGroup = action_group;

	action_group = gtk_action_group_new("EditActions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, edit_actions, G_N_ELEMENTS(edit_actions), bfwin);
	gtk_ui_manager_insert_action_group(manager, action_group, 0);
	g_object_unref(action_group);
	bfwin->editGroup = action_group;

	action_group = gtk_action_group_new("FindReplaceActions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, find_replace_actions, G_N_ELEMENTS(find_replace_actions),
								 bfwin);
	gtk_ui_manager_insert_action_group(manager, action_group, 0);
	g_object_unref(action_group);
	bfwin->findReplaceGroup = action_group;

	action_group = gtk_action_group_new("UndoRedoActions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, undo_redo_actions, G_N_ELEMENTS(undo_redo_actions), bfwin);
	gtk_ui_manager_insert_action_group(manager, action_group, 0);
	g_object_unref(action_group);
	bfwin->undoGroup = action_group;

	action_group = gtk_action_group_new("ProjectActions");
	gtk_action_group_set_translation_domain(action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions(action_group, project_actions, G_N_ELEMENTS(project_actions), bfwin);
	gtk_ui_manager_insert_action_group(manager, action_group, 0);
	g_object_unref(action_group);
	bfwin->projectGroup = action_group;

	gtk_ui_manager_add_ui_from_file(manager, MAIN_MENU_UI, &error);
	if (error != NULL) {
		g_warning("building main menu failed: %s", error->message);
		g_error_free(error);
	}

	menubar = gtk_ui_manager_get_widget(manager, "/MainMenu");
	gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
	gtk_widget_show(menubar);

	set_project_menu_actions(bfwin, FALSE);
}

void
bfwin_main_toolbar_init(Tbfwin * bfwin)
{
	GtkUIManager *manager;
	GtkWidget *toolbar;
	GError *error = NULL;

	manager = bfwin->uimanager;

	gtk_ui_manager_add_ui_from_file(manager, MAIN_TOOLBAR_UI, &error);
	if (error != NULL) {
		g_message("building main toolbar failed: %s", error->message);
		g_error_free(error);
	}

	toolbar = gtk_ui_manager_get_widget(manager, "/MainToolbar");
	gtk_toolbar_set_style(GTK_TOOLBAR(toolbar), GTK_TOOLBAR_ICONS);
	gtk_toolbar_set_icon_size(GTK_TOOLBAR(toolbar), GTK_ICON_SIZE_LARGE_TOOLBAR);
	gtk_container_add(GTK_CONTAINER(bfwin->main_toolbar_hb), toolbar);

	gtk_widget_show(toolbar);
}

void
bfwin_set_undo_redo_actions(Tbfwin * bfwin, gboolean undo, gboolean redo)
{
	GtkUIManager *manager = bfwin->uimanager;

	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/Undo", undo);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/UndoAll", undo);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/Redo", redo);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/RedoAll", redo);
}

void
bfwin_set_document_menu_items(Tdocument * doc)
{
	GtkUIManager *manager = BFWIN(doc->bfwin)->uimanager;

	bfwin_set_undo_redo_actions(doc->bfwin, doc_has_undo_list(doc), doc_has_redo_list(doc));

	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/AutoCompletion",
									 bluefish_text_view_get_auto_complete(BLUEFISH_TEXT_VIEW(doc->view)));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/AutoIndent",
									 bluefish_text_view_get_auto_indent(BLUEFISH_TEXT_VIEW(doc->view)));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/HighlightBlockDelimiters",
									 bluefish_text_view_get_show_mbhl(BLUEFISH_TEXT_VIEW(doc->view)));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/ShowBlocks",
									 bluefish_text_view_get_show_blocks(BLUEFISH_TEXT_VIEW(doc->view)));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/ShowLineNumbers",
									 bluefish_text_view_get_show_line_numbers(BLUEFISH_TEXT_VIEW(doc->view)));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/ShowRightMargin",
									 bluefish_text_view_get_show_right_margin(BLUEFISH_TEXT_VIEW(doc->view)));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/ShowVisibleSpacing",
									 bluefish_text_view_get_show_visible_spacing(BLUEFISH_TEXT_VIEW
																				 (doc->view)));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/ShowSplitView", (doc->slave != NULL));
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/WrapText", doc->wrapstate);
	setup_menu_toggle_item_from_path(manager, "/MainMenu/DocumentMenu/HighlightSyntax", doc->highlightstate);
#ifdef HAVE_LIBENCHANT
/*	setup_toggle_item(gtk_item_factory_from_widget(BFWIN(doc->bfwin)->menubar),
					  "/Document/Spell Check", BLUEFISH_TEXT_VIEW(doc->view)->spell_check);*/
#endif

/*#ifndef USE_SCANNER	why did we not set the encoding and filetype with the scanner enabled????*/
/*	menu_current_document_set_toggle_wo_activate(BFWIN(doc->bfwin), BLUEFISH_TEXT_VIEW(doc->view)->bflang,
												 doc->encoding); */

	/* we should also disable certain menu's if the document is readonly */
	bfwin_action_set_sensitive(manager, "/MainMenu/FileMenu/FileSave", !doc->readonly);
	bfwin_action_set_sensitive(manager, "/MainMenu/FileMenu/FileInsert", !doc->readonly);
	bfwin_action_set_sensitive(manager, "/MainMenu/FileMenu/FileRename", !doc->readonly);

	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/Cut", !doc->readonly);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/Paste", !doc->readonly);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/Replace", !doc->readonly);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/ReplaceAgain", !doc->readonly);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/EditIndent", !doc->readonly);
	bfwin_action_set_sensitive(manager, "/MainMenu/EditMenu/EditUnindent", !doc->readonly);
}

void
bfwin_action_set_sensitive(GtkUIManager * manager, const gchar * path, gboolean sensitive)
{
	GtkAction *action = gtk_ui_manager_get_action(manager, path);

	if (!action) {
		g_warning("Cannot set action sensitivity %s\n", path);
		return;
	}

	gtk_action_set_sensitive(action, sensitive);
}

void
setup_menu_toggle_item(GtkActionGroup * action_group, const gchar * action_name, gboolean is_active)
{
	GtkAction *action = gtk_action_group_get_action(action_group, action_name);

	if (!action) {
		g_warning("Cannot set-up menu widget %s\n", action_name);
		return;
	}

	if ((gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action))) != is_active)
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), is_active);
}

void
setup_menu_toggle_item_from_path(GtkUIManager * manager, const gchar * path, gboolean is_active)
{
	GtkAction *action = gtk_ui_manager_get_action(manager, path);

	if (!action) {
		g_warning("Cannot set-up menu widget %s\n", path);
		return;
	}

	if ((gtk_toggle_action_get_active(GTK_TOGGLE_ACTION(action))) != is_active)
		gtk_toggle_action_set_active(GTK_TOGGLE_ACTION(action), is_active);
}
