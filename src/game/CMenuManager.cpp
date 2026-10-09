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

#include "CControl.h"
#include "ui.h"
#include "CMenuManager.h"
#include "UITheme.h"
#include "Scene.h"
extern Scene* scene;
extern unsigned int uiPageArt;
#include "Helper.h"
#include "Scene.h"

CMenuManager menuManager;
extern Scene *scene;


CMenuManager::CMenuManager()
{
	activeControl = 0;
	focusControl = 0;
	hoveringControl = 0;
	root = 0;
	removeTop = 0;
}


CMenuManager::~CMenuManager()
{
	ZEVEN_SAFE_DELETE(root);
}



void CMenuManager::update(float delay, CControl * toUpdate)
{
	hoveringControl = 0;
	CVector2i res = dkwGetResolution();
	RECT rect;
	GetClientRect(dkwGetHandle(), &rect);
	res[0] = rect.right - rect.left;
	res[1] = rect.bottom - rect.top;
	mousePos = dkwGetCursorPos_main();

	//--- On criss la mouse pos sur 800x600
	mousePos[0] = (int)(((float)mousePos[0] / (float)res[0]) * (float)UI_W);
	mousePos[1] = (int)(((float)mousePos[1] / (float)res[1]) * (float)UI_H);

//	if (dialogs.empty() == false)
//		return;

	if (toUpdate)
	{
		toUpdate->update(delay);
	}
	else if (root != NULL && dialogs.empty()) 
	{
		root->update(delay);
	}
}

void CMenuManager::updateDialogs(float delay)
{
	if (dialogs.empty())
		return;
	hoveringControl = 0;
	CVector2i res = dkwGetResolution();
	RECT rect;
	GetClientRect(dkwGetHandle(), &rect);
	res[0] = rect.right - rect.left;
	res[1] = rect.bottom - rect.top;
	mousePos = dkwGetCursorPos_main();

	//--- On criss la mouse pos sur 800x600
	mousePos[0] = (int)(((float)mousePos[0] / (float)res[0]) * (float)UI_W);
	mousePos[1] = (int)(((float)mousePos[1] / (float)res[1]) * (float)UI_H);

	/*while (dialogs.empty() == false)
	{
		if (dialogs.top().done || dialogs.top().canceled)
			dialogs.pop();
		else
			break;
	}*/

//	int startSize = dialogs.c.size();// [dsl] Changed that to vector.. Wasn't used as a stack wasn't compiling on 2008...
	int startSize = dialogs.size();
	for (int i = startSize - 1; i >= 0; )
	{
	//	dialogs.c.at(i)->update(delay); // [dsl] Changed that to vector.. Wasn't used as a stack wasn't compiling on 2008...
		dialogs[i]->update(delay);	

	/*	if (startSize != dialogs.c.size()) // [dsl] Changed that to vector.. Wasn't used as a stack wasn't compiling on 2008...
		{
			startSize = dialogs.c.size();
			i = startSize - 1;
		}*/
		if (startSize != dialogs.size())
		{
			startSize = dialogs.size();
			i = startSize - 1;
		}
		else
			i--;
	}
/*	if (removeTop == 1) // [dsl] Changed that to vector.. Wasn't used as a stack wasn't compiling on 2008...
	{
		dialogs.pop();
	}
	else if (removeTop == 2)
	{
		delete dialogs.top();
		dialogs.pop();
	}*/
	if (removeTop == 1)
	{
		dialogs.pop_back();
	}
	else if (removeTop == 2)
	{
		delete dialogs.back();
		dialogs.pop_back();
	}
	removeTop = 0;
}

extern bool enableShadow;

void CMenuManager::render(CControl * toRender)
{
#ifndef _DX_
	glClear(GL_DEPTH_BUFFER_BIT);
#endif
	CVector2i res = dkwGetResolution();
	if(gameVar.r_widescreen > 1) res[0] = static_cast<int>(res[1]*1.333f);

#ifndef _DX_
	dkglPushOrtho(UI_W, UI_H);
		glPushAttrib(GL_ENABLE_BIT);
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			// openbv: the main menu sits on the game's menu art (it was black), over a game in progress
			// on the game itself; a vignette either way so the flat panels read
			if (toRender)
			{
				ui::vignette((float)UI_W, (float)UI_H);   // the in-game menus, over the game
			}
			else if (root)
			{
				if (!scene || (!scene->client && !scene->editor))
				{
					// the open page's own art (found while drawing the previous frame), else the default
					static unsigned int fallback = dktCreateTextureFromFile("main/textures/Menu2Back.tga", DKT_FILTER_LINEAR);
					unsigned int backdrop = uiPageArt ? uiPageArt : fallback;
					CVector2i tsize = dktGetTextureSize(backdrop);
					float aspect = (tsize[1] > 0) ? (float)tsize[0] / (float)tsize[1] : 4.0f / 3.0f;
					// cover the screen, keeping the picture's shape
					float bw = (float)UI_W, bh = bw / aspect;
					if (bh < UI_H) { bh = (float)UI_H; bw = bh * aspect; }
					glColor4f(.55f, .62f, .75f, 1);
					renderTexturedQuadSmooth((int)((UI_W - bw) / 2), (int)((UI_H - bh) / 2), (int)bw, (int)bh, backdrop);
				}
				ui::vignette((float)UI_W, (float)UI_H);
			}
			if (!toRender) uiPageArt = 0;
			if (toRender) toRender->render();
			else if (root) root->render();
		glPopAttrib();
	dkglPopOrtho();
#endif

	//--- Head games logo bottom right // Temporarly disabled until 2.07
	/*dkglPushOrtho(UI_W, UI_H);
		glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glColor3f(1,1,1);
			glBindTexture(GL_TEXTURE_2D, scene->tex_miniHeadGames);
			glEnable(GL_TEXTURE_2D);
			glBegin(GL_QUADS);
				glTexCoord2i(0,1);
				glVertex2i(UI_W-128,600-128);
				glTexCoord2i(0,0);
				glVertex2i(UI_W-128,600-64);
				glTexCoord2i(1,0);
				glVertex2i(UI_W-64,600-64);
				glTexCoord2i(1,1);
				glVertex2i(UI_W-64,600-128);
			glEnd();
		glPopAttrib();
	dkglPopOrtho();*/

	renderTooltip(hoveringControl, res);
}

void CMenuManager::renderDialogs()
{
	if (dialogs.empty() == false)
	{
#ifndef _DX_
		glClear(GL_DEPTH_BUFFER_BIT);
#endif
		CVector2i res = dkwGetResolution();
		if(gameVar.r_widescreen > 1) res[0] = static_cast<int>(res[1]*1.333f);

#ifndef _DX_
		dkglPushOrtho(UI_W, UI_H);
			glPushAttrib(GL_ENABLE_BIT);
				glDisable(GL_DEPTH_TEST);
				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
				//for (int i = dialogs.c.size() - 1; i >= 0; i--)
				//	dialogs.c.at(i)->render();
			//	dialogs.top()->render(); // [dsl] Changed that to vector.. Wasn't used as a stack wasn't compiling on 2008...
				dialogs.back()->render();
			glPopAttrib();
		dkglPopOrtho();
#endif

		renderTooltip(hoveringControl, res);
	}
}

void CMenuManager::renderTooltip(CControl * control, const CVector2i& res)
{
#ifndef _DX_
	//--- We render the tooltips text if the mouse if over that control
	// openbv: a themed card in UI units (it was drawn in screen pixels, tiny at high resolutions)
	dkglPushOrtho(UI_W, UI_H);
		glPushAttrib(GL_ENABLE_BIT | GL_CURRENT_BIT);
			glDisable(GL_DEPTH_TEST);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			if (control)
			{
				if (!control->toolTips.isNull())
				{
					const float ts = 16;
					int w = (int)dkfGetStringWidth(ts, control->toolTips.s) + 20;
					int h = (int)dkfGetStringHeight(ts, control->toolTips.s) + 14;

					CVector2i realMousePos = dkwGetCursorPos_main();

					int x = (int)((float)realMousePos[0] / (float)res[0] * UI_W) + 16;
					int y = (int)((float)realMousePos[1] / (float)res[1] * UI_H) + 18;

					if (x + w > UI_W - 4) x = UI_W - 4 - w;
					if (y + h > UI_H - 4) y = UI_H - 4 - h;

					ui::rect((float)x, (float)y, (float)w, (float)h, ui::withAlpha(ui::panel, .96f), ui::fieldLine, 5);

					glColor3f(ui::text.r, ui::text.g, ui::text.b);
					printLeftText((float)x+10,(float)y+7,ts,control->toolTips);
				}
			}
		glPopAttrib();
	dkglPopOrtho();
#endif
}

void CMenuManager::showDialog(IDialog* dialog)
{
//	dialogs.push(dialog); // [dsl] Changed that to vector.. Wasn't used as a stack wasn't compiling on 2008...
	dialogs.push_back(dialog);
}

void CMenuManager::hideDialog(bool autoMemFree/*IDialog* dialog*/)
{
	//dialogs.pop();
	if (!autoMemFree)
		removeTop = 1;
	else
		removeTop = 2;
}

#endif

