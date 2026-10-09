// The projectile reference driver's cases (see proj_head.cpp): random maps, players and one
// projectile of each kind, run through the original Projectile::update frame by frame, with what it
// sent, the radius hits it asked for and its state after each frame.
//
// This file is part of openbv, under the GNU General Public License v3 or later.

static unsigned long long genState = 0x9E3779B97F4A7C15ull;
static unsigned int gen() { genState ^= genState << 13; genState ^= genState >> 7; genState ^= genState << 17; return (unsigned int)(genState >> 11); }
static float genf(float lo, float hi) { return lo + (hi - lo) * (float)(gen() % 1000000) / 1000000.0f; }

static CVector3f floorPoint(Map & m)
{
	for (;;)
	{
		int x = 1 + gen() % (m.size[0] - 2), y = 1 + gen() % (m.size[1] - 2);
		if (m.cells[y * m.size[0] + x].passable) return CVector3f((float)x + genf(.1f, .9f), (float)y + genf(.1f, .9f), .25f);
	}
}

static void projCases(FILE * f)
{
	const float delay = 1.0f / 30;
	static const int types[] = {PROJECTILE_ROCKET, PROJECTILE_GRENADE, PROJECTILE_LIFE_PACK, PROJECTILE_DROPED_WEAPON,
		PROJECTILE_DROPED_GRENADE, PROJECTILE_COCKTAIL_MOLOTOV, PROJECTILE_FLAME};
	fprintf(f, "\"proj\":[");
	for (int k = 0; k < 700; ++k)
	{
		Map m;
		m.size = CVector2i(8 + gen() % 10, 8 + gen() % 10);
		m.dko_mapLM = 0;
		m.cells = new map_cell[m.size[0] * m.size[1]];
		for (int y = 0; y < m.size[1]; ++y)
			for (int x = 0; x < m.size[0]; ++x)
			{
				bool border = x == 0 || y == 0 || x == m.size[0] - 1 || y == m.size[1] - 1;
				map_cell & c = m.cells[y * m.size[0] + x];
				c.passable = !border && gen() % 100 >= 12;
				c.height = c.passable ? 0 : 1 + gen() % 4;
			}
		Game game;
		memset(game.players, 0, sizeof game.players);
		game.map = &m;
		game.gameType = 0;
		ServerStub server = {&game};
		SceneStub sc = {&server};
		scene = &sc;
		WeaponStub bazooka = {.75f};
		memset(gameVar.weapons, 0, sizeof gameVar.weapons);
		gameVar.weapons[WEAPON_BAZOOKA] = &bazooka;
		gameVar.sv_zookaRemoteDet = gen() % 2;
		gameVar.sv_serverType = gen() % 2;
		gameVar.sv_zookaRadius = genf(1, 8);
		gameVar.sv_zookaDamage = genf(.1f, 1.5f);

		int type = types[k % 7];
		CVector3f pos = floorPoint(m);
		pos[2] = genf(0, 1.5f);
		CVector3f vel(genf(-4, 4), genf(-4, 4), genf(-1, 3));
		int nPlayers = 1 + gen() % 4;
		int fromID = gen() % nPlayers;
		fprintf(f, "%s{\"w\":%d,\"h\":%d,\"cells\":[", k ? "," : "", m.size[0], m.size[1]);
		for (int c = 0; c < m.size[0] * m.size[1]; ++c)
			fprintf(f, "%s%d", c ? "," : "", m.cells[c].passable ? -1 : m.cells[c].height);
		fprintf(f, "],\"sv\":{\"remoteDet\":%s,\"serverType\":%d,\"zookaRadius\":%s,\"zookaDamage\":%s},\"players\":[",
			gameVar.sv_zookaRemoteDet ? "true" : "false", gameVar.sv_serverType, fbits(gameVar.sv_zookaRadius).c_str(), fbits(gameVar.sv_zookaDamage).c_str());
		Player store[4];
		for (int i = 0; i < nPlayers; ++i)
		{
			Player & p = store[i];
			p.playerID = i;
			p.status = (gen() % 5) ? PLAYER_STATUS_ALIVE : PLAYER_STATUS_DEAD;
			p.nbGrenadeLeft = gen() % 4;
			p.life = genf(.05f, 1);
			p.rocketInAir = gen() % 2;
			p.detonateRocket = (gen() % 6) == 0;
			// near the projectile now and then, so it hits someone
			p.currentCF.position = (gen() % 3 == 0) ? pos + CVector3f(genf(-.6f, .6f), genf(-.6f, .6f), 0) : floorPoint(m);
			p.currentCF.position[2] = .25f;
			game.players[i] = &p;
			fprintf(f, "%s{\"status\":%d,\"grenades\":%d,\"life\":%s,\"rocketInAir\":%s,\"detonate\":%s,\"pos\":%s}", i ? "," : "",
				p.status, p.nbGrenadeLeft, fbits(p.life).c_str(), p.rocketInAir ? "true" : "false", p.detonateRocket ? "true" : "false",
				vbits(p.currentCF.position).c_str());
		}
		int killAt = (gen() % 3 == 0) ? 1 + gen() % 40 : -1, killWho = gen() % nPlayers;
		unsigned int seed = gen();
		srand(seed);
		Projectile::uniqueProjectileID = 100;
		Projectile pr(pos, vel, (char)fromID, type, false, 100);
		if (type == PROJECTILE_FLAME)
		{
			pr.stickToPlayer = (gen() % 3 == 0) ? (int)(gen() % nPlayers) : -1;
			pr.stickFor = genf(-.2f, 3);
			pr.timeSinceThrown = genf(0, 1);
			pr.movementLock = gen() % 2;
			pr.damageTime = gen() % 20;
		}
		pr.projectileID = (short)(gen() % 5);
		fprintf(f, "],\"killAt\":%d,\"killWho\":%d,\"seed\":%u,\"type\":%d,\"pos\":%s,\"vel\":%s,\"from\":%d,\"stick\":%d,\"stickFor\":%s,\"thrown\":%s,\"lock\":%s,\"damageTime\":%d,\"index\":%d,\"frames\":[",
			killAt, killWho, seed, type, vbits(pos).c_str(), vbits(vel).c_str(), fromID, pr.stickToPlayer, fbits(pr.stickFor).c_str(),
			fbits(pr.timeSinceThrown).c_str(), pr.movementLock ? "true" : "false", pr.damageTime, pr.projectileID);
		bool deleting = false;
		for (int frame = 0; frame < 90; ++frame)
		{
			if (frame == killAt)
			{
				game.players[killWho]->status = PLAYER_STATUS_DEAD;
				game.players[killWho]->currentCF.position.set(-999, -999, 0);
			}
			events.clear();
			pr.update(delay, &m);
			fprintf(f, "%s{\"ev\":[%s],\"pos\":%s,\"vel\":%s,\"del\":%s,\"lock\":%s,\"stick\":%d,\"stickFor\":%s,\"duration\":%s,\"damageTime\":%d,\"serverType\":%d,\"zookaDamage\":%s,\"flags\":[",
				frame ? "," : "", events.c_str(), vbits(pr.currentCF.position).c_str(), vbits(pr.currentCF.vel).c_str(),
				pr.needToBeDeleted ? "true" : "false", pr.movementLock ? "true" : "false", pr.stickToPlayer,
				fbits(pr.stickFor).c_str(), fbits(pr.duration).c_str(), pr.damageTime, gameVar.sv_serverType, fbits(bazooka.damage).c_str());
			for (int i = 0; i < nPlayers; ++i)
				fprintf(f, "%s[%s,%s,%d,%s]", i ? "," : "", store[i].rocketInAir ? "true" : "false", store[i].detonateRocket ? "true" : "false",
					store[i].nbGrenadeLeft, fbits(store[i].life).c_str());
			fprintf(f, "],\"rand\":%u}", msvcSeed);
			if (deleting) break;
			if (pr.needToBeDeleted) deleting = true; // the one more frame it stays (Game.cpp:822)
		}
		fprintf(f, "]}");
		delete [] m.cells;
	}
	fprintf(f, "]");
}

static Map randomMap()
{
	Map m;
	m.size = CVector2i(8 + gen() % 10, 8 + gen() % 10);
	m.dko_mapLM = 0;
	m.cells = new map_cell[m.size[0] * m.size[1]];
	for (int y = 0; y < m.size[1]; ++y)
		for (int x = 0; x < m.size[0]; ++x)
		{
			bool border = x == 0 || y == 0 || x == m.size[0] - 1 || y == m.size[1] - 1;
			map_cell & c = m.cells[y * m.size[0] + x];
			c.passable = !border && gen() % 100 >= 12;
			c.height = c.passable ? 0 : 1 + gen() % 4;
		}
	return m;
}

static void mapJSON(FILE * f, Map & m)
{
	fprintf(f, "\"w\":%d,\"h\":%d,\"cells\":[", m.size[0], m.size[1]);
	for (int c = 0; c < m.size[0] * m.size[1]; ++c)
		fprintf(f, "%s%d", c ? "," : "", m.cells[c].passable ? -1 : m.cells[c].height);
	fprintf(f, "]");
}

// CMiniBot::Think and Game::shootMinibotSV: an owner, up to four others, frames of thinking
static void minibotCases(FILE * f)
{
	const float delay = 1.0f / 30;
	fprintf(f, "\"minibot\":[");
	for (int k = 0; k < 400; ++k)
	{
		Map m = randomMap();
		Game game;
		memset(game.players, 0, sizeof game.players);
		game.map = &m;
		game.gameType = gen() % 4;
		WeaponStub minibotGun = {genf(.01f, .2f)};
		memset(gameVar.weapons, 0, sizeof gameVar.weapons);
		gameVar.weapons[WEAPON_MINIBOT] = &minibotGun;
		int n = 2 + gen() % 4;
		Player store[6];
		CVector3f botPos = floorPoint(m);
		botPos[2] = .15f;
		fprintf(f, "%s{", k ? "," : "");
		mapJSON(f, m);
		fprintf(f, ",\"gameType\":%d,\"gun\":%s,\"players\":[", game.gameType, fbits(minibotGun.damage).c_str());
		for (int i = 0; i < n; ++i)
		{
			Player & p = store[i];
			p.playerID = i;
			p.status = (gen() % 6) ? PLAYER_STATUS_ALIVE : PLAYER_STATUS_DEAD;
			p.teamID = gen() % 2;
			p.currentCF.position = (gen() % 2) ? botPos + CVector3f(genf(-6, 6), genf(-6, 6), 0) : floorPoint(m);
			p.currentCF.position[2] = .25f;
			game.players[i] = &p;
			fprintf(f, "%s{\"status\":%d,\"team\":%d,\"pos\":%s}", i ? "," : "", p.status, p.teamID, vbits(p.currentCF.position).c_str());
		}
		CMiniBot bot;
		bot.owner = &store[0];
		bot.game = &game;
		bot.currentCF.position = botPos;
		bot.currentCF.mousePosOnMap = botPos;
		bot.m_fireRate = genf(-.3f, .3f);
		bot.nukeBot = (gen() % 5) == 0;
		unsigned int seed = gen();
		srand(seed);
		fprintf(f, "],\"bot\":%s,\"fireRate\":%s,\"nuke\":%s,\"seed\":%u,\"frames\":[", vbits(botPos).c_str(),
			fbits(bot.m_fireRate).c_str(), bot.nukeBot ? "true" : "false", seed);
		for (int frame = 0; frame < 24; ++frame)
		{
			events.clear();
			bot.Think(delay);
			fprintf(f, "%s{\"ev\":[%s],\"mouse\":%s,\"fireRate\":%s,\"rand\":%u}", frame ? "," : "", events.c_str(),
				vbits(bot.currentCF.mousePosOnMap).c_str(), fbits(bot.m_fireRate).c_str(), msvcSeed);
		}
		fprintf(f, "]}");
		delete [] m.cells;
	}
	fprintf(f, "]");
}

// Game::radiusHit: everyone within the radius with no wall between
static void radiusCases(FILE * f)
{
	static const int weaponIDs[] = {WEAPON_BAZOOKA, WEAPON_GRENADE, WEAPON_COCKTAIL_MOLOTOV, WEAPON_KNIVES, WEAPON_NUCLEAR};
	fprintf(f, "\"radius\":[");
	for (int k = 0; k < 600; ++k)
	{
		Map m = randomMap();
		Game game;
		memset(game.players, 0, sizeof game.players);
		game.map = &m;
		WeaponStub w = {genf(.1f, 8)};
		int wid = weaponIDs[k % 5];
		memset(gameVar.weapons, 0, sizeof gameVar.weapons);
		gameVar.weapons[wid] = &w;
		CVector3f pos = floorPoint(m);
		pos[2] = genf(0, .6f);
		float radius = genf(.3f, 7);
		int n = 1 + gen() % 6, from = gen() % n;
		bool same = (gen() % 3) == 0;
		Player store[6];
		fprintf(f, "%s{", k ? "," : "");
		mapJSON(f, m);
		fprintf(f, ",\"weapon\":%d,\"damage\":%s,\"pos\":%s,\"r\":%s,\"from\":%d,\"same\":%s,\"players\":[", wid,
			fbits(w.damage).c_str(), vbits(pos).c_str(), fbits(radius).c_str(), from, same ? "true" : "false");
		for (int i = 0; i < n; ++i)
		{
			Player & p = store[i];
			p.playerID = i;
			p.status = (gen() % 5) ? PLAYER_STATUS_ALIVE : PLAYER_STATUS_DEAD;
			p.currentCF.position = (gen() % 2) ? pos + CVector3f(genf(-radius, radius), genf(-radius, radius), 0) : floorPoint(m);
			p.currentCF.position[2] = .25f;
			game.players[i] = &p;
			fprintf(f, "%s{\"status\":%d,\"pos\":%s}", i ? "," : "", p.status, vbits(p.currentCF.position).c_str());
		}
		events.clear();
		game.realRadiusHit(pos, radius, (char)from, (char)wid, same);
		fprintf(f, "],\"ev\":[%s]}", events.c_str());
		delete [] m.cells;
	}
	fprintf(f, "]");
}

// Map::performCollision then Map::collisionClip, as Game::update runs them on a minibot
static void collisionCases(FILE * f)
{
	fprintf(f, "\"collision\":[");
	for (int k = 0; k < 1500; ++k)
	{
		Map m = randomMap();
		CoordFrame last, cf;
		cf.position = CVector3f(genf(1, (float)m.size[0] - 1.001f), genf(1, (float)m.size[1] - 1.001f), .15f);
		last.position = cf.position + CVector3f(genf(-.4f, .4f), genf(-.4f, .4f), 0);
		cf.vel = CVector3f(genf(-10, 10), genf(-10, 10), 0);
		if (k % 5 == 0) cf.vel[gen() % 2] = 0;
		float radius = (k % 2) ? .15f : genf(.05f, .45f);
		fprintf(f, "%s{", k ? "," : "");
		mapJSON(f, m);
		fprintf(f, ",\"last\":%s,\"pos\":%s,\"vel\":%s,\"r\":%s", vbits(last.position).c_str(), vbits(cf.position).c_str(),
			vbits(cf.vel).c_str(), fbits(radius).c_str());
		m.performCollision(last, cf, radius);
		fprintf(f, ",\"afterPos\":%s,\"afterVel\":%s,\"afterLast\":%s", vbits(cf.position).c_str(), vbits(cf.vel).c_str(), vbits(last.position).c_str());
		m.collisionClip(cf, radius);
		fprintf(f, ",\"clipped\":%s}", vbits(cf.position).c_str());
		delete [] m.cells;
	}
	fprintf(f, "]");
}

int main(int argc, char ** argv)
{
	FILE * f = argc > 1 ? fopen(argv[1], "w") : stdout;
	fprintf(f, "{");
	projCases(f); fprintf(f, ",\n");
	minibotCases(f); fprintf(f, ",\n");
	radiusCases(f); fprintf(f, ",\n");
	collisionCases(f);
	fprintf(f, "}\n");
	if (f != stdout) fclose(f);
	return 0;
}
