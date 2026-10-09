package game

import (
	"testing"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
)

type sentMsg struct {
	dest int32
	typ  uint16
	msg  any
}

// a DM server with two alive players facing each other across open floor, no spawn immunity
func duel(t *testing.T, weaponID int) (*Server, *Player, *Player, *[]sentMsg) {
	s := newTestServer(t, proto.GameTypeDM, "DM-MiniArena")
	var log []sentMsg
	s.sent = func(dest int32, typ uint16, msg any) { log = append(log, sentMsg{dest, typ, msg}) }
	a, b := s.openPair(t, 4)
	for i, at := range []bvmath.Vec3{a, b} {
		p := newPlayer(i, uint32(i+1), 5)
		p.TeamID = proto.TeamBlue + i
		p.NextSpawnWeapon = weaponID
		p.NextMeleeWeapon = proto.WeaponKnives
		p.spawn(at, 5, 0, &s.weapons)
		p.NetCF0.Position, p.NetCF1.Position = at, at
		p.CurrentCF.MousePosOnMap = b
		s.players[i] = p
	}
	return s, s.players[0], s.players[1], &log
}

// openPair finds two floor points dist apart along x with nothing in between.
func (s *Server) openPair(t *testing.T, dist float32) (bvmath.Vec3, bvmath.Vec3) {
	for y := 1; y < s.m.Height-1; y++ {
		for x := 1; x+int(dist)+1 < s.m.Width; x++ {
			a := bvmath.Vec3{float32(x) + .5, float32(y) + .5, .25}
			b := a.Add(bvmath.Vec3{dist, 0, 0})
			free := true
			for k := 0; k <= int(dist)+1 && free; k++ {
				if !s.m.Cells[y*s.m.Width+x+k].Passable {
					free = false
				}
			}
			p2 := b
			var n bvmath.Vec3
			if free && !s.m.RayTest(a, &p2, &n) {
				return a, b
			}
		}
	}
	t.Fatal("no open floor on the map")
	return bvmath.Vec3{}, bvmath.Vec3{}
}

func shootAt(s *Server, p, target *Player) {
	dir := bvmath.Normalize(target.CurrentCF.Position.Sub(p.CurrentCF.Position))
	s.playerShoot(p, &proto.ClsvPlayerShootMsg{
		PlayerID: int8(p.ID), WeaponID: int8(p.Weapon.ID),
		P1: shortPos(p.CurrentCF.Position), P2: shortPos(dir),
	})
}

func count(log []sentMsg, typ uint16) int {
	n := 0
	for _, m := range log {
		if m.typ == typ {
			n++
		}
	}
	return n
}

// The sniper (no spread) kills in two shots of two bullets at 0.30, each bullet a PLAYER_HIT with the
// life left; the death drops a life pack, the weapon and the two grenades; the scores move; and the
// first projectile update turns the server Pro (the sv_serverType = 1 quirk).
func TestSniperKill(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSniper)
	for shot := 0; shot < 10 && b.Status == proto.StatusAlive; shot++ {
		for f := 0; f < 61; f++ { // past the sniper's 2 s
			s.gameUpdate()
		}
		shootAt(s, a, b)
	}
	if b.Status != proto.StatusDead {
		t.Fatalf("the victim lives: life %v", b.Life)
	}
	var lives []float32
	for _, m := range *log {
		if m.typ == proto.SvclPlayerHit {
			h := m.msg.(*proto.SvclPlayerHitMsg)
			if h.PlayerID != 1 || h.FromID != 0 || h.WeaponID != proto.WeaponSniper || h.Vel != [3]int8{0, 0, 1} {
				t.Fatalf("hit %+v", h)
			}
			lives = append(lives, h.Damage)
		}
	}
	// 1 - .3 - .3 - .3 - .3: Normal rules throughout (no projectile has been updated yet)
	if len(lives) != 4 || lives[3] > fltEpsilon {
		t.Fatalf("life after each bullet: %v", lives)
	}
	if a.Score != 1 || a.Kills != 1 || b.Deaths != 1 || a.Deaths != 0 {
		t.Fatalf("scores: killer %d/%d, victim deaths %d", a.Score, a.Kills, b.Deaths)
	}
	if b.CurrentCF.Position != (bvmath.Vec3{-999, -999, 0}) {
		t.Fatalf("the dead are at %v", b.CurrentCF.Position)
	}
	var drops []int
	for _, m := range *log {
		if m.typ == proto.ClsvSvclPlayerProjectile {
			drops = append(drops, int(m.msg.(*proto.PlayerProjectileMsg).ProjectileType))
		}
	}
	want := []int{proto.ProjectileLifePack, proto.ProjectileDropedWeapon, proto.ProjectileDropedGrenade, proto.ProjectileDropedGrenade}
	if len(drops) != len(want) {
		t.Fatalf("drops %v", drops)
	}
	for i := range want {
		if drops[i] != want[i] {
			t.Fatalf("drops %v", drops)
		}
	}
	if len(s.projectiles) != 4 || s.projectiles[1].FromID != proto.WeaponSniper {
		t.Fatalf("projectiles %d", len(s.projectiles))
	}
	if s.SV.ServerType.I != 0 {
		t.Fatal("Pro before any projectile update")
	}
	s.gameUpdate()
	if s.SV.ServerType.I != 1 || s.weapons[proto.WeaponBazooka].Damage != s.SV.ZookaDamage.F {
		t.Fatal("the sv_serverType = 1 quirk didn't happen")
	}
}

// Drops fall and settle; the killer walking over the life pack heals and it's deleted one frame later.
func TestLifePackPickup(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSniper)
	a.Life = .4
	b.Life = .1
	shootAt(s, a, b)
	if b.Status != proto.StatusDead {
		t.Fatal("not dead")
	}
	for f := 0; f < 60; f++ {
		s.gameUpdate()
	}
	pack := s.projectiles[0]
	if pack.Type != proto.ProjectileLifePack || pack.CurrentCF.Vel != (bvmath.Vec3{}) {
		t.Fatalf("the pack hasn't settled: %+v", pack.CurrentCF)
	}
	*log = nil
	a.CurrentCF.Position = bvmath.Vec3{pack.CurrentCF.Position[0], pack.CurrentCF.Position[1], .25}
	a.NetCF0.Position, a.NetCF1.Position = a.CurrentCF.Position, a.CurrentCF.Position
	s.gameUpdate()
	if a.Life != .9 {
		t.Fatalf("life %v", a.Life)
	}
	if count(*log, proto.SvclPickupItem) != 1 || count(*log, proto.SvclDeleteProjectile) != 0 {
		t.Fatal("pickup")
	}
	s.gameUpdate()
	if count(*log, proto.SvclDeleteProjectile) != 1 {
		t.Fatal("the pack isn't deleted the frame after")
	}
}

// The rate limits: an SMG fires every 0.1 s (0.051 slack), a sniper's bullets come in a burst of at
// most 3, a fourth in the same window is dropped.
func TestFireRateLimits(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSMG)
	b.ImmuneTime = 100
	shoot := func() int {
		before := count(*log, proto.SvclPlayerShoot)
		shootAt(s, a, b)
		return count(*log, proto.SvclPlayerShoot) - before
	}
	if shoot() != 1 || shoot() != 0 {
		t.Fatal("two SMG shots in one frame")
	}
	s.gameUpdate() // 0.033 + 0.051 < 0.1
	if shoot() != 0 {
		t.Fatal("an SMG shot after one frame")
	}
	s.gameUpdate()
	if shoot() != 1 {
		t.Fatal("no SMG shot after two frames")
	}

	s, a, b, log = duel(t, proto.WeaponSniper)
	b.ImmuneTime = 100
	n := 0
	for i := 0; i < 5; i++ {
		shootAt(s, a, b)
	}
	for _, m := range *log {
		if m.typ == proto.SvclPlayerShoot {
			n++
		}
	}
	if n != 3*2 { // three accepted shots of two bullets each (camPosZ < 10)
		t.Fatalf("sniper bullets %d", n)
	}
}

// A shot from inside a wall starts at the wall; one aimed across a wall stops there.
func TestShotStopsAtWall(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSniper)
	// find a wall cell next to open floor and stand on the other side
	for y := 1; y < s.m.Height-1; y++ {
		for x := 2; x < s.m.Width-2; x++ {
			c := s.m.Cells[y*s.m.Width+x]
			if !c.Passable && s.m.Cells[y*s.m.Width+x-1].Passable && s.m.Cells[y*s.m.Width+x+1].Passable {
				a.CurrentCF.Position = bvmath.Vec3{float32(x) - .5, float32(y) + .5, .25}
				b.CurrentCF.Position = bvmath.Vec3{float32(x) + 1.5, float32(y) + .5, .25}
				shootAt(s, a, b)
				for _, m := range *log {
					if m.typ == proto.SvclPlayerShoot {
						sh := m.msg.(*proto.SvclPlayerShootMsg)
						if sh.HitPlayerID != -1 || sh.P2[0] > int16(x*100) {
							t.Fatalf("through the wall: %+v", sh)
						}
						if sh.Normal != [3]int8{-120, 0, 0} {
							t.Fatalf("normal %v", sh.Normal)
						}
					}
				}
				if b.Life != 1 {
					t.Fatal("hit through a wall")
				}
				return
			}
		}
	}
	t.Skip("no wall with floor on both sides")
}

// DM ends when someone reaches sv_scoreLimit: GAME_DONT_SHOW, the next map in 10 s, and no more
// player updates or spawns meanwhile.
func TestDMScoreLimitEndsTheRound(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSniper)
	s.SV.ScoreLimit.I = 1
	b.Life = .1
	shootAt(s, a, b)
	if a.Score != 1 {
		t.Fatal("no kill")
	}
	s.checkRoundEnd()
	if s.roundState != proto.GameDontShow || s.changeMapDelay != 10 {
		t.Fatalf("round state %d, map change in %v", s.roundState, s.changeMapDelay)
	}
	found := false
	for _, m := range *log {
		if m.typ == proto.SvclGameState && m.msg.(*proto.SvclRoundStateMsg).NewState == proto.GameDontShow {
			found = true
		}
	}
	if !found {
		t.Fatal("no GAME_STATE")
	}
}

// A pickup request over a dropped weapon: the player drops his own and takes it.
func TestWeaponPickup(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSniper)
	b.NextSpawnWeapon = proto.WeaponSMG
	b.switchWeapon(&s.weapons, proto.WeaponPhotonRifle, false)
	b.Life = .1
	shootAt(s, a, b)
	for f := 0; f < 60; f++ {
		s.gameUpdate()
	}
	var gun *projectile
	for _, p := range s.projectiles {
		if p.Type == proto.ProjectileDropedWeapon {
			gun = p
		}
	}
	if gun == nil || gun.FromID != proto.WeaponPhotonRifle {
		t.Fatal("no dropped photon rifle")
	}
	a.CurrentCF.Position = bvmath.Vec3{gun.CurrentCF.Position[0] + .3, gun.CurrentCF.Position[1], .25}
	*log = nil
	s.pickupRequest(a)
	if a.Weapon.ID != proto.WeaponPhotonRifle {
		t.Fatalf("weapon %d", a.Weapon.ID)
	}
	var item *proto.SvclPickupItemMsg
	drops := 0
	for _, m := range *log {
		switch m.typ {
		case proto.SvclPickupItem:
			item = m.msg.(*proto.SvclPickupItemMsg)
		case proto.ClsvSvclPlayerProjectile:
			if m.msg.(*proto.PlayerProjectileMsg).WeaponID == proto.WeaponSniper {
				drops++
			}
		}
	}
	if item == nil || item.ItemType != itemWeapon || item.ItemFlag != proto.WeaponPhotonRifle || drops != 1 {
		t.Fatalf("pickup %+v, sniper dropped %d", item, drops)
	}
}
