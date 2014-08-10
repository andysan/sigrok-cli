/*
 * This file is part of the sigrok-cli project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include "sigrok-cli.h"
#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>

#define BUFSIZE 16384

static void load_input_file_module(void)
{
	struct sr_session *session;
	const struct sr_input *in;
	const struct sr_input_module *imod;
	const struct sr_option **options;
	struct sr_dev_inst *sdi;
	GHashTable *mod_args, *mod_opts;
	GString *buf;
	int fd;
	ssize_t len;
	char *mod_id;

	if (!sr_input_list())
		g_critical("No supported input formats available.");

	mod_id = NULL;
	mod_args = NULL;
	if (opt_input_format) {
		mod_args = parse_generic_arg(opt_input_format, TRUE);
		mod_id = g_hash_table_lookup(mod_args, "sigrok_key");
	}

	buf = g_string_sized_new(BUFSIZE);
	if (mod_id) {
		/* User specified an input module to use. */
		if (!(imod = sr_input_find(mod_id)))
			g_critical("Error: unknown input module '%s'.", mod_id);
		g_hash_table_remove(mod_args, "sigrok_key");
		if ((options = sr_input_options_get(imod))) {
			mod_opts = generic_arg_to_opt(options, mod_args);
			sr_output_options_free(options);
		} else
			mod_opts = NULL;
		if (!(in = sr_input_new(imod, mod_opts)))
			g_critical("Error: failed to initialize input module.");
		if (mod_opts)
			g_hash_table_destroy(mod_opts);
		if (mod_args)
			g_hash_table_destroy(mod_args);
		if ((fd = open(opt_input_file, O_RDONLY)) == -1)
			g_critical("Failed to load %s: %s.", opt_input_file,
					strerror(errno));
	} else {
		/* Let the input modules try to identify the file. */
		if ((fd = open(opt_input_file, O_RDONLY)) == -1)
			g_critical("Failed to load %s: %s.", opt_input_file,
					strerror(errno));
		if ((len = read(fd, buf->str, BUFSIZE)) < 1)
			g_critical("Failed to read %s: %s.", opt_input_file,
					strerror(errno));
		buf->len = len;
		if (!(in = sr_input_scan_buffer(buf)))
			g_critical("Error: no input module found for this file.");
	}
	sdi = sr_input_dev_inst_get(in);

	if (select_channels(sdi) != SR_OK)
		return;

	sr_session_new(&session);
	sr_session_datafeed_callback_add(session, &datafeed_in, NULL);
	if (sr_session_dev_add(session, sdi) != SR_OK) {
		g_critical("Failed to use device.");
		sr_session_destroy(session);
		return;
	}

	while(TRUE) {
		g_string_truncate(buf, 0);
		len = read(fd, buf->str, BUFSIZE);
		if (len < 0)
			g_critical("Read failed: %s", strerror(errno));
		buf->len = len;
		if (sr_input_send(in, buf) != SR_OK)
			break;
		if (len < BUFSIZE)
			break;
	}
	sr_input_free(in);
	g_string_free(buf, TRUE);

	sr_session_destroy(session);

}

void load_input_file(void)
{
	struct sr_session *session;
	struct sr_dev_inst *sdi;
	GSList *devices;
	struct stat st;
	int ret;

	if (stat(opt_input_file, &st) == -1)
		g_critical("Failed to load %s: %s.", opt_input_file, strerror(errno));

	if (sr_session_load(opt_input_file, &session) == SR_OK) {
		/* sigrok session file */
		ret = sr_session_dev_list(session, &devices);
		if (ret != SR_OK || !devices->data) {
			g_critical("Failed to access session device.");
			sr_session_destroy(session);
			return;
		}
		sdi = devices->data;
		if (select_channels(sdi) != SR_OK) {
			sr_session_destroy(session);
			return;
		}
		sr_session_datafeed_callback_add(session, datafeed_in, NULL);
		sr_session_start(session);
		sr_session_run(session);
		sr_session_stop(session);
	}
	else {
		/* fall back on input modules */
		load_input_file_module();
	}
}
