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

func refVec(v reftest.V) bvmath.Vec3 { return bvmath.Vec3(v.Vec()) }

func bits(v bvmath.Vec3) [3]uint32 {
	return [3]uint32{fbits(v[0]), fbits(v[1]), fbits(v[2])}
}

// The projectiles against the original Projectile::update (reftest/cpp/proj_main.cpp): 700 random
// maps, players and projectiles of every kind, run frame by frame; every message (bytes), every
// radiusHit call, the projectile's state, the players' flags and the rand() state must match.
func TestProjectilesAgainstOriginal(t *testing.T) {
	g := loadProjectiles(t)
	cases := g.Proj
	frames := 0
	for ci, c := range cases {
		m := &bvmap.Map{Width: c.Width, Height: c.Height, Cells: make([]bvmap.Cell, len(c.Cells))}
		for i, h := range c.Cells {
			if h < 0 {
				m.Cells[i] = bvmap.Cell{Passable: true}
			} else {
				m.Cells[i] = bvmap.Cell{Height: uint8(h)}
			}
		}
		s := &Server{SV: cvar.NewSV(), net: bbnet.New(), m: m, rand: bvmath.NewRand(c.Seed), weapons: defaultWeapons(), log: quiet}
		s.SV.ZookaRemoteDet.B = c.SV.RemoteDet
		s.SV.ServerType.I = int32(c.SV.ServerType)
		s.SV.ZookaRadius.F = c.SV.ZookaRadius.Float()
		s.SV.ZookaDamage.F = c.SV.ZookaDamage.Float()
		s.weapons[proto.WeaponBazooka].Damage = .75
		var got []string
		s.sent = func(dest int32, typ uint16, msg any) {
			got = append(got, fmt.Sprintf("send %d %s", typ, hex.EncodeToString(proto.Encode(msg))))
		}
		s.radiusHitHook = func(pos bvmath.Vec3, radius float32, fromID, weaponID int, sameDmg bool) {
			got = append(got, fmt.Sprintf("radius %v %d %d %d %v", bits(pos), math.Float32bits(radius), int8(fromID), int8(weaponID), sameDmg))
		}
		for i, rp := range c.Players {
			p := newPlayer(i, uint32(i+1), 5)
			p.Status = rp.Status
			p.GrenadeLeft = rp.Grenades
			p.Life = rp.Life.Float()
			p.RocketInAir = rp.RocketInAir
			p.DetonateRocket = rp.Detonate
			p.CurrentCF.Position = refVec(rp.Pos)
			s.players[i] = p
		}
		s.uniqueProjectileID = 100
		p := newProjectile(refVec(c.Pos), refVec(c.Vel), c.From, c.Type, 100)
		if c.Type == proto.ProjectileFlame {
			p.StickToPlayer = c.Stick
			p.StickFor = c.StickFor.Float()
			p.TimeSinceThrown = c.Thrown.Float()
			p.MovementLock = c.Lock
			p.DamageTime = int32(c.DamageTime)
		}
		p.ProjectileID = c.Index
		for fi, want := range c.Frames {
			if fi == c.KillAt {
				k := s.players[c.KillWho]
				k.Status = proto.StatusDead
				k.CurrentCF.Position = bvmath.Vec3{-999, -999, 0}
			}
			got = got[:0]
			s.updateProjectile(p)
			frames++
			where := fmt.Sprintf("case %d (type %d) frame %d", ci, c.Type, fi)
			var wantEv []string
			for _, e := range want.Ev {
				if e.Send != nil {
					wantEv = append(wantEv, fmt.Sprintf("send %d %s", *e.Send, e.B))
				} else {
					wantEv = append(wantEv, fmt.Sprintf("radius %v %d %d %d %v", bits(refVec(*e.Radius)), uint32(e.R), int8(e.From), int8(e.W), e.Same))
				}
			}
			if fmt.Sprint(got) != fmt.Sprint(wantEv) {
				t.Fatalf("%s: events\n got  %v\n want %v", where, got, wantEv)
			}
			if bits(p.CurrentCF.Position) != bits(refVec(want.Pos)) || bits(p.CurrentCF.Vel) != bits(refVec(want.Vel)) {
				t.Fatalf("%s: position %v vel %v, want %v %v", where, p.CurrentCF.Position, p.CurrentCF.Vel, refVec(want.Pos), refVec(want.Vel))
			}
			if p.NeedToBeDeleted != want.Del || p.MovementLock != want.Lock || p.StickToPlayer != want.Stick ||
				math.Float32bits(p.StickFor) != uint32(want.StickFor) || math.Float32bits(p.Duration) != uint32(want.Duration) ||
				int(p.DamageTime) != want.DamageTime {
				t.Fatalf("%s: state del %v lock %v stick %d stickFor %v duration %v damageTime %d, want %v %v %d %v %v %d", where,
					p.NeedToBeDeleted, p.MovementLock, p.StickToPlayer, p.StickFor, p.Duration, p.DamageTime,
					want.Del, want.Lock, want.Stick, want.StickFor.Float(), want.Duration.Float(), want.DamageTime)
			}
			if int(s.SV.ServerType.I) != want.ServerType || math.Float32bits(s.weapons[proto.WeaponBazooka].Damage) != uint32(want.ZookaDamage) {
				t.Fatalf("%s: serverType %d bazooka %v, want %d %v", where, s.SV.ServerType.I, s.weapons[proto.WeaponBazooka].Damage, want.ServerType, want.ZookaDamage.Float())
			}
			for i, fl := range want.Flags {
				q := s.players[i]
				if q.RocketInAir != fl[0].(bool) || q.DetonateRocket != fl[1].(bool) || q.GrenadeLeft != int(fl[2].(float64)) ||
					math.Float32bits(q.Life) != uint32(fl[3].(float64)) {
					t.Fatalf("%s: player %d flags %v %v %d %v, want %v", where, i, q.RocketInAir, q.DetonateRocket, q.GrenadeLeft, q.Life, fl)
				}
			}
			if s.rand.State() != want.Rand {
				t.Fatalf("%s: rand state %d, want %d (a different number of rand() calls)", where, s.rand.State(), want.Rand)
			}
		}
	}
	t.Logf("%d cases, %d frames", len(cases), frames)
}

func loadProjectiles(t *testing.T) *reftest.Projectiles {
	t.Helper()
	g, err := reftest.LoadProjectiles()
	if err != nil {
		t.Fatalf("projectile golden data: %v (regenerate with reftest/cpp/build.sh)", err)
	}
	return g
}

func refMap(r reftest.RefMap) *bvmap.Map {
	m := &bvmap.Map{Width: r.Width, Height: r.Height, Cells: make([]bvmap.Cell, len(r.Cells))}
	for i, h := range r.Cells {
		if h < 0 {
			m.Cells[i] = bvmap.Cell{Passable: true}
		} else {
			m.Cells[i] = bvmap.Cell{Height: uint8(h)}
		}
	}
	return m
}

func hitEvents(ev []reftest.HitEvent) []string {
	var out []string
	for _, e := range ev {
		if e.Hit != nil {
			out = append(out, fmt.Sprintf("hit %d %d %d %d", *e.Hit, e.W, e.From, uint32(e.Damage)))
		} else {
			out = append(out, fmt.Sprintf("send %d %s", *e.Send, e.B))
		}
	}
	return out
}

// recordingServer is a bare server on m whose hits and messages go to *got.
func recordingServer(m *bvmap.Map, seed uint32, got *[]string) *Server {
	s := &Server{SV: cvar.NewSV(), net: bbnet.New(), m: m, rand: bvmath.NewRand(seed), weapons: defaultWeapons(), log: quiet}
	s.sent = func(dest int32, typ uint16, msg any) {
		*got = append(*got, fmt.Sprintf("send %d %s", typ, hex.EncodeToString(proto.Encode(msg))))
	}
	s.hitSVHook = func(v *Player, weaponID int, from *Player, damage float32) {
		f := -1
		if from != nil {
			f = from.ID
		}
		*got = append(*got, fmt.Sprintf("hit %d %d %d %d", v.ID, weaponID, f, math.Float32bits(damage)))
	}
	return s
}

// The minibot against the original CMiniBot::Think and Game::shootMinibotSV: whom it aims at, when
// it fires, the spread (rand() calls), the hit, the message.
func TestMinibotAgainstOriginal(t *testing.T) {
	g := loadProjectiles(t)
	shots := 0
	for ci, c := range g.Minibot {
		var got []string
		s := recordingServer(refMap(c.RefMap), c.Seed, &got)
		s.gameType = c.GameType
		for i, rp := range c.Players {
			p := newPlayer(i, uint32(i+1), 5)
			p.Status = rp.Status
			p.TeamID = rp.Team
			p.CurrentCF.Position = refVec(rp.Pos)
			s.players[i] = p
		}
		b := newMinibot(refVec(c.Bot), refVec(c.Bot), c.Nuke)
		b.FireRate = c.FireRate.Float()
		for fi, want := range c.Frames {
			got = got[:0]
			s.minibotThink(s.players[0], b, delay)
			where := fmt.Sprintf("case %d frame %d", ci, fi)
			if w := hitEvents(want.Ev); fmt.Sprint(got) != fmt.Sprint(w) {
				t.Fatalf("%s: events\n got  %v\n want %v", where, got, w)
			}
			if bits(b.CurrentCF.MousePosOnMap) != bits(refVec(want.Mouse)) || math.Float32bits(b.FireRate) != uint32(want.FireRate) {
				t.Fatalf("%s: aim %v fireRate %v, want %v %v", where, b.CurrentCF.MousePosOnMap, b.FireRate, refVec(want.Mouse), want.FireRate.Float())
			}
			if s.rand.State() != want.Rand {
				t.Fatalf("%s: rand state %d, want %d", where, s.rand.State(), want.Rand)
			}
			for _, e := range want.Ev {
				if e.Send != nil {
					shots++
				}
			}
		}
	}
	t.Logf("%d minibot cases, %d shots", len(g.Minibot), shots)
}

// radiusHit against the original Game::radiusHit: who is hit, in what order, for how much.
func TestRadiusHitAgainstOriginal(t *testing.T) {
	g := loadProjectiles(t)
	hits := 0
	for ci, c := range g.Radius {
		var got []string
		s := recordingServer(refMap(c.RefMap), 1, &got)
		s.weapons[c.Weapon].Damage = c.Damage.Float()
		for i, rp := range c.Players {
			p := newPlayer(i, uint32(i+1), 5)
			p.Status = rp.Status
			p.CurrentCF.Position = refVec(rp.Pos)
			s.players[i] = p
		}
		s.radiusHit(refVec(c.Pos), c.R.Float(), c.From, c.Weapon, c.Same)
		if w := hitEvents(c.Ev); fmt.Sprint(got) != fmt.Sprint(w) {
			t.Fatalf("case %d: hits\n got  %v\n want %v", ci, got, w)
		}
		hits += len(c.Ev)
	}
	t.Logf("%d radius cases, %d hits", len(g.Radius), hits)
}

// The minibot's wall collision against the original Map::performCollision and Map::collisionClip.
func TestCollisionAgainstOriginal(t *testing.T) {
	g := loadProjectiles(t)
	moved := 0
	for ci, c := range g.Collision {
		m := refMap(c.RefMap)
		last, pos, vel := refVec(c.Last), refVec(c.Pos), refVec(c.Vel)
		m.PerformCollision(&last, &pos, &vel, c.R.Float())
		if bits(pos) != bits(refVec(c.AfterPos)) || bits(vel) != bits(refVec(c.AfterVel)) || bits(last) != bits(refVec(c.AfterLast)) {
			t.Fatalf("case %d: performCollision -> %v %v %v, want %v %v %v", ci, pos, vel, last,
				refVec(c.AfterPos), refVec(c.AfterVel), refVec(c.AfterLast))
		}
		if bits(pos) != bits(refVec(c.Pos)) {
			moved++
		}
		m.CollisionClip(&pos, c.R.Float())
		if bits(pos) != bits(refVec(c.Clipped)) {
			t.Fatalf("case %d: collisionClip -> %v, want %v", ci, pos, refVec(c.Clipped))
		}
	}
	t.Logf("%d collision cases, %d pushed back", len(g.Collision), moved)
}

// fbits is a float's bits with every NaN the same: x86-64 and arm64 make NaNs with different signs and
// payloads (the golden data comes from whichever machine generated it), and a NaN is a NaN to the game.
func fbits(f float32) uint32 {
	if f != f {
		return 0x7fc00000
	}
	return math.Float32bits(f)
}
