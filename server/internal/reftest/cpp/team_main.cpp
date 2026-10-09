// The team reference driver's cases (see team_head.cpp): random players, teams, flags and spawns run
// through the original updateCTF, auto-balance, assignPlayerTeam, spawnPlayer and the "Champion"
// round reset, with what they sent and the state after each step.
//
// This file is part of openbv, under the GNU General Public License v3 or later.

static unsigned long long genState = 0xD1B54A32D192ED03ull;
static unsigned int gen() { genState ^= genState << 13; genState ^= genState >> 7; genState ^= genState << 17; return (unsigned int)(genState >> 11); }
static float genf(float lo, float hi) { return lo + (hi - lo) * (float)(gen() % 1000000) / 1000000.0f; }

static const float delay = 1.0f / 30;

static Player store[MAX_PLAYER];

// a game with n players in random slots (slots in order; some empty), random teams and states
static void makePlayers(Game & game, int n, int slots, bool teamsOnly)
{
	memset(game.players, 0, sizeof game.players);
	int placed = 0;
	for (int i = 0; i < slots && placed < n; ++i)
	{
		if (gen() % 4 == 0 && slots - i > n - placed) continue; // an empty slot
		Player & p = store[i];
		p = Player();
		p.playerID = (char)i;
		int t = gen() % 10;
		p.teamID = teamsOnly ? (char)(t < 5 ? PLAYER_TEAM_BLUE : PLAYER_TEAM_RED)
			: (char)(t < 4 ? PLAYER_TEAM_BLUE : t < 8 ? PLAYER_TEAM_RED : PLAYER_TEAM_SPECTATOR);
		p.status = (gen() % 5) ? PLAYER_STATUS_ALIVE : PLAYER_STATUS_DEAD;
		p.currentCF.position = CVector3f(genf(1, 20), genf(1, 20), .25f);
		p.timePlayedCurGame = (gen() % 3 == 0) ? 0 : genf(0, 600);
		p.score = gen() % 10;
		p.flagAttempts = gen() % 3;
		p.returns = gen() % 3;
		p.spawnSlot = (gen() % 3 == 0) ? (int)(gen() % 6) : -1;
		p.timeToSpawn = genf(0, 5);
		p.deadSince = 0;
		p.userID = 0;
		p.minibot = 0;
		p.game = &game;
		p.spawned = false;
		game.players[i] = &p;
		placed++;
	}
}

static void writePlayers(FILE * f, Game & game)
{
	fprintf(f, "\"players\":[");
	bool first = true;
	for (int i = 0; i < MAX_PLAYER; ++i)
	{
		Player * p = game.players[i];
		if (!p) continue;
		fprintf(f, "%s{\"id\":%d,\"team\":%d,\"status\":%d,\"pos\":%s,\"played\":%s,\"score\":%d,\"fa\":%d,\"ret\":%d,\"slot\":%d,\"tts\":%s}",
			first ? "" : ",", p->playerID, p->teamID, p->status, vbits(p->currentCF.position).c_str(), fbits(p->timePlayedCurGame).c_str(),
			p->score, p->flagAttempts, p->returns, p->spawnSlot, fbits(p->timeToSpawn).c_str());
		first = false;
	}
	fprintf(f, "]");
}

// the state after a step: per player in slot order
static void writeState(FILE * f, Game & game)
{
	fprintf(f, "\"st\":{\"state\":[");
	bool first = true;
	for (int i = 0; i < MAX_PLAYER; ++i)
	{
		Player * p = game.players[i];
		if (!p) continue;
		fprintf(f, "%s[%d,%d,%d,%d,%d,%d,%s,%s]", first ? "" : ",", p->teamID, p->status, p->score, p->flagAttempts, p->returns, p->spawnSlot,
			fbits(p->timeToSpawn).c_str(), vbits(p->currentCF.position).c_str());
		first = false;
	}
	fprintf(f, "],\"flagState\":[%d,%d],\"flagPos\":[%s,%s],\"scores\":[%d,%d,%d,%d],\"rand\":%u",
		game.map->flagState[0], game.map->flagState[1], vbits(game.map->flagPos[0]).c_str(), vbits(game.map->flagPos[1]).c_str(),
		game.blueScore, game.redScore, game.blueWin, game.redWin, msvcSeed);
	fprintf(f, "}");
}

static void writeEvents(FILE * f) { fprintf(f, "\"ev\":[%s]", events.c_str()); events.clear(); }

static void newMap(Map & m)
{
	m.flagPodPos[0] = CVector3f(genf(2, 18), genf(2, 18), (gen() % 4 == 0) ? genf(0, .5f) : 0);
	m.flagPodPos[1] = CVector3f(genf(2, 18), genf(2, 18), 0);
	m.dm_spawns.v.clear();
	m.dm_spawns.readPastEnd = false;
}

// --- CTF: players walk scripted paths (a step per frame) past the pods and the dropped flags
static void ctfCases(FILE * f)
{
	fprintf(f, "\"ctf\":[");
	for (int k = 0; k < 600; ++k)
	{
		Map m;
		newMap(m);
		Game game;
		game.map = &m;
		game.gameType = GAME_TYPE_CTF;
		game.isServerGame = true;
		game.blueScore = game.redScore = 0;
		game.blueWin = gen() % 4;
		game.redWin = gen() % 4;
		Server server;
		server.game = &game;
		makePlayers(game, 1 + gen() % 8, 12, false);
		int ids[MAX_PLAYER], n = 0;
		for (int i = 0; i < MAX_PLAYER; ++i) if (game.players[i]) ids[n++] = i;
		for (int fl = 0; fl < 2; ++fl)
		{
			int s = gen() % 4;
			// a carrier of the other team, else home or on the ground
			if (s == 0)
			{
				int c = ids[gen() % n];
				m.flagState[fl] = (char)c;
			}
			else m.flagState[fl] = (s == 1) ? -1 : -2;
			m.flagPos[fl] = (m.flagState[fl] == -1) ? CVector3f(genf(2, 18), genf(2, 18), 0) : CVector3f(0, 0, 0);
		}
		// each player heads for a pod or a dropped flag, passing within reach now and then
		CVector3f target[MAX_PLAYER], step[MAX_PLAYER];
		for (int j = 0; j < n; ++j)
		{
			Player * p = game.players[ids[j]];
			int which = gen() % 4;
			target[j] = which < 2 ? m.flagPodPos[which] : m.flagPos[which - 2];
			target[j][2] = .25f;
			if (gen() % 2) p->currentCF.position = target[j] + CVector3f(genf(-1.5f, 1.5f), genf(-1.5f, 1.5f), 0);
			p->currentCF.position[2] = .25f;
			int frames = 4 + gen() % 20;
			step[j] = (target[j] - p->currentCF.position) / (float)frames;
			step[j][2] = 0;
		}
		fprintf(f, "%s{\"pods\":[%s,%s],\"flagState\":[%d,%d],\"flagPos\":[%s,%s],\"wins\":[%d,%d],", k ? "," : "",
			vbits(m.flagPodPos[0]).c_str(), vbits(m.flagPodPos[1]).c_str(), m.flagState[0], m.flagState[1],
			vbits(m.flagPos[0]).c_str(), vbits(m.flagPos[1]).c_str(), game.blueWin, game.redWin);
		writePlayers(f, game);
		fprintf(f, ",\"frames\":[");
		int nFrames = 10 + gen() % 30;
		for (int fr = 0; fr < nFrames; ++fr)
		{
			// the movement before the frame (as coord frames would), positions an input; now and then a
			// death mid-way (Player::kill: drops a carried flag), in slot order with the moves
			events.clear();
			std::string killed;
			fprintf(f, "%s{\"pos\":[", fr ? "," : "");
			for (int j = 0; j < n; ++j)
			{
				Player * p = game.players[ids[j]];
				if (p->status == PLAYER_STATUS_ALIVE) p->currentCF.position += step[j];
				fprintf(f, "%s%s", j ? "," : "", vbits(p->currentCF.position).c_str());
				if (fr == nFrames / 2 && gen() % 4 == 0)
				{
					p->kill(true);
					char b[8];
					snprintf(b, sizeof b, "%s%d", killed.empty() ? "" : ",", ids[j]);
					killed += b;
				}
			}
			fprintf(f, "],\"kill\":[%s],", killed.c_str());
			server.updateCTF(delay);
			bool full = !events.empty() || fr == nFrames - 1;
			writeEvents(f);
			if (full) { fprintf(f, ",\"full\":true,"); writeState(f, game); }
			fprintf(f, "}");
		}
		fprintf(f, "]}");
	}
	fprintf(f, "]");
}

// --- auto-balance: Server::update's block frame by frame, uneven teams, carriers
static void balanceCases(FILE * f)
{
	fprintf(f, ",\"balance\":[");
	for (int k = 0; k < 500; ++k)
	{
		Map m;
		newMap(m);
		Game game;
		game.map = &m;
		static const int types[] = {GAME_TYPE_DM, GAME_TYPE_TDM, GAME_TYPE_CTF, GAME_TYPE_SND};
		game.gameType = (gen() % 5 == 0) ? types[gen() % 4] : (gen() % 2 ? GAME_TYPE_TDM : GAME_TYPE_CTF);
		game.isServerGame = true;
		game.blueScore = game.redScore = game.blueWin = game.redWin = 0;
		Server server;
		server.game = &game;
		server.autoBalanceTimer = (gen() % 3 == 0) ? genf(0, 2) : 0;
		gameVar.sv_autoBalance = gen() % 6 != 0;
		gameVar.sv_autoBalanceTime = 1 + gen() % 15;
		gameVar.sv_timeToSpawn = genf(0, 10);
		makePlayers(game, 2 + gen() % 10, 16, gen() % 3 != 0);
		// lopsided teams more often than not
		if (gen() % 3)
			for (int i = 0; i < MAX_PLAYER; ++i)
				if (game.players[i] && game.players[i]->teamID != PLAYER_TEAM_SPECTATOR && gen() % 3) game.players[i]->teamID = gen() % 4 ? PLAYER_TEAM_BLUE : PLAYER_TEAM_RED;
		for (int fl = 0; fl < 2; ++fl)
		{
			int ids[MAX_PLAYER], n = 0;
			for (int i = 0; i < MAX_PLAYER; ++i) if (game.players[i]) ids[n++] = i;
			m.flagState[fl] = (gen() % 2) ? (char)ids[gen() % n] : -2;
			m.flagPos[fl] = CVector3f(0, 0, 0);
		}
		fprintf(f, "%s{\"gameType\":%d,\"autoBalance\":%s,\"time\":%d,\"tts\":%s,\"timer\":%s,\"flagState\":[%d,%d],", k ? "," : "",
			game.gameType, gameVar.sv_autoBalance ? "true" : "false", gameVar.sv_autoBalanceTime, fbits(gameVar.sv_timeToSpawn).c_str(),
			fbits(server.autoBalanceTimer).c_str(), m.flagState[0], m.flagState[1]);
		writePlayers(f, game);
		fprintf(f, ",\"frames\":[");
		int nFrames = 20 + gen() % 500;
		bool first = true;
		for (int fr = 0; fr < nFrames; ++fr)
		{
			events.clear();
			server.balanceStep(delay);
			// write only frames where something happened, and the last one
			if (events.empty() && fr != nFrames - 1) continue;
			fprintf(f, "%s{\"frame\":%d,\"timer\":%s,", first ? "" : ",", fr, fbits(server.autoBalanceTimer).c_str());
			writeEvents(f);
			fprintf(f, ",");
			writeState(f, game);
			fprintf(f, "}");
			first = false;
		}
		fprintf(f, "],\"nframes\":%d}", nFrames);
	}
	fprintf(f, "]");
}

// --- assignPlayerTeam, auto-assign included
static void assignCases(FILE * f)
{
	fprintf(f, ",\"assign\":[");
	for (int k = 0; k < 1500; ++k)
	{
		Map m;
		newMap(m);
		Game game;
		game.map = &m;
		game.gameType = gen() % 4;
		game.isServerGame = true;
		game.blueScore = gen() % 3 ? (int)(gen() % 4) : 0;
		game.redScore = gen() % 3 ? (int)(gen() % 4) : 0;
		game.blueWin = game.redWin = 0;
		gameVar.sv_timeToSpawn = genf(0, 10);
		makePlayers(game, 1 + gen() % 10, 16, false);
		int ids[MAX_PLAYER], n = 0;
		for (int i = 0; i < MAX_PLAYER; ++i) if (game.players[i]) ids[n++] = i;
		for (int fl = 0; fl < 2; ++fl) { m.flagState[fl] = (gen() % 3 == 0) ? (char)ids[gen() % n] : -2; m.flagPos[fl] = CVector3f(0, 0, 0); }
		int id = ids[gen() % n];
		static const char reqs[] = {PLAYER_TEAM_SPECTATOR, PLAYER_TEAM_BLUE, PLAYER_TEAM_RED, PLAYER_TEAM_AUTO_ASSIGN, PLAYER_TEAM_AUTO_ASSIGN};
		char req = reqs[gen() % 5];
		unsigned int seed = gen();
		srand(seed);
		fprintf(f, "%s{\"seed\":%u,\"scores\":[%d,%d],\"tts\":%s,\"flagState\":[%d,%d],\"id\":%d,\"req\":%d,", k ? "," : "", seed,
			game.blueScore, game.redScore, fbits(gameVar.sv_timeToSpawn).c_str(), m.flagState[0], m.flagState[1], id, req);
		writePlayers(f, game);
		events.clear();
		int ret = game.assignPlayerTeam(id, req);
		fprintf(f, ",\"ret\":%d,", ret);
		writeEvents(f);
		fprintf(f, ",");
		writeState(f, game);
		fprintf(f, "}");
	}
	fprintf(f, "]");
}

// --- spawnPlayer for every game type
static void spawnCases(FILE * f)
{
	fprintf(f, ",\"spawn\":[");
	for (int k = 0; k < 2000; ++k)
	{
		Map m;
		newMap(m);
		int ns = (gen() % 10 == 0) ? 0 : 1 + gen() % 40;
		for (int i = 0; i < ns; ++i) m.dm_spawns.v.push_back(CVector3f(genf(1, 20), genf(1, 20), 0));
		Game game;
		game.map = &m;
		game.gameType = gen() % 4;
		game.spawnType = gen() % 2;
		game.isServerGame = true;
		gameVar.sv_gameTimeLimit = genf(60, 1800);
		game.gameTimeLeft = (gen() % 2) ? gameVar.sv_gameTimeLimit - genf(0, 20) : genf(0, 1800);
		makePlayers(game, 1 + gen() % 12, 20, false);
		int ids[MAX_PLAYER], n = 0;
		for (int i = 0; i < MAX_PLAYER; ++i) if (game.players[i]) ids[n++] = i;
		int id = ids[gen() % n];
		unsigned int seed = gen();
		srand(seed);
		fprintf(f, "%s{\"seed\":%u,\"gameType\":%d,\"spawnType\":%d,\"limit\":%s,\"left\":%s,\"pods\":[%s,%s],\"spawns\":[", k ? "," : "", seed,
			game.gameType, game.spawnType, fbits(gameVar.sv_gameTimeLimit).c_str(), fbits(game.gameTimeLeft).c_str(),
			vbits(m.flagPodPos[0]).c_str(), vbits(m.flagPodPos[1]).c_str());
		for (int i = 0; i < ns; ++i) fprintf(f, "%s%s", i ? "," : "", vbits(m.dm_spawns.v[i]).c_str());
		fprintf(f, "],\"id\":%d,", id);
		writePlayers(f, game);
		Player * p = game.players[id];
		bool ok = game.spawnPlayer(id);
		fprintf(f, ",\"ok\":%s,\"spawned\":%s,\"at\":%s,\"slot\":%d,\"past\":%s,\"rand\":%u}", ok ? "true" : "false", p->spawned ? "true" : "false",
			vbits(p->spawnedAt).c_str(), p->spawnSlot, m.dm_spawns.readPastEnd ? "true" : "false", msvcSeed);
	}
	fprintf(f, "]");
}

// --- type 3's round reset
static void championCases(FILE * f)
{
	fprintf(f, ",\"champion\":[");
	for (int k = 0; k < 200; ++k)
	{
		Map m;
		newMap(m);
		Game game;
		game.map = &m;
		game.gameType = GAME_TYPE_SND;
		game.isServerGame = true;
		game.blueScore = game.redScore = game.blueWin = game.redWin = 0;
		srand(1); // the round reset calls no rand(): the state must stay
		Server server;
		server.game = &game;
		gameVar.sv_roundTimeLimit = (gen() % 4 == 0) ? 0 : genf(10, 300);
		game.roundTimeLeft = (gen() % 2) ? 0 : genf(0, 300);
		makePlayers(game, 1 + gen() % 10, 16, false);
		m.flagState[0] = m.flagState[1] = -2;
		fprintf(f, "%s{\"limit\":%s,\"left\":%s,", k ? "," : "", fbits(gameVar.sv_roundTimeLimit).c_str(), fbits(game.roundTimeLeft).c_str());
		writePlayers(f, game);
		events.clear();
		server.championStep(delay);
		fprintf(f, ",\"after\":%s,", fbits(game.roundTimeLeft).c_str());
		writeEvents(f);
		fprintf(f, ",");
		writeState(f, game);
		fprintf(f, "}");
	}
	fprintf(f, "]");
}

int main(int argc, char ** argv)
{
	FILE * f = fopen(argv[1], "w");
	fprintf(f, "{");
	ctfCases(f);
	balanceCases(f);
	assignCases(f);
	spawnCases(f);
	championCases(f);
	fprintf(f, "}\n");
	fclose(f);
	return 0;
}
