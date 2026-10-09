// The team reference driver's fixed part (build.sh appends the original functions and team_main.cpp):
// stand-ins for what the team modes reach — Server::updateCTF (ServerCTF.cpp), Server::autoBalance and
// the auto-balance and "Champion" blocks of Server::update (Server.cpp), Game::assignPlayerTeam
// (Game.cpp), Game::spawnPlayer (GameSpawn.cpp) and Player::kill (Player.cpp), the dedicated Pro
// build — and recorders for every bb_serverSend (with the padding zeroed, as the Go server sends it).
//
// This file is part of openbv, under the GNU General Public License v3 or later.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>
#include "CVector.h"
#include "netPacket.h"

static unsigned int msvcSeed = 1;
extern "C" int rand(void)
{
	msvcSeed = msvcSeed * 214013u + 2531011u;
	return (int)((msvcSeed >> 16) & 0x7fff);
}
extern "C" void srand(unsigned int seed) { msvcSeed = seed; }

#define MAX_PLAYER 32
#define PLAYER_STATUS_ALIVE 0 // Player.h:33
#define PLAYER_STATUS_DEAD 1
#define PLAYER_TEAM_SPECTATOR -1 // Player.h:40
#define PLAYER_TEAM_BLUE 0
#define PLAYER_TEAM_RED 1
#define PLAYER_TEAM_AUTO_ASSIGN 2
#define GAME_TYPE_DM 0 // Game.h
#define GAME_TYPE_TDM 1
#define GAME_TYPE_CTF 2
#define GAME_TYPE_SND 3
#define SPAWN_TYPE_LADDER 1

// --- the recorders
static std::string events;
static void ev(const std::string & s) { if (!events.empty()) events += ","; events += s; }
static std::string fbits(float v) { unsigned int u; memcpy(&u, &v, 4); char b[16]; snprintf(b, sizeof b, "%u", u); return b; }
static std::string vbits(const CVector3f & v) { return "[" + fbits(v[0]) + "," + fbits(v[1]) + "," + fbits(v[2]) + "]"; }
static std::string hex(const void * p, int n)
{
	std::string s;
	char b[4];
	for (int i = 0; i < n; ++i) { snprintf(b, sizeof b, "%02x", ((const unsigned char *)p)[i]); s += b; }
	return s;
}

static void bb_serverSend(char * data, int size, int typeID, int dest = 0, int protocol = 0)
{
	char clean[64];
	memset(clean, 0, sizeof clean);
	switch (typeID)
	{
	case NET_SVCL_DROP_FLAG:
	{
		net_svcl_drop_flag in, & out = *(net_svcl_drop_flag *)clean;
		memcpy(&in, data, sizeof in);
		out.flagID = in.flagID;
		memcpy(out.position, in.position, sizeof in.position);
		break;
	}
	case NET_SVCL_PLAYER_HIT:
	{
		net_svcl_player_hit in, & out = *(net_svcl_player_hit *)clean;
		memcpy(&in, data, sizeof in);
		out.playerID = in.playerID;
		out.fromID = in.fromID;
		out.weaponID = in.weaponID;
		out.damage = in.damage;
		memcpy(out.vel, in.vel, sizeof in.vel);
		break;
	}
	default:
		if (data && size > 0) memcpy(clean, data, size); // no padding: CHANGE_FLAG_STATE, TEAM_REQUEST
	}
	char b[32];
	snprintf(b, sizeof b, "{\"send\":%d,\"b\":\"", typeID);
	ev(std::string(b) + hex(clean, size) + "\"}");
}

// console->add(CString(...)): the logs are not compared
struct CString
{
	char * s;
	CString() : s((char *)"") {}
	CString(const char *, ...) : s((char *)"") {}
};
struct ConsoleStub { void add(const CString &, bool = false) {} } consoleStub;
static ConsoleStub * console = &consoleStub;

// --- the map (Map.h): the flags and the spawns. dm_spawns records a read past its end (Champion's
// clamp lets index == size through: undefined in the original; the Go port clamps it, §8.4).
struct SpawnList
{
	std::vector<CVector3f> v;
	bool readPastEnd;
	size_t size() const { return v.size(); }
	CVector3f & operator[](size_t i)
	{
		static CVector3f past;
		if (i >= v.size()) { readPastEnd = true; return past; }
		return v[i];
	}
};
struct Map
{
	CVector3f flagPodPos[2];
	CVector3f flagPos[2];
	char flagState[2];
	SpawnList dm_spawns;
};

struct CoordFrame
{
	CVector3f position;
};

struct CMiniBot { };
struct Game;
struct Player
{
	char playerID; // Player.h:264-270: char, as the messages carry them
	char teamID;
	char status;
	CoordFrame currentCF;
	float timePlayedCurGame;
	int score, flagAttempts, returns;
	int spawnSlot;
	float timeToSpawn, deadSince;
	CString name;
	int userID;
	CMiniBot * minibot;
	Game * game;
	bool spawned;
	CVector3f spawnedAt;
	void kill(bool silenceDeath);
	void spawn(const CVector3f & pos) { spawned = true; spawnedAt = pos; status = PLAYER_STATUS_ALIVE; currentCF.position = pos; }
};

struct Client;
struct Game
{
	Player * players[MAX_PLAYER];
	Map * map;
	int gameType, spawnType;
	int blueScore, redScore, blueWin, redWin;
	float gameTimeLeft, roundTimeLeft;
	bool isServerGame;
	bool isApproved(int, char) { return true; } // no account server: everyone is approved
	int assignPlayerTeam(int playerID, char teamRequested, Client * client = 0);
	bool spawnPlayer(int playerID);
};

struct GameVarStub
{
	float sv_timeToSpawn, sv_gameTimeLimit, sv_roundTimeLimit;
	bool sv_autoBalance;
	int sv_autoBalanceTime;
} gameVar;

struct Server
{
	Game * game;
	float autoBalanceTimer;
	void updateCTF(float delay);
	void autoBalance();
	void balanceStep(float delay);  // Server::update's auto-balance block (Server.cpp:1230)
	void championStep(float delay); // Server::update's type-3 block, Pro (Server.cpp:1288)
};
