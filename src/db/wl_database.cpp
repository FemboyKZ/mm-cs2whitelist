#include "wl_database.h"
#include "mmu/log.h"
#include "wl_config.h"
#include "common.h"

#include <sql_mm.h>

#include <ctime>

WLDatabase g_WLDatabase;

bool WLDatabase::Init(const WLConfig &cfg)
{
	if (!cfg.dbEnabled)
	{
		MMU_LOG_WARN("Database disabled (dbEnabled=0 in core.cfg).\n");
		return false;
	}

	m_bMySQL = (cfg.dbType == "mysql");
	m_prefix = cfg.dbPrefix;

	if (!m_conn.Init(m_bMySQL ? mmu::sql::DbType::MySQL : mmu::sql::DbType::SQLite))
	{
		return false;
	}
	m_conn.SetSchemaHook([this] { CreateSchema(); });

	m_params.path = cfg.dbPath;
	m_params.host = cfg.dbHost;
	m_params.user = cfg.dbUser;
	m_params.pass = cfg.dbPass;
	m_params.database = cfg.dbName;
	m_params.port = cfg.dbPort;

	m_enabled = true;
	MMU_LOG_INFO("Database initialized (type=%s).\n", m_bMySQL ? "mysql" : "sqlite");
	return true;
}

void WLDatabase::Connect(std::function<void(bool)> callback)
{
	if (!m_enabled)
	{
		if (callback)
		{
			callback(false);
		}
		return;
	}
	m_conn.Connect(m_params, std::move(callback));
}

void WLDatabase::Shutdown()
{
	m_conn.Shutdown();
	m_enabled = false;
}

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

void WLDatabase::LoadEntries(std::unordered_set<std::string> &outSet, std::function<void(int)> callback)
{
	if (!m_conn.IsConnected())
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

void WLDatabase::AddEntry(const std::string &authid)
{
	if (!m_conn.IsConnected())
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
	if (!m_conn.IsConnected())
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
