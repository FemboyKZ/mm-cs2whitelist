#include "wl_database.h"
#include "wl_config.h"
#include "common.h"

#include <sql_mm.h>
#include <mysql_mm.h>
#include <sqlite_mm.h>

#include <ISmmAPI.h>

#include <cstdarg>
#include <ctime>

WLDatabase g_WLDatabase;

// Init
bool WLDatabase::Init(const WLConfig &cfg)
{
	if (!cfg.dbEnabled)
	{
		META_CONPRINTF("[WHITELIST] Database disabled (dbEnabled=0 in core.cfg).\n");
		return false;
	}

	m_pSQLInterface = static_cast<ISQLInterface *>(g_SMAPI->MetaFactory(SQLMM_INTERFACE, nullptr, nullptr));

	if (!m_pSQLInterface)
	{
		META_CONPRINTF("[WHITELIST] sql_mm not loaded; database support unavailable.\n");
		return false;
	}

	m_prefix = cfg.dbPrefix;
	m_bMySQL = (cfg.dbType == "mysql");

	if (m_bMySQL)
	{
		m_pMySQLClient = m_pSQLInterface->GetMySQLClient();
		if (!m_pMySQLClient)
		{
			META_CONPRINTF("[WHITELIST] sql_mm MySQL client unavailable.\n");
			return false;
		}
		m_dbPath = cfg.dbHost;
	}
	else
	{
		m_pSQLiteClient = m_pSQLInterface->GetSQLiteClient();
		if (!m_pSQLiteClient)
		{
			META_CONPRINTF("[WHITELIST] sql_mm SQLite client unavailable.\n");
			return false;
		}
		m_dbPath = cfg.dbPath;
	}

	m_connectCfg = cfg;
	m_bEnabled = true;
	META_CONPRINTF("[WHITELIST] Database initialized (type=%s).\n", m_bMySQL ? "mysql" : "sqlite");
	return true;
}

// Connect
void WLDatabase::Connect(std::function<void(bool)> callback)
{
	if (!m_bEnabled)
	{
		if (callback)
		{
			callback(false);
		}
		return;
	}

	if (m_bMySQL)
	{
		MySQLConnectionInfo info;
		info.host = m_connectCfg.dbHost.c_str();
		info.user = m_connectCfg.dbUser.c_str();
		info.pass = m_connectCfg.dbPass.c_str();
		info.database = m_connectCfg.dbName.c_str();
		info.port = m_connectCfg.dbPort;

		m_pConnection = m_pMySQLClient->CreateMySQLConnection(info);
	}
	else
	{
		SQLiteConnectionInfo info;
		info.database = m_connectCfg.dbPath.c_str();
		m_pConnection = m_pSQLiteClient->CreateSQLiteConnection(info);
	}

	if (!m_pConnection)
	{
		META_CONPRINTF("[WHITELIST] Failed to create database connection object.\n");
		if (callback)
		{
			callback(false);
		}
		return;
	}

	m_pConnection->Connect(
		[this, callback](bool success)
		{
			m_bConnected = success;
			if (success)
			{
				META_CONPRINTF("[WHITELIST] Database connected (%s).\n", m_bMySQL ? "MySQL" : "SQLite");

				if (!m_bMySQL)
				{
					Query("PRAGMA journal_mode=WAL", [](ISQLQuery *) {});
					Query("PRAGMA foreign_keys=ON", [](ISQLQuery *) {});
				}
				else
				{
					Query("SET NAMES utf8mb4", [](ISQLQuery *) {});
				}

				CreateSchema();
			}
			else
			{
				META_CONPRINTF("[WHITELIST] Database connection failed.\n");
			}
			if (callback)
			{
				callback(success);
			}
		});
}

// Shutdown
void WLDatabase::Shutdown()
{
	if (m_pConnection)
	{
		m_pConnection->Destroy();
		m_pConnection = nullptr;
	}
	m_bConnected = false;
	m_bEnabled = false;
	m_pMySQLClient = nullptr;
	m_pSQLiteClient = nullptr;
	m_pSQLInterface = nullptr;
}

// Schema
void WLDatabase::CreateSchema()
{
	const char *p = m_prefix.c_str();
	char q[1024];

	if (!m_bMySQL)
	{
		snprintf(q, sizeof(q),
				 "CREATE TABLE IF NOT EXISTS %s_whitelist ("
				 "id INTEGER PRIMARY KEY AUTOINCREMENT, "
				 "authid TEXT NOT NULL UNIQUE, "
				 "created_at INTEGER NOT NULL DEFAULT 0, "
				 "note TEXT DEFAULT NULL"
				 ")",
				 p);
	}
	else
	{
		snprintf(q, sizeof(q),
				 "CREATE TABLE IF NOT EXISTS `%s_whitelist` ("
				 "`id` INT UNSIGNED NOT NULL AUTO_INCREMENT, "
				 "`authid` VARCHAR(64) NOT NULL, "
				 "`created_at` INT UNSIGNED NOT NULL DEFAULT 0, "
				 "`note` VARCHAR(255) DEFAULT NULL, "
				 "PRIMARY KEY (`id`), "
				 "UNIQUE KEY `uk_authid` (`authid`)"
				 ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
				 p);
	}
	Query(q, [](ISQLQuery *) {});
}

// Load entries
void WLDatabase::LoadEntries(std::unordered_set<std::string> &outSet, std::function<void(int)> callback)
{
	if (!m_bConnected)
	{
		if (callback)
		{
			callback(0);
		}
		return;
	}

	const char *p = m_prefix.c_str();
	char q[256];

	if (!m_bMySQL)
	{
		snprintf(q, sizeof(q), "SELECT authid FROM %s_whitelist", p);
	}
	else
	{
		snprintf(q, sizeof(q), "SELECT `authid` FROM `%s_whitelist`", p);
	}

	// Capture outSet by reference - safe because LoadEntries is called on the main
	// thread and the callback fires on the main thread too (sql_mm guarantees this).
	Query(q,
		  [&outSet, callback](ISQLQuery *query)
		  {
			  int count = 0;
			  if (!query)
			  {
				  if (callback)
				  {
					  callback(0);
				  }
				  return;
			  }

			  ISQLResult *res = query->GetResultSet();
			  if (res)
			  {
				  while (res->MoreRows())
				  {
					  ISQLRow *row = res->FetchRow();
					  (void)row;
					  const char *authid = res->GetString(0);
					  if (authid && authid[0])
					  {
						  outSet.insert(authid);
						  ++count;
					  }
				  }
			  }
			  if (callback)
			  {
				  callback(count);
			  }
		  });
}

// AddEntry / RemoveEntry
void WLDatabase::AddEntry(const std::string &authid)
{
	if (!m_bConnected)
	{
		return;
	}

	std::string esc = Escape(authid.c_str());
	long long now = static_cast<long long>(std::time(nullptr));
	const char *p = m_prefix.c_str();
	char q[512];

	if (!m_bMySQL)
	{
		snprintf(q, sizeof(q), "INSERT OR IGNORE INTO %s_whitelist (authid, created_at) VALUES ('%s', %lld)", p, esc.c_str(), now);
	}
	else
	{
		snprintf(q, sizeof(q), "INSERT IGNORE INTO `%s_whitelist` (`authid`, `created_at`) VALUES ('%s', %lld)", p, esc.c_str(), now);
	}
	Query(q, [](ISQLQuery *) {});
}

void WLDatabase::RemoveEntry(const std::string &authid)
{
	if (!m_bConnected)
	{
		return;
	}

	std::string esc = Escape(authid.c_str());
	const char *p = m_prefix.c_str();
	char q[512];

	if (!m_bMySQL)
	{
		snprintf(q, sizeof(q), "DELETE FROM %s_whitelist WHERE authid = '%s'", p, esc.c_str());
	}
	else
	{
		snprintf(q, sizeof(q), "DELETE FROM `%s_whitelist` WHERE `authid` = '%s'", p, esc.c_str());
	}

	Query(q, [](ISQLQuery *) {});
}

// Internal helpers
void WLDatabase::Query(const char *query, std::function<void(ISQLQuery *)> cb)
{
	if (!m_pConnection || !m_bConnected)
	{
		if (cb)
		{
			cb(nullptr);
		}
		return;
	}
	m_pConnection->Query(query, cb ? cb : [](ISQLQuery *) {});
}

void WLDatabase::QueryFmt(std::function<void(ISQLQuery *)> cb, const char *fmt, ...)
{
	char buf[4096];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	Query(buf, cb);
}

std::string WLDatabase::Escape(const char *str)
{
	if (!m_pConnection)
	{
		return str ? str : "";
	}
	return m_pConnection->Escape(str);
}
