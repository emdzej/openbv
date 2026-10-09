package game

import (
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/wire"
)

// The team modes' per-frame rules (milestone 4): auto-balance (TDM, CTF), CTF's flags and the
// "Champion" round reset. In the original they run inside Server::update's `roundState ==
// GAME_PLAYING` block, after the coord-frame broadcast (Server.cpp:1228; design/server.md §1.3,
// steps 17 and 18).

// updateTeamModes is that part of Server::update (Server.cpp:1228-1330).
func (s *Server) updateTeamModes() {
	// the auto-balance, "au 2mins" (Server.cpp:1230)
	if (s.gameType == proto.GameTypeTDM || s.gameType == proto.GameTypeCTF) && s.SV.AutoBalance.B {
		if s.autoBalanceTimer == 0 {
			reds, blues := s.teamLists()
			if len(reds) < len(blues)-1 || len(blues) < len(reds)-1 {
				s.autoBalanceTimer = float32(s.SV.AutoBalanceTime.I)
				s.sendEmpty(0, proto.SvclAutobalance) // bb_serverSend(0, 0, NET_SVCL_AUTOBALANCE, 0)
			}
		} else if s.autoBalanceTimer > 0 {
			s.autoBalanceTimer -= delay
			// `< 0`, not `<= 0`: a timer landing exactly on 0 is picked up again by the `== 0` branch
			// next frame (a recount, no balance), as in the original
			if s.autoBalanceTimer < 0 {
				s.autoBalanceTimer = 0
				s.autoBalance()
			}
		}
	} else {
		s.autoBalanceTimer = 0
	}

	if s.m == nil {
		return
	}
	switch s.gameType {
	case proto.GameTypeCTF:
		s.updateCTF()
	case proto.GameTypeSND:
		s.updateChampion()
	}
}

// teamLists is the reds and blues in slot order, as the original's two vectors.
func (s *Server) teamLists() (reds, blues []*Player) {
	for _, p := range s.players {
		if p == nil {
			continue
		}
		switch p.TeamID {
		case proto.TeamRed:
			reds = append(reds, p)
		case proto.TeamBlue:
			blues = append(blues, p)
		}
	}
	return
}

// autoBalance is Server::autoBalance (Server.cpp:1418).
//
// Quirk kept: the two lists are made once, so when more than one player has to move, every pass
// picks from the same, unchanged list: the same player again (whose team is already the new one;
// assignPlayerTeam changes nothing) and the TEAM_REQUEST is sent again. With one to move — teams
// differing by two, the usual case — it makes no difference.
func (s *Server) autoBalance() {
	reds, blues := s.teamLists()
	move := func(from []*Player, carried int8, to int) {
		switchID := len(from) - 1
		if s.flagState[carried] == int8(from[switchID].ID) {
			switchID--
		}
		for i := switchID - 1; i >= 0; i-- {
			if from[switchID].TimePlayedCurGame > from[i].TimePlayedCurGame && s.flagState[carried] != int8(from[i].ID) {
				switchID = i
			}
		}
		p := from[switchID]
		p.TeamID = s.assignPlayerTeam(p.ID, to)
		s.broadcast(proto.ClsvSvclTeamRequest, &proto.TeamRequestMsg{PlayerID: int8(p.ID), TeamRequested: int8(to)})
	}
	switch {
	case len(reds) < len(blues)-1:
		// blues carry the red flag (flagState[1]); a carrier isn't moved
		for n := len(blues) - 1 - len(reds); n > 0; n-- {
			move(blues, 1, proto.TeamRed)
		}
	case len(blues) < len(reds)-1:
		for n := len(reds) - 1 - len(blues); n > 0; n-- {
			move(reds, 0, proto.TeamBlue)
		}
	}
}

// updateCTF is Server::updateCTF (ServerCTF.cpp:24, design/server.md §5.14): per flag, at most one
// event per frame (each loop breaks after it). Distances are 2D: the player's (x, y, 0) against the
// pod's or the dropped flag's 3D position.
func (s *Server) updateCTF() {
	near := func(p *Player, at bvmath.Vec3, r float32) bool {
		pos := bvmath.Vec3{p.CurrentCF.Position[0], p.CurrentCF.Position[1], 0}
		return bvmath.DistanceSquared(at, pos) <= r*r
	}
	alive := func(p *Player, team int) bool {
		return p != nil && p.TeamID == team && p.Status == proto.StatusAlive
	}
	// flag f, now held by the player i or back home (-2): the message to everyone
	changed := func(f int, i int) {
		s.broadcast(proto.SvclChangeFlagState, &proto.SvclChangeFlagStateMsg{FlagID: int8(f), NewFlagState: s.flagState[f], PlayerID: int8(i)})
	}
	take := func(f int, i int, p *Player) {
		s.flagState[f] = int8(i)
		changed(f, i)
		s.log.Info("took the flag", "flag", flagNames[f], "player", i, "name", p.Name)
		p.FlagAttempts++
	}

	// the blue flag (0): reds take it, blues return it; a blue with the red flag scores at its pod
	switch s.flagState[0] {
	case -2:
		for i, p := range s.players {
			if alive(p, proto.TeamRed) && near(p, s.m.FlagPodPos[0], .25) {
				take(0, i, p)
				break
			}
			if alive(p, proto.TeamBlue) && s.flagState[1] == int8(i) && near(p, s.m.FlagPodPos[0], .25) {
				s.flagState[1] = -2 // on a scoré
				changed(1, i)
				s.log.Info("scores for the Blue team", "player", i, "name", p.Name)
				p.Score++
				s.blueWin++
				s.blueScore = s.blueWin
				break
			}
		}
	case -1:
		for i, p := range s.players {
			if alive(p, proto.TeamRed) && near(p, s.flagPos[0], .5) {
				take(0, i, p)
				break
			}
			if alive(p, proto.TeamBlue) && near(p, s.flagPos[0], .5) {
				s.flagState[0] = -2
				changed(0, i)
				s.log.Info("returned the blue flag", "player", i, "name", p.Name)
				p.Returns++
				break
			}
		}
	}

	// the red flag (1): the same with the teams swapped, and 0.25 on the ground too
	switch s.flagState[1] {
	case -2:
		for i, p := range s.players {
			if alive(p, proto.TeamBlue) && near(p, s.m.FlagPodPos[1], .25) {
				take(1, i, p)
				break
			}
			if alive(p, proto.TeamRed) && s.flagState[0] == int8(i) && near(p, s.m.FlagPodPos[1], .25) {
				s.flagState[0] = -2
				changed(0, i)
				s.log.Info("scores for the Red team", "player", i, "name", p.Name)
				p.Score++
				s.redWin++
				s.redScore = s.redWin
				break
			}
		}
	case -1:
		for i, p := range s.players {
			if alive(p, proto.TeamBlue) && near(p, s.flagPos[1], .25) {
				take(1, i, p)
				break
			}
			if alive(p, proto.TeamRed) && near(p, s.flagPos[1], .25) {
				s.flagState[1] = -2
				changed(1, i)
				s.log.Info("returned the red flag", "player", i, "name", p.Name) // the original logs "blue"
				p.Returns++
				break
			}
		}
	}
}

var flagNames = [2]string{"blue", "red"}

// updateChampion is type 3's per-frame part in the Pro build (Server.cpp:1288, §5.15): when the round
// time runs out, a new round: spawn slots are cleared and every team player is told he died (a
// PLAYER_HIT of -100 from himself). The server's own players stay alive; the clients kill theirs.
func (s *Server) updateChampion() {
	if s.roundTimeLeft != 0 {
		return
	}
	s.roundTimeLeft = s.SV.RoundTimeLimit.F
	for _, p := range s.players {
		if p != nil {
			p.SpawnSlot = -1
		}
	}
	for _, p := range s.players {
		if p != nil && (p.TeamID == proto.TeamBlue || p.TeamID == proto.TeamRed) {
			s.broadcast(proto.SvclPlayerHit, &proto.SvclPlayerHitMsg{PlayerID: int8(p.ID), FromID: int8(p.ID), Damage: -100, Vel: [3]int8{0, 0, 1}})
		}
	}
}

// sendEmpty sends a message without a payload.
func (s *Server) sendEmpty(dest int32, typ uint16) {
	if s.sent != nil {
		s.sent(dest, typ, struct{}{})
	}
	s.net.Send(dest, typ, nil, wire.TCP)
}
