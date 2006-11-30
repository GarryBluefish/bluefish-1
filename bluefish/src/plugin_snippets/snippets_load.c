/* Bluefish HTML Editor
 * snippets_load.c - plugin for snippets sidebar
 *
 * Copyright (C) 2006 Olivier Sessink
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
/*
the XML format is not yet fully designed
I'm starting the programming to get a feel for libXML

we'll have two main types of items in the xml document: branches and leaves
the branches are just placeholders, for leaves and for other branches
leaves are of some type:
- python script
- search and replace action
- custom insert
- custom dialog

<?xml version="1.0"?>
<snippets>
<branch title="html">
<branch title="form">
<leaf title="html form" type="insert" tooltip="html form" accelerator="&lt;Control&gt;F1">
<param name="action"/>
<param name="method"/>
<before>&lt;form method="%1" action="%0"&gt;</before>
<after>&lt;/form&gt;</after>
</leaf>
<leaf title="input type text" type="insert" tooltip="input type is text">
<param name="name"/>
<param name="value"/>
<before>&lt;input type="text" name="%0" value="%1" /&gt;</before>
</leaf>
<leaf title="input type submit" type="insert" tooltip="input type is submit" accelerator="&lt;Control&gt;F1">
<param name="name"/>
<param name="value"/>
<before>&lt;input type="submit" name="%0" value="%1" /&gt;</before>
</leaf>
</branch>
</branch>
</snippets>

*/
#define DEBUG

#include <string.h>

#include "snippets.h"
#include "snippets_load.h"
#include "../bf_lib.h"

/* GdkPixbuf RGBA C-Source image dump */

#ifdef __SUNPRO_C
#pragma align 4 (pixmap_snr)
#endif
#ifdef __GNUC__
static const guint8 pixmap_snr[] __attribute__ ((__aligned__ (4))) = 
#else
static const guint8 pixmap_snr[] = 
#endif
{ ""
  /* Pixbuf magic (0x47646b50) */
  "GdkP"
  /* length: header (24) + pixel_data (900) */
  "\0\0\3\234"
  /* pixdata_type (0x1010002) */
  "\1\1\0\2"
  /* rowstride (60) */
  "\0\0\0<"
  /* width (15) */
  "\0\0\0\17"
  /* height (15) */
  "\0\0\0\17"
  /* pixel_data: */
  "cla\0wto\377NMK\377VWS\377VWS\377VWS\377VWS\377VWS\377VWS\377VWS\377"
  "\223\224\220\377cla\0cla\0cla\0cla\0cla\0NMK\377\361\363\360\377\373"
  "\375\372\377\373\375\372\377\361\363\360\377\373\375\372\377\373\375"
  "\372\377\373\375\372\377\305\310\303\377\245\245\242\377wto\377cla\0"
  "cla\0cla\0cla\0VWS\377\373\375\372\377\324\325\320\377wto\377VWS\377"
  "__Z\377\223\224\220\377\361\363\360\377\305\310\303\377\361\363\360\377"
  "\223\224\220\377jic\377cla\0cla\0cla\0NMK\377\373\375\372\377wto\377"
  "\242\243\224\377\261\263\235\377\324\325\320\377\210\206\204\377\245"
  "\245\242\377\264\266\261\377\334\336\333\377\324\325\320\377\210\206"
  "\204\377\210\206\204\377cla\0cla\0VWS\377\373\375\372\377NMK\377\242"
  "\243\224\377\261\263\235\377\324\325\320\377\242\243\224\377\210\206"
  "\204\377\245\245\242\377__Z\377NMK\377\23\23\20\377j[\34\377bWG\377c"
  "la\0NMK\377\373\375\372\377jic\377\245\245\242\377\261\263\235\377\261"
  "\263\235\377\203\200s\377\210\206\204\377\334\336\333\377\334\336\333"
  "\377\223\224\220\377\234\212*\377\305\235;\377:4$\377cla\0NMK\377\245"
  "\245\242\377\37\40\36\377jic\377__Z\377ebU\377__Z\377\245\245\242\377"
  "\334\336\333\377\245\245\242\377\234\212*\377\305\235;\377($\24\377_"
  "_Z\377cla\0.-+\377\23\23\20\377.-+\377\245\245\242\377\210\206\204\377"
  "\223\224\220\377\245\245\242\377\305\310\303\377\223\224\220\377\234"
  "\212*\377\264\2143\377:4$\377;;5\377wto\377\245\245\242\377\23\23\20"
  "\377\37\40\36\377\210\206\204\377\305\310\303\377\324\325\320\377\305"
  "\310\303\377\324\325\320\377\223\224\220\377\264\2143\377\305\235;\377"
  ":4$\377\210\206\204\377__Z\377\210\206\204\377\210\206\204\377\37\40"
  "\36\377\223\224\220\377\264\266\261\377\324\325\320\377\334\336\333\377"
  "\341\344\340\377\245\245\242\377\207t/\377\264\2143\377:4$\377\223\224"
  "\220\377\264\266\261\377__Z\377\210\206\204\377cla\0NMK\377\305\310\303"
  "\377\324\325\320\377\334\336\333\377\361\363\360\377\341\344\340\377"
  "__Z\377G;%\377:4$\377\210\206\204\377\264\266\261\377\324\325\320\377"
  "jic\377\210\206\204\377cla\0NMK\377\334\336\333\377\334\336\333\377\341"
  "\344\340\377\334\336\333\377\334\336\333\377;;5\377jic\377\245\245\242"
  "\377\264\266\261\377\324\325\320\377\324\325\320\377jic\377\210\206\204"
  "\377cla\0NMK\377\334\336\333\377\334\336\333\377\341\344\340\377\334"
  "\336\333\377\324\325\320\377\264\266\261\377\245\245\242\377\305\310"
  "\303\377\324\325\320\377\324\325\320\377\324\325\320\377jic\377\210\206"
  "\204\377cla\0NMK\377jic\377wto\377wto\377wto\377wto\377jic\377jic\377"
  "jic\377wto\377wto\377wto\377;;5\377\223\224\220\377cla\0cla\0\210\206"
  "\204\377\210\206\204\377\210\206\204\377\210\206\204\377\210\206\204"
  "\377\210\206\204\377\210\206\204\377\210\206\204\377\210\206\204\377"
  "\210\206\204\377\210\206\204\377\223\224\220\377cla\0"};


/* GdkPixbuf RGBA C-Source image dump */

#ifdef __SUNPRO_C
#pragma align 4 (pixmap_insert)
#endif
#ifdef __GNUC__
static const guint8 pixmap_insert[] __attribute__ ((__aligned__ (4))) = 
#else
static const guint8 pixmap_insert[] = 
#endif
{ ""
  /* Pixbuf magic (0x47646b50) */
  "GdkP"
  /* length: header (24) + pixel_data (840) */
  "\0\0\3`"
  /* pixdata_type (0x1010002) */
  "\1\1\0\2"
  /* rowstride (56) */
  "\0\0\0""8"
  /* width (14) */
  "\0\0\0\16"
  /* height (15) */
  "\0\0\0\17"
  /* pixel_data: */
  "WXU\377PRO\377WXU\377WXU\377WXU\377WXU\377WXU\377WXU\377WXU\377\234\234"
  "\231\377_`[\0_`[\0_`[\0_`[\0PRO\377\372\374\371\377\372\374\371\377\372"
  "\374\371\377\372\374\371\377\372\374\371\377\372\374\371\377\372\374"
  "\371\377\272\272\267\377\234\234\231\377\222\221\214\377_`[\0_`[\0_`"
  "[\0WXU\377\372\374\371\377\372\374\371\377\363\365\362\377\372\374\371"
  "\377\363\365\362\377\372\374\371\377\363\365\362\377\324\325\322\377"
  "\351\353\350\377}}{\377\212\206\204\377_`[\0_`[\0WXU\377\372\374\371"
  "\377\363\365\362\377\372\374\371\377\372\374\371\377\372\374\371\377"
  "\363\365\362\377\351\353\350\377\272\272\267\377\337\341\336\377\313"
  "\314\310\377}}{\377\234\234\231\377_`[\0WXU\377\372\374\371\377\363\365"
  "\362\377\363\365\362\377\372\374\371\377\372\374\371\377\372\374\371"
  "\377\351\353\350\377\212\206\204\377WXU\377GHC\377\35\33\17\377j[\34"
  "\377pnf\377WXU\377\372\374\371\377\363\365\362\377\372\374\371\377\372"
  "\374\371\377\363\365\362\377\363\365\362\377\363\365\362\377\351\353"
  "\350\377\324\325\322\377\203\200r\377\260\231,\377\255\2117\377@<0\377"
  "WXU\377\372\374\371\377\372\374\371\377\363\365\362\377\372\374\371\377"
  "\363\365\362\377\363\365\362\377\363\365\362\377\351\353\350\377\222"
  "\221\214\377\260\231,\377\255\2117\377\35\33\17\377\212\206\204\377W"
  "XU\377\363\365\362\377\363\365\362\377\363\365\362\377\363\365\362\377"
  "\351\353\350\377\363\365\362\377\351\353\350\377\217\214~\377\260\231"
  ",\377\237x2\377@<0\377,-+\377\222\221\214\377PRO\377\363\365\362\377"
  "\363\365\362\377\363\365\362\377\363\365\362\377\363\365\362\377\351"
  "\353\350\377\212\206\204\377\263\222+\377\255\2117\377@<0\377\234\234"
  "\231\377GHC\377\253\254\251\377WXU\377\351\353\350\377\363\365\362\377"
  "\351\353\350\377\363\365\362\377\351\353\350\377\234\234\231\377\255"
  "\2117\377\237x2\377@<0\377\272\272\267\377\313\314\310\377PRO\377\253"
  "\254\251\377WXU\377\351\353\350\377\351\353\350\377\351\353\350\377\351"
  "\353\350\377\324\325\322\377LI=\377K>%\377@<0\377\234\234\231\377\324"
  "\325\322\377\313\314\310\377SPI\377\253\254\251\377WXU\377\351\353\350"
  "\377\351\353\350\377\351\353\350\377\351\353\350\377\324\325\322\377"
  "796\377\212\206\204\377\253\254\251\377\313\314\310\377\324\325\322\377"
  "\313\314\310\377SPI\377\253\254\251\377PRO\377\351\353\350\377\351\353"
  "\350\377\351\353\350\377\351\353\350\377\324\325\322\377\313\314\310"
  "\377\313\314\310\377\324\325\322\377\324\325\322\377\324\325\322\377"
  "\313\314\310\377SPI\377\253\254\251\377GHC\377pnf\377pnf\377pnf\377p"
  "nf\377pnf\377pnf\377pnf\377pnf\377pnf\377pnf\377pnf\377796\377\272\272"
  "\267\377_`[\0\212\206\204\377\212\206\204\377\212\206\204\377\212\206"
  "\204\377\212\206\204\377\212\206\204\377\212\206\204\377\212\206\204"
  "\377\212\206\204\377\212\206\204\377\212\206\204\377\234\234\231\377"
  "_`[\0"};

enum {
	pixmap_type_none,
	pixmap_type_insert,
	pixmap_type_snr
};

static void add_tree_item(GtkTreeIter *parent, GtkTreeIter *child, gint pixmaptype, const gchar *name, gpointer ptr) {
	GdkPixbuf* pixmap=NULL; 
	DEBUG_MSG("add_tree_item, adding %s\n",name);
	gtk_tree_store_append(snippets_v.store, child, parent);
	
	switch (pixmaptype) {
		case pixmap_type_insert:
			pixmap = gdk_pixbuf_new_from_inline(-1, pixmap_insert, FALSE, NULL);
		break;
		case pixmap_type_snr:
			pixmap = gdk_pixbuf_new_from_inline(-1, pixmap_snr, FALSE, NULL);
		break;
	}
	
	gtk_tree_store_set(snippets_v.store, child, PIXMAP_COLUMN, pixmap, TITLE_COLUMN, name,NODE_COLUMN, ptr,-1);
}

static void walk_tree(xmlNodePtr cur, GtkTreeIter *parent) {
	cur = cur->xmlChildrenNode;
	while (cur != NULL) {
		xmlChar *title;
		GtkTreeIter iter;
		if ((xmlStrEqual(cur->name, (const xmlChar *)"branch"))) {
			title = xmlGetProp(cur, (const xmlChar *)"title");
			add_tree_item(parent, &iter, pixmap_type_none, (const gchar *)title, cur);
			walk_tree(cur, &iter);
		} else if ((xmlStrEqual(cur->name, (const xmlChar *)"leaf"))) {
			xmlChar *type;
			gint pixtype=pixmap_type_none;
			title = xmlGetProp(cur, (const xmlChar *)"title");
			type = xmlGetProp(cur, (const xmlChar *)"type");
			if (xmlStrEqual(type, (const xmlChar *)"insert")) {
				pixtype=pixmap_type_insert;
			} else if (xmlStrEqual(type, (const xmlChar *)"snr")) {
				pixtype=pixmap_type_snr;
			}
			add_tree_item(parent, &iter, pixtype, (const gchar *)title, cur);
		}
		cur = cur->next;
	}
}

void snippets_load(const gchar *filename) {
	xmlNodePtr cur=NULL;
	DEBUG_MSG("snippets_load, filename=%s\n",filename);
	snippets_v.doc = xmlParseFile(filename);
	
	if (snippets_v.doc == NULL ) {
		DEBUG_MSG("snippets_load, parse error\n");
		return;
	}

	cur = xmlDocGetRootElement(snippets_v.doc);
	if (cur == NULL) {
		DEBUG_MSG("snippets_load, empty document\n");
		xmlFreeDoc(snippets_v.doc);
		snippets_v.doc = NULL;
		return;
	}
	if (!xmlStrEqual(cur->name, (const xmlChar *) "snippets")) {
		DEBUG_MSG("snippets_load, wrong type of document, root is called %s\n",cur->name);
		xmlFreeDoc(snippets_v.doc);
		snippets_v.doc = NULL;
		return;
	}
	
	walk_tree(cur, NULL);
}

void snippets_store(void) {
	gchar *snipfile = user_bfdir("snippets");
	xmlSaveFormatFile(snipfile, snippets_v.doc, 1);
	g_free(snipfile);
}
