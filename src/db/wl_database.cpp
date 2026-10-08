#include "wl_database.h"
#include "utils/log.h"
#include "utils/steamid.h"
#include "wl_config.h"
#include "common.h"

#include "interfaces/sql_mm/sql_mm.h"

#include <ctime>

WLDatabase g_WLDatabase;

bool WLDatabase::Init(const WLConfig &cfg)
{
	if (!cfg.dbEnabled)
	{
		MMU_LOG_WARN("Database disabled (dbEnabled=0 in core.cfg).\n");
		return false;
	}

	m_bMySQL = cfg.database.IsMySQL();
	m_prefix = cfg.database.prefix;

	if (!m_conn.Init(m_bMySQL ? mmu::sql::DbType::MySQL : mmu::sql::DbType::SQLite))
	{
		return false;
	}
	m_conn.SetSchemaHook([this] { CreateSchema(); });

	m_params = cfg.database.ToConnectParams();

	m_enabled = true;
	m_startupLoadPending = true;
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
	m_conn.Connect(m_params,
				   [this, callback](bool success)
				   {
					   if (!success)
					   {
						   m_startupLoadPending = false;
					   }
					   if (callback)
					   {
						   callback(success);
					   }
				   });
}

void WLDatabase::Shutdown()
{
	m_conn.Shutdown();
	m_enabled = false;
	m_startupLoadPending = false;
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
	// Logged from the query callback, which only fires once the statement actually round-trips.
	Query(q,
		  [](ISQLQuery *result)
		  {
			  if (result)
			  {
				  MMU_LOG_INFO("Whitelist schema created/verified.\n");
			  }
			  else
			  {
				  MMU_LOG_WARN("Whitelist schema creation failed, table may be missing.\n");
			  }
		  });
}

void WLDatabase::LoadEntries(std::function<void(bool ok, const std::unordered_set<std::string> &rows)> callback)
{
	if (!m_conn.IsConnected())
	{
		callback(false, {});
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
		  [this, callback](ISQLQuery *query)
		  {
			  m_startupLoadPending = false;

			  std::unordered_set<std::string> rows;
			  if (!query)
			  {
				  callback(false, rows);
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
						  rows.insert(authid);
					  }
				  }
			  }
			  callback(true, rows);
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

	// Rows may hold the SteamID in another form.
	std::string match = "authid = '" + Escape(authid.c_str()) + "'";
	if (const uint64_t steamid64 = ParseSteamID64(authid))
	{
		match = mmu::sql::AuthMatch("authid", SteamID64ToSuffix(steamid64)) + " OR authid = '" + std::to_string(steamid64) + "'";
	}

	const char *p = m_prefix.c_str();
	char q[512];
	snprintf(q, sizeof(q), m_bMySQL ? "DELETE FROM `%s_whitelist` WHERE %s" : "DELETE FROM %s_whitelist WHERE %s", p, match.c_str());

	Query(q, [](ISQLQuery *) {});
}
