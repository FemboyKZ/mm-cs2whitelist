#pragma once

#include <functional>
#include <string>
#include <unordered_set>

#include "wl_config.h"

// Forward declarations – full types come from sql_mm headers included in wl_database.cpp
class ISQLInterface;
class ISQLConnection;
class ISQLQuery;
class IMySQLClient;
class ISQLiteClient;

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
		return m_bConnected;
	}

	bool IsEnabled() const
	{
		return m_bEnabled;
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

	// Wrappers around m_pConnection->Query()
	void Query(const char *query, std::function<void(ISQLQuery *)> cb);
	void QueryFmt(std::function<void(ISQLQuery *)> cb, const char *fmt, ...);

	std::string Escape(const char *str);

	ISQLInterface *m_pSQLInterface = nullptr;
	IMySQLClient *m_pMySQLClient = nullptr;
	ISQLiteClient *m_pSQLiteClient = nullptr;
	ISQLConnection *m_pConnection = nullptr;

	bool m_bConnected = false;
	bool m_bEnabled = false;
	bool m_bMySQL = false;

	std::string m_prefix;
	std::string m_dbPath;  // scratch field (not used after Connect())
	WLConfig m_connectCfg; // full config stored for Connect()
};

extern WLDatabase g_WLDatabase;
