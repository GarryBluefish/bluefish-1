#define DEBUG

#include <gtk/gtk.h>
#include <string.h>

#include "config.h"
#include "bluefish.h"
#include "document.h"
#include "bf_lib.h"
#include "outputbox.h"

/*
 * for the external commands, the external filters, and the outputbox, we have some general code
 * to create the command, and to start the command
 *
 * the external command is simple: it only needs formatting of the commandstring, and creation of any output files
 * for the outputbox, it needs the above, but also the output needs to go back to the outputbox widget
 * for filters it needs formatting of the commandstring, and the output should go to the text widget
 *
 */
typedef struct {
	gint refcount;
	Tbfwin *bfwin;
	const gchar *formatstring;
	gchar *commandstring;

	gchar *securedir; /* if we need any local temporary files for input or output we'll use fifo's */
	gchar *fifo_in;
	gchar *fifo_out;
	gchar *tmp_in;
	gchar *tmp_out;
	gchar *inplace;
	gboolean pipe_in;
	gboolean pipe_out;
	
	gboolean include_stderr;
	
	GIOChannel* channel_in;
	gchar *buffer_out;
	gchar *buffer_out_position;
	GIOChannel* channel_out;
	
	GIOFunc channel_out_lcb;
	gpointer channel_out_data;
	
	GPid child_pid;
	
	gpointer data;
} Texternalp;

static void start_command_backend(Texternalp *ep);

static void externalp_unref(Texternalp *ep) {
	ep->refcount--;
	DEBUG_MSG("externalp_unref, started for %p, refount=%d\n",ep,ep->refcount);
	if (ep->refcount <= 0) {
		DEBUG_MSG("externalp_unref, cleanup!\n");
		if (ep->commandstring) g_free(ep->commandstring);
		if (ep->fifo_in) {
			unlink(ep->fifo_in);
			g_free(ep->fifo_in);
		}
		if (ep->fifo_out) {
			unlink(ep->fifo_out);
			g_free(ep->fifo_out);
		}
		if (ep->tmp_in) {
			unlink(ep->tmp_in);
			g_free(ep->tmp_in);
		}
		if (ep->tmp_out) {
			unlink(ep->tmp_out);
			g_free(ep->tmp_out);
		}
		if (ep->inplace) {
			unlink(ep->inplace);
			g_free(ep->inplace);
		}
		
		if (ep->buffer_out) g_free(ep->buffer_out);
		if (ep->securedir) {
			rmdir(ep->securedir);
			g_free(ep->securedir);
		}
		g_free(ep);
	}
}

static gboolean start_command_write_lcb(GIOChannel *channel,GIOCondition condition,gpointer data) {
	Texternalp *ep = data;
	GIOStatus status;
	GError *error=NULL;
	gsize bytes_written=0;
	DEBUG_MSG("start_command_write_lcb, started, still %d bytes to go\n",strlen(ep->buffer_out_position));

	status = g_io_channel_write_chars(channel,ep->buffer_out_position,-1,&bytes_written,&error);
	DEBUG_MSG("start_command_write_lcb, %d bytes written\n",bytes_written);
	ep->buffer_out_position += bytes_written;
	if (strlen(ep->buffer_out) <= (ep->buffer_out_position - ep->buffer_out)) {
		DEBUG_MSG("start_command_write_lcb, finished, shutting down channel\n");
		g_io_channel_shutdown(channel,TRUE,&error);
		if (ep->tmp_in) {
			start_command_backend(ep);
		} else {
			externalp_unref(ep);
		}
		return FALSE;
	}
	return TRUE;
}

static void spawn_setup_lcb(gpointer data) {
	Texternalp *ep = data;
#ifndef WIN32
	/* because win32 does not have both fork() and excec(), this function is
	not called in the child but in the parent on win32 */
	if (ep->include_stderr) {
		dup2(STDOUT_FILENO,STDERR_FILENO);
	}
#endif
}
static void child_watch_lcb(GPid pid,gint status,gpointer data) {
	Texternalp *ep = data;
	DEBUG_MSG("child_watch_lcb, status=%d\n",status);
	/* if there was a temporary output file, we should now open it and start to read it */
	if (ep->tmp_out) {
		ep->channel_out = g_io_channel_new_file(ep->tmp_out,"r",NULL);
		DEBUG_MSG("child_watch_lcb, created channel_out from file %s\n",ep->tmp_out);
	} else if (ep->inplace) {
		ep->channel_out = g_io_channel_new_file(ep->inplace,"r",NULL);
		DEBUG_MSG("child_watch_lcb, created channel_out from file %s\n",ep->inplace);
	}
	if (ep->tmp_out||ep->inplace){
		ep->refcount++;
		g_io_channel_set_flags(ep->channel_out,G_IO_FLAG_NONBLOCK,NULL);
		DEBUG_MSG("child_watch_lcb, add watch for channel_out\n");
		g_io_add_watch(ep->channel_out, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP,ep->channel_out_lcb,ep->channel_out_data);
	}
	g_spawn_close_pid(pid);
	externalp_unref(ep);
}

static void start_command_backend(Texternalp *ep) {
	gchar *argv[4];
	gint standard_input=0,standard_output=0;
	GError *error=NULL;

	argv[0] = "/bin/sh";
	argv[1] = "-c";
	argv[2] = ep->commandstring;
	argv[3] = NULL;
	if (ep->fifo_in) {
		if (mkfifo(ep->fifo_in, 0600) != 0) {
			g_print("some error happened creating fifo %s??\n",ep->fifo_in);
			return;
		}
		DEBUG_MSG("start_command, created fifo %s\n",ep->fifo_in);
	}
	if (ep->fifo_out) {
		if (mkfifo(ep->fifo_out, 0600) != 0) {
			g_print("some error happened creating fifo %s??\n",ep->fifo_out);
			return;
		}
	}
#ifdef WIN32
	ep->include_stderr = FALSE;
#endif
	DEBUG_MSG("start_command, pipe_in=%d, pipe_out=%d, fifo_in=%s, fifo_out=%s,include_stderr=%d\n",ep->pipe_in,ep->pipe_out,ep->fifo_in,ep->fifo_out,ep->include_stderr);
	DEBUG_MSG("start_command, about to spawn process /bin/sh -c %s\n",argv[2]);
	g_spawn_async_with_pipes(NULL,argv,NULL,G_SPAWN_DO_NOT_REAP_CHILD,(ep->include_stderr)?spawn_setup_lcb:NULL,ep,&ep->child_pid,
				(ep->pipe_in) ? &standard_input : NULL,
				(ep->pipe_out) ? &standard_output : NULL,
				NULL,&error);
	ep->refcount++;
	g_child_watch_add(ep->child_pid,child_watch_lcb,ep);
	
	if (error) {
		DEBUG_MSG("start_command, there is an error!!\n");
	}
	if (ep->pipe_in) {
		ep->channel_in = g_io_channel_unix_new(standard_input);
	} else if (ep->fifo_in) {
		DEBUG_MSG("start_command, connecting channel_in to fifo %s\n",ep->fifo_in);
		ep->channel_in = g_io_channel_new_file(ep->fifo_in,"w",NULL);
	}
	if (ep->pipe_in || ep->fifo_in) {
		ep->refcount++;
		ep->buffer_out = ep->buffer_out_position = doc_get_chars(ep->bfwin->current_document,0,-1);
		g_io_channel_set_flags(ep->channel_in,G_IO_FLAG_NONBLOCK,NULL);
		/* now we should start writing, correct ? */
		DEBUG_MSG("start_command, add watch for channel_in\n");
		g_io_add_watch(ep->channel_in,G_IO_OUT,start_command_write_lcb,ep);
	}
	if (ep->pipe_out) {
		ep->channel_out = g_io_channel_unix_new(standard_output);
		DEBUG_MSG("start_command, created channel_out from pipe\n");
	} else if (ep->fifo_out) {
		ep->channel_out = g_io_channel_new_file(ep->fifo_out,"r",NULL);
		DEBUG_MSG("start_command, created channel_out from fifo %s\n",ep->fifo_out);
	}
	if (ep->channel_out_lcb && (ep->pipe_out || ep->fifo_out)) {
		ep->refcount++;
		g_io_channel_set_flags(ep->channel_out,G_IO_FLAG_NONBLOCK,NULL);
		DEBUG_MSG("start_command, add watch for channel_out\n");
		g_io_add_watch(ep->channel_out, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP,ep->channel_out_lcb,ep->channel_out_data);
	}
	
}

static void start_command(Texternalp *ep) {
	if (ep->tmp_in) {
		/* first create tmp_in, then start the real command in the callback */
		ep->channel_in = g_io_channel_new_file(ep->tmp_in,"w",NULL);
		ep->buffer_out = ep->buffer_out_position = doc_get_chars(ep->bfwin->current_document,0,-1);
		g_io_channel_set_flags(ep->channel_in,G_IO_FLAG_NONBLOCK,NULL);
		DEBUG_MSG("start_command, add watch for channel_in\n");
		g_io_add_watch(ep->channel_in,G_IO_OUT,start_command_write_lcb,ep);
	} else {
		start_command_backend(ep);
	}
}
/*
static void start_command_alt2(Texternalp *ep, gboolean include_stderr, GIOFunc channel_out_lcb,gpointer out_data) {
	int out_pipe[2],in_pipe[2]; / * in pipes, 0 is for reading, 1 is for writing * /
	
	if (ep->pipe_in) {
		if (pipe(in_pipe) != 0) {
			g_print("some error happened creating a pipe??\n");
			return;
		}
	} else if (ep->fifo_in) {
		if (mkfifo(ep->fifo_in, 0600) != 0) {
			g_print("some error happened creating fifo %s??\n",ep->fifo_in);
			return;
		}
		DEBUG_MSG("start_command, created fifo %s\n",ep->fifo_in);
	}
	if (ep->pipe_out) {
		if (pipe(out_pipe) != 0) {
			g_print("some error happened ??\n");
			return;
		}
	} else if (ep->fifo_out) {
		if (mkfifo(ep->fifo_out, 0600) != 0) {
			g_print("some error happened creating fifo %s??\n",ep->fifo_out);
			return;
		}
	}
	
	DEBUG_MSG("start_command, before fork(), commandstring='%s'\n",ep->commandstring);
	if ((ep->child_process = fork()) == 0) {
		if (ep->pipe_out) {
			close(out_pipe[0]);
			dup2(out_pipe[1],STDOUT_FILENO);
			close(out_pipe[1]);
		}
		if (ep->pipe_in) {
			close(in_pipe[1]);
			dup2(in_pipe[0],STDIN_FILENO);
			close(in_pipe[0]);
		}
		if (include_stderr) {
			dup2(STDOUT_FILENO,STDERR_FILENO);
		}
		execlp("/bin/sh", "sh", "-c", ep->commandstring, NULL);
		exit(127);
	}
	if (ep->pipe_in) {
		close(in_pipe[0]);
		ep->channel_in = g_io_channel_unix_new(in_pipe[1]);
	} else if (ep->fifo_in) {
		ep->channel_in = g_io_channel_new_file(ep->fifo_in,"w",NULL);
	}
	if (ep->pipe_in || ep->fifo_in) {
		ep->refcount++;
		ep->buffer_out = ep->buffer_out_position = doc_get_chars(ep->bfwin->current_document,0,-1);
		g_io_channel_set_flags(ep->channel_in,G_IO_FLAG_NONBLOCK,NULL);
		DEBUG_MSG("start_command, add watch for channel_in\n");
		g_io_add_watch(ep->channel_in,G_IO_OUT,start_command_write_lcb,ep);
	}
	if (ep->pipe_out) {
		close(out_pipe[1]);
		ep->channel_out = g_io_channel_unix_new(out_pipe[0]);
		DEBUG_MSG("start_command, created channel_out from pipe\n");
	} else if (ep->fifo_out) {
		ep->channel_out = g_io_channel_new_file(ep->fifo_out,"r",NULL);
		DEBUG_MSG("start_command, created channel_out from fifo %s\n",ep->fifo_out);
	}
	if (ep->pipe_out || ep->fifo_out) {
		ep->refcount++;
		g_io_channel_set_flags(ep->channel_out,G_IO_FLAG_NONBLOCK,NULL);
		DEBUG_MSG("start_command, add watch for channel_out\n");
		g_io_add_watch(ep->channel_out, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP,channel_out_lcb,out_data);
	}
}*/


/* The format string should have new options:
    * %s local full path (function should abort for remote files)
    * %c local directory of file (function should abort for remote files)
    * %n filename without path
    * %u URL
    * %p preview URL if basedir and preview dir are set in project settings, else identical to %u
    * %i temporary fifo for input, if the document is not modified and local equal to %s
    * %o temporary fifo for output of filters or outputbox
    * %I temporary filename for input (fifo is faster), if the document is not modified and local equal to %s
    * %O temporary filename for output of filters or outputbox (fifo is faster) (previously %f)
    * %t temporary filename for both input and output (for in-place-editing filters)
*/
static gchar *create_commandstring(Texternalp *ep, const gchar *formatstring, gboolean discard_output) {
	Tconvert_table *table;
	gchar *localname=NULL, *localfilename=NULL, *retstring, *curi=NULL;
	gboolean is_local_non_modified;
	gboolean need_local = FALSE, 
		need_tmpin = FALSE,
		need_tmpout = FALSE,
		need_fifoin = FALSE,
		need_fifoout = FALSE,
		need_inplace = FALSE,
		need_preview_uri = FALSE,
		need_filename=FALSE;
	gint items = 2, cur=0;
	
	if (!ep->bfwin->current_document) {
		return NULL;
	}
	need_filename = need_local = (strstr(formatstring, "%c") != NULL || strstr(formatstring, "%s") != NULL);
	need_preview_uri = (strstr(formatstring, "%p") != NULL);
	if (!need_filename) { /* local already implies we need a filename */
		need_filename = (need_preview_uri || strstr(formatstring, "%n") != NULL || strstr(formatstring, "%u") != NULL);
	}
	if (need_filename && !ep->bfwin->current_document->uri) {
		/* BUG: give a warning that the current command only works for files with a name */
		DEBUG_MSG("create_commandstring, this command needs a filename, but there is no\n");
		return NULL;
	}
	if (need_local && !gnome_vfs_uri_is_local(ep->bfwin->current_document->uri)) {
		DEBUG_MSG("create_commandstring, this command needs a local filefilename, but there is no\n");
		/* BUG: give a warning that the current command only works for local files */
		return NULL;
	}
	need_tmpin = (strstr(formatstring, "%I") != NULL);
	/* %f is for backwards compatibility with bluefish 1.0 */
	need_tmpout = (strstr(formatstring, "%O") != NULL);
	need_fifoin = (strstr(formatstring, "%i") != NULL);
	need_fifoout = (strstr(formatstring, "%o") != NULL);
	need_inplace = (strstr(formatstring, "%t") != NULL);
	
	if ((need_tmpout || need_fifoout || need_inplace) && discard_output) {
		DEBUG_MSG("create_commandstring, external_commands should not have %%o\n");
		/*  BUG: give a warning that external commands should not use %o */
		return NULL;
	}
	if ((need_tmpin && (need_fifoin || need_inplace)) || (need_fifoin && need_inplace)) {
		DEBUG_MSG("create_commandstring, cannot have multiple inputs\n");
		/*  BUG: give a warning that you cannot have multiple inputs */
		return NULL;
	}
	if ((need_tmpout && (need_fifoout || need_inplace)) || (need_fifoout && need_inplace)) {
		DEBUG_MSG("create_commandstring, cannot have multiple outputs\n");
		/*  BUG: give a warning that you cannot have multiple outputs */
		return NULL;
	}
	if (!discard_output && !need_tmpout && !need_fifoout && !need_inplace) {
		ep->pipe_out = TRUE;
	}
	DEBUG_MSG("create_commandstring, formatstring '%s' seems OK\n",formatstring);

	is_local_non_modified = (ep->bfwin->current_document->uri 
		&& !ep->bfwin->current_document->modified
		&& gnome_vfs_uri_is_local(ep->bfwin->current_document->uri));

	if (need_filename || is_local_non_modified) {
		curi = gnome_vfs_uri_to_string(ep->bfwin->current_document->uri,GNOME_VFS_URI_HIDE_PASSWORD);
		if (need_local) {
			localname = gnome_vfs_get_local_path_from_uri(curi);
			localfilename = strrchr(localname, '/')+1;
			items += 2;
		}
		if (need_preview_uri) {
			items++;
		}
	}
	if (need_tmpin) items++;
	if (need_tmpout) items++;
	if (need_fifoin) items++;
	if (need_fifoout) items++;
	if (need_inplace) items++;
	
	table = g_new(Tconvert_table, items+1);
	if (need_filename) {
		table[cur].my_int = 'n';
		table[cur].my_char = need_local ? g_strdup(localfilename) : gnome_vfs_uri_extract_short_path_name(ep->bfwin->current_document->uri);
		cur++;
		table[cur].my_int = 'u';
		table[cur].my_char = curi;
		cur++;
		if (need_preview_uri) {
			table[cur].my_int = 'p';
			if (ep->bfwin->project && ep->bfwin->project->webdir 
					&& ep->bfwin->project->basedir && strlen(ep->bfwin->project->webdir)>2
					&& strlen(ep->bfwin->project->basedir)>2 
					&& strncmp(curi, ep->bfwin->project->basedir, strlen(ep->bfwin->project->basedir))==0) {
						table[cur].my_char = g_strconcat(ep->bfwin->project->webdir, &curi[strlen(ep->bfwin->project->basedir)], NULL);
			} else {
				table[cur].my_char = g_strdup(curi);
			}
			cur++;
		}
	}
	if (need_tmpin) {
		table[cur].my_int = 'I';
		if (is_local_non_modified) {
			table[cur].my_char = gnome_vfs_get_local_path_from_uri(curi);
		} else {
			ep->tmp_in = create_secure_dir_return_filename();
			table[cur].my_char = g_strdup(ep->tmp_in);
		}
		cur++;
	} else if (need_fifoin) {
		table[cur].my_int = 'i';
		if (is_local_non_modified) {
			table[cur].my_char = gnome_vfs_get_local_path_from_uri(curi);
		} else {
			ep->fifo_in = create_secure_dir_return_filename();
			table[cur].my_char = g_strdup(ep->fifo_in);
		}
		DEBUG_MSG("create_commandstring, %%i will be at %s\n",table[cur].my_char);
		cur++;
	} else if (need_inplace) {
		table[cur].my_int = 't';
		ep->inplace = create_secure_dir_return_filename();
		table[cur].my_char = g_strdup(ep->inplace);
		DEBUG_MSG("create_commandstring, %%t will be at %s\n",ep->inplace);
		cur++;
	}
	if (need_tmpout) {
		table[cur].my_int = 'O';
		ep->tmp_out = create_secure_dir_return_filename();
		table[cur].my_char = g_strdup(ep->tmp_out);
		cur++;
	} else if (need_fifoout) {
		table[cur].my_int = 'o';
		ep->fifo_out = create_secure_dir_return_filename();
		table[cur].my_char = g_strdup(ep->fifo_out);
		DEBUG_MSG("create_commandstring, %%o will be at %s\n",ep->fifo_out);
		cur++;
	}
	if (need_local) {
		table[cur].my_int = 's';
		table[cur].my_char = localname;
		cur++;
		table[cur].my_int = 'c';
		table[cur].my_char = g_strndup(localname, strlen(localname) - strlen(localfilename));
		cur++;
	}
	table[cur].my_int = '\0';
	table[cur].my_char = NULL;
	if (cur == 0) {
		retstring = g_strdup(formatstring);
	} else {
		retstring = replace_string_printflike(formatstring, table);
	}
	free_convert_table(table);
	DEBUG_MSG("create_commandstring, returning %s\n",retstring);
	return retstring;
}

static gboolean outputbox_io_watch_lcb(GIOChannel *channel,GIOCondition condition,gpointer data) {
	Texternalp *ep = data;
	DEBUG_MSG("outputbox_io_watch_lcb, called with condition %d\n",condition);
	if (condition & G_IO_IN) {
		gchar *buf=NULL;
		gsize buflen=0,termpos=0;
		GError *error=NULL;
		GIOStatus status = g_io_channel_read_line(channel,&buf,&buflen,&termpos,&error);
		while (status == G_IO_STATUS_NORMAL) {
			if (buflen > 0) {
				if (termpos < buflen) buf[termpos] = '\0';
				fill_output_box(ep->bfwin->outputbox, buf);
				g_free(buf);
			}
			status = g_io_channel_read_line(channel,&buf,&buflen,&termpos,&error);
		}
		if (status == G_IO_STATUS_EOF) {
			GError *error=NULL;
			/* cleanup !!!!!!!!! */
			g_io_channel_shutdown(channel,TRUE,&error);
			externalp_unref(ep);
			return FALSE;
		}
	}
	if (condition & G_IO_OUT) {
		DEBUG_MSG("outputbox_io_watch_lcb, condition %d G_IO_OUT not handled\n",condition);
	}
	if (condition & G_IO_PRI) {
		DEBUG_MSG("outputbox_io_watch_lcb, condition %d G_IO_PRI not handled\n",condition);
	}
	if (condition & G_IO_ERR) {
		DEBUG_MSG("outputbox_io_watch_lcb, condition %d G_IO_ERR not handled\n",condition);
	}
	if (condition & G_IO_HUP) {
		GError *error=NULL;
		DEBUG_MSG("outputbox_io_watch_lcb, condition %d G_IO_HUP\n",condition);
		/* cleanup !!!!!!!!! */
		g_io_channel_shutdown(channel,TRUE,&error);
		externalp_unref(ep);
		return FALSE;
	}
	if (condition & G_IO_NVAL) {
		DEBUG_MSG("outputbox_io_watch_lcb, condition %d G_IO_NVAL not handled\n",condition);
	}
	return TRUE;
}

void outputbox_command(Tbfwin *bfwin, const gchar *formatstring) {
	Texternalp *ep;
	
	ep = g_new0(Texternalp,1);
	ep->bfwin = bfwin;
	ep->formatstring = formatstring;
	ep->commandstring = create_commandstring(ep, formatstring, FALSE);
	if (!ep->commandstring) {
		g_free(ep);
		/* BUG: is the user notified of the error ?*/
		return;
	}
	ep->include_stderr = TRUE;
	ep->channel_out_lcb = outputbox_io_watch_lcb;
	ep->channel_out_data = ep;
	start_command(ep);
}

static gboolean filter_io_watch_lcb(GIOChannel *channel,GIOCondition condition,gpointer data) {
	Texternalp *ep = data;
	DEBUG_MSG("filter_io_watch_lcb, started with condition=%d\n",condition);
	if (condition & G_IO_IN) {
		gchar *str_return;
		gsize length;
		GIOStatus status;
		GError *error=NULL;
		
		status = g_io_channel_read_to_end(channel,&str_return,&length,&error);
		if (status == G_IO_STATUS_NORMAL && str_return) {
			gint end;
			GError *error=NULL;
			DEBUG_MSG("filter_io_watch_lcb, received '%s'\n",str_return);
			end = doc_get_max_offset(ep->bfwin->current_document);
			doc_replace_text(ep->bfwin->current_document, str_return, 0, end);
			g_io_channel_shutdown(channel,TRUE,&error);
			externalp_unref(ep);
			g_free(str_return);
			return FALSE;
		} else {
			DEBUG_MSG("filter_io_watch_lcb, status=%d\n",status);
		}
		if (str_return) g_free(str_return);
	}
	if (condition & G_IO_HUP) {
		GError *error=NULL;
		DEBUG_MSG("filter_io_watch_lcb, condition %d G_IO_HUP\n",condition);
		g_io_channel_shutdown(channel,TRUE,&error);
		externalp_unref(ep);
		return FALSE;
	}
	if (condition & G_IO_OUT) {
		DEBUG_MSG("filter_io_watch_lcb, condition %d G_IO_OUT not handled\n",condition);
	}
	if (condition & G_IO_PRI) {
		DEBUG_MSG("filter_io_watch_lcb, condition %d G_IO_PRI not handled\n",condition);
	}
	if (condition & G_IO_ERR) {
		DEBUG_MSG("filter_io_watch_lcb, condition %d G_IO_ERR not handled\n",condition);
	}
	if (condition & G_IO_NVAL) {
		DEBUG_MSG("filter_io_watch_lcb, condition %d G_IO_NVAL not handled\n",condition);
	}
	return TRUE;
}

void filter_command(Tbfwin *bfwin, const gchar *formatstring) {
	Texternalp *ep;
	
	DEBUG_MSG("filter_command, started\n");
	ep = g_new0(Texternalp,1);
	ep->bfwin = bfwin;
	ep->formatstring = formatstring;
	ep->commandstring = create_commandstring(ep, formatstring, FALSE);
	if (!ep->commandstring) {
		DEBUG_MSG("filter_command, failed to create commandstring\n");
		g_free(ep);
		/* BUG: is the user notified of the error ?*/
		return;
	}
	ep->include_stderr = FALSE;
	ep->channel_out_lcb = filter_io_watch_lcb;
	ep->channel_out_data = ep;
	start_command(ep);
}

void external_command(Tbfwin *bfwin, const gchar *formatstring) {
	Texternalp *ep;
	
	ep = g_new0(Texternalp,1);
	ep->bfwin = bfwin;
	ep->formatstring = formatstring;
	ep->commandstring = create_commandstring(ep, formatstring, TRUE);
	if (!ep->commandstring) {
		g_free(ep);
		/* BUG: is the user notified of the error ?*/
		return;
	}
	ep->include_stderr = FALSE;
	ep->channel_out_lcb = NULL;
	ep->channel_out_data = NULL;
	
	start_command(ep);
}

