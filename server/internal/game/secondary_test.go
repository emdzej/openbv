package game

import (
	"testing"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
	"github.com/emdzej/openbv/server/internal/wire"
)

func frames(s *Server, n int) {
	for i := 0; i < n; i++ {
		s.gameUpdate()
	}
}

func lastMsg(log []sentMsg, typ uint16) any {
	for i := len(log) - 1; i >= 0; i-- {
		if log[i].typ == typ {
			return log[i].msg
		}
	}
	return nil
}

// A rocket goes up with the uniqueID the server gave it; with remote detonation (Pro) the second
// press doesn't fire another but sets the one in the air off where it is, normal zero.
func TestRocketRemoteDetonation(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponBazooka)
	s.SV.ServerType.I = serverTypePro
	s.SV.ZookaRemoteDet.B = true
	a.MfElapsedSinceLastShot = 5
	dir := bvmath.Normalize(b.CurrentCF.Position.Sub(a.CurrentCF.Position))
	// fired along the open line to b, who is dead (playerInRadius skips him)
	b.Status = proto.StatusDead
	m := proto.PlayerProjectileMsg{PlayerID: int8(a.ID), WeaponID: proto.WeaponBazooka, ProjectileType: proto.ProjectileRocket,
		Position: shortPos(a.CurrentCF.Position), Vel: charVel(dir)}
	s.playerProjectile(a, &m)
	echo, ok := lastMsg(*log, proto.ClsvSvclPlayerProjectile).(*proto.PlayerProjectileMsg)
	if !ok || echo.UniqueID != s.uniqueProjectileID || !a.RocketInAir || len(s.projectiles) != 1 {
		t.Fatalf("rocket not created: echo %+v inAir %v projectiles %d", echo, a.RocketInAir, len(s.projectiles))
	}
	frames(s, 9) // .3 s: past the .25 s of remote detonation, about a metre of the 4 m open line
	if len(s.projectiles) != 1 || s.projectiles[0].NeedToBeDeleted {
		t.Fatal("the rocket went off on its own")
	}
	at := s.projectiles[0].CurrentCF.Position
	m2 := m
	s.playerProjectile(a, &m2)
	if len(s.projectiles) != 1 || !a.DetonateRocket {
		t.Fatalf("second press: projectiles %d detonate %v", len(s.projectiles), a.DetonateRocket)
	}
	before := count(*log, proto.SvclExplosion)
	frames(s, 1)
	if count(*log, proto.SvclExplosion) != before+1 {
		t.Fatal("no explosion after the detonation press")
	}
	ex := lastMsg(*log, proto.SvclExplosion).(*proto.SvclExplosionMsg)
	if ex.Normal != [3]float32{} || ex.Radius != s.SV.ZookaRadius.F || int(ex.PlayerID) != a.ID {
		t.Fatalf("explosion %+v, want zero normal, radius %v, from %d", ex, s.SV.ZookaRadius.F, a.ID)
	}
	if bvmath.Distance(bvmath.Vec3(ex.Position), at) > 1 {
		t.Fatalf("exploded at %v, the rocket was at %v", ex.Position, at)
	}
	if a.RocketInAir || a.DetonateRocket {
		t.Fatal("flags not cleared")
	}
}

// Knives: 0.6 to whoever is within a metre (not the owner), and the message echoed to all.
func TestKnives(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSMG)
	b.CurrentCF.Position = a.CurrentCF.Position.Add(bvmath.Vec3{.7, 0, 0})
	lifeA, lifeB := a.Life, b.Life
	s.recvPacket(a.BabonetID, wire.Packet{Type: proto.ClsvSvclPlayerShootMelee, Data: proto.Encode(&proto.PlayerShootMeleeMsg{PlayerID: int8(a.ID)})})
	if a.Life != lifeA || b.Life >= lifeB || count(*log, proto.ClsvSvclPlayerShootMelee) != 1 {
		t.Fatalf("lives %v -> %v, %v -> %v; echoes %d", lifeA, a.Life, lifeB, b.Life, count(*log, proto.ClsvSvclPlayerShootMelee))
	}
	if a.Melee.CurrentFireDelay != a.Melee.FireDelay || a.FireFrameDelay != 2 {
		t.Fatalf("fire delay %v frame delay %d", a.Melee.CurrentFireDelay, a.FireFrameDelay)
	}
}

// The nuke: a bot at the owner's feet (NET_SVCL_CREATE_MINIBOT, x10), 30*sv_nukeTimer frames later
// the explosion there (sv_nukeRadius, the owner's) and the bot is gone.
func TestNukeBot(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSMG)
	a.switchMeleeWeapon(&s.weapons, proto.WeaponNuclear, true)
	b.CurrentCF.Position = a.CurrentCF.Position.Add(bvmath.Vec3{2, 0, 0})
	b.NetCF0.Position, b.NetCF1.Position = b.CurrentCF.Position, b.CurrentCF.Position
	s.shootMeleeSV(a)
	cm, ok := lastMsg(*log, proto.SvclCreateMinibot).(*proto.SvclCreateMinibotMsg)
	if !ok || a.Minibot == nil || !a.Minibot.NukeBot || cm.Position[2] != 1 /* .15*10 */ {
		t.Fatalf("no nuke bot: %+v %+v", cm, a.Minibot)
	}
	botAt := a.Minibot.CurrentCF.Position
	n := int(30 * s.SV.NukeTimer.F)
	frames(s, n-1)
	if count(*log, proto.SvclExplosion) != 0 || a.Minibot == nil {
		t.Fatalf("went off early")
	}
	frames(s, 1)
	ex, ok := lastMsg(*log, proto.SvclExplosion).(*proto.SvclExplosionMsg)
	if !ok || ex.Radius != s.SV.NukeRadius.F || int(ex.PlayerID) != a.ID || bvmath.Vec3(ex.Position) != botAt {
		t.Fatalf("explosion %+v, want at %v radius %v", ex, botAt, s.SV.NukeRadius.F)
	}
	if a.Minibot != nil {
		t.Fatal("the bot is still there")
	}
	if b.Status != proto.StatusDead || a.Status != proto.StatusDead {
		t.Fatalf("8.0 damage within 6 m should kill both: a %d b %d", a.Status, b.Status)
	}
}

// The minibot: a turret a metre towards the aim, shooting (weapon 100) the enemy within 6 m, its
// coord frames to everyone, gone after 5 s.
func TestMinibot(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSMG)
	a.switchMeleeWeapon(&s.weapons, proto.WeaponMinibot, true)
	s.shootMeleeSV(a)
	if a.Minibot == nil || a.Minibot.NukeBot || count(*log, proto.SvclCreateMinibot) != 1 {
		t.Fatalf("no minibot")
	}
	shots := 0
	for i := 0; i < 30 && b.Status == proto.StatusAlive; i++ {
		frames(s, 1)
		for _, m := range *log {
			if ps, ok := m.msg.(*proto.SvclPlayerShootMsg); ok && ps.WeaponID == minibotWeapon {
				shots++
			}
		}
		*log = (*log)[:0]
	}
	if shots == 0 {
		t.Fatal("the minibot never fired at b (4 m away, in sight)")
	}
	// coord frames: both players get the bot's frame
	sent := map[int32]int{}
	s.sent = func(dest int32, typ uint16, msg any) {
		if typ == proto.SvclMinibotCoordFrame {
			sent[dest]++
		}
	}
	for _, p := range s.players {
		if p != nil {
			p.SendPosFrame = 1000
		}
	}
	s.sendCoordFrames()
	if b.Status == proto.StatusAlive && (sent[int32(a.BabonetID)] != 1 || sent[int32(b.BabonetID)] != 1) {
		t.Fatalf("minibot frames %v, want one to each", sent)
	}
	frames(s, 6*30)
	if a.Minibot != nil {
		t.Fatal("the minibot should be gone after 5 s")
	}
}

// The shield: two seconds of protection.
func TestShield(t *testing.T) {
	s, a, _, _ := duel(t, proto.WeaponSMG)
	a.switchMeleeWeapon(&s.weapons, proto.WeaponShield, true)
	s.shootMeleeSV(a)
	if a.Protection != 2 {
		t.Fatalf("protection %v", a.Protection)
	}
}

// A molotov thrown at a wall breaks there: the sound, and two flames broadcast by the server.
func TestMolotovOnAWall(t *testing.T) {
	s, a, b, log := duel(t, proto.WeaponSMG)
	a.MolotovLeft = 1
	// thrown away from b, towards whatever wall is behind a
	dir := bvmath.Normalize(a.CurrentCF.Position.Sub(b.CurrentCF.Position))
	m := proto.PlayerProjectileMsg{PlayerID: int8(a.ID), WeaponID: proto.WeaponMolotov, ProjectileType: proto.ProjectileMolotov,
		Position: shortPos(a.CurrentCF.Position), Vel: charVel(dir)}
	s.playerProjectile(a, &m)
	if a.MolotovLeft != 0 || len(s.projectiles) != 1 {
		t.Fatalf("molotov not thrown: left %d projectiles %d", a.MolotovLeft, len(s.projectiles))
	}
	for i := 0; i < 120 && count(*log, proto.SvclPlaySound) == 0; i++ {
		frames(s, 1)
	}
	flames := 0
	for _, e := range *log {
		if pm, ok := e.msg.(*proto.PlayerProjectileMsg); ok && pm.ProjectileType == proto.ProjectileFlame {
			flames++
		}
	}
	if count(*log, proto.SvclPlaySound) != 1 || flames != 2 {
		t.Fatalf("sounds %d flames %d, want 1 and 2", count(*log, proto.SvclPlaySound), flames)
	}
}
