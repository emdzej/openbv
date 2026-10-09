package game

import (
	"fmt"
	"sort"
	"strconv"
	"strings"

	"github.com/emdzej/openbv/server/internal/bvmap"
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/cvar"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/store"
)

// Command runs a console command the server issues itself (the ping and idle checks).
func (s *Server) Command(line string) { s.exec(line, "server") }

// Exec runs a console command for the admin page (who: the admin) and returns what it printed.
func (s *Server) Exec(line, who string) []string {
	var out []string
	s.capture = &out
	s.exec(line, who)
	s.capture = nil
	if out == nil {
		out = []string{}
	}
	return out
}

// exec is Console::sendCommand (Console.cpp:586) for a dedicated server: the commands of
// design/server.md §7. Names compare case-insensitively (CString ==), player names without colour
// codes.
func (s *Server) exec(line, who string) {
	line = strings.TrimLeft(line, " ")
	cmd, rest, _ := strings.Cut(line, " ")
	switch strings.ToLower(cmd) {
	case "":
		return
	case "help", "?":
		s.out("help ? info set quit restart dedicate changemap addmap removemap maplist maplistall")
		s.out("voteon novote playerlist playerinfo playersinfo move moveid allwatch kick kickid")
		s.out("ban banid banip banmac unban banlist sayall sayid nuke nukeid nukeall forceplayerspawn")
		s.out("blueTeamScore redTeamScore blueFlagReturn redFlagReturn mapinfos listbluespawns listredspawns")
	case "info": // Console.cpp:747
		s.out(fmt.Sprintf("[Server Info] Game Type: %d - Port: %d - Name: %s", s.SV.GameType.I, s.SV.Port.I, s.SV.GameName.S))
	case "set":
		s.set(rest, who)
	case "dedicate", "changemap": // Console.cpp:902, :1566: a running server changes map
		s.changeMap(strings.TrimSpace(rest))
	case "addmap":
		s.addMapCmd(strings.TrimSpace(rest))
	case "removemap":
		s.removeMapCmd(strings.TrimSpace(rest))
	case "maplist": // Console.cpp:1496
		for _, m := range s.mapList {
			s.out(m)
		}
		if len(s.mapList) == 0 {
			s.out("> No maps on list")
		}
	case "maplistall":
		maps := s.lib.List()
		for _, m := range maps {
			s.out(m)
		}
		if len(maps) == 0 {
			s.out("> No maps on server")
		}
	case "voteon": // Console.cpp:913
		c := strings.ToLower(firstToken(rest))
		if c == "" {
			return
		}
		for _, v := range s.voteList {
			if v == c {
				s.out("> " + c + " can now be voted on")
				return
			}
		}
		s.voteList = append(s.voteList, c)
		s.out("> " + c + " can now be voted on")
		s.changed()
	case "novote":
		s.voteList = nil
		s.out("> All commands are no longer votable")
		s.changed()
	case "playerlist": // Console.cpp:950
		for i, p := range s.players {
			if p != nil {
				s.out(fmt.Sprintf("[%02d] %s - IP:%s", i, p.Name, p.IP))
			}
		}
	case "playerinfo": // Console.cpp:1650
		id := atoi(rest)
		if id < 0 || id >= proto.MaxPlayer || s.players[id] == nil {
			s.out("that player id is not valid")
			return
		}
		s.playerInfo(id)
	case "playersinfo":
		for i, p := range s.players {
			if p != nil {
				s.playerInfo(i)
			}
		}
	case "move": // Console.cpp:990: the team, then the name
		team, name, _ := strings.Cut(strings.TrimLeft(rest, " "), " ")
		t := atoi(team)
		if t < -1 || t > 1 {
			s.out("Error: team ID must be one of the following: -1 for spectator, 0 for blue or 1 for red.")
			return
		}
		id := s.findPlayer(name)
		if id == -1 {
			s.out("Error: No players were found with given name")
			return
		}
		s.assignPlayerTeam(id, t)
		s.broadcast(proto.ClsvSvclTeamRequest, &proto.TeamRequestMsg{PlayerID: int8(id), TeamRequested: int8(t)})
	case "moveid":
		s.moveID(rest)
	case "allwatch": // Console.cpp:1169: everyone to the spectators
		for i, p := range s.players {
			if p != nil {
				s.assignPlayerTeam(i, proto.TeamSpectator)
				s.broadcast(proto.ClsvSvclTeamRequest, &proto.TeamRequestMsg{PlayerID: int8(i), TeamRequested: proto.TeamSpectator})
			}
		}
	case "kick": // Scene::kick(name) (SceneNet.cpp:202): every player with that name
		name := colorLess(rest)
		for i, p := range s.players {
			if p != nil && strings.EqualFold(colorLess(p.Name), name) {
				s.out(fmt.Sprintf("> Disconnecting client %s (%s), kicked by server", p.Name, p.IP))
				s.kick(i)
			}
		}
	case "kickid":
		if id := atoi(rest); id >= 0 && id < proto.MaxPlayer && s.players[id] != nil {
			s.out(fmt.Sprintf("> Disconnecting client %s (%s), kicked by server", s.players[id].Name, s.players[id].IP))
			s.kick(id)
		}
	case "ban": // Scene::ban(name) (SceneNet.cpp:251)
		name := colorLess(rest)
		for i, p := range s.players {
			if p != nil && strings.EqualFold(colorLess(p.Name), name) {
				s.banPlayer(i, who)
			}
		}
	case "banid":
		if id := atoi(rest); id >= 0 && id < proto.MaxPlayer && s.players[id] != nil {
			s.banPlayer(id, who)
		}
	case "banip": // Scene::banIP (SceneNet.cpp:294)
		ip := strings.TrimSpace(rest)
		if ip == "" {
			return
		}
		s.addBan(store.Ban{Name: "MANUAL-IP-BAN", IP: ip, By: who})
		s.out("> " + ip + " banned")
	case "banmac": // openbv: by the client's MAC
		mac := strings.TrimSpace(rest)
		if mac == "" {
			return
		}
		s.addBan(store.Ban{Name: "MANUAL-MAC-BAN", MAC: mac, By: who})
		s.out("> " + mac + " banned")
	case "unban": // Console.cpp:2057
		if s.bans == nil {
			return
		}
		if b, ok, err := s.bans.Remove(atoi(rest)); err != nil {
			s.log.Error("saving the ban list", "err", err)
		} else if ok {
			s.out("> " + b.Name + " unbanned")
		}
	case "banlist": // Console.cpp:2017
		if s.bans == nil {
			return
		}
		for i, b := range s.bans.List() {
			addr := b.IP
			if b.MAC != "" {
				addr = strings.TrimSpace(addr + " " + b.MAC)
			}
			s.out(fmt.Sprintf("[%02d] %s - %s", i, b.Name, addr))
		}
	case "sayall":
		s.sayall(rest)
	case "sayid":
		s.sayID(rest)
	case "nukeall": // Server::nukeAll (Server.cpp:1514)
		for i := range s.players {
			s.nukePlayer(i)
		}
	case "nuke": // Console.cpp:1116; an unknown name used an uninitialised ID in the original
		if id := s.findPlayer(rest); id == -1 {
			s.out("Error: No players were found with given name")
		} else {
			s.nukePlayer(id)
		}
	case "nukeid":
		id := atoi(firstToken(rest))
		switch {
		case id == -1:
			for i := range s.players {
				s.nukePlayer(i)
			}
		case id < -1 || id >= proto.MaxPlayer || s.players[id] == nil:
			s.out("Error: Bad player ID (use playerlist command to obtain the correct ID)")
		default:
			s.nukePlayer(id)
		}
	case "forceplayerspawn":
		s.forcePlayerSpawn(rest)
	case "blueteamscore", "redteamscore", "redflagreturn", "blueflagreturn":
		s.flagCommand(strings.ToLower(cmd), atoi(rest))
	case "mapinfos": // Console.cpp:1869
		if s.m == nil {
			return
		}
		s.out(fmt.Sprintf("Map Infos, name:%s size:%d,%d nbSpawn:%d", s.mapName, s.m.Width, s.m.Height, len(s.m.DMSpawns)))
		for i, p := range s.m.DMSpawns {
			s.out(fmt.Sprintf("Spawn %d:%s,%s", i, ftoa(p[0]), ftoa(p[1])))
		}
	case "listbluespawns", "listredspawns":
		if s.m == nil {
			return
		}
		spawns, label := s.m.BlueSpawns, "Blue"
		if strings.EqualFold(cmd, "listredspawns") {
			spawns, label = s.m.RedSpawns, "Red"
		}
		for i, p := range spawns {
			s.out(fmt.Sprintf("%s Spawn #%d:%s,%s", label, i, ftoa(p[0]), ftoa(p[1])))
		}
	case "restart": // the original rebuilt its scene: the session starts again
		s.out("> Restarting the server")
		if s.control.Restart != nil {
			s.control.Restart()
		}
	case "quit":
		s.out("> Stopping the server")
		if s.control.Stop != nil {
			s.control.Stop()
		}
	case "approveall", "approveplayer", "rejectplayer", "rejectallplayers", "listapprovedplayers",
		"allplayerpos", "cachelist", "cacheban", "cachebanned", "cacheunban", "cachelistremote",
		"cachebanremote", "getinvalidchecksums", "deleteinvalidchecksums", "invalidchecksumsinfo",
		"addreporturl", "removereporturl", "removeallreporturls", "listreporturls", "status", "remoteadmin":
		// account-server, master-cache and remote-admin features: no such services here
		s.out("> " + cmd + " is not available on this server")
	default:
		s.out(fmt.Sprintf("> Unkown command : \"%s\"", cmd))
		s.out("> Type \"?\" for commands list")
	}
}

// set is the console's set (Console.cpp:792) for server variables: sv_* values go to every client
// (Server::sendSVChange, in the formatted form of GameVar::sendOne, after validation: §8.4); zsv_*
// (the admin login) stay on the server. A password longer than 15 characters is cut. Setting
// sv_gameTimeLimit restarts the game clock.
func (s *Server) set(rest, who string) {
	name, value, _ := strings.Cut(strings.TrimLeft(rest, " "), " ")
	v := s.SV.Lookup(name)
	if v == nil {
		s.out("> Unknown variable")
		return
	}
	lower := strings.ToLower(v.Name)
	isSV := strings.HasPrefix(lower, "sv_")
	if !isSV && !strings.HasPrefix(lower, "zsv_") {
		s.out("> Unknown variable")
		return
	}
	if v.Name == "sv_password" {
		pw := strings.Trim(strings.TrimSpace(value), `"`)
		if len(pw) > 15 {
			pw = pw[:15]
			s.out(fmt.Sprintf("Max password length is 15 characters, password changed to '%s'", pw))
		}
		value = pw
	}
	if !v.Set(value) {
		s.out("> Invalid arguments")
		return
	}
	s.out("> " + v.Name)
	if isSV {
		var sc proto.SvclSvChangeMsg
		proto.SetCString(sc.SvChange[:], s.SV.ChangeText(v.Name))
		s.broadcast(proto.SvclSvChange, &sc)
	}
	if v.Name == "sv_gameTimeLimit" {
		s.gameTimeLeft = s.SV.GameTimeLimit.F
	}
	if v.Name == "sv_serverType" {
		s.updateProSettings()
	}
	if v.Name == "sv_port" {
		s.out("> The port changes when the session restarts")
	}
	s.setCvars[v.Name] = strings.Trim(v.Value(), `"`)
	s.changed()
}

// changeMap is Server::changeMap (Server.cpp:200): the map (or the next of the rotation) in 10 s.
func (s *Server) changeMap(name string) {
	if name == "" {
		name = s.queryNextMap()
	}
	name = bvmap.MapName(name)
	if _, ok := s.lib.Path(name); !ok {
		s.out("> Warning, map not found " + name)
		return
	}
	s.nextMap = name
	s.changeMapDelay = 10
	s.roundState = proto.GameMapChange
	s.broadcast(proto.SvclGameState, &proto.SvclRoundStateMsg{NewState: proto.GameMapChange})
}

// addMapCmd is Server::addmap (Server.cpp:236).
func (s *Server) addMapCmd(name string) {
	name = bvmap.MapName(name)
	if _, ok := s.lib.Path(name); !ok {
		s.out("> Warning, map not found " + name)
		return
	}
	for _, n := range s.mapList {
		if strings.EqualFold(n, name) {
			return
		}
	}
	s.mapList = append(s.mapList, name)
	s.out("> " + name + " added")
	s.changed()
}

// removeMapCmd is Server::removemap (Server.cpp:261): never the map being played.
func (s *Server) removeMapCmd(name string) {
	name = bvmap.MapName(name)
	if strings.EqualFold(name, s.mapName) {
		return
	}
	for _, n := range s.mapList {
		if strings.EqualFold(n, name) {
			s.removeMap(name)
			s.out("> " + name + " removed")
			s.changed()
			return
		}
	}
}

func (s *Server) playerInfo(i int) {
	p := s.players[i]
	w, m := -1, -1
	if p.Weapon != nil {
		w = p.Weapon.ID
	}
	if p.Melee != nil {
		m = p.Melee.ID
	}
	s.out(fmt.Sprintf("Player %d WeaponID:%d SecondaryID:%d TeamID:%d Position:%s,%s", i, w, m, p.TeamID,
		ftoa(p.CurrentCF.Position[0]), ftoa(p.CurrentCF.Position[1])))
}

// findPlayer is the console's name lookup: the first player whose name without colours matches.
func (s *Server) findPlayer(name string) int {
	name = colorLess(strings.TrimSpace(name))
	for i, p := range s.players {
		if p != nil && strings.EqualFold(colorLess(p.Name), name) {
			return i
		}
	}
	return -1
}

// moveID is the console's moveid (Console.cpp:1031): <team> <id>, id -1 for everyone. Moving puts
// the player out of the map and drops carried flags without DROP_FLAG (quirk).
func (s *Server) moveID(rest string) {
	f := strings.Fields(rest)
	if len(f) < 2 {
		return
	}
	team, id := atoi(f[0]), atoi(f[1])
	if team < -1 || team > 1 || id < -1 || id >= proto.MaxPlayer || (id >= 0 && s.players[id] == nil) {
		s.out("Error: Bad team or player ID (use playerlist command to obtain the correct ID)")
		return
	}
	move := func(i int) {
		p := s.players[i]
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
}

// banPlayer is Scene::ban(int) (SceneNet.cpp:269): onto the list (with the MAC, openbv), then
// disconnected.
func (s *Server) banPlayer(i int, who string) {
	p := s.players[i]
	s.addBan(store.Ban{Name: colorLess(p.Name), IP: p.IP, MAC: p.MAC, By: who})
	s.out(fmt.Sprintf("> Disconnecting client %s (%s), banned by server", p.Name, p.IP))
	s.kick(i)
}

func (s *Server) addBan(b store.Ban) {
	if s.bans == nil {
		return
	}
	if err := s.bans.Add(b); err != nil {
		s.log.Error("saving the ban list", "err", err)
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
	s.log.Info("chat", "team", c.TeamID, "text", "console : "+text)
}

// sayID is the console's sayid (Console.cpp:1602): a private message, "Server: ".
func (s *Server) sayID(rest string) {
	idText, text, _ := strings.Cut(strings.TrimLeft(rest, " "), " ")
	id := atoi(idText)
	if id < 0 || id >= proto.MaxPlayer || s.players[id] == nil {
		s.out("Error: Bad player ID (use playerlist command to obtain the correct ID)")
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
}

// forcePlayerSpawn is the console's forceplayerspawn (Console.cpp:1708): <id> <x> <y> [weapon]
// [secondary]. The original copied the skin and decals into the message as raw bytes of its
// CStrings and floats (garbage the clients ignore); openbv sends zeros there.
func (s *Server) forcePlayerSpawn(rest string) {
	f := strings.Fields(rest)
	if len(f) < 3 {
		return
	}
	id := atoi(f[0])
	if id < 0 || id >= proto.MaxPlayer || s.players[id] == nil {
		return
	}
	p := s.players[id]
	x, y := atof(f[1]), atof(f[2])
	weapon := 0
	if p.Weapon != nil {
		weapon = p.Weapon.ID
	}
	if len(f) > 3 {
		weapon = atoi(f[3])
		p.NextSpawnWeapon = weapon
	}
	melee := proto.WeaponKnives
	if p.Melee != nil {
		melee = p.Melee.ID
	}
	if len(f) > 4 {
		melee = atoi(f[4])
		p.NextMeleeWeapon = melee
	}
	if weapon < 0 || weapon > proto.WeaponMinibot || melee < 0 || melee > proto.WeaponMinibot {
		s.out("Error: bad weapon ID")
		return
	}
	s.spawnAt(p, bvmath.Vec3{x, y, 0})
	s.broadcast(proto.SvclPlayerSpawn, &proto.SvclPlayerSpawnMsg{
		PlayerID: int8(id), WeaponID: int8(weapon), MeleeID: int8(melee),
		Position: [3]int16{int16(x * 10), int16(y * 10), int16(p.CurrentCF.Position[2] * 10)},
	})
}

// flagCommand is the console's blueTeamScore, redTeamScore, redFlagReturn and blueFlagReturn
// (Console.cpp:1757-1868): a flag goes home with a capture (-3) or a return (-1) message first; the
// scoring ones count a capture for the player. Note the original's pairing: blueTeamScore and
// redFlagReturn act on flag 1, redTeamScore and blueFlagReturn on flag 0.
func (s *Server) flagCommand(cmd string, id int) {
	if id < 0 || id >= proto.MaxPlayer || s.players[id] == nil {
		return
	}
	flag, state := int8(1), int8(-3)
	switch cmd {
	case "redteamscore":
		flag = 0
	case "redflagreturn":
		state = -1
	case "blueflagreturn":
		flag, state = 0, -1
	}
	s.flagState[flag] = -2
	s.broadcast(proto.SvclChangeFlagState, &proto.SvclChangeFlagStateMsg{FlagID: flag, NewFlagState: state, PlayerID: int8(id)})
	s.broadcast(proto.SvclChangeFlagState, &proto.SvclChangeFlagStateMsg{FlagID: flag, NewFlagState: -2, PlayerID: int8(id)})
	switch cmd {
	case "blueteamscore":
		s.players[id].Score++
		s.blueWin++
		s.blueScore = s.blueWin
	case "redteamscore":
		s.players[id].Score++
		s.redWin++
		s.redScore = s.redWin
	}
}

// --- what the admin page reads

// Status is the session's game for the admin page and the master.
func (s *Server) Status() session.Status {
	st := session.Status{
		Name: s.SV.GameName.S, Map: s.mapName, NextMap: s.nextMap, GameType: int(s.SV.GameType.I),
		RoundState: s.roundState, MaxPlayers: int(s.SV.MaxPlayer.I), Port: int(s.SV.Port.I),
		Passworded: s.SV.Password.S != "", Public: s.SV.GamePublic.B,
		BlueScore: int(s.blueScore), RedScore: int(s.redScore), BlueWin: int(s.blueWin), RedWin: int(s.redWin),
		GameTimeLeft: s.gameTimeLeft, RoundTimeLeft: s.roundTimeLeft,
		Rotation: append([]string{}, s.mapList...), VoteOn: append([]string{}, s.voteList...),
	}
	for _, p := range s.players {
		if p != nil {
			st.Players++
		}
	}
	if s.vote.inProgress {
		st.Vote = &session.VoteStatus{From: s.vote.from, What: s.vote.what, Yes: s.vote.yes, No: s.vote.no,
			Voters: len(s.vote.active), Remaining: s.vote.remaining}
	}
	return st
}

// Cvars are the server variables, with their current values. The admin password is never shown.
func (s *Server) Cvars() []session.CvarInfo {
	out := []session.CvarInfo{}
	for _, v := range s.SV.All() {
		value := strings.Trim(v.Value(), `"`)
		if v.Name == "zsv_adminPass" && value != "" {
			value = "********"
		}
		kind := map[cvar.Kind]string{cvar.Bool: "bool", cvar.Int: "int", cvar.Float: "float", cvar.String: "string"}[v.Kind]
		out = append(out, session.CvarInfo{Name: v.Name, Help: v.Help, Kind: kind, Value: value})
	}
	sort.Slice(out, func(i, j int) bool { return strings.ToLower(out[i].Name) < strings.ToLower(out[j].Name) })
	return out
}

func firstToken(s string) string {
	f := strings.Fields(s)
	if len(f) == 0 {
		return ""
	}
	return f[0]
}

// atof is CString::toFloat in the game (atof): a leading number, 0 otherwise.
func atof(s string) float32 {
	s = strings.TrimSpace(s)
	end := 0
	for end < len(s) && strings.IndexByte("+-.0123456789eE", s[end]) >= 0 {
		end++
	}
	for end > 0 {
		if f, err := strconv.ParseFloat(s[:end], 32); err == nil {
			return float32(f)
		}
		end--
	}
	return 0
}

// ftoa is CString += float: "%f".
func ftoa(f float32) string { return fmt.Sprintf("%f", f) }
