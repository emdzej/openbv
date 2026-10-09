// The reference driver's cases (see driver_head.cpp): deterministic inputs run through the original
// functions, written as JSON with every float as its IEEE-754 bits.
//
// This file is part of openbv, under the GNU General Public License v3 or later.

static void rotateCases(FILE *f)
{
	fprintf(f, "\"rotate\":[");
	for (int k = 0; k < 3000; ++k)
	{
		CVector3f point = genv(-150, 150);
		float angle = (k % 10 == 0) ? (float)((k / 10) % 8 * 45) : genf(-400, 400);
		CVector3f axis = genv(-2, 2);
		if (k % 2) normalize(axis);
		if (k % 7 == 0) axis = CVector3f(0, 0, 1);
		CVector3f r = rotateAboutAxis(point, angle, axis);
		fprintf(f, "%s{\"p\":", k ? "," : ""); vec(f, point);
		fprintf(f, ",\"a\":"); hexf(f, angle);
		fprintf(f, ",\"x\":"); vec(f, axis);
		fprintf(f, ",\"r\":"); vec(f, r); fprintf(f, "}");
	}
	fprintf(f, "]");
}

static void segmentCases(FILE *f)
{
	fprintf(f, "\"segment\":[");
	for (int k = 0; k < 3000; ++k)
	{
		CVector3f p1 = genv(0, 20);
		CVector3f p2 = (k % 50 == 0) ? p1 : p1 + genv(-15, 15);
		CVector3f c = (k % 3 == 0) ? p1 + (p2 - p1) * genf(-.2f, 1.2f) + genv(-.4f, .4f) : genv(0, 20);
		float r = genf(.1f, .6f);
		CVector3f in2 = p2;
		bool hit = segmentToSphere(p1, p2, c, r);
		fprintf(f, "%s{\"p1\":", k ? "," : ""); vec(f, p1);
		fprintf(f, ",\"p2\":"); vec(f, in2);
		fprintf(f, ",\"c\":"); vec(f, c);
		fprintf(f, ",\"r\":"); hexf(f, r);
		fprintf(f, ",\"hit\":%s,\"out\":", hit ? "true" : "false"); vec(f, p2); fprintf(f, "}");
	}
	fprintf(f, "]");
}

static void rayCases(FILE *f)
{
	fprintf(f, "\"ray\":[");
	for (int mi = 0; mi < 20; ++mi)
	{
		Map m;
		m.size = CVector2i(6 + gen() % 19, 6 + gen() % 19);
		m.dko_mapLM = 0;
		m.cells = new map_cell[m.size[0] * m.size[1]];
		for (int c = 0; c < m.size[0] * m.size[1]; ++c)
		{
			m.cells[c].passable = (gen() % 10) < 7;
			m.cells[c].height = m.cells[c].passable ? 0 : 1 + gen() % 4;
		}
		fprintf(f, "%s{\"w\":%d,\"h\":%d,\"cells\":[", mi ? "," : "", m.size[0], m.size[1]);
		for (int c = 0; c < m.size[0] * m.size[1]; ++c)
			fprintf(f, "%s%d", c ? "," : "", m.cells[c].passable ? -1 : m.cells[c].height);
		fprintf(f, "],\"rays\":[");
		for (int k = 0; k < 200; ++k)
		{
			CVector3f p1(genf(-1, (float)m.size[0] + 1), genf(-1, (float)m.size[1] + 1), genf(-.5f, 3));
			CVector3f d = genv(-12, 12);
			d[2] = genf(-3, 3);
			if (k % 9 == 0) d[2] = 0;
			CVector3f p2 = p1 + d;
			CVector3f in2 = p2;
			CVector3f normal(7, 7, 7);
			bool hit = m.rayTest(p1, p2, normal);
			fprintf(f, "%s{\"p1\":", k ? "," : ""); vec(f, p1);
			fprintf(f, ",\"p2\":"); vec(f, in2);
			fprintf(f, ",\"hit\":%s,\"out\":", hit ? "true" : "false"); vec(f, p2);
			fprintf(f, ",\"n\":"); vec(f, normal); fprintf(f, "}");
		}
		fprintf(f, "]}");
		delete [] m.cells;
	}
	fprintf(f, "]");
}

// The spread of one bullet: these statements are Game::shootSV(playerID, ...)'s (Game.cpp:1415).
static void spreadCases(FILE *f)
{
	fprintf(f, "\"spread\":[");
	for (int k = 0; k < 2000; ++k)
	{
		unsigned int seed = gen();
		float imp = (k % 8 == 0) ? 0 : genf(0, 20);
		CVector3f dir = genv(-1, 1);
		CVector3f p1 = genv(0, 30);
		srand(seed);
		CVector3f p2 = dir;
		p2 = p2 * 128;
		p2 = rotateAboutAxis(p2, rand(-imp, imp), CVector3f(0,0,1));
		p2 = rotateAboutAxis(p2, rand(0.0f, 360.0f), dir);
		p2[2] *= .5f;
		p2 += p1;
		int next = rand();
		fprintf(f, "%s{\"seed\":%u,\"imp\":", k ? "," : "", seed); hexf(f, imp);
		fprintf(f, ",\"dir\":"); vec(f, dir);
		fprintf(f, ",\"p1\":"); vec(f, p1);
		fprintf(f, ",\"p2\":"); vec(f, p2);
		fprintf(f, ",\"next\":%d}", next);
	}
	fprintf(f, "]");
}

static void reflectCases(FILE *f)
{
	fprintf(f, "\"reflect\":[");
	for (int k = 0; k < 1000; ++k)
	{
		CVector3f u = genv(-12, 12);
		CVector3f n;
		switch (k % 4)
		{
		case 0: n = CVector3f(0, 0, 1); break;
		case 1: n = CVector3f((k & 4) ? 1.f : -1.f, 0, 0); break;
		case 2: n = CVector3f(0, (k & 4) ? 1.f : -1.f, 0); break;
		default: n = genv(-1, 1); normalize(n); break;
		}
		CVector3f r = reflect(u, n) * .65f;
		fprintf(f, "%s{\"u\":", k ? "," : ""); vec(f, u);
		fprintf(f, ",\"n\":"); vec(f, n);
		fprintf(f, ",\"r\":"); vec(f, r); fprintf(f, "}");
	}
	fprintf(f, "]");
}

static void damageCases(FILE *f)
{
	fprintf(f, "\"damage\":[");
	for (int k = 0; k < 3000; ++k)
	{
		gameVar.sv_serverType = gen() % 2;
		gameVar.sv_photonType = gen() % 5;
		gameVar.sv_subGameType = (gen() % 10 == 0) ? 1 : 0;
		gameVar.sv_smgDamage = genf(0, 1); gameVar.sv_sniperDamage = genf(0, 1); gameVar.sv_shottyDamage = genf(0, 1);
		gameVar.sv_dmgDamage = genf(0, 1); gameVar.sv_cgDamage = genf(0, 1); gameVar.sv_ftDamage = genf(0, 1);
		gameVar.sv_ftMaxRange = genf(1, 24);
		gameVar.sv_photonVerticalShift = genf(-2, 2); gameVar.sv_photonDamageCoefficient = genf(-2, 2);
		gameVar.sv_photonHorizontalShift = genf(-5, 5); gameVar.sv_photonDistMult = genf(-2, 2);
		Weapon fromWeapon; fromWeapon.weaponID = gen() % 10; fromWeapon.damage = genf(0, 1.5f);
		Weapon shooterWeapon; shooterWeapon.shotFrom = genv(0, 30);
		Player victim, shooter;
		shooter.weapon = &shooterWeapon;
		victim.weapon = 0;
		victim.currentCF.position = (k % 25 == 0) ? shooterWeapon.shotFrom : genv(0, 30);
		victim.protection = genf(0, 2); victim.immuneTime = genf(0, 1); victim.life = genf(0, 1);
		bool self = (k % 10 == 0);
		float damage = (k % 2) ? -1 : genf(0, 2);
		float cdamage = victim.damagePrelude(&fromWeapon, self ? &victim : &shooter, damage);
		fprintf(f, "%s{\"sv\":[%d,%d,%d", k ? "," : "", gameVar.sv_serverType, gameVar.sv_photonType, gameVar.sv_subGameType);
		float fl[] = {gameVar.sv_smgDamage, gameVar.sv_sniperDamage, gameVar.sv_shottyDamage, gameVar.sv_dmgDamage,
			gameVar.sv_cgDamage, gameVar.sv_ftDamage, gameVar.sv_ftMaxRange, gameVar.sv_photonVerticalShift,
			gameVar.sv_photonDamageCoefficient, gameVar.sv_photonHorizontalShift, gameVar.sv_photonDistMult};
		for (int i = 0; i < 11; ++i) { fprintf(f, ","); hexf(f, fl[i]); }
		fprintf(f, "],\"weapon\":%d,\"wdamage\":", fromWeapon.weaponID); hexf(f, fromWeapon.damage);
		fprintf(f, ",\"shotFrom\":"); vec(f, shooterWeapon.shotFrom);
		fprintf(f, ",\"pos\":"); vec(f, victim.currentCF.position);
		fprintf(f, ",\"self\":%s,\"prot\":", self ? "true" : "false"); hexf(f, victim.protection);
		fprintf(f, ",\"immune\":"); hexf(f, victim.immuneTime);
		fprintf(f, ",\"life\":"); hexf(f, victim.life);
		fprintf(f, ",\"damage\":"); hexf(f, damage);
		fprintf(f, ",\"out\":"); hexf(f, cdamage); fprintf(f, "}");
	}
	fprintf(f, "]");
}

int main(int argc, char **argv)
{
	FILE *f = argc > 1 ? fopen(argv[1], "w") : stdout;
	fprintf(f, "{");
	rotateCases(f); fprintf(f, ",\n");
	segmentCases(f); fprintf(f, ",\n");
	rayCases(f); fprintf(f, ",\n");
	spreadCases(f); fprintf(f, ",\n");
	reflectCases(f); fprintf(f, ",\n");
	damageCases(f);
	fprintf(f, "}\n");
	if (f != stdout) fclose(f);
	return 0;
}
