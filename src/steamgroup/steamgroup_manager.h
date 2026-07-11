#ifndef _INCLUDE_WL_STEAMGROUP_MANAGER_H_
#define _INCLUDE_WL_STEAMGROUP_MANAGER_H_

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Steam group whitelisting over the shared mmu::http client.
// HTTP callbacks arrive on a background thread and are marshalled to the game thread via mmu::http::QueueMainThread, drained from the GameFrame hook.
class SteamGroupManager
{
public:
	enum class Method
	{
		XML, // Pre-fetch full member list via public XML endpoint (no API key)
		API, // Per-player check via Steam Web API (requires API key)
	};

	struct Config
	{
		bool enabled = false;
		Method method = Method::XML;
		std::string apiKey;
		float timeout = 10.0f;
		std::vector<uint64_t> groupIds;
	};

	void Init(const Config &cfg);
	void Shutdown();

	// Kick off XML pre-fetches (call on plugin load and each map start).
	void FetchGroups();

	// Check whether this player is whitelisted via a Steam group.
	//   pending=true  -> async check started; caller MUST NOT kick yet.
	//   pending=false -> synchronous result; return value is the answer.
	bool CheckPlayer(int slot, uint64_t xuid, bool &pending);

	// Timeout/cleanup tick - call once per game frame.
	void OnGameFrame();

	// Cancel any in-progress check for a disconnecting player.
	void OnPlayerDisconnect(int slot);

	bool IsEnabled() const
	{
		return m_cfg.enabled;
	}

private:
	struct PendingPlayer
	{
		uint64_t xuid;
		std::chrono::steady_clock::time_point startTime;
	};

	// XML member sets: groupId64 -> set of member steamid64s
	std::unordered_map<uint64_t, std::unordered_set<uint64_t>> m_memberSets;
	// Expected total member count per group (read from first XML page)
	std::unordered_map<uint64_t, int> m_expectedCounts;
	// Groups whose XML fetch is complete
	std::unordered_set<uint64_t> m_fetchedGroups;

	// Players waiting for XML data to finish loading: slot -> info
	std::unordered_map<int, PendingPlayer> m_pendingXml;
	// Players waiting for an API response: slot -> info
	std::unordered_map<int, PendingPlayer> m_pendingApi;

	Config m_cfg;
	// Merged group IDs: m_cfg.groupIds n IDs from whitelist.txt (populated in FetchGroups)
	std::vector<uint64_t> m_effectiveGroupIds;

	// Bumped on Shutdown and each StartXmlFetches so late HTTP continuations from a previous cycle are dropped instead of touching cleared state.
	uint32_t m_generation = 0;
	// XML fetches currently in flight (drives the deferred-fetch trigger).
	int m_xmlInFlight = 0;

	void StartXmlFetches();
	void StartXmlFetch(uint64_t groupId, int page);
	bool StartApiFetch(int slot, uint64_t xuid);

	// Main-thread continuations for completed requests.
	void OnXmlResponse(uint64_t groupId, int page, bool ok, const std::string &body);
	void OnApiResponse(int slot, uint64_t xuid, bool ok, const std::string &body);

	void ParseXmlBody(uint64_t groupId, int page, const std::string &body);
	bool ParseApiResponse(uint64_t xuid, const std::string &body) const;

	bool IsXuidInAnyGroup(uint64_t xuid) const;
	bool AllGroupsFetched() const;

	void ProcessPendingXmlPlayers();
	void AllowPlayer(int slot, uint64_t xuid);
	void KickPlayer(int slot);
};

extern SteamGroupManager g_SteamGroupManager;

#endif // _INCLUDE_WL_STEAMGROUP_MANAGER_H_
