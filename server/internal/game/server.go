// Package game is Babo Violent 2.11's server, ported from the original's Server*.cpp and the parts
// of Game, Player and Map it runs (design/server.md is the specification; every function cites its
// source). One Server is one session's game; all calls come from the session's goroutine.
//
// Milestone 1 (design/server.md §10.2): connections, the handshake and state dump, map downloads,
// pings, chat, names and skins, teams, spawning (DM spawn choice and the team types' basic one),
// coord-frame interpolation and broadcast, cvars to clients, round end and map rotation. Shooting,
// damage, projectiles and the team modes' rules come next; their messages are accepted and ignored.
package game

import (
	"context"
	"fmt"
	"log/slog"
	"path/filepath"
	"strings"
	"time"

	"github.com/emdzej/openbv/server/internal/bbnet"
	"github.com/emdzej/openbv/server/internal/bvmap"
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/cvar"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/session"
	"github.com/emdzej/openbv/server/internal/wire"
)

// delay is the fixed step: dkcGetElapsedf() with dkcInit(30), 1.0f/30.
const delay = float32(1.0) / 30

const epsilon = 0.0001 // EPSILON (CVector.h)

// Options are the deliberate deviations, each documented in design/server.md §8.4.
type Options struct {
	// BindPlayerID ignores messages whose playerID isn't the sender's own player (§1.4). Default on.
	BindPlayerID bool
}

type mapInfo struct {
	name       string
	area       int
	lastPlayed int
}

type mapTransfer struct {
	netID    uint32
	name     string
	chunkNum int
}

type checksumQuery struct {
	playerID int
	netID    uint32
	elapsed  float32
}

type ban struct{ name, ip string }

// Server is one game server (Server + its Game).
type Server struct {
	SV   *cvar.SV
	Opts Options

	net  *bbnet.Server
	log  *slog.Logger
	lib  bvmap.Library
	rand *bvmath.Rand
	now  func() time.Time

	players [proto.MaxPlayer]*Player

	mapName                              string
	m                                    *bvmap.Map
	flagState                            [2]int8
	flagPos                              [2]bvmath.Vec3
	gameType                             int
	spawnType                            int
	blueScore, redScore, blueWin, redWin int32
	gameTimeLeft, roundTimeLeft          float32
	roundState                           int

	frameID        int32
	changeMapDelay float32
	nextMap        string
	mapList        []string
	mapInfos       []mapInfo
	transfers      []mapTransfer
	checksums      []checksumQuery
	banList        []ban
	infoSendDelay  float32
	nbPlayers      int
}

// New hosts a game for a session (Server::host, Server.cpp:150): its map is the first of the
// rotation.
func New(st session.Settings, content string, net *bbnet.Server, log *slog.Logger) (*Server, error) {
	s := &Server{
		SV:            cvar.NewSV(),
		Opts:          Options{BindPlayerID: true},
		net:           net,
		log:           log,
		lib:           bvmap.Library{Dir: filepath.Join(content, "main", "maps")},
		rand:          bvmath.NewRand(0),
		now:           time.Now,
		roundState:    proto.GamePlaying,
		infoSendDelay: 15,
	}
	if st.Name != "" {
		s.SV.GameName.S = st.Name
	}
	if len(st.Password) > 15 {
		st.Password = st.Password[:15]
	}
	s.SV.Password.S = st.Password
	if st.GameType >= 0 && st.GameType <= 3 {
		s.SV.GameType.I = int32(st.GameType)
	}
	if st.MaxPlayers >= 1 && st.MaxPlayers <= proto.MaxPlayer {
		s.SV.MaxPlayer.I = int32(st.MaxPlayers)
	}
	if st.Port != 0 {
		s.SV.Port.I = int32(st.Port)
	}
	maps := st.Maps
	if len(maps) == 0 {
		maps = []string{"CTF-Daivuk"}
	}
	// Game::Game (Game.cpp:79): the game type and the timers from the cvars
	s.gameType = int(s.SV.GameType.I)
	s.spawnType = int(s.SV.SpawnType.I)
	s.gameTimeLeft = s.SV.GameTimeLimit.F
	s.roundTimeLeft = s.SV.RoundTimeLimit.F

	s.srand()
	s.rand.Int() // game->mapSeed = rand()%1000000 (unused)
	s.mapName = bvmap.MapName(maps[0])
	if err := s.createMap(); err != nil {
		return nil, err
	}
	if !bvmap.Valid(s.m, s.gameType) {
		return nil, fmt.Errorf("map %s is missing some entities for game type %d", s.mapName, s.gameType)
	}
	s.nextMap = s.mapName
	s.mapList = append(s.mapList, s.mapName)
	for _, name := range maps[1:] {
		s.addMap(name)
	}
	net.Trace = func(dir string, netID uint32, p wire.Packet) {
		if log.Enabled(context.Background(), slog.LevelDebug-4) {
			log.Log(context.Background(), slog.LevelDebug-4, "packet", "dir", dir, "netId", netID, "type", p.Type, "size", len(p.Data))
		}
	}
	return s, nil
}

// srand is srand(time(0)), done in Server::host and in every Game::createMap.
func (s *Server) srand() { s.rand.Seed(uint32(s.now().Unix())) }

// createMap is Game::createMap (Game.cpp:246) on the server.
func (s *Server) createMap() error {
	s.srand()
	m, err := s.lib.Load(s.mapName)
	if err != nil {
		s.m = nil
		return err
	}
	s.m = m
	s.flagState = [2]int8{-2, -2} // Map::Map (Map.cpp:148)
	s.flagPos = [2]bvmath.Vec3{}
	return nil
}

func (s *Server) addMap(name string) {
	name = bvmap.MapName(name)
	if _, ok := s.lib.Path(name); !ok {
		s.log.Warn("addmap: no such map", "map", name)
		return
	}
	for _, n := range s.mapList {
		if strings.EqualFold(n, name) {
			return
		}
	}
	s.mapList = append(s.mapList, name)
}

// --- sending

func (s *Server) send(dest int32, typ uint16, msg any) {
	s.net.Send(dest, typ, proto.Encode(msg), wire.TCP)
}

func (s *Server) sendUDP(dest int32, typ uint16, msg any) {
	s.net.Send(dest, typ, proto.Encode(msg), wire.UDP)
}

func (s *Server) broadcast(typ uint16, msg any) { s.send(0, typ, msg) }

// --- the frame

// Frame is Server::update (Server.cpp:566), one fixed 1/30 s step (design/server.md §1.3).
func (s *Server) Frame() {
	// 1. the master report: with the master (milestone 5)
	s.infoSendDelay += delay
	if s.infoSendDelay > 20 {
		s.infoSendDelay = 0
	}
	// 5. checksum queries (Pro): no reply within 10 s disconnects the player
	for i := 0; i < len(s.checksums); i++ {
		q := &s.checksums[i]
		q.elapsed += delay
		if q.elapsed > 10.0 {
			if s.players[q.playerID] != nil {
				s.log.Info("checksum timeout", "player", q.playerID)
				s.kick(q.playerID)
			}
			s.checksums = append(s.checksums[:i], s.checksums[i+1:]...)
			i--
		}
	}
	// 7.
	s.updateNet()

	// 8. the map change countdown
	if s.changeMapDelay > 0 {
		s.changeMapDelay -= delay
		if s.changeMapDelay <= 0 {
			s.changeMapDelay = 0
			s.doMapChange()
		}
	}
	// 9.
	if int(s.SV.GameType.I) != s.gameType {
		s.resetGameType(int(s.SV.GameType.I))
	}
	// 10.
	if s.gameTimeLeft > 0 {
		s.gameTimeLeft -= delay
	}
	if s.roundTimeLeft > 0 {
		s.roundTimeLeft -= delay
	}
	if s.gameTimeLeft < 0 {
		s.gameTimeLeft = 0
	}
	if s.roundTimeLeft < 0 {
		s.roundTimeLeft = 0
	}
	// 11.
	if s.roundState == proto.GamePlaying {
		s.checkRoundEnd()
	}
	// 12. received messages, all of the first client's before the next one's
	for {
		netID, p, ok := s.net.Receive()
		if !ok {
			break
		}
		s.recvPacket(netID, p)
	}
	// 13. pings
	s.nbPlayers = 0
	for i, p := range s.players {
		if p == nil {
			continue
		}
		s.nbPlayers++
		if !p.WaitForPong {
			if p.CurrentPingFrame >= 30 {
				p.CurrentPingFrame = 0
				p.WaitForPong = true
				p.ConnectionInterrupted = false
				s.send(int32(p.BabonetID), proto.SvclPing, &proto.SvclPingMsg{PlayerID: int8(i)})
				continue
			}
		} else {
			if p.CurrentPingFrame > p.Ping {
				p.Ping = p.CurrentPingFrame
			}
			if p.CurrentPingFrame > 30 {
				saveFrame := p.CurrentCF.FrameID
				p.CurrentCF.assign(p.NetCF1)
				p.CurrentCF.FrameID = saveFrame
				p.ConnectionInterrupted = true
				p.SendPosFrame = 0
			}
			if p.CurrentPingFrame > 300 { // the log says 3 s; it is 10 (design/server.md §8.3)
				s.log.Info("disconnecting client, no respond since 3sec", "player", i)
				s.kick(i)
				continue
			}
		}
		p.CurrentPingFrame++
	}
	// 14. max ping, idling, the join message
	for i, p := range s.players {
		if p == nil {
			continue
		}
		if p.BabySitTime <= epsilon && p.Ping*33 > s.SV.MaxPing.I && s.SV.MaxPing.I != 0 {
			p.PingOverMax += delay
			if p.PingOverMax > maxTimeOverMaxPing {
				s.Command(fmt.Sprintf("sayid %d Maximum ping exceeded, moving to spectator", i))
				s.Command(fmt.Sprintf("moveid %d %d", proto.TeamSpectator, i))
				if s.players[i] != nil {
					s.players[i].PingOverMax = 0
				}
			}
		} else {
			p.PingOverMax = 0
		}
		if p = s.players[i]; p == nil {
			continue
		}
		if s.SV.AutoSpectateWhenIdle.B && p.TimeIdle > float32(s.SV.AutoSpectateIdleMaxTime.I) &&
			(p.TeamID == proto.TeamRed || p.TeamID == proto.TeamBlue) {
			s.log.Info("too much idling, not enough playing", "player", i)
			s.Command(fmt.Sprintf("moveid %d %d", proto.TeamSpectator, i))
		}
		if s.SV.SendJoinMessage.B && p.TimeInServer > 1.0 && p.TimeInServer < 1.0+delay {
			s.Command(fmt.Sprintf("sayid %d %s", i, s.SV.JoinMessage.S))
		}
	}
	// 15. Game::update (Game.cpp:348): the players, while playing
	if s.roundState == proto.GamePlaying {
		for _, p := range s.players {
			if p != nil {
				p.update(delay, s.SV.CubicMotion.B)
			}
		}
	}
	// 16. the coord-frame batches
	if s.roundState == proto.GamePlaying {
		s.sendCoordFrames()
	}
	// 19.
	s.updateNet()
	// 20.
	s.sendMapChunks()
	// 21.
	s.frameID++
}

// maxTimeOverMaxPing (Server.h).
const maxTimeOverMaxPing = 5.0

// updateNet is Server::updateNet (Server.cpp:440): at most one connection event.
func (s *Server) updateNet() {
	ev := s.net.Update(delay)
	switch ev.Kind {
	case bbnet.EvNew:
		s.log.Info("a client has connected", "netId", ev.NetID, "ip", ev.IP)
		for _, b := range s.banList {
			if b.ip == ev.IP {
				s.net.Disconnect(ev.NetID)
				s.log.Info("disconnecting banned client", "name", b.name, "ip", ev.IP)
				return
			}
		}
		id := s.createNewPlayer(ev.NetID)
		if id == -1 {
			s.net.Disconnect(ev.NetID)
			s.log.Info("disconnecting client, server is full", "netId", ev.NetID)
			return
		}
		ip := ev.IP
		if len(ip) > 15 {
			ip = ip[:15]
		}
		s.players[id].IP = ip
	case bbnet.EvLost:
		for i, p := range s.players {
			if p != nil && p.BabonetID == ev.NetID {
				s.log.Info("player disconnected", "name", p.Name, "player", i)
				s.deletePlayer(i)
				s.broadcast(proto.SvclPlayerDisconnect, &proto.SvclPlayerDisconnectMsg{PlayerID: int8(i)})
				break
			}
		}
	}
}

// createNewPlayer is Game::createNewPlayerSV (Game.cpp:1644): the lowest free slot.
func (s *Server) createNewPlayer(netID uint32) int {
	for i := 0; i < int(s.SV.MaxPlayer.I); i++ {
		if s.players[i] == nil {
			s.players[i] = newPlayer(i, netID, s.SV.TimeToSpawn.F)
			s.broadcast(proto.SvclNewPlayer, &proto.SvclNewPlayerMsg{NewPlayerID: int8(i), BaboNetID: int32(netID)})
			s.send(int32(netID), proto.SvclGameVersion, &proto.SvclGameVersionMsg{GameVersion: proto.GameVersion})
			return i
		}
	}
	return -1
}

// deletePlayer is ZEVEN_SAFE_DELETE(players[i]): the destructor drops a carried flag (Player.cpp:181).
func (s *Server) deletePlayer(i int) {
	p := s.players[i]
	if p == nil {
		return
	}
	for f := 0; f < 2; f++ {
		if int(s.flagState[f]) == i {
			s.flagState[f] = -1
			s.flagPos[f] = p.CurrentCF.Position
			s.flagPos[f][2] = 0
			s.broadcast(proto.SvclDropFlag, &proto.SvclDropFlagMsg{FlagID: int8(f), Position: s.flagPos[f]})
		}
	}
	s.players[i] = nil
	for q := 0; q < len(s.checksums); q++ {
		if s.checksums[q].playerID == i {
			s.checksums = append(s.checksums[:q], s.checksums[q+1:]...)
			q--
		}
	}
}

// kick is Scene::kick(int) (SceneNet.cpp:231).
func (s *Server) kick(i int) {
	p := s.players[i]
	if p == nil {
		return
	}
	s.net.Disconnect(p.BabonetID)
	s.deletePlayer(i)
	s.broadcast(proto.SvclPlayerDisconnect, &proto.SvclPlayerDisconnectMsg{PlayerID: int8(i)})
}

// killPlayer is Player::kill (Player.cpp:232), the server's (silent) side.
func (s *Server) killPlayer(p *Player) {
	p.Status = proto.StatusDead
	for f := 0; f < 2; f++ {
		if int(s.flagState[f]) == p.ID {
			s.flagState[f] = -1
			s.flagPos[f] = p.CurrentCF.Position
			s.flagPos[f][2] = 0
			s.broadcast(proto.SvclDropFlag, &proto.SvclDropFlagMsg{FlagID: int8(f), Position: s.flagPos[f]})
		}
	}
	p.CurrentCF.Position = bvmath.Vec3{-999, -999, 0}
}

// sendCoordFrames is the coord-frame broadcast (Server.cpp:1133, design/server.md §2.5).
func (s *Server) sendCoordFrames() {
	for i, pi := range s.players {
		if pi == nil {
			continue
		}
		pi.SendPosFrame++
		if pi.SendPosFrame < pi.AvgPing || pi.SendPosFrame < s.SV.MinSendInterval.I+int32(s.nbPlayers/8) {
			continue
		}
		pi.SendPosFrame = 0
		dest := int32(pi.BabonetID)
		for j, pj := range s.players {
			if pj == nil {
				continue
			}
			if j != i && pj.Status == proto.StatusAlive {
				cf := &pj.CurrentCF
				s.sendUDP(dest, proto.ClsvSvclPlayerCoordFrame, &proto.PlayerCoordFrameMsg{
					PlayerID:  int8(j),
					BaboNetID: int32(pj.BabonetID),
					FrameID:   cf.FrameID,
					MousePos:  [3]int16{int16(cf.MousePosOnMap[0] * 100), int16(cf.MousePosOnMap[1] * 100), int16(cf.MousePosOnMap[2] * 100)},
					Position:  [3]int16{int16(cf.Position[0] * 100), int16(cf.Position[1] * 100), int16(cf.Position[2] * 100)},
					Vel:       [3]int8{int8(cf.Vel[0] * 10), int8(cf.Vel[1] * 10), int8(cf.Vel[2] * 10)},
				})
			}
			// (the minibot's frame goes here: milestone 3)
			s.sendUDP(dest, proto.SvclPlayerPing, &proto.SvclPlayerPingMsg{PlayerID: int8(j), Ping: int16(pj.Ping)})
		}
		s.sendUDP(dest, proto.SvclSynchronizeTimer, &proto.SvclSynchronizeTimerMsg{
			FrameID: s.frameID, GameTimeLeft: s.gameTimeLeft, RoundTimeLeft: s.roundTimeLeft,
		})
	}
}

// sendMapChunks is the map transfer loop (Server.cpp:1342).
func (s *Server) sendMapChunks() {
	bytesSent := 0
	bytesPerFrame := int(s.SV.MaxUploadRate.F * 1024 / 30)
	var keep []mapTransfer
	for _, t := range s.transfers {
		if bytesSent > bytesPerFrame {
			keep = append(keep, t)
			continue
		}
		if t.name == "" {
			continue
		}
		m, err := s.lib.Load(t.name)
		if err != nil {
			continue // a missing file crashed the original (fclose(NULL)): end the transfer
		}
		var chunk proto.SvclMapChunkMsg
		start := 250 * t.chunkNum
		if start < len(m.File) {
			chunk.Size = uint16(copy(chunk.Data[:], m.File[start:]))
		}
		s.send(int32(t.netID), proto.SvclMapChunk, &chunk)
		bytesSent += 250
		if chunk.Size != 0 {
			t.chunkNum++
			keep = append(keep, t)
		}
	}
	s.transfers = keep
}

// checkRoundEnd is the end-of-game checks (Server.cpp:823, design/server.md §5.12).
func (s *Server) checkRoundEnd() {
	timeOut := s.gameTimeLeft == 0 && s.SV.GameTimeLimit.F > 0
	scoreLimit, winLimit := s.SV.ScoreLimit.I, s.SV.WinLimit.I
	changed := false
	set := func(state int) { s.roundState = state; changed = true }
	switch s.gameType {
	case proto.GameTypeDM:
		if timeOut {
			set(proto.GameDontShow)
		}
		for _, p := range s.players {
			if p != nil && p.Score >= scoreLimit && scoreLimit > 0 {
				set(proto.GameDontShow)
				break
			}
		}
	case proto.GameTypeTDM:
		switch {
		case s.blueScore == s.redScore && s.redScore >= scoreLimit && scoreLimit > 0:
			set(proto.GameDraw)
		case s.blueScore >= scoreLimit && scoreLimit > 0:
			set(proto.GameBlueWin)
		case s.redScore >= scoreLimit && scoreLimit > 0:
			set(proto.GameRedWin)
		}
		if timeOut {
			set(proto.GameDontShow)
		}
	case proto.GameTypeCTF, proto.GameTypeSND:
		switch {
		case s.blueWin == s.redWin && s.redWin >= winLimit && winLimit > 0:
			set(proto.GameDraw)
		case s.blueWin >= winLimit && winLimit > 0:
			set(proto.GameBlueWin)
		case s.redWin >= winLimit && winLimit > 0:
			set(proto.GameRedWin)
		}
		if timeOut {
			switch {
			case s.blueWin == s.redWin:
				set(proto.GameDraw)
			case s.blueWin > s.redWin:
				set(proto.GameBlueWin)
			default:
				set(proto.GameRedWin)
			}
		}
	}
	if changed {
		s.changeMapDelay = 10
		s.nextMap = s.queryNextMap()
		s.broadcast(proto.SvclGameState, &proto.SvclRoundStateMsg{NewState: int8(s.roundState)})
	}
}

// doMapChange is the end of the map change countdown (Server.cpp:740).
func (s *Server) doMapChange() {
	s.resetGameType(int(s.SV.GameType.I))
	s.mapName = s.nextMap
	err := s.createMap()
	for (err != nil || !bvmap.Valid(s.m, s.gameType)) && len(s.mapList) > 0 {
		s.log.Warn("map is missing some entities, removing it from the list", "map", s.mapName)
		s.removeMap(s.mapName)
		if len(s.mapList) == 0 {
			break
		}
		s.mapName = s.queryNextMap()
		err = s.createMap()
	}
	if s.m == nil || err != nil {
		s.log.Error("no map left to play; the session can't go on")
		return
	}
	var mc proto.SvclMapChangeMsg
	proto.SetCString(mc.MapName[:], s.mapName)
	mc.GameType = int8(s.SV.GameType.I)
	s.broadcast(proto.SvclMapChange, &mc)
	s.roundState = proto.GamePlaying
	s.broadcast(proto.SvclGameState, &proto.SvclRoundStateMsg{NewState: proto.GamePlaying, ReInit: 1})
}

func (s *Server) removeMap(name string) {
	for i, n := range s.mapList {
		if strings.EqualFold(n, name) {
			s.mapList = append(s.mapList[:i], s.mapList[i+1:]...)
			break
		}
	}
	for i, mi := range s.mapInfos {
		if strings.EqualFold(mi.name, name) {
			s.mapInfos = append(s.mapInfos[:i], s.mapInfos[i+1:]...)
			break
		}
	}
}

// resetGameType is Game::resetGameType (Game.cpp:150).
func (s *Server) resetGameType(gameType int) {
	s.gameType = gameType
	s.spawnType = int(s.SV.SpawnType.I)
	s.blueScore, s.redScore, s.blueWin, s.redWin = 0, 0, 0, 0
	s.gameTimeLeft = s.SV.GameTimeLimit.F
	s.roundTimeLeft = s.SV.RoundTimeLimit.F
	s.resetRound()
	s.broadcast(proto.SvclChangeGameType, &proto.SvclChangeGameTypeMsg{NewGameType: int8(gameType)})
	for _, p := range s.players {
		if p != nil {
			p.reinit()
		}
	}
}

// resetRound is Game::resetRound (Game.cpp:205): flags home, projectiles gone (without deletes),
// everyone dead with no wait.
func (s *Server) resetRound() {
	s.roundTimeLeft = s.SV.RoundTimeLimit.F
	s.flagState = [2]int8{-2, -2}
	for _, p := range s.players {
		if p != nil {
			s.killPlayer(p)
			p.TimeToSpawn = 0
			p.TimePlayedCurGame = 0
		}
	}
}

// queryNextMap is Server::queryNextMap (Server.cpp:363).
func (s *Server) queryNextMap() string {
	current := s.mapName
	for i := len(s.mapInfos); i < len(s.mapList); i++ {
		// the original loads each new map as the game's map to measure it, reseeding rand
		s.srand()
		area := 0
		if m, err := s.lib.Load(s.mapList[i]); err == nil {
			area = m.Area()
		}
		s.mapInfos = append(s.mapInfos, mapInfo{name: s.mapList[i], area: area, lastPlayed: 1000000000})
	}
	best := -1
	for i := range s.mapInfos {
		mi := &s.mapInfos[i]
		if strings.EqualFold(mi.name, current) {
			mi.lastPlayed = 1
		} else {
			mi.lastPlayed++
		}
		if s.suitable(mi) {
			if best == -1 || mi.lastPlayed > s.mapInfos[best].lastPlayed {
				best = i
			}
		}
	}
	if best >= 0 {
		s.mapInfos[best].lastPlayed = 0
		return s.mapInfos[best].name
	}
	if len(s.mapInfos) == 0 {
		return current
	}
	n := int32(len(s.mapInfos) - 1)
	index := s.rand.Range(0, n)
	s.mapInfos[index].lastPlayed = 0
	return s.mapInfos[index].name
}

// suitable is Server::filterMapFromRotation (Server.cpp:1394).
func (s *Server) suitable(mi *mapInfo) bool {
	n := 0
	for _, p := range s.players {
		if p != nil && (p.TeamID == proto.TeamBlue || p.TeamID == proto.TeamRed) {
			n++
		}
	}
	if n < 2 {
		n = 2
	}
	tiles := float32(mi.area) / float32(n)
	min, max := s.SV.MinTilesPerBabo.F, s.SV.MaxTilesPerBabo.F
	return !(tiles < min || (tiles > max && max != 0))
}

// spawnPlayer is Game::spawnPlayer (GameSpawn.cpp:144).
func (s *Server) spawnPlayer(id int) bool {
	p := s.players[id]
	if p == nil || s.m == nil || (p.TeamID != proto.TeamBlue && p.TeamID != proto.TeamRed) {
		return false
	}
	spawns := s.m.DMSpawns
	if len(spawns) == 0 {
		return false
	}
	at := func(i int) bvmath.Vec3 { return bvmath.Vec3{spawns[i][0], spawns[i][1], .25} }
	switch s.gameType {
	case proto.GameTypeSND: // "Champion": a slot of five spawns per player
		best := 0
		if p.SpawnSlot != -1 {
			best = p.SpawnSlot
		} else {
			for i := 0; i < proto.MaxPlayer; i++ {
				used := false
				for j, o := range s.players {
					if o != nil && j != id && o.SpawnSlot == i && (o.TeamID == proto.TeamBlue || o.TeamID == proto.TeamRed) {
						used = true
					}
				}
				if !used {
					best = i
					p.SpawnSlot = best
					break
				}
			}
		}
		loc := best*5 + int(s.rand.Range(0, 5))
		if loc >= len(spawns) { // the original clamps only past size (an index of size is read past the end)
			loc = len(spawns) - 1
		}
		s.spawnAt(p, at(loc))
		return true
	case proto.GameTypeDM, proto.GameTypeTDM, proto.GameTypeCTF:
		var currentScore float32
		best := 0
		for i := range spawns {
			nearest := float32(100000)
			nb := 0
			for j, o := range s.players {
				if o == nil || j == id || o.Status != proto.StatusAlive {
					continue
				}
				if s.gameType != proto.GameTypeDM && o.TeamID == p.TeamID {
					continue
				}
				nb++
				if d := bvmath.DistanceSquared(spawns[i], o.CurrentCF.Position); d < nearest {
					nearest = d
				}
			}
			if nearest > currentScore {
				currentScore = nearest
				best = i
			}
			if nb == 0 {
				best = int(s.rand.Int() % int32(len(spawns)))
				break
			}
		}
		pos := at(best)
		if s.gameType == proto.GameTypeCTF && s.spawnType == 1 && s.SV.GameTimeLimit.F-s.gameTimeLeft < 10 {
			pos = s.m.FlagPodPos[p.TeamID] // ladder: the first 10 s on the own flag pod
		}
		s.spawnAt(p, pos)
		return true
	}
	return false
}

func (s *Server) spawnAt(p *Player, pos bvmath.Vec3) {
	p.spawn(pos, s.SV.TimeToSpawn.F, s.SV.SpawnImmunityTime.F)
}

// assignPlayerTeam is Game::assignPlayerTeam (Game.cpp:880).
func (s *Server) assignPlayerTeam(id int, requested int) int {
	p := s.players[id]
	if p == nil {
		return proto.TeamSpectator
	}
	p.SpawnSlot = -1
	if requested == proto.TeamAutoAssign {
		blue, red := 0, 0
		for i, o := range s.players {
			if o != nil && i != id {
				switch o.TeamID {
				case proto.TeamBlue:
					blue++
				case proto.TeamRed:
					red++
				}
			}
		}
		switch {
		case red > blue:
			requested = proto.TeamBlue
		case red < blue:
			requested = proto.TeamRed
		case s.blueScore < s.redScore:
			requested = proto.TeamBlue
		case s.blueScore > s.redScore:
			requested = proto.TeamRed
		default:
			if s.rand.Int()%2 == 0 {
				requested = proto.TeamBlue
			} else {
				requested = proto.TeamRed
			}
		}
	}
	// isApproved: without the account server every player is approved
	if requested < proto.TeamSpectator || requested > proto.TeamRed {
		requested = proto.TeamSpectator // the original stored any value; the client's arrays can't take it
	}
	if p.TeamID != requested {
		s.killPlayer(p)
		p.TimeToSpawn = s.SV.TimeToSpawn.F
		p.TeamID = requested
		s.log.Info("team change", "player", id, "name", p.Name, "team", requested)
	}
	p.TeamID = requested
	return requested
}

// Players is for the admin page.
func (s *Server) Players() []session.PlayerInfo {
	out := []session.PlayerInfo{}
	for i, p := range s.players {
		if p != nil {
			out = append(out, session.PlayerInfo{ID: i, NetID: p.BabonetID, Name: p.Name, Remote: p.IP,
				Team: p.TeamID, Score: int(p.Score), Ping: int(p.Ping) * 33})
		}
	}
	return out
}

// Close ends the game.
func (s *Server) Close() {}
