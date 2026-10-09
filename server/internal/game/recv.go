package game

import (
	"strconv"
	"strings"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/cvar"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/wire"
)

// player returns the player a message names, or nil: out-of-range IDs (the original indexed its
// array with them) and, with BindPlayerID, IDs that aren't the sender's own (design/server.md §1.4,
// §8.4) are refused.
func (s *Server) player(id int8, netID uint32) *Player {
	if id < 0 || int(id) >= proto.MaxPlayer {
		return nil
	}
	p := s.players[id]
	if p == nil {
		return nil
	}
	if s.Opts.BindPlayerID && p.BabonetID != netID {
		return nil
	}
	return p
}

// recvPacket is Server::recvPacket (ServerRecv.cpp:41).
func (s *Server) recvPacket(netID uint32, pk wire.Packet) {
	d := pk.Data
	switch pk.Type {
	case proto.ClsvMapRequest:
		var m proto.ClsvMapRequestMsg
		proto.Decode(d, &m)
		// ServerRecv.cpp:45: the name is the client's; only maps from the map folder are served
		s.transfers = append(s.transfers, mapTransfer{netID: netID, name: proto.CString(m.MapName[:])})

	case proto.ClsvGameVersionAccepted:
		var m proto.ClsvGameVersionAcceptedMsg
		proto.Decode(d, &m)
		if p := s.player(m.PlayerID, netID); p != nil {
			s.gameVersionAccepted(p, proto.CString(m.Password[:]))
		}

	case proto.ClsvSvclPlayerInfo:
		var m proto.PlayerInfoMsg
		proto.Decode(d, &m)
		p := s.player(m.PlayerID, netID)
		if p == nil {
			return
		}
		// ServerRecv.cpp:439
		m.PlayerName[31] = 0
		p.Name = proto.CString(m.PlayerName[:])
		proto.SetCString(m.PlayerIP[:], p.IP)
		s.broadcast(proto.ClsvSvclPlayerInfo, &m)
		s.log.Info("player joined the game", "name", p.Name, "player", p.ID)
		// the Pro checksum query (ChecksumQuery.h): four shorts rand()%60000+10, kept in a short
		hs := proto.HashSeedMsg{
			S1: checksumSeed(s.rand), S2: checksumSeed(s.rand), S3: checksumSeed(s.rand), S4: checksumSeed(s.rand),
		}
		s.send(int32(netID), proto.SvclHashSeed, &hs)
		s.checksums = append(s.checksums, checksumQuery{playerID: p.ID, netID: netID})
		// the account-server request and the master's cache-ban query: no such services

	case proto.SvclPlaySound:
		// relayed to every other player as received (sizeof bytes)
		var m proto.SvclPlaySoundMsg
		proto.Decode(d, &m)
		for _, o := range s.players {
			if o != nil && o.BabonetID != netID {
				s.send(int32(o.BabonetID), proto.SvclPlaySound, &m)
			}
		}

	case proto.ClsvSvclChat:
		var m proto.ChatMsg
		proto.Decode(d, &m)
		// ServerRecv.cpp:552: nobody's ID is checked; the team decides who gets it
		m.Message[129] = 0
		s.log.Info("chat", "team", m.TeamID, "text", proto.CString(m.Message[:]))
		switch m.TeamID {
		case -2:
			s.broadcast(proto.ClsvSvclChat, &m)
		case proto.TeamSpectator, proto.TeamBlue, proto.TeamRed:
			for _, o := range s.players {
				if o != nil && o.TeamID == int(m.TeamID) {
					s.send(int32(o.BabonetID), proto.ClsvSvclChat, &m)
				}
			}
		}

	case proto.ClsvSvclTeamRequest:
		var m proto.TeamRequestMsg
		proto.Decode(d, &m)
		p := s.player(m.PlayerID, netID)
		if p == nil {
			return
		}
		old := p.TeamID
		team := s.assignPlayerTeam(p.ID, int(m.TeamRequested))
		if team != old {
			if (old == proto.TeamRed || old == proto.TeamBlue) && p.TimePlayedCurGame > epsilon {
				p.reinit()
			}
			m.TeamRequested = int8(team)
			s.broadcast(proto.ClsvSvclTeamRequest, &m)
		}

	case proto.ClsvPong:
		var m proto.ClsvPongMsg
		proto.Decode(d, &m)
		if p := s.player(m.PlayerID, netID); p != nil && p.WaitForPong {
			p.WaitForPong = false
			p.Ping = p.CurrentPingFrame
		}

	case proto.ClsvSpawnRequest:
		if s.roundState != proto.GamePlaying {
			return
		}
		var m proto.ClsvSpawnRequestMsg
		proto.Decode(d, &m)
		p := s.player(m.PlayerID, netID)
		if p == nil {
			return
		}
		s.spawnRequest(p, &m)

	case proto.ClsvSvclPlayerCoordFrame:
		var m proto.PlayerCoordFrameMsg
		proto.Decode(d, &m)
		p := s.player(m.PlayerID, netID)
		if p == nil {
			return
		}
		s.coordFrame(p, &m)

	case proto.ClsvSvclPlayerChangeName:
		var m proto.PlayerChangeNameMsg
		proto.Decode(d, &m)
		p := s.player(m.PlayerID, netID)
		if p == nil {
			return
		}
		m.PlayerName[31] = 0
		s.log.Info("name change", "from", p.Name, "to", proto.CString(m.PlayerName[:]))
		p.Name = proto.CString(m.PlayerName[:])
		for i, o := range s.players {
			if o != nil && i != p.ID {
				s.send(int32(o.BabonetID), proto.ClsvSvclPlayerChangeName, &m)
			}
		}

	case proto.ClsvMapListRequest:
		var m proto.ClsvMapListRequestMsg
		proto.Decode(d, &m)
		p := s.player(m.PlayerID, netID)
		if p == nil {
			return
		}
		maps := s.mapList
		if m.All != 0 {
			maps = s.lib.List()
		}
		for _, name := range maps {
			var ml proto.SvclMapListMsg
			copy(ml.MapName[:], name) // strncpy of up to 16 characters, zero-filled
			s.send(int32(p.BabonetID), proto.SvclMapList, &ml)
		}

	case proto.SvclHashSeedReply:
		// any reply is good (isValid is true in the non-Windows builds, and openbv has no bv2.exe)
		for i, q := range s.checksums {
			if q.netID == netID {
				if p := s.players[q.playerID]; p != nil {
					s.log.Info("player was successfully authenticated", "name", p.Name)
				}
				s.checksums = append(s.checksums[:i], s.checksums[i+1:]...)
				break
			}
		}

	case proto.ClsvSvclPlayerUpdateSkin:
		var m proto.PlayerUpdateSkinMsg
		proto.Decode(d, &m)
		if s.Opts.BindPlayerID && s.player(m.PlayerID, netID) == nil {
			return
		}
		// ServerRecv.cpp:1213: sent to everyone once per connected player (quirk §8.3.7); not stored
		for _, o := range s.players {
			if o != nil {
				s.broadcast(proto.ClsvSvclPlayerUpdateSkin, &m)
			}
		}

	case proto.ClsvPlayerShoot:
		var m proto.ClsvPlayerShootMsg
		proto.Decode(d, &m)
		if p := s.player(m.PlayerID, netID); p != nil {
			s.playerShoot(p, &m)
		}

	case proto.ClsvPickupRequest:
		var m proto.ClsvPickupRequestMsg
		proto.Decode(d, &m)
		if p := s.player(m.PlayerID, netID); p != nil {
			s.pickupRequest(p)
		}

	case proto.ClsvSvclPlayerProjectile:
		var m proto.PlayerProjectileMsg
		proto.Decode(d, &m)
		if p := s.player(m.PlayerID, netID); p != nil {
			s.playerProjectile(p, &m)
		}

	case proto.ClsvSvclPlayerShootMelee:
		var m proto.PlayerShootMeleeMsg
		proto.Decode(d, &m)
		// ServerRecv.cpp:139: no rate limit (§5.8)
		if p := s.player(m.PlayerID, netID); p != nil && p.Status == proto.StatusAlive && p.Melee != nil {
			s.shootMeleeSV(p)
			s.broadcast(proto.ClsvSvclPlayerShootMelee, &m)
		}

	case proto.ClsvVote, proto.ClsvSvclVoteRequest, proto.ClsvAdminRequest, proto.SvclConsole:
		// votes and admin: the next milestones

	default:
		s.log.Debug("unknown message", "type", pk.Type, "netId", netID)
	}
}

func checksumSeed(r *bvmath.Rand) int16 {
	return int16(int32(int16(r.Int()%60000)) + 10)
}

// gameVersionAccepted is NET_CLSV_GAMEVERSION_ACCEPTED (ServerRecv.cpp:305): the password, then the
// state dump.
func (s *Server) gameVersionAccepted(p *Player, password string) {
	if s.SV.Password.S != "" && !strings.EqualFold(password, s.SV.Password.S) {
		s.net.Disconnect(p.BabonetID)
		return
	}
	dest := int32(p.BabonetID)
	var info proto.SvclServerInfoMsg
	proto.SetCString(info.MapName[:], s.mapName)
	info.BlueScore, info.RedScore = int16(s.blueScore), int16(s.redScore)
	info.BlueWin, info.RedWin = int16(s.blueWin), int16(s.redWin)
	info.GameType = int8(s.gameType)
	s.send(dest, proto.SvclServerInfo, &info)
	s.send(dest, proto.SvclGameState, &proto.SvclRoundStateMsg{NewState: int8(s.roundState)})
	for _, name := range cvar.SentToClients {
		var sc proto.SvclSvChangeMsg
		proto.SetCString(sc.SvChange[:], s.SV.ChangeText(name))
		s.send(dest, proto.SvclSvChange, &sc)
	}
	for i, o := range s.players {
		if o == nil || i == p.ID {
			continue
		}
		var st proto.SvclPlayerEnumStateMsg
		st.PlayerID = int8(i)
		proto.SetCString(st.PlayerName[:], o.Name)
		proto.SetCString(st.PlayerIP[:], o.IP)
		st.Kills, st.Deaths, st.Score = int16(o.Kills), int16(o.Deaths), int16(o.Score)
		st.Returns, st.FlagAttempts, st.Damage = int16(o.Returns), int16(o.FlagAttempts), int16(o.Damage)
		st.Status, st.TeamID = int8(o.Status), int8(o.TeamID)
		st.Life, st.Dmg = o.Life, o.Dmg
		st.BaboNetID = int32(o.BabonetID)
		skin := o.Skin
		if len(skin) > 6 {
			skin = skin[:6]
		}
		copy(st.Skin[:], skin) // the original copied len+1 (or 7) bytes; the rest was garbage, here zeros
		for k := 0; k < 3; k++ {
			st.BlueDecal[k] = uint8(o.BlueDecal[k] * 255)
			st.GreenDecal[k] = uint8(o.GreenDecal[k] * 255)
			st.RedDecal[k] = uint8(o.RedDecal[k] * 255)
		}
		st.WeaponID = proto.WeaponSMG
		if o.Status == proto.StatusAlive && o.Weapon != nil {
			st.WeaponID = int8(o.Weapon.ID)
		}
		s.send(dest, proto.SvclPlayerEnumState, &st)
	}
	s.projectileEnum(dest)
	if s.gameType == proto.GameTypeCTF {
		s.send(dest, proto.SvclFlagEnum, &proto.SvclFlagEnumMsg{
			FlagState: s.flagState, PositionBlue: s.flagPos[0], PositionRed: s.flagPos[1],
		})
	}
}

// spawnRequest is NET_CLSV_SPAWN_REQUEST (ServerRecv.cpp:667).
func (s *Server) spawnRequest(p *Player, m *proto.ClsvSpawnRequestMsg) {
	if s.SV.ValidateWeapons.B {
		if m.WeaponID < proto.WeaponSMG || m.WeaponID > proto.WeaponFlameThrower {
			s.Command("sayall " + p.Name + " is trying to hack his primary weapon! Kicked him")
			s.kick(p.ID)
			return
		}
		if !(m.MeleeID == proto.WeaponKnives || m.MeleeID == proto.WeaponNuclear || m.MeleeID == proto.WeaponShield || m.MeleeID == proto.WeaponMinibot) {
			s.Command("sayall " + p.Name + " is trying to hack his secondary weapon! Kicked him")
			s.kick(p.ID)
			return
		}
	} else if m.WeaponID < 0 || m.WeaponID > proto.WeaponMinibot || m.MeleeID < 0 || m.MeleeID > proto.WeaponMinibot {
		return // out of the weapon table: the original indexed past it (§8.4)
	}
	if !s.SV.EnableMinibot.B && m.MeleeID == proto.WeaponMinibot {
		m.MeleeID = proto.WeaponKnives
	}
	p.NextSpawnWeapon = int(m.WeaponID)
	p.NextMeleeWeapon = int(m.MeleeID)
	if !s.spawnPlayer(p.ID) {
		return
	}
	sp := proto.SvclPlayerSpawnMsg{
		PlayerID: m.PlayerID, WeaponID: m.WeaponID, MeleeID: m.MeleeID,
		Skin: m.Skin, RedDecal: m.RedDecal, GreenDecal: m.GreenDecal, BlueDecal: m.BlueDecal,
	}
	pos := p.CurrentCF.Position
	sp.Position = [3]int16{int16(pos[0] * 10), int16(pos[1] * 10), int16(pos[2] * 10)} // ×10 (§8.3.6)
	m.Skin[6] = 0
	p.Skin = proto.CString(m.Skin[:])
	for k := 0; k < 3; k++ {
		p.BlueDecal[k] = float32(m.BlueDecal[k]) / 255
		p.GreenDecal[k] = float32(m.GreenDecal[k]) / 255
		p.RedDecal[k] = float32(m.RedDecal[k]) / 255
	}
	s.broadcast(proto.SvclPlayerSpawn, &sp)
	s.log.Info("player spawned", "name", p.Name, "player", p.ID, "weapon", p.NextSpawnWeapon,
		"secondary", p.NextMeleeWeapon, "x", pos[0], "y", pos[1], "team", p.TeamID)
}

// coordFrame is NET_CLSV_SVCL_PLAYER_COORD_FRAME (ServerRecv.cpp:777).
func (s *Server) coordFrame(p *Player, m *proto.PlayerCoordFrameMsg) {
	// (Pro) the camera-zoom kick only runs with sv_beGoodServer false: the combat milestone
	if p.Status != proto.StatusAlive {
		p.SpeedHackCount, p.FrameSinceLast, p.LastFrame, p.CurrentFrame = 0, 0, 0, 0
		return
	}
	if p.BabonetID != uint32(m.BaboNetID) {
		return
	}
	p.TimeIdle = 0
	if p.LastFrame == 0 {
		p.LastFrame = m.FrameID
	}
	p.CurrentFrame = m.FrameID
	if p.FrameSinceLast >= 90 {
		vel := bvmath.Vec3{float32(m.Vel[0]) / 10, float32(m.Vel[1]) / 10, float32(m.Vel[2]) / 10}
		if p.CurrentFrame-p.LastFrame > p.FrameSinceLast+5 || vel.Length() > 3.3 {
			p.SpeedHackCount++
			if p.SpeedHackCount >= 3 {
				s.log.Info("disconnecting a speed hack", "name", p.Name, "ip", p.IP)
				s.kick(p.ID)
				return
			}
		} else {
			p.SpeedHackCount = 0
		}
		p.FrameSinceLast, p.LastFrame, p.CurrentFrame = 0, 0, 0
	}
	p.setCoordFrame(m)
}

// Command runs a server console command (Console::sendCommand, Console.cpp:586): the ones the
// server issues itself at this stage. The admin page will run the rest.
func (s *Server) Command(line string) {
	cmd, rest, _ := strings.Cut(strings.TrimLeft(line, " "), " ")
	switch strings.ToLower(cmd) {
	case "sayall":
		s.sayall(rest)
	case "sayid": // Console.cpp:1602
		idText, text, _ := strings.Cut(rest, " ")
		id := atoi(idText)
		if id < 0 || id >= proto.MaxPlayer || s.players[id] == nil {
			s.log.Warn("sayid: bad player ID", "id", id)
			return
		}
		msg := "\x08Server: " + text
		if len(msg) > 49+80 {
			msg = msg[:49+80]
		}
		var c proto.ChatMsg
		c.TeamID = -3
		proto.SetCString(c.Message[:], msg)
		s.send(int32(s.players[id].BabonetID), proto.ClsvSvclChat, &c)
	case "moveid": // Console.cpp:1031
		f := strings.Fields(rest)
		if len(f) < 2 {
			return
		}
		team, id := atoi(f[0]), atoi(f[1])
		if team < -1 || team > 1 || id < -1 || id >= proto.MaxPlayer || (id >= 0 && s.players[id] == nil) {
			s.log.Warn("moveid: bad team or player", "team", team, "id", id)
			return
		}
		move := func(i int) {
			p := s.players[i]
			// carried flags fall without a DROP_FLAG message (quirk)
			for f := 0; f < 2; f++ {
				if int(s.flagState[f]) == p.ID {
					s.flagState[f] = -1
					s.flagPos[f] = p.CurrentCF.Position
					s.flagPos[f][2] = 0
				}
			}
			p.CurrentCF.Position = bvmath.Vec3{-999, -999, 0}
			s.assignPlayerTeam(i, team)
			s.broadcast(proto.ClsvSvclTeamRequest, &proto.TeamRequestMsg{PlayerID: int8(i), TeamRequested: int8(team)})
		}
		if id == -1 {
			for i := range s.players {
				if s.players[i] != nil {
					move(i)
				}
			}
		} else {
			move(id)
		}
	case "kickid":
		if id := atoi(rest); id >= 0 && id < proto.MaxPlayer {
			s.kick(id)
		}
	case "set":
		name, value, _ := strings.Cut(strings.TrimLeft(rest, " "), " ")
		if v := s.SV.Lookup(name); v != nil {
			if !v.Set(value) {
				s.log.Warn("set: invalid value", "var", name, "value", value)
				return
			}
			if strings.HasPrefix(strings.ToLower(name), "sv_") {
				var sc proto.SvclSvChangeMsg
				proto.SetCString(sc.SvChange[:], s.SV.ChangeText(v.Name))
				s.broadcast(proto.SvclSvChange, &sc)
			}
		}
	default:
		s.log.Warn("unknown command", "command", line)
	}
}

// sayall is Server::sayall (Server.cpp:1554).
func (s *Server) sayall(text string) {
	if text == "" {
		return
	}
	msg := "console : \x08" + text
	if len(msg) > 49+80 {
		msg = msg[:49+80]
	}
	var c proto.ChatMsg
	c.TeamID = proto.TeamSpectator - 1
	proto.SetCString(c.Message[:], msg)
	s.broadcast(proto.ClsvSvclChat, &c)
}

// atoi is CString::toInt in the game (atoi): a leading number, 0 otherwise.
func atoi(s string) int {
	s = strings.TrimLeft(s, " ")
	end := 0
	if end < len(s) && (s[end] == '-' || s[end] == '+') {
		end++
	}
	for end < len(s) && s[end] >= '0' && s[end] <= '9' {
		end++
	}
	n, _ := strconv.Atoi(s[:end])
	return n
}
