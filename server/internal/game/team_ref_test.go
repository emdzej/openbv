package game

import (
	"encoding/hex"
	"fmt"
	"math"
	"testing"

	"github.com/emdzej/openbv/server/internal/bbnet"
	"github.com/emdzej/openbv/server/internal/bvmap"
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/cvar"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/reftest"
)

// The team modes against the original (reftest/cpp/team_main.cpp): updateCTF over scripted paths,
// the auto-balance block frame by frame, assignPlayerTeam, spawnPlayer for every game type and the
// "Champion" round reset. Every message (bytes), the players' team state, the flags, the scores and
// the rand() state must match.

func loadTeam(t *testing.T) *reftest.Team {
	t.Helper()
	g, err := reftest.LoadTeam()
	if err != nil {
		t.Fatal(err)
	}
	return g
}

// teamServer is a Server with the case's players, a map with its pods and spawns, and a recorder.
func teamServer(players []reftest.TeamPlayer, pods [2]reftest.V, spawns []reftest.V, seed uint32) (*Server, *[]string) {
	m := &bvmap.Map{Width: 22, Height: 22, Cells: make([]bvmap.Cell, 22*22)}
	m.FlagPodPos = [2]bvmath.Vec3{refVec(pods[0]), refVec(pods[1])}
	for _, sp := range spawns {
		m.DMSpawns = append(m.DMSpawns, refVec(sp))
	}
	s := &Server{SV: cvar.NewSV(), net: bbnet.New(), m: m, rand: bvmath.NewRand(seed), weapons: defaultWeapons(), log: quiet}
	s.flagState = [2]int8{-2, -2}
	for _, rp := range players {
		p := newPlayer(rp.ID, uint32(rp.ID+1), rp.TTS.Float())
		p.TeamID = rp.Team
		p.Status = rp.Status
		p.CurrentCF.Position = refVec(rp.Pos)
		p.TimePlayedCurGame = rp.Played.Float()
		p.Score, p.FlagAttempts, p.Returns = int32(rp.Score), int32(rp.FA), int32(rp.Ret)
		p.SpawnSlot = rp.Slot
		s.players[rp.ID] = p
	}
	var got []string
	s.sent = func(dest int32, typ uint16, msg any) {
		got = append(got, fmt.Sprintf("send %d %s", typ, hex.EncodeToString(proto.Encode(msg))))
	}
	return s, &got
}

func wantEvents(ev []reftest.Event) []string {
	var out []string
	for _, e := range ev {
		out = append(out, fmt.Sprintf("send %d %s", e.Send, e.B))
	}
	return out
}

// checkState compares the players, flags, scores and rand() with a recorded state.
func checkState(t *testing.T, where string, s *Server, want reftest.TeamState, flagPos bool) {
	t.Helper()
	i := 0
	for _, p := range s.players {
		if p == nil {
			continue
		}
		w := want.State[i]
		i++
		got := []any{p.TeamID, p.Status, int(p.Score), int(p.FlagAttempts), int(p.Returns), p.SpawnSlot, math.Float32bits(p.TimeToSpawn), bits(p.CurrentCF.Position)}
		wantV := []any{int(w[0].(float64)), int(w[1].(float64)), int(w[2].(float64)), int(w[3].(float64)), int(w[4].(float64)),
			int(w[5].(float64)), uint32(w[6].(float64)), [3]uint32{uint32(w[7].([]any)[0].(float64)), uint32(w[7].([]any)[1].(float64)), uint32(w[7].([]any)[2].(float64))}}
		if fmt.Sprint(got) != fmt.Sprint(wantV) {
			t.Fatalf("%s: player %d [team status score flagAttempts returns slot tts pos]\n got  %v\n want %v", where, p.ID, got, wantV)
		}
	}
	if int(s.flagState[0]) != want.FlagState[0] || int(s.flagState[1]) != want.FlagState[1] {
		t.Fatalf("%s: flags %v, want %v", where, s.flagState, want.FlagState)
	}
	if flagPos && (bits(s.flagPos[0]) != bits(refVec(want.FlagPos[0])) || bits(s.flagPos[1]) != bits(refVec(want.FlagPos[1]))) {
		t.Fatalf("%s: flag positions %v, want %v %v", where, s.flagPos, refVec(want.FlagPos[0]), refVec(want.FlagPos[1]))
	}
	if [4]int{int(s.blueScore), int(s.redScore), int(s.blueWin), int(s.redWin)} != want.Scores {
		t.Fatalf("%s: scores %d %d %d %d, want %v", where, s.blueScore, s.redScore, s.blueWin, s.redWin, want.Scores)
	}
	if s.rand.State() != want.Rand {
		t.Fatalf("%s: rand state %d, want %d", where, s.rand.State(), want.Rand)
	}
}

func TestCTFAgainstOriginal(t *testing.T) {
	g := loadTeam(t)
	events := 0
	for ci, c := range g.CTF {
		s, got := teamServer(c.Players, c.Pods, nil, 0)
		s.gameType = proto.GameTypeCTF
		s.flagState = [2]int8{int8(c.FlagState[0]), int8(c.FlagState[1])}
		s.flagPos = [2]bvmath.Vec3{refVec(c.FlagPos[0]), refVec(c.FlagPos[1])}
		s.blueWin, s.redWin = int32(c.Wins[0]), int32(c.Wins[1])
		s.rand = bvmath.NewRand(1) // the driver's rand() starts at msvcSeed 1, and updateCTF never calls it
		for fi, fr := range c.Frames {
			*got = (*got)[:0]
			j := 0
			for _, p := range s.players {
				if p == nil {
					continue
				}
				p.CurrentCF.Position = refVec(fr.Pos[j])
				j++
				for _, k := range fr.Kill {
					if k == p.ID {
						s.killPlayer(p)
					}
				}
			}
			s.updateCTF()
			where := fmt.Sprintf("ctf case %d frame %d", ci, fi)
			if fmt.Sprint(*got) != fmt.Sprint(wantEvents(fr.Ev)) {
				t.Fatalf("%s: events\n got  %v\n want %v", where, *got, wantEvents(fr.Ev))
			}
			events += len(fr.Ev)
			if fr.Full {
				checkState(t, where, s, fr.St, true)
			}
		}
	}
	t.Logf("%d cases, %d events", len(g.CTF), events)
}

func TestAutoBalanceAgainstOriginal(t *testing.T) {
	g := loadTeam(t)
	events := 0
	for ci, c := range g.Balance {
		s, got := teamServer(c.Players, [2]reftest.V{}, nil, 1)
		s.gameType = c.GameType
		s.SV.AutoBalance.B = c.AutoBalance
		s.SV.AutoBalanceTime.I = int32(c.Time)
		s.SV.TimeToSpawn.F = c.TTS.Float()
		s.autoBalanceTimer = c.Timer.Float()
		s.flagState = [2]int8{int8(c.FlagState[0]), int8(c.FlagState[1])}
		next := 0
		for fr := 0; fr < c.NFrames; fr++ {
			*got = (*got)[:0]
			s.updateTeamModesBalanceOnly()
			if next < len(c.Frames) && c.Frames[next].Frame == fr {
				w := c.Frames[next]
				next++
				where := fmt.Sprintf("balance case %d frame %d", ci, fr)
				if fmt.Sprint(*got) != fmt.Sprint(wantEvents(w.Ev)) {
					t.Fatalf("%s: events\n got  %v\n want %v", where, *got, wantEvents(w.Ev))
				}
				if math.Float32bits(s.autoBalanceTimer) != uint32(w.Timer) {
					t.Fatalf("%s: timer %v, want %v", where, s.autoBalanceTimer, w.Timer.Float())
				}
				checkState(t, where, s, w.St, false)
				events += len(w.Ev)
			} else if len(*got) != 0 {
				t.Fatalf("balance case %d frame %d: sent %v, the original sent nothing", ci, fr, *got)
			}
		}
	}
	t.Logf("%d cases, %d events", len(g.Balance), events)
}

// updateTeamModesBalanceOnly is updateTeamModes' auto-balance part (the driver runs that block alone).
func (s *Server) updateTeamModesBalanceOnly() {
	m := s.m
	s.m = nil // no CTF or Champion update
	s.updateTeamModes()
	s.m = m
}

func TestAssignPlayerTeamAgainstOriginal(t *testing.T) {
	g := loadTeam(t)
	for ci, c := range g.Assign {
		s, got := teamServer(c.Players, [2]reftest.V{}, nil, c.Seed)
		s.blueScore, s.redScore = int32(c.Scores[0]), int32(c.Scores[1])
		s.SV.TimeToSpawn.F = c.TTS.Float()
		s.flagState = [2]int8{int8(c.FlagState[0]), int8(c.FlagState[1])}
		ret := s.assignPlayerTeam(c.ID, c.Req)
		where := fmt.Sprintf("assign case %d (player %d asks %d)", ci, c.ID, c.Req)
		if ret != c.Ret {
			t.Fatalf("%s: returned %d, want %d", where, ret, c.Ret)
		}
		if fmt.Sprint(*got) != fmt.Sprint(wantEvents(c.Ev)) {
			t.Fatalf("%s: events\n got  %v\n want %v", where, *got, wantEvents(c.Ev))
		}
		checkState(t, where, s, c.St, true)
	}
	t.Logf("%d cases", len(g.Assign))
}

func TestSpawnPlayerAgainstOriginal(t *testing.T) {
	g := loadTeam(t)
	past := 0
	for ci, c := range g.Spawn {
		if c.Past {
			past++ // dm_spawns[size]: undefined in the original, clamped in Go (design/server.md §8.4)
			continue
		}
		s, _ := teamServer(c.Players, c.Pods, c.Spawns, c.Seed)
		s.gameType = c.GameType
		s.spawnType = c.SpawnType
		s.SV.GameTimeLimit.F = c.Limit.Float()
		s.gameTimeLeft = c.Left.Float()
		p := s.players[c.ID]
		before := p.CurrentCF.Position
		ok := s.spawnPlayer(c.ID)
		where := fmt.Sprintf("spawn case %d (type %d, %d spawns)", ci, c.GameType, len(c.Spawns))
		if ok != c.OK {
			t.Fatalf("%s: returned %v, want %v", where, ok, c.OK)
		}
		if c.Spawned {
			if bits(p.CurrentCF.Position) != bits(refVec(c.At)) {
				t.Fatalf("%s: at %v, want %v", where, p.CurrentCF.Position, refVec(c.At))
			}
		} else if p.CurrentCF.Position != before {
			t.Fatalf("%s: moved without spawning", where)
		}
		if p.SpawnSlot != c.Slot {
			t.Fatalf("%s: spawn slot %d, want %d", where, p.SpawnSlot, c.Slot)
		}
		if s.rand.State() != c.Rand {
			t.Fatalf("%s: rand state %d, want %d", where, s.rand.State(), c.Rand)
		}
	}
	t.Logf("%d cases (%d past the end, skipped)", len(g.Spawn), past)
}

func TestChampionAgainstOriginal(t *testing.T) {
	g := loadTeam(t)
	for ci, c := range g.Champion {
		s, got := teamServer(c.Players, [2]reftest.V{}, nil, 1)
		s.gameType = proto.GameTypeSND
		s.SV.RoundTimeLimit.F = c.Limit.Float()
		s.roundTimeLeft = c.Left.Float()
		s.updateChampion()
		where := fmt.Sprintf("champion case %d", ci)
		if math.Float32bits(s.roundTimeLeft) != uint32(c.After) {
			t.Fatalf("%s: round time %v, want %v", where, s.roundTimeLeft, c.After.Float())
		}
		if fmt.Sprint(*got) != fmt.Sprint(wantEvents(c.Ev)) {
			t.Fatalf("%s: events\n got  %v\n want %v", where, *got, wantEvents(c.Ev))
		}
		checkState(t, where, s, c.St, false)
	}
	t.Logf("%d cases", len(g.Champion))
}
