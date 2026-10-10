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
#include "Client.h"
#include "ui.h"
#include "UITheme.h"
#include "KeyManager.h"
#include "Console.h"
#include "CControl.h"
#include "CMenuManager.h"


#ifdef RENDER_LAYER_TOGGLE
	extern int renderToggle;
#endif



//
// Pour l'afficher
//
// openbv: the HUD in the menus' style (UITheme.h): small dark panels, the accent for progress, a health
// bar that shifts from green to red. What it shows, where and when, is the original's.
namespace
{
	const UIColor hudGreen  = {0.24f, 0.82f, 0.45f, 1};
	const UIColor hudOrange = {1.00f, 0.66f, 0.18f, 1};
	const UIColor hudRed    = {0.90f, 0.28f, 0.30f, 1};
	const UIColor hudTrack  = {0.02f, 0.03f, 0.05f, 0.70f};
	const UIColor teamBlue  = {0.24f, 0.48f, 1.00f, 1};
	const UIColor teamRed   = {0.90f, 0.28f, 0.30f, 1};

	UIColor healthColor(float life)
	{
		if (life > .6f) return hudGreen;
		if (life > .3f) return ui::mix(hudOrange, hudGreen, (life - .3f) / .3f);
		return ui::mix(hudRed, hudOrange, life / .3f);
	}

	// A panel behind a line of text centred on cx (y is the text's top, as printCenterText takes it).
	void banner(float cx, float y, float size, const CString & text, const UIColor & tint, float alpha = 1)
	{
		float w = dkfGetStringWidth(size, textColorLess(text).s) + size * .9f;
		float h = size * .62f;
		float top = y + size / 2 - h / 2;
		glDisable(GL_TEXTURE_2D);
		ui::rect(cx - w / 2, top, w, h, ui::withAlpha(ui::panel, ui::panel.a * alpha), ui::withAlpha(ui::panelLine, ui::panelLine.a * alpha), h / 3);
		if (tint.a > 0) ui::fillRect(cx - w / 2, top, w, h, ui::withAlpha(tint, .22f * alpha), h / 3);
		glEnable(GL_TEXTURE_2D);
		ui::setColor(ui::withAlpha(ui::text, alpha));
		printCenterText(cx, y, size, text);
	}

	// The reload / grenade progress under the crosshair area: a thin accent bar in a small panel.
	void progress(float alpha, float frac, bool showText)
	{
		if (frac < 0) frac = 0;
		if (frac > 1) frac = 1;
		float x = UI_CX - 110, y = 438, w = 220, h = 8;
		glDisable(GL_TEXTURE_2D);
		ui::rect(x - 8, y - 8, w + 16, h + 16, ui::panel, ui::panelLine, 8);
		ui::fillRect(x, y, w, h, hudTrack, 4);
		ui::fillRect(x, y, w * frac, h, ui::withAlpha(ui::accent, .55f + .45f * alpha), 4);
		glEnable(GL_TEXTURE_2D);
		if (showText)
		{
			ui::setColor(ui::text);
			printCenterText(UI_CX, 400, 28, gameVar.lang_reloading);
		}
	}

	// A secondary's count: its icon on a small tile, the number in the corner. pulse grows the icon as
	// the original did while the throw recharges.
	void itemTile(unsigned int tex, float cx, float cy, float pulse, int count)
	{
		const float t = 46;
		glDisable(GL_TEXTURE_2D);
		ui::rect(cx - t / 2, cy - t / 2, t, t, ui::panel, ui::panelLine, 7);
		glEnable(GL_TEXTURE_2D);
		glBindTexture(GL_TEXTURE_2D, tex);
		float r = 19 + pulse * 10;
		glColor3f(1, 1, 1);
		glBegin(GL_QUADS);
			glTexCoord2f(0, 1); glVertex2f(cx - r, cy - r);
			glTexCoord2f(0, 0); glVertex2f(cx - r, cy + r);
			glTexCoord2f(1, 0); glVertex2f(cx + r, cy + r);
			glTexCoord2f(1, 1); glVertex2f(cx + r, cy - r);
		glEnd();
		ui::setColor(ui::text);
		printRightText(cx + t / 2 - 4, cy + t / 2 - 22, 26, CString("%i", count));
	}

	// A vertical gauge filling from the bottom: health, the chain gun's heat.
	void gauge(float x, float y, float w, float h, float frac, const UIColor & fill, bool showFill)
	{
		if (frac < 0) frac = 0;
		if (frac > 1) frac = 1;
		glDisable(GL_TEXTURE_2D);
		ui::rect(x - 5, y - 5, w + 10, h + 10, ui::panel, ui::panelLine, (w + 10) / 2);
		ui::fillRect(x, y, w, h, hudTrack, w / 2);
		if (showFill && frac > 0)
		{
			float fh = h * frac;
			if (fh < w) fh = w;   // a full rounded end even when nearly empty
			ui::fillRect(x, y + h - fh, w, fh, fill, w / 2);
		}
		glEnable(GL_TEXTURE_2D);
	}
}

void Client::render(float & alphaScope)
{
  int i;
	CVector2i res = dkwGetResolution();

	if (game->thisPlayer)
	{
		if (game->thisPlayer->status == PLAYER_STATUS_ALIVE)
		{
			if (game->map)
			{
				game->map->camLookAt = (
					game->thisPlayer->currentCF.position*5 + 
					game->thisPlayer->currentCF.mousePosOnMap*4) / 9.0f;
				if (game->map->camLookAt[0] < 0) game->map->camLookAt[0] = 0;
				if (game->map->camLookAt[1] < -1) game->map->camLookAt[1] = -1;
				if (game->map->camLookAt[0] > (float)game->map->size[0]) game->map->camLookAt[0] = (float)game->map->size[0];
				if (game->map->camLookAt[1] > (float)game->map->size[1]+1) game->map->camLookAt[1] = (float)game->map->size[1]+1;
			}
		}
	}

	// C'est côté client qu'on fait ça ;)
	if (isConnected) game->render();

	// LE SNIPER SCOPE
	CVector2i cursor = dkwGetCursorPos_main();
	int xM = (int)(((float)cursor[0]/(float)res[0])* (float)UI_W);
	int yM = (int)(((float)cursor[1]/(float)res[1])* (float)UI_H);
#ifndef _DX_
	dkglPushOrtho(UI_W, UI_H);
		glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			//--- Sniper scope!!
			if (game->thisPlayer && game->thisPlayer->status == PLAYER_STATUS_ALIVE)
			{
				if (game->thisPlayer->weapon && game->thisPlayer->weapon->weaponID == WEAPON_SNIPER)
				{
					if (game->map && game->map->camPos[2] > 8)
					{
						alphaScope = 10 - (game->map->camPos[2]-2);
						alphaScope = (alphaScope > 0)? 1-(alphaScope/2) : 1;
						glColor4f(0,0,0,alphaScope);
						if (!(menuManager.root && menuManager.root->visible) && !showMenu)
						{
							// render the scope view
							renderTexturedQuad(xM-128,yM-128,256,256,gameVar.tex_sniperScope);
							renderTexturedQuad(0,0,UI_W,yM-128,0);
							renderTexturedQuad(0,yM+128,UI_W,UI_H-(yM+128),0);
							renderTexturedQuad(0,yM-128,xM-128,256,0);
							renderTexturedQuad(xM+128,yM-128,UI_W-(xM+128),256,0);
						}
						else
						{
							// render black background in the menu
							renderTexturedQuad(0,0,UI_W,UI_H,0);
						}
					}
				}
			}

		glPopAttrib();
	dkglPopOrtho();
#endif

	// Si on doit spawner on marque dans combient de temps
	if (game->thisPlayer)
	{
#ifndef _DX_
		dkglPushOrtho(UI_W, UI_H);
			glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
				glEnable(GL_TEXTURE_2D);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				// Si on c faite tirer dessus
				if (game->thisPlayer->screenHit > 0)
				{
					glColor4f(1,1,1,game->thisPlayer->screenHit*3);
					glEnable(GL_TEXTURE_2D);
					glBindTexture(GL_TEXTURE_2D, tex_screenHit);
					glBegin(GL_QUADS);
						glTexCoord2i(0,1);
						glVertex2i(0,0);
						glTexCoord2i(0,0);
						glVertex2i(0,600);
						glTexCoord2i(1,0);
						glVertex2i(UI_W,600);
						glTexCoord2i(1,1);
						glVertex2i(UI_W,0);
					glEnd();
				}

				#ifdef RENDER_LAYER_TOGGLE
					if (renderToggle >= 16)
				#endif
				if (game->thisPlayer->status == PLAYER_STATUS_ALIVE)
				{
					// Quand on reload le gun il faut le marquer
					if (game->thisPlayer->weapon)
					{
						if (game->thisPlayer->weapon->currentFireDelay > 0 && game->thisPlayer->weapon->fireDelay >= 1.0f)
						{
							// openbv: a themed bar (it was a framed green one)
							progress(game->thisPlayer->weapon->currentFireDelay/game->thisPlayer->weapon->fireDelay, 1-game->thisPlayer->weapon->currentFireDelay/game->thisPlayer->weapon->fireDelay, blink < .25f);
						}
						else if (game->thisPlayer->grenadeDelay > 0)
						{
							// openbv: a themed bar (it was a framed green one)
							progress(game->thisPlayer->grenadeDelay/gameVar.weapons[WEAPON_GRENADE]->fireDelay, 1-game->thisPlayer->grenadeDelay/gameVar.weapons[WEAPON_GRENADE]->fireDelay, blink < .25f);
						}
						else if (game->thisPlayer->weapon->weaponID == WEAPON_SHOTGUN && game->thisPlayer->weapon->currentFireDelay > 0)
						{
							// openbv: a themed bar (it was a framed green one)
							progress(game->thisPlayer->weapon->currentFireDelay/game->thisPlayer->weapon->fireDelay, 1-game->thisPlayer->weapon->currentFireDelay/3, blink < .25f);
						}
						else if (game->thisPlayer->meleeDelay > 0)
						{
							// openbv: a themed bar (it was a framed green one)
							progress(game->thisPlayer->meleeDelay/game->thisPlayer->meleeWeapon->fireDelay, 1-game->thisPlayer->meleeDelay/game->thisPlayer->meleeWeapon->fireDelay, blink < .25f);
						}
					}

					glDisable(GL_TEXTURE_2D);

					// On affiche sa vie à droite
					// openbv: a slim rounded gauge, green to red with the health left, and the number above it;
					// it still blinks below a quarter, as the original's bar did
					gauge(UI_W - 30, 392, 14, 190, game->thisPlayer->life, healthColor(game->thisPlayer->life),
						game->thisPlayer->life > .25f || blink < .25f);
					ui::setColor(game->thisPlayer->life > .25f ? ui::text : hudRed);
					printCenterText(UI_W - 23, 356, 24, CString("%i", (int)(game->thisPlayer->life * 100 + .5f)));

					// Le heat du gun (ChainGun | FlameThrower)
					// openbv: the chain gun's cooling as a second gauge, left of the health; blinking when overheated
					if (game->thisPlayer->weapon && game->thisPlayer->weapon->weaponID == WEAPON_CHAIN_GUN)
					{
						float heat = game->thisPlayer->weapon->chainOverHeat;
						gauge(UI_W - 58, 392, 10, 190, heat, ui::mix(hudRed, UIColor{.85f, .92f, 1, 1}, heat),
							!game->thisPlayer->weapon->overHeated || blink < .25f);
					}

					// Le nb de grenade quil lui reste
					if (game->thisPlayer->nbGrenadeLeft > 0)
					{
						itemTile(tex_grenadeLeft, UI_W - 82, 526 + 32, (game->thisPlayer->lastShootWasNade ? game->thisPlayer->grenadeDelay : 0), game->thisPlayer->nbGrenadeLeft);
					}

					// Le nb de molotov quil lui reste
					if (game->thisPlayer->nbMolotovLeft > 0 && gameVar.sv_enableMolotov)
					{
						itemTile(tex_molotovLeft, UI_W - 82, 474 + 32, (!game->thisPlayer->lastShootWasNade ? game->thisPlayer->grenadeDelay : 0), game->thisPlayer->nbMolotovLeft);
					}

					// Le nb de balle de shotgun quil lui reste
					if ( (game->thisPlayer->weapon->weaponID == WEAPON_SHOTGUN) &&
						(gameVar.sv_enableShotgunReload == true) )
					{
						itemTile(tex_shotgunLeft, UI_W - 82, 422 + 32, game->thisPlayer->weapon->currentFireDelay, 6 - game->thisPlayer->weapon->shotInc);
					}
				}
				else if ((
					game->thisPlayer->teamID == PLAYER_TEAM_BLUE || 
					game->thisPlayer->teamID == PLAYER_TEAM_RED) &&
					game->thisPlayer->status == PLAYER_STATUS_DEAD  && !game->thisPlayer->spawnRequested)
				{
					// openbv: on a banner, below the scoreboard the game shows while you are dead (it was at 200, under it)
					if (game->thisPlayer->timeToSpawn > 0) banner(UI_CX, 470, 56, CString(gameVar.lang_spawnIn.s, ((int)game->thisPlayer->timeToSpawn+1)/60, ((int)(game->thisPlayer->timeToSpawn+1)%60)), UIColor{0,0,0,0});
					else if (!gameVar.sv_forceRespawn) banner(UI_CX, 470, 44, CString("Press shoot key [%s] to respawn", keyManager.getKeyName(gameVar.k_shoot).s), ui::accent);
				}

				//--- Auto balance
				if (gameVar.sv_autoBalance && autoBalanceTimer > 0 && blink < .25f)
				{
					banner(UI_CX, 8, 40, CString("Autobalance in %i seconds", (int)autoBalanceTimer), ui::accent);
				}

				/*printCenterText(200,100,20,CString("Time played: %.2f", game->thisPlayer->timePlayedCurGame));
				printCenterText(200,124,20,CString("Flag attempts: %d", game->thisPlayer->flagAttempts));*/

			glPopAttrib();
		dkglPopOrtho();

		if(gameVar.r_widescreen > 1) res[0] = static_cast<int>(res[1]*1.333f);
		// openbv: the ping graph, timer, scores, chat and events were laid out in screen pixels (their size
		// depended on the resolution); they use the UI's units now, so they look as at 800x600, at any size
		res.set(UI_W, UI_H);
		if (gameVar.r_showLatency)
		{
			dkglPushOrtho((float)res[0], (float)res[1]);
				glPushAttrib(GL_LINE_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT);
					glEnable(GL_BLEND);
					float left = res[0] - (float)(PING_LOG_SIZE) - 15;
					float bottom = 65;
					float height = 40;
					float scaledHeight = height / 1000.0f;
					glDisable(GL_TEXTURE_2D);
					// openbv: in a themed panel (it was a grey box)
					ui::rect(left - 7, bottom - height - 6, PING_LOG_SIZE + 13, height + 12, ui::panel, ui::panelLine, 6);

					glLineWidth(1.0f);
					glBegin(GL_LINES);

					glColor4f(1.0f, 0.0f, 0.0f, 0.6f);
					glVertex3f(left-1, bottom, 0.0f);
					glVertex3f(left-1, bottom-height, 0.0f);

					glColor4f(1.0f, 1.0f, 0.0f, 0.6f);
					glVertex3f(left-1, bottom, 0.0f);
					glVertex3f(left-1, bottom-200*scaledHeight, 0.0f);

					glColor4f(0.0f, 1.0f, 0.0f, 0.6f);
					glVertex3f(left-1, bottom, 0.0f);
					glVertex3f(left-1, bottom-100*scaledHeight, 0.0f);

					int j = game->thisPlayer->pingLogID;//, k = 0;
					int ping;
					for (int i = 0; i < PING_LOG_SIZE; i++, j++)//, k += 2)
					{
						if (j >= PING_LOG_SIZE)
							j = 0;
						ping = game->thisPlayer->pingLog[j]*33;
						if (ping > 1000 || ping < 0)
							ping = 1000;
						if (ping <= 100)
							glColor4f(0.0f, 1.0f, 0.0f, 0.5f);
						else if (ping <= 200)
							glColor4f(1.0f, 1.0f, 0.0f, 0.5f);
						else
							glColor4f(1.0f, 0.0f, 0.0f, 0.5f);
						glVertex3f(left+i, bottom, 0.0f);
						glVertex3f(left+i, bottom-1-ping*scaledHeight, 0.0f);
					}
					glEnd();
					/*glColor4f(0.0f, 0.0f, 0.0f, 0.8f);
					glBegin(GL_LINES);
						glVertex3f(left, bottom-game->thisPlayer->avgPing*(height/1000.0f), 0.0f);
						glVertex3f(left+PING_LOG_SIZE, bottom-game->thisPlayer->avgPing*(height/1000.0f), 0.0f);
					glEnd();
					glColor4f(1.0f, 1.0f, 1.0f, 0.7f);
					printLeftText(left, bottom + 5, 20, CString("Ang. ping: %d", game->thisPlayer->avgPing));*/
				glPopAttrib();
			dkglPopOrtho();
		}

		// On render les chat message par dessus le jeu
		dkglPushOrtho((float)res[0], (float)res[1]);
			glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				// Afficher les win des team ou les score si en TDM ou CTF ou SND
				// openbv: the clock and the teams' scores in one panel, top left (the original printed
				// them in large text straight over the map); the leading team first, as before
				{
					CString lines[3];
					unsigned int icons[3] = {0, 0, 0};
					int n = 0;
					bool roundClock = false;
					switch (game->gameType)
					{
					case GAME_TYPE_DM:
					case GAME_TYPE_TDM:
					case GAME_TYPE_CTF:
						lines[n++] = CString("%01i:%02i", (int)((game->gameTimeLeft+1)/60), (int)(game->gameTimeLeft+1)%60);
						break;
					case GAME_TYPE_SND:
#ifdef _PRO_
						lines[n++] = CString("%01i:%02i", (int)((game->gameTimeLeft+1)/60), (int)(game->gameTimeLeft+1)%60);
						lines[n++] = CString("%01i:%02i", (int)((game->roundTimeLeft+1)/60), (int)(game->roundTimeLeft+1)%60);
						roundClock = true;
#else
						lines[n++] = CString("%01i:%02i", (int)((game->roundTimeLeft+1)/60), (int)(game->roundTimeLeft+1)%60);
#endif
						break;
					}
					int blue = 0, red = 0, limit = 0;
					bool scores = false;
					if (game->gameType == GAME_TYPE_TDM) { blue = game->blueScore; red = game->redScore; limit = gameVar.sv_scoreLimit; scores = true; }
					if (game->gameType == GAME_TYPE_CTF) { blue = game->blueWin; red = game->redWin; limit = gameVar.sv_winLimit; scores = true; }
#ifndef _PRO_
					if (game->gameType == GAME_TYPE_SND) { blue = game->blueWin; red = game->redWin; limit = gameVar.sv_winLimit; scores = true; }
#endif
					if (scores)
					{
						bool blueFirst = blue >= red;
						icons[n] = blueFirst ? tex_blueFlag : tex_redFlag;
						lines[n++] = CString("%i/%i", blueFirst ? blue : red, limit);
						icons[n] = blueFirst ? tex_redFlag : tex_blueFlag;
						lines[n++] = CString("%i/%i", blueFirst ? red : blue, limit);
					}
					const float clockSize = 52, lineSize = 32, rowClock = 36, rowLine = 30, pad = 10;
					float w = 0, h = pad * 2;
					for (int k = 0; k < n; k++)
					{
						bool clock = k == 0 || (roundClock && k == 1);
						float lw = dkfGetStringWidth(clock ? clockSize : lineSize, lines[k].s) + (icons[k] ? 34 : 0);
						if (lw > w) w = lw;
						h += clock ? rowClock : rowLine;
					}
					w += pad * 2 + 4;
					glDisable(GL_TEXTURE_2D);
					ui::rect(10, 10, w, h, ui::panel, ui::panelLine, 8);
					glEnable(GL_TEXTURE_2D);
					float y = 10 + pad;
					for (int k = 0; k < n; k++)
					{
						bool clock = k == 0 || (roundClock && k == 1);
						float row = clock ? rowClock : rowLine, size = clock ? clockSize : lineSize;
						float x = 10 + pad + 2;
						if (icons[k])
						{
							glColor3f(1, 1, 1);
							renderTexturedQuad((int)x - 4, (int)(y + row / 2 - 16), 32, 32, icons[k]);
							x += 34;
						}
						ui::setColor(k == 1 && roundClock ? ui::textMuted : ui::text);
						printLeftText(x, y + row / 2 - size / 2, size, lines[k]);
						y += row;
					}
				}

#ifdef _PRO_	      

            float textSize = (float)gameVar.r_chatTextSize;
#endif

            float xPos = ((float)res[0] / (float)UI_W) * 128 + 40;
				float yPos = res[1] - (((float)res[1] / (float)UI_H) * 128 + 40)-60;

				for (i=0;i<(int)chatMessages.size();++i)
				{
					if (chatMessages[i].duration > 1)
					{
						glColor4f(0,0,0, .5f);
					}
					else
					{
						glColor4f(0,0,0,chatMessages[i].duration*.5f);
					}
					glDisable(GL_TEXTURE_2D);
					glBegin(GL_QUADS);

         
#ifdef _PRO_	      
               float chatWidth = dkfGetStringWidth(textSize, chatMessages[i].message.s);

					glVertex2f(8,yPos - (float)((chatMessages.size() - i - 1) * textSize)+1);
					glVertex2f(8,yPos - (float)((chatMessages.size() - i - 1) * textSize)+textSize-1);
					glColor4f(0,0,0,0);
					glVertex2f(8+chatWidth,yPos - (float)((chatMessages.size() - i - 1) * textSize)+textSize-1);
					glVertex2f(8+chatWidth,yPos - (float)((chatMessages.size() - i - 1) * textSize)+1);
#else
					glVertex2f(8,yPos - (float)(chatMessages.size() - i - 1) * 28+1);
					glVertex2f(8,yPos - (float)(chatMessages.size() - i - 1) * 28+27);
					glColor4f(0,0,0,0);
					glVertex2f(500,yPos - (float)(chatMessages.size() - i - 1) * 28+27);
					glVertex2f(500,yPos - (float)(chatMessages.size() - i - 1) * 28+1);
#endif

					glEnd();
					if (chatMessages[i].duration > 1)
					{
						glColor3f(1,1,1);
					}
					else
					{
						glColor4f(1,1,1,chatMessages[i].duration);
					}
					// On l'écris à peut pret au tier de l'écran à gauche
					glEnable(GL_TEXTURE_2D);

#ifdef _PRO_	
					printLeftText(10,yPos - (float)(chatMessages.size() - i - 1) * textSize, textSize, chatMessages[i].message);
#else
               printLeftText(10,yPos - (float)(chatMessages.size() - i - 1) * 28, 28, chatMessages[i].message);
#endif
				}

				// Si on est apres chatter
				if (chatting.haveFocus())
				{
					glDisable(GL_TEXTURE_2D);
					glBegin(GL_QUADS);
						glColor4f(0,0,0, .5f);
						glVertex2f(8,yPos +32+1);
						glVertex2f(8,yPos +32+27);
						glColor4f(0,0,0,0);
						glVertex2f(500,yPos +32+27);
						glVertex2f(500,yPos +32+1);
					glEnd();
					glEnable(GL_TEXTURE_2D);
					glColor3f(1,1,1);
					CString chattingTo;
					if (isChattingTeam)
						chattingTo = "sayteam : ";
					else
						chattingTo = "say : ";

					
               float chattingToWidth = dkfGetStringWidth(28, chattingTo.s);
					printLeftText(10,yPos + 32,28,chattingTo);
					chatting.print(28, 10+chattingToWidth,yPos + 32, 0);
				}


				// Les events
				for (i=0;i<(int)eventMessages.size();++i)
				{
					if (eventMessages[i].duration > 1)
					{
						glColor3f(1,1,1);
					}
					else
					{
						glColor4f(1,1,1,eventMessages[i].duration);
					}
					// On l'écris à peut pret au 2 tier de l'écran à gauche

#ifdef _PRO_	                  
            float eventTextSize = (float)gameVar.r_eventTextSize;

            if (gameVar.r_showEventText)
            {
               // openbv: on a soft dark backing, so it reads over bright maps
               float ey = (float)res[1] - (float)(eventMessages.size() - i - 1) * eventTextSize-20-eventTextSize;
               float fade = eventMessages[i].duration > 1 ? 1 : eventMessages[i].duration;
               float ew = dkfGetStringWidth(eventTextSize, textColorLess(eventMessages[i].message).s);
               glDisable(GL_TEXTURE_2D);
               ui::fillRect(xPos - 6, ey + eventTextSize * .18f, ew + 12, eventTextSize * .64f, ui::withAlpha(ui::panel, .6f * fade), 4);
               glEnable(GL_TEXTURE_2D);
               glColor4f(1, 1, 1, fade);
               printLeftText(xPos, ey, eventTextSize, eventMessages[i].message);
            }
#else
            printLeftText(xPos,(float)res[1] - (float)(eventMessages.size() - i - 1) * 28-20-28, 28, eventMessages[i].message);
#endif

					
				}

			glPopAttrib();
		dkglPopOrtho();

		// Les stats
		if (game->showStats)
		{
			game->renderStats();

			if (blink < .25f)
			{
				dkglPushOrtho(UI_W, UI_H);
					glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
						glEnable(GL_BLEND);
						glColor3f(1,1,1);
						glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
						switch (game->roundState)
						{
						case GAME_PLAYING: break;
						case GAME_BLUE_WIN:
							banner(UI_CX, 8, 48, gameVar.lang_blueTeamWin, teamBlue);
							break;
						case GAME_RED_WIN:
							banner(UI_CX, 8, 48, gameVar.lang_redTeamWin, teamRed);
							break;
						case GAME_DRAW:
							banner(UI_CX, 8, 48, gameVar.lang_roundDraw, UIColor{0,0,0,0});
							break;
						case GAME_MAP_CHANGE:
							banner(UI_CX, 8, 48, gameVar.lang_changingMap, UIColor{0,0,0,0});
							break;
						}
					glPopAttrib();
				dkglPopOrtho();
			}
		}
	}

	// On render le menu si c'est le cas
	if (showMenu && isConnected)
	{
		dkwClipMouse( false );
		menuManager.render(clientRoot);
	//	if (clientRoot) clientRoot->render();
	
	//	renderMenu();
		dkglPushOrtho(UI_W, UI_H);
			glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
				glEnable(GL_BLEND);
				glColor3f(1,1,1);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

				//--- Square around the choosen gun
				// openbv: an accent outline around the chosen weapons (it was a green square)
				{
					CControl *chosen[2] = {currentGun, currentMelee};
					for (int c = 0; c < 2; c++)
					{
						float cx = (float)chosen[c]->pos[0], cy = (float)chosen[c]->pos[1];
						float cw = (float)chosen[c]->size[0], ch = (float)chosen[c]->size[1];
						ui::lineRect(cx - 3, cy - 3, cw + 6, ch + 6, ui::accent, 7);
						ui::lineRect(cx - 2, cy - 2, cw + 4, ch + 4, ui::accent, 6);
					}
				}
				glColor3f(1,1,1);

				printCenterText(UI_CX, 5+48, 32, gameVar.sv_gameName);
				switch (game->gameType)
				{
				case GAME_TYPE_DM:
					printCenterText(UI_CX, 5, 64, gameVar.lang_deathmatchC);
					printCenterText(UI_CX, 5+88, 32, gameVar.lang_deathmatchD);
					break;
				case GAME_TYPE_TDM:
					printCenterText(UI_CX, 5, 64, gameVar.lang_teamDeathmatchC);
					printCenterText(UI_CX, 5+88, 32, gameVar.lang_teamDeathmatchD);
					break;
				case GAME_TYPE_CTF:
					printCenterText(UI_CX, 5, 64, gameVar.lang_captureTheFlagC);
					printCenterText(UI_CX, 5+88, 32, gameVar.lang_captureTheFlagD);
					break;
				case GAME_TYPE_SND:
#ifdef _PRO_
					printCenterText(UI_CX, 5, 64, gameVar.lang_championC);
					printCenterText(UI_CX, 5+88, 32, gameVar.lang_championD);
#else
					printCenterText(UI_CX, 5, 64, gameVar.lang_counterBaboristC);
					printCenterText(UI_CX, 5+88, 32, gameVar.lang_counterBaboristD);
#endif
					break;
				}
				CString mapInfo (game->map->mapName);
				if(game->map->author_name.len() > 0)
					mapInfo.set("%s created by %s", game->map->mapName.s, game->map->author_name.s);
				printCenterText(UI_CX, 5+64, 32, mapInfo);
			glPopAttrib();
		dkglPopOrtho();
#endif
	}
	else
	{
		if ((menuManager.root) && (menuManager.root->visible))
		{
			dkwClipMouse( false );
		}
		else
		{
			dkwClipMouse( true );
		}
	}

	// Finalement, par dessus tout, le crosshair
//	CVector2i cursor = dkwGetCursorPos_main();
//	int xM = (int)(((float)cursor[0]/(float)res[0])* (float)UI_W);
//	int yM = (int)(((float)cursor[1]/(float)res[1])* (float)UI_H);
#ifndef _DX_
	dkglPushOrtho(UI_W, UI_H);
		glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor4f(1,.2f,.2f, hitIndicator);
			renderTexturedQuad(xM-16,yM-16,32,32,tex_crossHit);			
		glPopAttrib();
	dkglPopOrtho();
#endif

	// On se connecte
	if (!isConnected)
	{
#ifndef _DX_
		dkglPushOrtho(UI_W, UI_H);
			glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
				glEnable(GL_BLEND);
				glColor3f(1,1,1);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				if (blink > .25f) printCenterText(UI_CX, 300-32, 64, gameVar.lang_connectingC);
				printCenterText(UI_CX, 332, 48, gameVar.lang_pressF10ToCancel);
				if (dkiGetState(DIK_F10) == DKI_DOWN) console->sendCommand("disconnect");
			glPopAttrib();
		dkglPopOrtho();
#endif
	}
}
#endif

