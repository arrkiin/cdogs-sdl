/*
    C-Dogs SDL
    A port of the legendary (and fun) action/arcade cdogs.
    Copyright (C) 1995 Ronny Wester
    Copyright (C) 2003 Jeremy Chin
    Copyright (C) 2003-2007 Lucas Martin-King

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    This file incorporates work covered by the following copyright and
    permission notice:

    Copyright (c) 2013-2017 Cong Xu
    All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    Redistributions of source code must retain the above copyright notice, this
    list of conditions and the following disclaimer.
    Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/
#include "screens.h"

#include <time.h>

#include <cdogs/ai.h>
#include <cdogs/gamedata.h>
#include <cdogs/game_events.h>
#include <cdogs/handle_game_events.h>
#include <cdogs/hiscores.h>
#include <cdogs/net_client.h>
#include <cdogs/net_server.h>

#include "briefing_screens.h"
#include "game.h"
#include "prep.h"
#include "screens_end.h"


void ScreenStart(GraphicsDevice *graphics, CampaignOptions *co)
{
	GameLoopData g = ScreenCampaignIntro(&co->Setting);
	GameLoop(&g);
	GameLoopTerminate(&g);
	if (!co->IsLoaded)
	{
		return;
	}

	bool run = false;
	bool gameOver = true;
	do
	{
		g = GameOptions(co->Entry.Mode);
		GameLoop(&g);
		GameLoopTerminate(&g);
		if (!co->IsLoaded)
		{
			run = false;
			goto bail;
		}

		// Mission briefing
		g = ScreenMissionBriefing(&gMission);
		GameLoop(&g);
		GameLoopTerminate(&g);
		if (!co->IsLoaded)
		{
			run = false;
			goto bail;
		}

		// Equip guns
		g = PlayerEquip();
		GameLoop(&g);
		GameLoopTerminate(&g);
		if (!co->IsLoaded)
		{
			run = false;
			goto bail;
		}

		g = ScreenWaitForGameStart();
		GameLoop(&g);
		GameLoopTerminate(&g);
		if (!co->IsLoaded)
		{
			run = false;
			goto bail;
		}

		g = RunGame(co, &gMission, &gMap);
		GameLoop(&g);
		GameLoopTerminate(&g);
		run = !gMission.IsQuit;

		const int survivingPlayers =
			GetNumPlayers(PLAYER_ALIVE, false, false);
		const bool survivedAndCompletedObjectives =
			survivingPlayers > 0 && MissionAllObjectivesComplete(&gMission);
		if (run && GetNumPlayers(PLAYER_ANY, false, true) > 0)
		{
			switch (co->Entry.Mode)
			{
			case GAME_MODE_DOGFIGHT:
				{
					g = ScreenDogfightScores();
					GameLoop(&g);
					GameLoopTerminate(&g);
					// Calculate PVP rounds won
					int maxScore = 0;
					CA_FOREACH(PlayerData, p, gPlayerDatas)
						if (IsPlayerAlive(p))
						{
							p->Totals.Score++;
							maxScore = MAX(maxScore, p->Totals.Score);
						}
					CA_FOREACH_END()
					gameOver = maxScore == ModeMaxRoundsWon(co->Entry.Mode);
					CASSERT(maxScore <= ModeMaxRoundsWon(co->Entry.Mode),
						"score exceeds max rounds won");
					if (gameOver)
					{
						g = ScreenDogfightFinalScores();
						GameLoop(&g);
						GameLoopTerminate(&g);
					}
				}
				break;
			case GAME_MODE_DEATHMATCH:
				g = ScreenDeathmatchFinalScores();
				GameLoop(&g);
				GameLoopTerminate(&g);
				break;
			default:
				// In co-op (non-PVP) modes, at least one player must survive
				gameOver = !survivedAndCompletedObjectives ||
					co->MissionIndex == (int)co->Setting.Missions.size - 1;
				g = ScreenMissionSummary(
					co, &gMission, survivedAndCompletedObjectives);
				GameLoop(&g);
				GameLoopTerminate(&g);
				run = !gMission.IsQuit;
				// Note: must use cached value because players get cleaned up
				// in CleanupMission()
				if (gameOver && survivedAndCompletedObjectives)
				{
					g = ScreenVictory(co);
					GameLoop(&g);
					GameLoopTerminate(&g);
				}
				break;
			}
		}

		// Check if any scores exceeded high scores, if we're not a PVP mode
		if (!IsPVP(co->Entry.Mode) &&
			GetNumPlayers(PLAYER_ANY, false, true) > 0)
		{
			bool allTime = false;
			bool todays = false;
			CA_FOREACH(PlayerData, p, gPlayerDatas)
				if (((run && !p->survived) || gameOver) && p->IsLocal)
				{
					EnterHighScore(p);
					allTime |= p->allTime >= 0;
					todays |= p->today >= 0;
				}

				if (!p->survived)
				{
					// Reset scores because we died :(
					memset(&p->Totals, 0, sizeof p->Totals);
					p->missions = 0;
				}
				else
				{
					p->missions++;
				}
				p->lastMission = co->MissionIndex;
			CA_FOREACH_END()
			if (allTime)
			{
				g = DisplayAllTimeHighScores(graphics);
				GameLoop(&g);
				GameLoopTerminate(&g);
			}
			if (todays)
			{
				g = DisplayTodaysHighScores(graphics);
				GameLoop(&g);
				GameLoopTerminate(&g);
			}
		}
		if (!HasRounds(co->Entry.Mode) && !gameOver)
		{
			co->MissionIndex++;
		}

	bail:
		// Need to terminate the mission later as it is used in calculating scores
		MissionOptionsTerminate(&gMission);
	} while (run && !gameOver);

	CampaignUnload(&gCampaign);
}
