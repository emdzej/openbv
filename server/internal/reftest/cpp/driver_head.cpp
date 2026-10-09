// The reference driver's fixed part: minimal stand-ins for the types the original functions use, the
// MSVC rand() the Windows build had, and the JSON writers. build.sh appends the original functions
// (extracted verbatim by extract.py) and driver_main.cpp.
//
// This file is part of openbv, under the GNU General Public License v3 or later.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits>
#include "CVector.h"

// MSVC's rand(): what the original Windows server called; the game's rand(int,int) and
// rand(float,float) (CVector.cpp) call it.
static unsigned int msvcSeed = 1;
extern "C" int rand(void)
{
	msvcSeed = msvcSeed * 214013u + 2531011u;
	return (int)((msvcSeed >> 16) & 0x7fff);
}
extern "C" void srand(unsigned int seed) { msvcSeed = seed; }

// --- stand-ins for Map (Map.h): only what rayTest and rayTileTest read
struct map_cell
{
	bool passable;
	int height;
};
typedef void dko_stub;
static bool dkoRayIntersection(dko_stub *, float *, float *, float *, float *, int &) { return false; }
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
};

// --- stand-ins for hitSV's damage prelude (Player.cpp:1111): the game variables, the weapon, the
// two players
#define WEAPON_SMG 0
#define WEAPON_SHOTGUN 1
#define WEAPON_SNIPER 2
#define WEAPON_DUAL_MACHINE_GUN 3
#define WEAPON_CHAIN_GUN 4
#define WEAPON_BAZOOKA 5
#define WEAPON_PHOTON_RIFLE 6
#define WEAPON_FLAME_THROWER 7
#define WEAPON_GRENADE 8
#define WEAPON_COCKTAIL_MOLOTOV 9
#define WEAPON_KNIVES 10
#define SUBGAMETYPE_INSTAGIB 1

struct GameVarStub
{
	int sv_serverType, sv_photonType, sv_subGameType;
	float sv_smgDamage, sv_sniperDamage, sv_shottyDamage, sv_dmgDamage, sv_cgDamage, sv_ftDamage, sv_ftMaxRange;
	float sv_photonVerticalShift, sv_photonDamageCoefficient, sv_photonHorizontalShift, sv_photonDistMult;
} gameVar;

struct Weapon
{
	int weaponID;
	float damage;
	CVector3f shotFrom;
};

struct CoordFrameStub { CVector3f position; };

struct Player
{
	Weapon *weapon;
	CoordFrameStub currentCF;
	float protection, immuneTime, life;
	float damagePrelude(Weapon * fromWeapon, Player * from, float damage);
};

// --- JSON
static void hexf(FILE *f, float v) { unsigned int u; memcpy(&u, &v, 4); fprintf(f, "%u", u); }
static void vec(FILE *f, const CVector3f &v) { fprintf(f, "["); hexf(f, v[0]); fprintf(f, ","); hexf(f, v[1]); fprintf(f, ","); hexf(f, v[2]); fprintf(f, "]"); }

// a deterministic input generator, independent of the rand() under test
static unsigned long long genState = 88172645463325252ull;
static unsigned int gen() { genState ^= genState << 13; genState ^= genState >> 7; genState ^= genState << 17; return (unsigned int)(genState >> 11); }
static float genf(float lo, float hi) { return lo + (hi - lo) * (float)(gen() % 1000000) / 1000000.0f; }
static CVector3f genv(float lo, float hi) { return CVector3f(genf(lo, hi), genf(lo, hi), genf(lo, hi)); }
