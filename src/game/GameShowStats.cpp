/*
	Copyright 2012 bitHeads inc.

	This file is part of the BaboViolent 2 source code.

	The BaboViolent 2 source code is free software: you can redistribute it and/or 
	modify it under the terms of the GNU General Public License as published by the 
	Free Software Foundation, either version 3 of the License, or (at your option) 
	any later version.

	The BaboViolent 2 source code is distributed in the hope that it will be useful, 
	but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
	FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along with the 
	BaboViolent 2 source code. If not, see http://www.gnu.org/licenses/.
*/

#ifndef CONSOLE
#include "Game.h"
#include "ui.h"
#include "Helper.h"
#include "UITheme.h"


// openbv: the scoreboard is redrawn in the menus' style (UITheme.h): one centred panel, a header row of
// muted column labels, a band per team in its colour, player rows with the local player marked by the
// accent, numbers right-aligned in fixed columns. What it shows is the original's.
namespace
{
	enum SliceKind { SLICE_HEADER, SLICE_SECTION, SLICE_ROW, SLICE_SELF, SLICE_MUTED };

	const float SB_W = 720;
	const float SB_X = (UI_W - SB_W) / 2;
	const float SB_PAD = 14;
	const float SB_ROW = 28;
	const float SB_TEXT = 22;
	const float SB_COL = 74;   // the numeric columns, right-aligned, from the right edge leftwards

	bool measuring = false;    // the first pass only adds up the height, for the panel behind
	int columns = 5;           // the table's numeric columns: as many as the header names

	const UIColor teamBlue = {0.24f, 0.48f, 1.00f, 1};
	const UIColor teamRed  = {0.90f, 0.28f, 0.30f, 1};
	const UIColor teamNone = {0.75f, 0.78f, 0.84f, 1};

	// the ping in the font's colour codes: green, orange, red, grey for unknown
	CString pingText(int ping)
	{
		int ms = ping * 33;
		if (ms < 100) return CString("\x2%i", ms);
		if (ms < 200) return CString("\x6%i", ms);
		if (ms < 999) return CString("\x4%i", ms);
		return CString("\x7???");
	}
}

void renderStatsSlice(SliceKind kind, const UIColor & team, const char * text1, const char * c1, const char * c2, const char * c3,
	const char * c4, const char * c5, const char * pingStr, int & vPos)
{
	float h = (kind == SLICE_HEADER) ? 22 : (kind == SLICE_SECTION) ? 30 : SB_ROW;
	if (measuring)
	{
		vPos += (int)h + 2;
		return;
	}
	float x = SB_X + SB_PAD, w = SB_W - 2 * SB_PAD, y = (float)vPos;
#ifndef _DX_
	switch (kind)
	{
	case SLICE_HEADER:
		ui::bar(x, y + h, w, 1, ui::panelLine);
		break;
	case SLICE_SECTION:
		ui::fillRect(x, y, w, h, ui::withAlpha(team, .20f), 4);
		ui::fillRect(x, y, 4, h, team, 2);
		break;
	case SLICE_ROW:
	case SLICE_MUTED:
		ui::fillRect(x, y, w, h, ui::withAlpha(ui::field, .45f), 3);
		break;
	case SLICE_SELF:
		ui::fillRect(x, y, w, h, ui::accentDim, 3);
		ui::fillRect(x, y, 3, h, ui::accent, 1.5f);
		break;
	}
	glEnable(GL_TEXTURE_2D);
#endif
	float size = (kind == SLICE_HEADER) ? 16 : (kind == SLICE_SECTION) ? 24 : SB_TEXT;
	float ty = y + h / 2 - size / 2;   // the font's capitals sit in the middle of its cell
	if (kind == SLICE_HEADER || kind == SLICE_MUTED) ui::setColor(ui::textMuted);
	else ui::setColor(ui::text);
	printLeftText(x + 14, ty, size, CString("%s", text1));
	const char * cols[5] = {c1, c2, c3, c4, c5};
	if (kind == SLICE_HEADER)
	{
		columns = 0;
		while (columns < 5 && cols[columns][0]) columns++;
	}
	float right = x + w - 14;
	// a team's band carries its score in the last column; with fewer columns than that (DM, TDM), it
	// goes to the far right, where the band has no ping
	if (kind == SLICE_SECTION && c5[0] && columns < 5 && !pingStr[0]) pingStr = c5;
	printRightText(right, ty, size, CString("%s", pingStr));
	for (int i = 0; i < columns; i++)
		printRightText(right - (columns - i) * SB_COL, ty, size, CString("%s", cols[i]));
	vPos += (int)h + 2;
}

void Game::renderBlueTeam(std::vector<Player*> & blueTeam, int & vPos)
{
	// Blue Team
	renderStatsSlice(SLICE_SECTION, teamBlue, gameVar.lang_blueTeamC.s, "","","","",CString("%i", blueScore).s, CString(""/*%i", bluePing*33*/).s, vPos);
	for (int j=0;j<(int)blueTeam.size();++j)
	{
		CString showName = blueTeam[j]->name;
		if (blueTeam[j]->status == PLAYER_STATUS_DEAD) showName.insert(CString("(%s) ", gameVar.lang_dead.s).s, 0);
		showName.insert((char*)(blueTeam[j]->status == PLAYER_STATUS_DEAD ? "\x7" : "\x8"), 0);

		CString pingStr = pingText(blueTeam[j]->ping);

		renderStatsSlice(	(blueTeam[j] == thisPlayer ? SLICE_SELF : SLICE_ROW), ui::text, showName.s,
							CString("%i",(int)blueTeam[j]->kills).s,
							CString("%i",(int)blueTeam[j]->deaths).s,
							CString("%.1f",blueTeam[j]->dmg).s,
							CString("%i",(int)blueTeam[j]->returns).s,
							CString("%i",(int)blueTeam[j]->score).s,
							pingStr.s, vPos);

	}
	vPos += 10;
}

void Game::renderRedTeam(std::vector<Player*> & redTeam, int & vPos)
{
	// Red Team
	renderStatsSlice(SLICE_SECTION, teamRed, gameVar.lang_redTeamC.s,"","","","", CString("%i", redScore).s, CString(""/*%i", redPing*33*/).s, vPos);
	for (int j=0;j<(int)redTeam.size();++j)
	{
		CString showName = redTeam[j]->name;
		if (redTeam[j]->status == PLAYER_STATUS_DEAD) showName.insert(CString("(%s) ", gameVar.lang_dead.s).s, 0);
		showName.insert((char*)(redTeam[j]->status == PLAYER_STATUS_DEAD ? "\x7" : "\x8"), 0);

		CString pingStr = pingText(redTeam[j]->ping);

		renderStatsSlice(	(redTeam[j] == thisPlayer ? SLICE_SELF : SLICE_ROW), ui::text, showName.s,
							CString("%i",(int)redTeam[j]->kills).s,
							CString("%i",(int)redTeam[j]->deaths).s,
							CString("%.1f",redTeam[j]->dmg).s,
							CString("%i",(int)redTeam[j]->returns).s,
							CString("%i",(int)redTeam[j]->score).s,
							pingStr.s, vPos);
	}
	vPos += 10;
}

void Game::renderFFA(std::vector<Player*> & ffaTeam, int & vPos)
{
	// All Team
	renderStatsSlice(SLICE_SECTION, teamNone, CString(gameVar.lang_freeForAllC.s, redWin+blueWin).s,"","","","", CString(""/*%i", blueScore + redScore*/).s, CString("%i", ffaPing*33).s, vPos);
	for (int j=0;j<(int)ffaTeam.size();++j)
	{
		CString showName = ffaTeam[j]->name;
		if (ffaTeam[j]->status == PLAYER_STATUS_DEAD) showName.insert((CString("(") + gameVar.lang_dead + ") ").s, 0);
		showName.insert((char*)(ffaTeam[j]->status == PLAYER_STATUS_DEAD ? "\x7" : "\x8"), 0);
		CString pingStr = pingText(ffaTeam[j]->ping);

		renderStatsSlice((ffaTeam[j] == thisPlayer ? SLICE_SELF : SLICE_ROW), ui::text, showName.s,CString("%i",(int)ffaTeam[j]->score).s, CString("%i", (int)ffaTeam[j]->deaths).s, CString("%.1f", ffaTeam[j]->dmg).s,"","", pingStr.s, vPos);


	}
	vPos += 10;
}

void Game::renderSpectator(std::vector<Player*> & spectatorTeam, int & vPos)
{
	// Spectators
	renderStatsSlice(SLICE_SECTION, ui::textMuted, gameVar.lang_spectatorC.s, "","","","","", CString(""/*%i", spectatorPing*33*/).s, vPos);
	for (int j=0;j<(int)spectatorTeam.size();++j)
	{
		CString showName = spectatorTeam[j]->name;
		showName.insert("\x7", 0);
		CString pingStr = pingText(spectatorTeam[j]->ping);

		renderStatsSlice(	SLICE_MUTED, ui::text, showName.s,
							CString("%i",(int)spectatorTeam[j]->kills).s,
							CString("%i",(int)spectatorTeam[j]->deaths).s,
							CString("%.1f",spectatorTeam[j]->dmg).s,
							CString("%i",(int)spectatorTeam[j]->returns).s,
							CString("%i",(int)spectatorTeam[j]->score).s,
							pingStr.s, vPos);
	}
}


//
// Afficher les stats
//
void Game::renderStats()
{
	CVector2i res(UI_W, UI_H);// = dkwGetResolution();
#ifndef _DX_
	dkglPushOrtho((float)res[0], (float)res[1]);
		glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			// openbv: an even scrim (it was a black gradient from the top and bottom edges)
			glDisable(GL_TEXTURE_2D);
			ui::fillRect(0, 0, UI_W, UI_H, ui::withAlpha(ui::scrim, .55f), 0);
			glEnable(GL_TEXTURE_2D);
#endif

			// On construit la blue team vector et la red team vector puis on tri
			std::vector<Player*> blueTeam;
			std::vector<Player*> redTeam;
			std::vector<Player*> ffaTeam;
			std::vector<Player*> spectatorTeam;
			spectatorPing=0;
			bluePing = 0;
			redPing = 0;
			ffaPing = 0;
			int j;
			for (int i=0;i<MAX_PLAYER;++i)
			{
				if (players[i])
				{
					switch (players[i]->teamID)
					{
					case PLAYER_TEAM_BLUE:
						for (j=0;j<(int)blueTeam.size();++j)
						{
							if (players[i]->kills > blueTeam[j]->kills) break;
						}
						bluePing+=players[i]->ping;
						blueTeam.insert(blueTeam.begin()+j, players[i]);
						break;
					case PLAYER_TEAM_RED:
						for (j=0;j<(int)redTeam.size();++j)
						{
							if (players[i]->kills > redTeam[j]->kills) break;
						}
						redPing+=players[i]->ping;
						redTeam.insert(redTeam.begin()+j, players[i]);
						break;
					case PLAYER_TEAM_SPECTATOR:
						for (j=0;j<(int)spectatorTeam.size();++j)
						{
							if (players[i]->kills > spectatorTeam[j]->kills) break;
						}
						spectatorPing+=players[i]->ping;
						spectatorTeam.insert(spectatorTeam.begin()+j, players[i]);
						break;
					}

					// On les class tous dans le ffa (sauf spectator)
					if (players[i]->teamID != PLAYER_TEAM_SPECTATOR)
					{
						for (j=0;j<(int)ffaTeam.size();++j)
						{
							if (players[i]->score > ffaTeam[j]->score) break;
						}
						ffaPing+=players[i]->ping;
						ffaTeam.insert(ffaTeam.begin()+j, players[i]);
					}
				}
			}

			if (blueTeam.size() > 0) bluePing /= (int)blueTeam.size();
			if (redTeam.size() > 0) redPing /= (int)redTeam.size();
			if (spectatorTeam.size() > 0) spectatorPing /= (int)spectatorTeam.size();
			if (ffaTeam.size() > 0) ffaPing /= (int)ffaTeam.size();

			// Temporairement juste la liste des joueurs pas trié là pis toute
			dkfBindFont(font);
			int vPos = 60, tableEnd = 60;

			// Title [FIX]: Does not use language file


			// openbv: twice: once to measure (the panel goes behind the table), once to draw
			for (int pass = 0; pass < 2; ++pass)
			{
			measuring = (pass == 0);
			vPos = 60;
			if (pass == 1)
			{
				int top = 60 - (int)SB_PAD;
				ui::rect(SB_X, (float)top, SB_W, (float)(tableEnd - top) + SB_PAD, ui::panel, ui::panelLine, 8);
			}
			switch (gameType)
				{
	         case GAME_TYPE_SND:
				case GAME_TYPE_DM:
					renderStatsSlice(SLICE_HEADER, ui::text, gameVar.lang_playerNameC.s, "Kills", "Death", "Damage", "", "", gameVar.lang_pingC.s, vPos);
					vPos += 10;
					renderFFA(ffaTeam, vPos);
					renderSpectator(spectatorTeam, vPos);
					break;
				case GAME_TYPE_TDM:
					renderStatsSlice(SLICE_HEADER, ui::text, gameVar.lang_playerNameC.s, "Kills", "Death", "Damage", "","", gameVar.lang_pingC.s, vPos);
					vPos += 10;
					if (blueScore >= redScore) 
					{
						renderBlueTeam(blueTeam, vPos);
						renderRedTeam(redTeam, vPos);
					}
					else
					{
						renderRedTeam(redTeam, vPos);
						renderBlueTeam(blueTeam, vPos);
					}
					renderSpectator(spectatorTeam, vPos);
					break;
				case GAME_TYPE_CTF:			
					renderStatsSlice(SLICE_HEADER, ui::text, gameVar.lang_playerNameC.s, "Kills", "Death", "Damage", "Retrn", "Caps", gameVar.lang_pingC.s, vPos);
					vPos += 10;
					if (blueWin >= redWin) 
					{
						renderBlueTeam(blueTeam, vPos);
						renderRedTeam(redTeam, vPos);
					}
					else
					{
						renderRedTeam(redTeam, vPos);
						renderBlueTeam(blueTeam, vPos);
					}
					renderSpectator(spectatorTeam, vPos);
					break;
				}
	
			if (pass == 0) tableEnd = vPos;
			}
			measuring = false;

			blueTeam.clear();
			redTeam.clear();
			spectatorTeam.clear();
			ffaTeam.clear();
#ifndef _DX_
		glPopAttrib();
	dkglPopOrtho();
#endif
}
#endif


