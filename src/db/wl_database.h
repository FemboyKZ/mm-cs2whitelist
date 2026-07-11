#pragma once

#include "mmu/sql.h"
#include "wl_config.h"

#include <functional>
#include <string>
#include <unordered_set>

class WLDatabase
{
public:
	// Acquire sql_mm interface via MetaFactory. Call in AllPluginsLoaded().
	// Returns false if sql_mm is not loaded or database is disabled in config.
	bool Init(const WLConfig &cfg);

	// Async connect. Callback fires on the main thread with success flag.
	void Connect(std::function<void(bool)> callback);

	// Gracefully destroy the connection.
	void Shutdown();

	bool IsConnected() const
	{
		return m_conn.IsConnected();
	}

	bool IsEnabled() const
	{
		return m_enabled;
	}

	// Asynchronously merge all rows from the DB into outSet.
	// Callback receives the number of entries loaded.
	void LoadEntries(std::unordered_set<std::string> &outSet, std::function<void(int)> callback);

	// persist an add/remove to the DB.
	void AddEntry(const std::string &authid);
	void RemoveEntry(const std::string &authid);

private:
	// Create schema tables if they don't exist.
	void CreateSchema();

	// Forwarders to the shared connection.
	void Query(const char *query, std::function<void(ISQLQuery *)> cb)
	{
		m_conn.Query(query, std::move(cb));
	}

	std::string Escape(const char *str)
	{
		return m_conn.Escape(str);
	}

	mmu::sql::Connection m_conn;
	mmu::sql::ConnectParams m_params;

	bool m_enabled = false;
	bool m_bMySQL = false;
	std::string m_prefix;
};

extern WLDatabase g_WLDatabase;
