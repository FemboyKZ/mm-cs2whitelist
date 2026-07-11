#include "steamgroup_manager.h"
#include "mmu/http_client.h"
#include "mmu/log.h"

#include "common.h"
#include "lang/translations.h"
#include "whitelist/whitelist_manager.h"

#include <eiface.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

SteamGroupManager g_SteamGroupManager;

// Reject absurd response bodies before parsing.
static constexpr size_t kMaxBodySize = 4u * 1024u * 1024u;

static int ExtractIntTag(const std::string &body, const char *open, const char *close)
{
	size_t pos = body.find(open);
	if (pos == std::string::npos)
	{
		return 0;
	}
	pos += strlen(open);
	size_t end = body.find(close, pos);
	if (end == std::string::npos)
	{
		return 0;
	}
	char *endptr = nullptr;
	long val = std::strtol(body.c_str() + pos, &endptr, 10);
	if (endptr == body.c_str() + pos)
	{
		return 0;
	}
	return static_cast<int>(val);
}

void SteamGroupManager::Init(const Config &cfg)
{
	m_cfg = cfg;

	if (m_cfg.enabled)
	{
		MMU_LOG_INFO("SteamGroup: initialized (method=%s).\n", m_cfg.method == Method::API ? "api" : "xml");
	}
}

void SteamGroupManager::Shutdown()
{
	// Drop any late HTTP continuations from this cycle.
	m_generation++;
	m_xmlInFlight = 0;

	m_pendingXml.clear();
	m_pendingApi.clear();
	m_memberSets.clear();
	m_fetchedGroups.clear();
	m_expectedCounts.clear();
}

void SteamGroupManager::FetchGroups()
{
	if (!m_cfg.enabled)
	{
		return;
	}

	{
		std::unordered_set<uint64_t> seen;
		m_effectiveGroupIds.clear();
		for (uint64_t gid : m_cfg.groupIds)
		{
			if (seen.insert(gid).second)
			{
				m_effectiveGroupIds.push_back(gid);
			}
		}
		for (uint64_t gid : g_WLManager.GetFileGroupIds())
		{
			if (seen.insert(gid).second)
			{
				m_effectiveGroupIds.push_back(gid);
			}
		}
	}

	if (m_effectiveGroupIds.empty())
	{
		return;
	}

	if (m_cfg.method != Method::XML)
	{
		MMU_LOG_INFO("SteamGroup: effective group IDs loaded (%d), method=api.\n", (int)m_effectiveGroupIds.size());
		return;
	}

	StartXmlFetches();
}

void SteamGroupManager::StartXmlFetches()
{
	// Invalidate any fetch cycle still in flight before clearing its state.
	m_generation++;
	m_xmlInFlight = 0;

	m_memberSets.clear();
	m_fetchedGroups.clear();
	m_expectedCounts.clear();

	for (uint64_t gid : m_effectiveGroupIds)
	{
		StartXmlFetch(gid, 1);
	}
}

void SteamGroupManager::StartXmlFetch(uint64_t groupId, int page)
{
	char url[300];
	if (page <= 1)
	{
		snprintf(url, sizeof(url), "https://steamcommunity.com/gid/%llu/memberslistxml/?xml=1", (unsigned long long)groupId);
	}
	else
	{
		snprintf(url, sizeof(url), "https://steamcommunity.com/gid/%llu/memberslistxml/?xml=1&p=%d", (unsigned long long)groupId, page);
	}

	m_xmlInFlight++;
	const uint32_t gen = m_generation;
	mmu::http::Get(url,
				   [this, gen, groupId, page](bool ok, std::string body)
				   {
					   mmu::http::QueueMainThread(
						   [this, gen, groupId, page, ok, body = std::move(body)]()
						   {
							   if (gen != m_generation)
							   {
								   return;
							   }
							   OnXmlResponse(groupId, page, ok, body);
						   });
				   });

	MMU_LOG_INFO("SteamGroup: fetching group %llu page %d...\n", (unsigned long long)groupId, page);
}

bool SteamGroupManager::StartApiFetch(int slot, uint64_t xuid)
{
	if (m_cfg.apiKey.empty())
	{
		MMU_LOG_INFO("SteamGroup: StartApiFetch skipped (no API key).\n");
		return false;
	}

	char url[600];
	snprintf(url, sizeof(url), "https://api.steampowered.com/ISteamUser/GetUserGroupList/v1/?key=%s&steamid=%llu", m_cfg.apiKey.c_str(),
			 (unsigned long long)xuid);

	PendingPlayer pp;
	pp.xuid = xuid;
	pp.startTime = std::chrono::steady_clock::now();
	m_pendingApi[slot] = pp;

	const uint32_t gen = m_generation;
	mmu::http::Get(url,
				   [this, gen, slot, xuid](bool ok, std::string body)
				   {
					   mmu::http::QueueMainThread(
						   [this, gen, slot, xuid, ok, body = std::move(body)]()
						   {
							   if (gen != m_generation)
							   {
								   return;
							   }
							   OnApiResponse(slot, xuid, ok, body);
						   });
				   });

	MMU_LOG_INFO("SteamGroup: API check started for slot=%d xuid=%llu\n", slot, (unsigned long long)xuid);
	return true;
}

bool SteamGroupManager::CheckPlayer(int slot, uint64_t xuid, bool &pending)
{
	pending = false;

	if (!m_cfg.enabled)
	{
		MMU_LOG_WARN("SteamGroup: CheckPlayer slot=%d - disabled.\n", slot);
		return false;
	}
	if (m_effectiveGroupIds.empty())
	{
		MMU_LOG_INFO("SteamGroup: CheckPlayer slot=%d - no group IDs configured.\n", slot);
		return false;
	}

	if (m_cfg.method == Method::XML)
	{
		if (!AllGroupsFetched())
		{
			if (m_xmlInFlight == 0)
			{
				MMU_LOG_INFO("SteamGroup: slot=%d triggered deferred XML fetch.\n", slot);
				StartXmlFetches();
			}

			PendingPlayer pp;
			pp.xuid = xuid;
			pp.startTime = std::chrono::steady_clock::now();
			m_pendingXml[slot] = pp;
			pending = true;
			return false;
		}
		return IsXuidInAnyGroup(xuid);
	}
	else
	{
		if (m_pendingApi.count(slot))
		{
			pending = true;
			return false;
		}
		if (!StartApiFetch(slot, xuid))
		{
			MMU_LOG_WARN("SteamGroup: StartApiFetch failed for slot=%d xuid=%llu\n", slot, (unsigned long long)xuid);
			return false; // fail open to kick rather than hang
		}
		pending = true;
		return false;
	}
}

void SteamGroupManager::OnXmlResponse(uint64_t groupId, int page, bool ok, const std::string &body)
{
	if (m_xmlInFlight > 0)
	{
		m_xmlInFlight--;
	}

	if (ok && !body.empty() && body.size() < kMaxBodySize)
	{
		ParseXmlBody(groupId, page, body);
		return;
	}

	MMU_LOG_WARN("SteamGroup: XML fetch failed for group %llu p%d\n", (unsigned long long)groupId, page);
	// Mark as done so pending players aren't stuck forever
	m_fetchedGroups.insert(groupId);
	if (AllGroupsFetched())
	{
		ProcessPendingXmlPlayers();
	}
}

void SteamGroupManager::OnApiResponse(int slot, uint64_t xuid, bool ok, const std::string &body)
{
	// Timed out, disconnected, or the slot was reused: drop the late answer.
	auto it = m_pendingApi.find(slot);
	if (it == m_pendingApi.end() || it->second.xuid != xuid)
	{
		return;
	}
	m_pendingApi.erase(it);

	const bool inGroup = ok && !body.empty() && body.size() < kMaxBodySize && ParseApiResponse(xuid, body);
	MMU_LOG_INFO("SteamGroup: API response for slot=%d xuid=%llu: %s (body_len=%u)\n", slot, (unsigned long long)xuid,
				 inGroup ? "IN GROUP" : "NOT IN GROUP", (unsigned)body.size());
	if (inGroup)
	{
		AllowPlayer(slot, xuid);
	}
	else
	{
		KickPlayer(slot);
	}
}

void SteamGroupManager::ParseXmlBody(uint64_t groupId, int page, const std::string &body)
{
	auto &members = m_memberSets[groupId];

	// First page: read expected member count
	if (page == 1)
	{
		int total = ExtractIntTag(body, "<memberCount>", "</memberCount>");
		m_expectedCounts[groupId] = total;
		MMU_LOG_INFO("SteamGroup: group %llu has %d members.\n", (unsigned long long)groupId, total);
	}

	// Parse <members> block
	const size_t membersStart = body.find("<members>");
	const size_t membersEnd = body.find("</members>", membersStart);
	if (membersStart == std::string::npos || membersEnd == std::string::npos)
	{
		m_fetchedGroups.insert(groupId);
		if (AllGroupsFetched())
		{
			ProcessPendingXmlPlayers();
		}
		return;
	}

	static const char *kOpen = "<steamID64>";
	static const char *kClose = "</steamID64>";
	static const size_t kOpenLen = 11;  // strlen("<steamID64>")
	static const size_t kCloseLen = 12; // strlen("</steamID64>")

	int count = 0;
	size_t pos = membersStart;
	while (true)
	{
		size_t tagStart = body.find(kOpen, pos);
		if (tagStart == std::string::npos || tagStart >= membersEnd)
		{
			break;
		}
		tagStart += kOpenLen;
		size_t tagEnd = body.find(kClose, tagStart);
		if (tagEnd == std::string::npos || tagEnd >= membersEnd)
		{
			break;
		}

		const std::string idStr = body.substr(tagStart, tagEnd - tagStart);
		uint64_t id = std::strtoull(idStr.c_str(), nullptr, 10);
		if (id != 0)
		{
			members.insert(id);
			++count;
		}

		pos = tagEnd + kCloseLen;
	}

	MMU_LOG_INFO("SteamGroup: group %llu p%d: +%d members (%d total in set)\n", (unsigned long long)groupId, page, count, (int)members.size());

	const int expected = m_expectedCounts.count(groupId) ? m_expectedCounts.at(groupId) : 0;
	if (expected > 0 && static_cast<int>(members.size()) < expected)
	{
		StartXmlFetch(groupId, page + 1);
	}
	else
	{
		m_fetchedGroups.insert(groupId);
		MMU_LOG_INFO("SteamGroup: group %llu complete (%d members).\n", (unsigned long long)groupId, static_cast<int>(members.size()));
		if (AllGroupsFetched())
		{
			MMU_LOG_INFO("SteamGroup: all groups fetched.\n");
			ProcessPendingXmlPlayers();
		}
	}
}

bool SteamGroupManager::ParseApiResponse(uint64_t xuid, const std::string &body) const
{
	// Response: {"response":{"success":true,"groups":[{"gid":"103582791..."},...]}}
	// We just scan for "gid":"VALUE" patterns.
	(void)xuid; // xuid not needed to identify the player here; groupIds are what matter

	size_t pos = 0;
	while (true)
	{
		pos = body.find("\"gid\"", pos);
		if (pos == std::string::npos)
		{
			break;
		}
		pos += 5; // skip past "gid"

		// skip : " (possibly with whitespace)
		while (pos < body.size() && (body[pos] == ':' || body[pos] == '"' || body[pos] == ' '))
		{
			++pos;
		}

		size_t end = pos;
		while (end < body.size() && body[end] >= '0' && body[end] <= '9')
		{
			++end;
		}

		if (end > pos)
		{
			uint64_t gid = std::strtoull(body.c_str() + pos, nullptr, 10);

			// The API returns short 32-bit clan IDs; promote to full group ID64
			// using the same formula as whitelist_manager.cpp: 0x0170000000000000 | shortId
			if (gid != 0 && (gid >> 32) == 0)
			{
				gid = 0x0170000000000000ULL | gid;
			}

			for (uint64_t wanted : m_effectiveGroupIds)
			{
				if (gid == wanted)
				{
					return true;
				}
			}
		}
		pos = end;
	}
	return false;
}

bool SteamGroupManager::IsXuidInAnyGroup(uint64_t xuid) const
{
	for (const auto &kv : m_memberSets)
	{
		if (kv.second.count(xuid))
		{
			return true;
		}
	}
	return false;
}

bool SteamGroupManager::AllGroupsFetched() const
{
	if (m_effectiveGroupIds.empty())
	{
		return false;
	}
	for (uint64_t gid : m_effectiveGroupIds)
	{
		if (!m_fetchedGroups.count(gid))
		{
			return false;
		}
	}
	return true;
}

void SteamGroupManager::ProcessPendingXmlPlayers()
{
	for (auto &[slot, pp] : m_pendingXml)
	{
		if (IsXuidInAnyGroup(pp.xuid))
		{
			AllowPlayer(slot, pp.xuid);
		}
		else
		{
			KickPlayer(slot);
		}
	}
	m_pendingXml.clear();
}

void SteamGroupManager::AllowPlayer(int slot, uint64_t xuid)
{
	g_WLManager.AddToWhitelistCache(xuid);
	MMU_LOG_INFO("SteamGroup: slot %d allowed via group membership.\n", slot);
}

void SteamGroupManager::KickPlayer(int slot)
{
	if (!g_pEngine)
	{
		return;
	}
	std::string msg = WL_Translate(slot, "You are not whitelisted on this server.");
	CPlayerSlot playerSlot(slot);
	char kickmsg[512];
	snprintf(kickmsg, sizeof(kickmsg), "[WHITELIST] %s\n", msg.c_str());
	g_pEngine->ClientPrintf(playerSlot, kickmsg);
	g_pEngine->DisconnectClient(playerSlot, NETWORK_DISCONNECT_KICKED, msg.c_str());
}

void SteamGroupManager::OnGameFrame()
{
	const auto now = std::chrono::steady_clock::now();
	const auto timeout = std::chrono::duration<float>(m_cfg.timeout);

	for (auto it = m_pendingXml.begin(); it != m_pendingXml.end();)
	{
		if (std::chrono::duration<float>(now - it->second.startTime) >= timeout)
		{
			const int slot = it->first;
			it = m_pendingXml.erase(it);
			MMU_LOG_WARN("SteamGroup: slot %d timed out waiting for XML data.\n", slot);
			KickPlayer(slot);
		}
		else
		{
			++it;
		}
	}

	for (auto it = m_pendingApi.begin(); it != m_pendingApi.end();)
	{
		if (std::chrono::duration<float>(now - it->second.startTime) >= timeout)
		{
			const int slot = it->first;
			it = m_pendingApi.erase(it);
			// The late HTTP response is dropped by OnApiResponse's pending lookup.
			MMU_LOG_WARN("SteamGroup: slot %d API check timed out.\n", slot);
			KickPlayer(slot);
		}
		else
		{
			++it;
		}
	}
}

void SteamGroupManager::OnPlayerDisconnect(int slot)
{
	m_pendingXml.erase(slot);
	m_pendingApi.erase(slot);
}
