#define PURPLE_PLUGINS

#include <stdio.h>

//#define SQLITE_THREADSAFE 0
//#define SQLITE_OMIT_LOAD_EXTENSION
#include "sqlite3.h"

#include "debug.h"
#include "log.h"
#include "plugin.h"
#include "pluginpref.h"
#include "prefs.h"
#include "stringref.h"
#include "util.h"
#include "version.h"
#include "xmlnode.h"

#ifdef _WIN32
#	include "win32dep.h"
#endif

static sqlite3 *db = NULL;
static PurpleLogLogger *sqlite_logger;

static void
sqlitelog_init_db()
{
	gchar *logfile;
	int res;
	
	logfile = g_build_filename(purple_user_dir(), "logs", "log.sqlite", NULL);
	
	res = sqlite3_open(logfile, &db);
	if (res != SQLITE_OK)
	{
		purple_debug_error("sqlitelog", "Cannot open database: %s\n", sqlite3_errmsg(db));
		sqlite3_close(db);
		return;
	}
	
	sqlite3_exec(db, "PRAGMA temp_store=MEMORY", NULL, NULL, NULL);
	sqlite3_exec(db, "PRAGMA synchronous=OFF", NULL, NULL, NULL);
	sqlite3_exec(db, "PRAGMA page_size=4096", NULL, NULL, NULL);
	
	sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS [accounts] (id INTEGER PRIMARY KEY AUTOINCREMENT, [username] VARCHAR(255), [protocol_id] VARCHAR(64))", NULL, NULL, NULL);
	sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS [logs] ([id] INTEGER PRIMARY KEY AUTOINCREMENT, [account_id] INTEGER NOT NULL REFERENCES accounts(id), [type] INTEGER NOT NULL, [name] VARCHAR(255), [starttime] TIMESTAMP, [endtime] TIMESTAMP)", NULL, NULL, NULL);
	sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS [messages] ([id] INTEGER PRIMARY KEY AUTOINCREMENT, [log_id] INTEGER NOT NULL REFERENCES logs(id), [type] INTEGER NOT NULL, [who] VARCHAR(255), [message] TEXT, [time] TIMESTAMP)", NULL, NULL, NULL);
	
	// Maintenance - make everything into y-m-d h:i:s for better date math :)
	sqlite3_exec(db, "UPDATE logs SET starttime = DATETIME(starttime, 'unixepoch') WHERE starttime NOT LIKE '%-%'", NULL, NULL, NULL);
	sqlite3_exec(db, "UPDATE messages SET time = DATETIME(time, 'unixepoch') WHERE time NOT LIKE '%-%'", NULL, NULL, NULL);
}

static gint64
sqlitelog_get_account_id(PurpleAccount *account)
{
	sqlite3_stmt *stmt;
	gint64 account_id;
	int res;
	
	sqlite3_prepare(db, "SELECT id FROM accounts WHERE username = ? AND protocol_id = ?", -1, &stmt, NULL);
	
	sqlite3_bind_text(stmt, 1, purple_normalize(account, purple_account_get_username(account)), -1, NULL);
	sqlite3_bind_text(stmt, 2, purple_account_get_protocol_id(account), -1, NULL);
	
	res = sqlite3_step(stmt);
	account_id = sqlite3_column_int64(stmt, 0);
	if (account_id != 0)
	{
		sqlite3_finalize(stmt);
		return account_id;
	}
	
	sqlite3_finalize(stmt);
	sqlite3_prepare(db, "INSERT INTO accounts (username, protocol_id) VALUES (?, ?)", -1, &stmt, NULL);
	
	sqlite3_bind_text(stmt, 1, purple_normalize(account, purple_account_get_username(account)), -1, NULL);
	sqlite3_bind_text(stmt, 2, purple_account_get_protocol_id(account), -1, NULL);
	
	res = sqlite3_step(stmt);
	account_id = sqlite3_last_insert_rowid(db);
	sqlite3_finalize(stmt);
	
	if (account_id)
	{
		return account_id;
	} else {
		purple_debug_error("sqlitelog", "Couldn't add account: %s\n", sqlite3_errmsg(db));
	}
	
	return 0;
}

static void
sqlitelog_create(PurpleLog *log)
{
	sqlite3_stmt *stmt, *logstmt;
	PurpleAccount *account = log->account;
	gint64 log_id;
	gint64 account_id;
	int res;
	
	g_return_if_fail(log != NULL);
	if(log->conv == NULL)
	{
		//This is likely when listing the logs
		return;
	}
	
	account_id = sqlitelog_get_account_id(account);
	
	sqlite3_prepare(db, "INSERT INTO logs (account_id, type, name, starttime) VALUES (?, ?, ?, DATETIME(?, 'unixepoch'))", -1, &stmt, NULL);
	
	sqlite3_bind_int64(stmt, 1, account_id);
	sqlite3_bind_int(stmt, 2, log->type);
	sqlite3_bind_text(stmt, 3, purple_normalize(log->account, log->name), -1, NULL);
	sqlite3_bind_int(stmt, 4, log->time);
	
	res = sqlite3_step(stmt);
	log_id = sqlite3_last_insert_rowid(db);
	sqlite3_finalize(stmt);
	
	if (sqlite3_prepare(db, "INSERT INTO messages (log_id, type, who, message, time) VALUES (?, @type, @who, @message, DATETIME(@time, 'unixepoch'))", -1, &logstmt, NULL) == SQLITE_OK)
	{
		sqlite3_bind_int64(logstmt, 1, log_id); 
		log->logger_data = logstmt;
	} else {
		purple_debug_error("sqlitelog", "Failure preparing message to be saved %s\n", sqlite3_errmsg(db));
	}
}

static gsize
sqlitelog_write(PurpleLog *log, PurpleMessageFlags type, const char *from, time_t time, const char *message)
{
	sqlite3_stmt *stmt = log->logger_data;
	int res;
	
	if (stmt == NULL)
	{
		purple_debug_error("sqlitelog", "The statment is NULL\n");
		return 0;
	}
	
	sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, "@type"), type);
	sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, "@who"), from, -1, NULL);
	sqlite3_bind_text(stmt, sqlite3_bind_parameter_index(stmt, "@message"), message, -1, NULL);
	sqlite3_bind_int(stmt, sqlite3_bind_parameter_index(stmt, "@time"), time);
	
	res = sqlite3_step(stmt);
	sqlite3_reset(stmt);
	
	return 1;
}

static GList *
sqlitelog_list(PurpleLogType type, const char *name, PurpleAccount *account)
{
	GList *list = NULL;
	const gchar *protocol_id;
	PurpleLog *log;
	sqlite3_stmt *stmt;
	gint64 account_id;
	time_t starttime;
	int res;
	
	account_id = sqlitelog_get_account_id(account);
	protocol_id = purple_account_get_protocol_id(account);
	
	sqlite3_prepare(db, "SELECT STRFTIME('%s', starttime) FROM logs WHERE account_id=? AND type=? AND name IN (?,?) AND (SELECT COUNT(*) FROM messages WHERE log_id=logs.id) > 0", -1, &stmt, NULL);
	
	sqlite3_bind_int64(stmt, 1, account_id);
	sqlite3_bind_int(stmt, 2, type);
	sqlite3_bind_text(stmt, 3, name, -1, NULL); // Old SQLite logging
	sqlite3_bind_text(stmt, 4, purple_normalize(account, name), -1, NULL); // New SQLite logging
	
	while((res = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		starttime = sqlite3_column_int(stmt, 0);
		
		log = purple_log_new(type, name, account, NULL, starttime, NULL);
		log->logger = sqlite_logger;

		list = g_list_prepend(list, log);
	}
	
	return list;
}

static char *
sqlitelog_read (PurpleLog *log, PurpleLogReadFlags *flags)
{
	GString *readdata;
	sqlite3_stmt *stmt;
	gint64 account_id;
	int res;

	if (flags != NULL)
		*flags = PURPLE_LOG_READ_NO_NEWLINE;
	
	g_return_val_if_fail(log != NULL, g_strdup(""));
	
	readdata = g_string_new(NULL);
	account_id = sqlitelog_get_account_id(log->account);
	
	sqlite3_prepare(db, "SELECT messages.type, messages.who, messages.message, STRFTIME('%s', messages.time) FROM messages, logs WHERE messages.log_id=logs.id AND logs.account_id=? AND logs.type=? AND logs.name IN (?,?) AND logs.starttime=DATETIME(?, 'unixepoch')", -1, &stmt, NULL);
	
	sqlite3_bind_int64(stmt, 1, account_id);
	sqlite3_bind_int(stmt, 2, log->type);
	sqlite3_bind_text(stmt, 3, log->name, -1, NULL); // Old SQLite logging
	sqlite3_bind_text(stmt, 4, purple_normalize(log->account, log->name), -1, NULL); // New SQLite logging
	sqlite3_bind_int(stmt, 5, log->time);
	
	while((res = sqlite3_step(stmt)) == SQLITE_ROW)
	{
		PurpleMessageFlags type = sqlite3_column_int(stmt, 0);
		const gchar *from = (gchar *)sqlite3_column_text(stmt, 1);
		const gchar *message = (gchar *)sqlite3_column_text(stmt, 2);
		time_t time = sqlite3_column_int(stmt, 3);
		gchar *msg_fixed;
		gchar *escaped_from = g_markup_escape_text(from, -1);
		const gchar *date = purple_date_format_long(localtime(&time));
	
		purple_markup_html_to_xhtml(message, &msg_fixed, NULL);
		
		// Borrowed the format from the HTML logger in libpurple/log.c
		
		if (type & PURPLE_MESSAGE_SYSTEM)
			g_string_append_printf(readdata, "<font size=\"2\">(%s)</font><b> %s</b><br/>\n", date, msg_fixed);
		else if (type & PURPLE_MESSAGE_RAW)
			g_string_append_printf(readdata, "<font size=\"2\">(%s)</font> %s<br/>\n", date, msg_fixed);
		else if (type & PURPLE_MESSAGE_ERROR)
			g_string_append_printf(readdata, "<font color=\"#FF0000\"><font size=\"2\">(%s)</font><b> %s</b></font><br/>\n", date, msg_fixed);
		else if (type & PURPLE_MESSAGE_WHISPER)
			g_string_append_printf(readdata, "<font color=\"#6C2585\"><font size=\"2\">(%s)</font><b> %s:</b></font> %s<br/>\n",
					date, escaped_from, msg_fixed);
		else if (type & PURPLE_MESSAGE_AUTO_RESP) {
			if (type & PURPLE_MESSAGE_SEND)
				g_string_append_printf(readdata, ("<font color=\"#16569E\"><font size=\"2\">(%s)</font> <b>%s &lt;AUTO-REPLY&gt;:</b></font> %s<br/>\n"), date, escaped_from, msg_fixed);
			else if (type & PURPLE_MESSAGE_RECV)
				g_string_append_printf(readdata, ("<font color=\"#A82F2F\"><font size=\"2\">(%s)</font> <b>%s &lt;AUTO-REPLY&gt;:</b></font> %s<br/>\n"), date, escaped_from, msg_fixed);
		} else if (type & PURPLE_MESSAGE_RECV) {
			if(purple_message_meify(msg_fixed, -1))
				g_string_append_printf(readdata, "<font color=\"#062585\"><font size=\"2\">(%s)</font> <b>***%s</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
			else
				g_string_append_printf(readdata, "<font color=\"#A82F2F\"><font size=\"2\">(%s)</font> <b>%s:</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
		} else if (type & PURPLE_MESSAGE_SEND) {
			if(purple_message_meify(msg_fixed, -1))
				g_string_append_printf(readdata, "<font color=\"#062585\"><font size=\"2\">(%s)</font> <b>***%s</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
			else
				g_string_append_printf(readdata, "<font color=\"#16569E\"><font size=\"2\">(%s)</font> <b>%s:</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
		} else {
			g_string_append_printf(readdata, "<font size=\"2\">(%s)</font><b> %s:</b></font> %s<br/>\n",
						date, escaped_from, msg_fixed);
		}
		
		g_free(msg_fixed);
		g_free(escaped_from);
	}
	
	return g_string_free(readdata, FALSE);
}

static int
sqlitelog_size (PurpleLog *log)
{
	return 0;
}

static void
sqlitelog_finalize(PurpleLog *log)
{
	sqlite3_stmt *stmt;
	gint64 account_id;
	
	g_return_if_fail(log != NULL);
	
	stmt = log->logger_data;
	sqlite3_finalize(stmt);
	
	account_id = sqlitelog_get_account_id(log->account);
	sqlite3_prepare(db, "UPDATE logs SET endtime=CURRENT_TIMESTAMP WHERE logs.account_id=? AND logs.type=? AND logs.name=? AND logs.starttime=DATETIME(?, 'unixepoch') AND endtime IS NULL", -1, &stmt, NULL);
	
	sqlite3_bind_int64(stmt, 1, account_id);
	sqlite3_bind_int(stmt, 2, log->type);
	sqlite3_bind_text(stmt, 3, purple_normalize(log->account, log->name), -1, NULL);
	sqlite3_bind_int(stmt, 4, log->time);
	
	sqlite3_step(stmt);
	sqlite3_finalize(stmt);
}

static gboolean
sqlitelog_remove(PurpleLog *log)
{
	sqlite3_stmt *stmt;
	int ret;
	gint64 account_id;
	
	account_id = sqlitelog_get_account_id(log->account);
	
	sqlite3_prepare(db, "DELETE FROM logs WHERE logs.account_id=? AND logs.type=? AND logs.name=? AND logs.starttime=DATETIME(?, 'unixepoch')", -1, &stmt, NULL);
	
	sqlite3_bind_int64(stmt, 1, account_id);
	sqlite3_bind_int(stmt, 2, log->type);
	sqlite3_bind_text(stmt, 3, purple_normalize(log->account, log->name), -1, NULL);
	sqlite3_bind_int(stmt, 4, log->time);
	
	ret = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	
	return (ret == SQLITE_OK);
}

/*****************************************************************************
 * Plugin Code                                                               *
 *****************************************************************************/

static void
init_plugin(PurplePlugin *plugin)
{

}

static gboolean
plugin_load(PurplePlugin *plugin)
{
	sqlite_logger = purple_log_logger_new("sqlite", "SQLite", 11,
									   sqlitelog_create,
									   sqlitelog_write,
									   sqlitelog_finalize,
									   sqlitelog_list,
									   sqlitelog_read,
									   NULL, //sqlitelog_size,
									   NULL, //sqlitelog_total_size,
									   NULL, //sqlitelog_list_syslog,
									   NULL, //sqlitelog_get_log_sets,
									   sqlitelog_remove,
									   NULL, //sqlitelog_is_deletable
									   
									   NULL //padding
									   );
	purple_log_logger_add(sqlite_logger);
	
	sqlitelog_init_db();

	return TRUE;
}

static gboolean
plugin_unload(PurplePlugin *plugin)
{
	g_return_val_if_fail(plugin != NULL, FALSE);

	purple_log_logger_remove(sqlite_logger);
	purple_log_logger_free(sqlite_logger);
	sqlite_logger = NULL;
	
	sqlite3_close(db);

	return TRUE;
}

static PurplePluginInfo info =
{
	PURPLE_PLUGIN_MAGIC,
	PURPLE_MAJOR_VERSION,
	PURPLE_MINOR_VERSION,
	PURPLE_PLUGIN_STANDARD,                             /**< type           */
	NULL,                                             /**< ui_requirement */
	0,                                                /**< flags          */
	NULL,                                             /**< dependencies   */
	PURPLE_PRIORITY_DEFAULT,                            /**< priority       */
	"core-sqlite-log",                                /**< id             */
	("SQLite Logging"),                                 /**< name           */
	"0.2",                                  /**< version        */

	/** summary */
	("Log conversations to SQLite backend."),

	/** description */
	(""),

	"Eion Robb <eionrobb@gmail.com>",             /**< author         */
	"",                                     /**< homepage       */
	plugin_load,                                      /**< load           */
	plugin_unload,                                    /**< unload         */
	NULL,                                             /**< destroy        */
	NULL,                                             /**< ui_info        */
	NULL,                                             /**< extra_info     */
	NULL,                                      /**< prefs_info     */
	NULL,                                             /**< actions        */

	/* padding */
	NULL,
	NULL,
	NULL,
	NULL
};

PURPLE_INIT_PLUGIN(sqlitelog, init_plugin, info)
