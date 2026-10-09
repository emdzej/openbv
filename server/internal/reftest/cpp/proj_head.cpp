// The projectile reference driver's fixed part (build.sh appends the original functions and
// proj_main.cpp): stand-ins for what Projectile::update (GameProjectile.cpp:234, the dedicated build)
// reaches, and recorders for what it does: every bb_serverSend (with the padding zeroed, as the Go
// server sends it), every radiusHit and the flame spawns of Game::spawnProjectile.
//
// This file is part of openbv, under the GNU General Public License v3 or later.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
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
#define PROJECTILE_ROCKET 2
#define PROJECTILE_GRENADE 3
#define PROJECTILE_LIFE_PACK 4
#define PROJECTILE_DROPED_WEAPON 5
#define PROJECTILE_DROPED_GRENADE 6
#define PROJECTILE_COCKTAIL_MOLOTOV 7
#define PROJECTILE_FLAME 8
#define PROJECTILE_GIB 9
#define WEAPON_BAZOOKA 5
#define WEAPON_GRENADE 8
#define WEAPON_COCKTAIL_MOLOTOV 9
#define WEAPON_KNIVES 10
#define WEAPON_NUCLEAR 11
#define WEAPON_MINIBOT 13
#define WEAPON_MINIBOT_WEAPON 100
#define GAME_TYPE_DM 0
#define GAME_TYPE_SND 3
#define COLLISION_EPSILON 0.05f
#define BOUNCE_FACTOR 0.45f
#define ITEM_LIFE_PACK 1
#define ITEM_GRENADE 3
#define SOUND_MOLOTOV 2
#define TO_DEGREE 57.295780f

// --- the map (Map.h): what rayTest and rayTileTest read
struct map_cell { bool passable; int height; };
typedef void dko_stub;
static bool dkoRayIntersection(dko_stub *, float *, float *, float *, float *, int &) { return false; }
static bool dkoSphereIntersection(dko_stub *, float *, float *, float, float *, float *, int &) { return false; }
struct CoordFrame;
#define TEST_DIR_X 0
#define TEST_DIR_X_NEG 1
#define TEST_DIR_Y 2
#define TEST_DIR_Y_NEG 3
struct Map
{
	CVector2i size;
	map_cell *cells;
	dko_stub *dko_mapLM;
	bool rayTest(CVector3f & p1, CVector3f & p2, CVector3f & normal);
	bool rayTileTest(int x, int y, CVector3f & p1, CVector3f & p2, CVector3f & normal);
	void performCollision(CoordFrame & lastCF, CoordFrame & coordFrame, float radius);
	void collisionClip(CoordFrame & coordFrame, float radius);
};

// --- CoordFrame (Player.h:47): the fields and the copy the projectiles use
struct CoordFrame
{
	CVector3f position, vel, mousePosOnMap;
	long frameID;
	float angle;
	CoordFrame() { reset(); }
	void reset() { frameID = 0; angle = 0; }
	void operator=(const CoordFrame & o) { position = o.position; vel = o.vel; angle = o.angle; frameID = o.frameID; mousePosOnMap = o.mousePosOnMap; }
};

struct WeaponStub;
struct Player
{
	int playerID, status, nbGrenadeLeft, teamID;
	float life;
	bool rocketInAir, detonateRocket;
	CoordFrame currentCF;
	void hitSV(WeaponStub * fromWeapon, Player * from, float damage = -1);
};

struct Projectile
{
	int projectileType;
	CoordFrame currentCF, lastCF, netCF0, netCF1;
	long cFProgression;
	bool remoteEntity, needToBeDeleted, reallyNeedToBeDeleted;
	float duration;
	char fromID;
	bool movementLock;
	long whenToShoot;
	int damageTime;
	int stickToPlayer;
	float stickFor;
	float timeSinceThrown;
	short projectileID;
	long uniqueID;
	static long uniqueProjectileID;
	Projectile(CVector3f & position, CVector3f & vel, char pFromID, int pProjectileType, bool pRemoteEntity, long pUniqueProjectileID);
	void update(float delay, Map * map);
};
long Projectile::uniqueProjectileID = 0;

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

// the messages as the Go server sends them: the fields copied into zeroed structs (the original sent
// whatever the stack held in the padding)
static void bb_serverSend(char * data, int size, int typeID, int dest = 0, int protocol = 0)
{
	char clean[64];
	memset(clean, 0, sizeof clean);
	switch (typeID)
	{
	case NET_SVCL_EXPLOSION:
	{
		net_svcl_explosion in, & out = *(net_svcl_explosion *)clean;
		memcpy(&in, data, sizeof in);
		memcpy(out.position, in.position, sizeof in.position);
		memcpy(out.normal, in.normal, sizeof in.normal);
		out.radius = in.radius;
		out.playerID = in.playerID;
		break;
	}
	case NET_SVCL_FLAME_STICK_TO_PLAYER:
	{
		net_svcl_flame_stick_to_player in, & out = *(net_svcl_flame_stick_to_player *)clean;
		memcpy(&in, data, sizeof in);
		out.projectileID = in.projectileID;
		out.playerID = in.playerID;
		break;
	}
	case NET_CLSV_SVCL_PLAYER_PROJECTILE:
	{
		net_clsv_svcl_player_projectile in, & out = *(net_clsv_svcl_player_projectile *)clean;
		memcpy(&in, data, sizeof in);
		out.playerID = in.playerID;
		out.weaponID = in.weaponID;
		out.nuzzleID = in.nuzzleID;
		out.projectileType = in.projectileType;
		memcpy(out.position, in.position, sizeof in.position);
		memcpy(out.vel, in.vel, sizeof in.vel);
		out.uniqueID = in.uniqueID;
		break;
	}
	case NET_SVCL_PLAYER_SHOOT:
	{
		net_svcl_player_shoot in, & out = *(net_svcl_player_shoot *)clean;
		memcpy(&in, data, sizeof in);
		out.playerID = in.playerID;
		out.hitPlayerID = in.hitPlayerID;
		out.nuzzleID = in.nuzzleID;
		out.weaponID = in.weaponID;
		memcpy(out.p1, in.p1, sizeof in.p1);
		memcpy(out.p2, in.p2, sizeof in.p2);
		memcpy(out.normal, in.normal, sizeof in.normal);
		break;
	}
	default:
		memcpy(clean, data, size); // no padding: NET_SVCL_PLAY_SOUND, NET_SVCL_PICKUP_ITEM
	}
	char b[32];
	snprintf(b, sizeof b, "{\"send\":%d,\"b\":\"", typeID);
	ev(std::string(b) + hex(clean, size) + "\"}");
}

struct CMiniBot;
struct Game
{
	Player * players[MAX_PLAYER];
	Map * map;
	int gameType;
	void realRadiusHit(CVector3f & pos, float radius, char fromID, char weaponID, bool sameDmg = false);
	void shootMinibotSV(CMiniBot * minibot, float imp, CVector3f p1, CVector3f p2);
	Player * playerInRadius(CVector3f position, float radius, int ignore = -1);
	void radiusHit(CVector3f & position, float radius, char fromID, char weaponID, bool sameDmg = false)
	{
		char b[96];
		snprintf(b, sizeof b, ",\"r\":%s,\"from\":%d,\"w\":%d,\"same\":%s}", fbits(radius).c_str(), fromID, weaponID, sameDmg ? "true" : "false");
		ev("{\"radius\":" + vbits(position) + b);
	}
	// Game::spawnProjectile(..., true) (GameSpawn.cpp:430), the part a flame takes: the next uniqueID,
	// a flame built from the message, and the server's broadcast of it
	bool spawnProjectile(net_clsv_svcl_player_projectile & playerProjectile, bool imServer)
	{
		++(Projectile::uniqueProjectileID);
		playerProjectile.uniqueID = Projectile::uniqueProjectileID;
		CVector3f position;
		position[0] = (float)playerProjectile.position[0] / 100.0f;
		position[1] = (float)playerProjectile.position[1] / 100.0f;
		position[2] = (float)playerProjectile.position[2] / 100.0f;
		CVector3f vel;
		vel[0] = (float)playerProjectile.vel[0] / 10.0f;
		vel[1] = (float)playerProjectile.vel[1] / 10.0f;
		vel[2] = (float)playerProjectile.vel[2] / 10.0f;
		Projectile * projectile = new Projectile(position, vel, playerProjectile.playerID, playerProjectile.projectileType, false, Projectile::uniqueProjectileID);
		if (playerProjectile.projectileType == PROJECTILE_FLAME)
		{
			net_clsv_svcl_player_projectile out;
			out.playerID = projectile->fromID;
			out.nuzzleID = 0;
			out.projectileType = projectile->projectileType;
			out.weaponID = WEAPON_COCKTAIL_MOLOTOV;
			out.position[0] = (short)(projectile->currentCF.position[0] * 100);
			out.position[1] = (short)(projectile->currentCF.position[1] * 100);
			out.position[2] = (short)(projectile->currentCF.position[2] * 100);
			out.vel[0] = (char)(projectile->currentCF.vel[0] * 10);
			out.vel[1] = (char)(projectile->currentCF.vel[1] * 10);
			out.vel[2] = (char)(projectile->currentCF.vel[2] * 10);
			out.uniqueID = projectile->uniqueID;
			bb_serverSend((char *)&out, sizeof out, NET_CLSV_SVCL_PLAYER_PROJECTILE, 0);
		}
		delete projectile;
		return true;
	}
};
struct ServerStub { Game * game; };
struct SceneStub { ServerStub * server; };
static SceneStub * scene;

struct WeaponStub { float damage; };
struct GameVarStub
{
	bool sv_zookaRemoteDet;
	int sv_serverType;
	float sv_zookaRadius, sv_zookaDamage;
	WeaponStub * weapons[16];
} gameVar;

// the minibot (Minibot.cpp): what Think reads
struct CMiniBot
{
	Player * owner;
	Game * game;
	CoordFrame currentCF;
	float m_fireRate;
	bool nukeBot;
	void Think(float delay);
};

// Player::hitSV, recorded: the victim, the weapon (by its slot in gameVar.weapons), the attacker, the damage
void Player::hitSV(WeaponStub * fromWeapon, Player * from, float damage)
{
	int w = -1;
	for (int i = 0; i < 16; ++i) if (gameVar.weapons[i] == fromWeapon) w = i;
	char b[96];
	snprintf(b, sizeof b, "{\"hit\":%d,\"w\":%d,\"from\":%d,\"damage\":%s}", playerID, w, from ? from->playerID : -1, fbits(damage).c_str());
	ev(b);
}
