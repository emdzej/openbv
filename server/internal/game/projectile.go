package game

import (
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
)

// projectile is the server's Projectile (GameProjectile.cpp): rockets, grenades, molotovs and flames
// thrown by players, and the items a dying player drops (life pack, weapon, grenades).
type projectile struct {
	FromID          int // the thrower; for a dropped weapon, the weapon it is (Game::spawnProjectile)
	UniqueID        int32
	ProjectileID    int // its index in the vector, refreshed every frame
	Type            int
	CurrentCF       CoordFrame
	LastCF          CoordFrame
	Duration        float32
	TimeSinceThrown float32
	DamageTime      int32
	StickToPlayer   int
	StickFor        float32
	MovementLock    bool

	NeedToBeDeleted       bool
	ReallyNeedToBeDeleted bool
}

// newProjectile is Projectile::Projectile (GameProjectile.cpp:31): the per-type lifetime and the
// launch speed-up.
func newProjectile(position, vel bvmath.Vec3, fromID, typ int, uniqueID int32) *projectile {
	p := &projectile{FromID: fromID, UniqueID: uniqueID, Type: typ, StickToPlayer: -1}
	p.CurrentCF.Position = position
	p.CurrentCF.Vel = vel
	p.LastCF = p.CurrentCF
	switch typ {
	case proto.ProjectileRocket:
		p.Duration = 10
		dir := vel
		dir[2] = 0
		dir = bvmath.Normalize(dir)
		dotY := bvmath.Dot(bvmath.Vec3{0, 1, 0}, dir)
		dotX := bvmath.Dot(bvmath.Vec3{1, 0, 0}, dir)
		p.CurrentCF.Angle = float32(float32(acosf(dotY)) * bvmath.ToDegree)
		if dotX > 0 {
			p.CurrentCF.Angle = -p.CurrentCF.Angle
		}
		p.CurrentCF.Vel = p.CurrentCF.Vel.Scale(2.5)
	case proto.ProjectileGrenade:
		p.Duration = 2
		p.CurrentCF.Vel = p.CurrentCF.Vel.Scale(5)
		p.CurrentCF.Vel[2] += 5
	case proto.ProjectileMolotov:
		p.Duration = 10
		p.CurrentCF.Vel = p.CurrentCF.Vel.Scale(6)
		p.CurrentCF.Vel[2] += 2
	case proto.ProjectileLifePack:
		p.Duration = 20
	case proto.ProjectileDropedWeapon:
		p.Duration = 30
	case proto.ProjectileDropedGrenade:
		p.Duration = 25
	case proto.ProjectileFlame:
		p.Duration = 10
	}
	return p
}

// spawnProjectile is Game::spawnProjectile(…, imServer = true) (GameSpawn.cpp:430): the next
// uniqueID, the thrower's grenade or molotov count, and the projectile at the end of the vector.
// It writes the uniqueID into m, which the caller then broadcasts.
func (s *Server) spawnProjectile(m *proto.PlayerProjectileMsg) bool {
	s.uniqueProjectileID++
	m.UniqueID = s.uniqueProjectileID
	from := s.players[m.PlayerID]
	switch int(m.ProjectileType) {
	case proto.ProjectileGrenade:
		if from == nil || from.GrenadeLeft <= 0 {
			return false
		}
		from.GrenadeLeft--
	case proto.ProjectileMolotov:
		if from == nil || from.MolotovLeft <= 0 {
			return false
		}
		from.MolotovLeft--
	}
	pos := bvmath.Vec3{float32(m.Position[0]) / 100, float32(m.Position[1]) / 100, float32(m.Position[2]) / 100}
	vel := bvmath.Vec3{float32(m.Vel[0]) / 10, float32(m.Vel[1]) / 10, float32(m.Vel[2]) / 10}
	fromID := int(m.PlayerID)
	if int(m.ProjectileType) == proto.ProjectileDropedWeapon {
		fromID = int(m.WeaponID)
	}
	p := newProjectile(pos, vel, fromID, int(m.ProjectileType), s.uniqueProjectileID)
	s.projectiles = append(s.projectiles, p)
	p.ProjectileID = len(s.projectiles) - 1
	if p.Type == proto.ProjectileFlame {
		s.broadcast(proto.ClsvSvclPlayerProjectile, &proto.PlayerProjectileMsg{
			PlayerID: int8(p.FromID), ProjectileType: int8(p.Type), WeaponID: proto.WeaponMolotov,
			Position: shortPos(p.CurrentCF.Position), Vel: charVel(p.CurrentCF.Vel), UniqueID: p.UniqueID,
		})
	}
	return true
}

// updateProjectiles is the projectile loop of Game::update (Game.cpp:822): every projectile, every
// frame (round over or not); a deleted one stays one more frame and is updated once more, then goes
// with NET_SVCL_DELETE_PROJECTILE.
func (s *Server) updateProjectiles() {
	for i := 0; i < len(s.projectiles); i++ {
		p := s.projectiles[i]
		s.updateProjectile(p)
		p.ProjectileID = i
		if p.NeedToBeDeleted {
			if !p.ReallyNeedToBeDeleted {
				p.ReallyNeedToBeDeleted = true
				continue
			}
			s.projectiles = append(s.projectiles[:i], s.projectiles[i+1:]...)
			s.broadcast(proto.SvclDeleteProjectile, &proto.SvclDeleteProjectileMsg{ProjectileID: p.UniqueID})
			i--
		}
	}
}

// updateProjectile is Projectile::update (GameProjectile.cpp:234) on the server (design/server.md §5.6),
// in the original's order.
func (s *Server) updateProjectile(p *projectile) {
	p.LastCF = p.CurrentCF
	p.CurrentCF.FrameID++
	p.TimeSinceThrown += delay

	speed := p.CurrentCF.Vel.Length()
	if p.Type == proto.ProjectileRocket {
		if speed > 10 {
			p.CurrentCF.Vel = p.CurrentCF.Vel.Div(speed)
			speed = 10
			p.CurrentCF.Vel = p.CurrentCF.Vel.Scale(speed)
		}
		p.CurrentCF.Position = p.CurrentCF.Position.Add(p.CurrentCF.Vel.Scale(delay))
		// the rocket speeds up exponentially
		p.CurrentCF.Vel = p.CurrentCF.Vel.Add(p.CurrentCF.Vel.Scale(delay).Scale(3))
	}
	if p.Type == proto.ProjectileMolotov || (p.Type == proto.ProjectileFlame && !p.MovementLock) {
		p.CurrentCF.Position = p.CurrentCF.Position.Add(p.CurrentCF.Vel.Scale(delay))
		p.CurrentCF.Vel[2] -= float32(9.8 * delay)
	}

	if p.Type == proto.ProjectileFlame {
		s.updateFlame(p)
	}
	// a flame that hits the map stays there (GameProjectile.cpp:585)
	if p.Type == proto.ProjectileFlame && !p.MovementLock {
		p2 := p.CurrentCF.Position
		var normal bvmath.Vec3
		if s.m.RayTest(p.LastCF.Position, &p2, &normal) {
			p.MovementLock = true
			p.CurrentCF.Position = p2.Add(normal.Scale(.1))
		}
	}

	bouncing := p.Type == proto.ProjectileGrenade || p.Type == proto.ProjectileLifePack ||
		p.Type == proto.ProjectileDropedWeapon || p.Type == proto.ProjectileDropedGrenade
	if speed > .5 || p.CurrentCF.Position[2] > .2 {
		if bouncing {
			p.CurrentCF.Position = p.CurrentCF.Position.Add(p.CurrentCF.Vel.Scale(delay))
			p.CurrentCF.Vel[2] -= float32(9.8 * delay)
			// `map && grenade || lifepack || ...`: the map test applies to grenades only, but there is
			// always a map on the server
			p2 := p.CurrentCF.Position
			var normal bvmath.Vec3
			if s.m != nil && s.m.RayTest(p.LastCF.Position, &p2, &normal) {
				p.CurrentCF.Position = p2.Add(normal.Scale(.01))
				p.CurrentCF.Vel = bvmath.Reflect(p.CurrentCF.Vel, normal)
				p.CurrentCF.Vel = p.CurrentCF.Vel.Scale(.65)
			}
		}
	} else {
		// slow and low: every projectile type stops
		p.CurrentCF.Vel = bvmath.Vec3{}
	}

	// only the server ends a projectile's life
	p.Duration -= delay
	if p.Duration <= 0 {
		if p.Type == proto.ProjectileGrenade {
			if p.NeedToBeDeleted {
				return
			}
			p.NeedToBeDeleted = true
			s.broadcast(proto.SvclExplosion, &proto.SvclExplosionMsg{
				Position: p.CurrentCF.Position, Normal: [3]float32{0, 0, 1}, Radius: 1.5, PlayerID: -1,
			})
			s.radiusHit(p.CurrentCF.Position, 3, p.FromID, proto.WeaponGrenade, false)
			return
		}
		p.NeedToBeDeleted = true
		return
	}

	zookaRadius := float32(3)
	if s.SV.ZookaRemoteDet.B && s.SV.ServerType.I == serverTypePro {
		zookaRadius = s.SV.ZookaRadius.F
	}
	// `if (gameVar.sv_serverType = 1)` (GameProjectile.cpp:718): an assignment, so every projectile
	// update switches the server to Pro rules and sets the bazooka damage (design/server.md §8.3.1)
	s.SV.ServerType.I = serverTypePro
	s.weapons[proto.WeaponBazooka].Damage = s.SV.ZookaDamage.F

	switch p.Type {
	case proto.ProjectileRocket:
		if !p.NeedToBeDeleted {
			s.rocketCollision(p, zookaRadius)
		}
	case proto.ProjectileMolotov:
		if !p.NeedToBeDeleted {
			s.molotovCollision(p)
		}
	case proto.ProjectileLifePack:
		if p.NeedToBeDeleted {
			return
		}
		if o := s.playerInRadius(bvmath.Vec3{p.CurrentCF.Position[0], p.CurrentCF.Position[1], .25}, .25, -1); o != nil {
			o.Life += .5
			if o.Life > 1 {
				o.Life = 1
			}
			p.NeedToBeDeleted = true
			s.broadcast(proto.SvclPickupItem, &proto.SvclPickupItemMsg{PlayerID: int8(o.ID), ItemType: itemLifePack})
		}
	case proto.ProjectileDropedGrenade:
		if p.NeedToBeDeleted {
			return
		}
		if o := s.playerInRadius(bvmath.Vec3{p.CurrentCF.Position[0], p.CurrentCF.Position[1], .25}, .25, -1); o != nil {
			o.GrenadeLeft++
			if o.GrenadeLeft > 3 {
				o.GrenadeLeft = 3
			}
			p.NeedToBeDeleted = true
			s.broadcast(proto.SvclPickupItem, &proto.SvclPickupItemMsg{PlayerID: int8(o.ID), ItemType: itemGrenade})
		}
	}
}

// updateFlame is the server's flame block (GameProjectile.cpp:492): a flame follows the player it
// sticks to for 3 s (unstuck: two NET_SVCL_FLAME_STICK_TO_PLAYER with -1, then 1 s before it can
// stick again), catches the first player within 0.5 (its thrower only after 0.5 s), and burns what
// is within 0.5 every 20 frames. The messages carry the vector index, not the uniqueID.
func (s *Server) updateFlame(p *projectile) {
	if p.StickToPlayer >= 0 {
		if o := s.players[p.StickToPlayer]; o != nil {
			if o.Status == proto.StatusDead {
				p.StickToPlayer = -1
			} else {
				p.CurrentCF.Position = o.CurrentCF.Position
			}
		}
		p.StickFor -= delay
		if p.StickFor <= 0 {
			p.StickFor = 0
			p.StickToPlayer = -1
			m := proto.SvclFlameStickToPlayerMsg{ProjectileID: int16(p.ProjectileID), PlayerID: -1}
			s.broadcast(proto.SvclFlameStickToPlayer, &m)
			p.MovementLock = false
			p.StickFor = 1
			s.broadcast(proto.SvclFlameStickToPlayer, &m)
		}
	}
	if p.StickToPlayer == -1 {
		p.StickFor -= delay
		if p.StickFor <= 0 {
			p.StickFor = 0
			ignore := p.FromID
			if p.TimeSinceThrown > .5 {
				ignore = -1
			}
			if o := s.playerInRadius(p.CurrentCF.Position, .5, ignore); o != nil {
				p.MovementLock = true
				p.StickToPlayer = o.ID
				p.StickFor = 3
				s.broadcast(proto.SvclFlameStickToPlayer, &proto.SvclFlameStickToPlayerMsg{
					ProjectileID: int16(p.ProjectileID), PlayerID: int8(o.ID),
				})
			}
		}
	}
	p.DamageTime++
	if p.DamageTime >= 20 {
		p.DamageTime = 0
		s.radiusHit(p.CurrentCF.Position, .5, p.FromID, proto.WeaponMolotov, false)
	}
}

// rocketCollision is the rocket part of Projectile::update (GameProjectile.cpp:726): a player within
// 0.5 of it (not its shooter), a wall, or the shooter's second press (remote detonation) sets it off.
func (s *Server) rocketCollision(p *projectile, zookaRadius float32) {
	var owner *Player
	if p.FromID >= 0 && p.FromID < proto.MaxPlayer {
		owner = s.players[p.FromID]
	}
	clearFlags := func() {
		// the original dereferenced the shooter without a check (a rocket of a player who left crashed
		// it); here the flags of a missing shooter are skipped
		if owner != nil {
			owner.RocketInAir = false
			owner.DetonateRocket = false
		}
	}
	if o := s.playerInRadius(p.CurrentCF.Position, .25, -1); o != nil && o.ID != p.FromID {
		clearFlags()
		p.NeedToBeDeleted = true
		pos := o.CurrentCF.Position
		s.broadcast(proto.SvclExplosion, &proto.SvclExplosionMsg{
			Position: pos, Normal: [3]float32{0, 0, 1}, Radius: zookaRadius, PlayerID: int8(p.FromID),
		})
		s.radiusHit(pos, zookaRadius, p.FromID, proto.WeaponBazooka, false)
		return
	}
	p2 := p.CurrentCF.Position
	var normal bvmath.Vec3 // CVector3f(): zero when the press, not a wall, sets it off
	hit := s.m.RayTest(p.LastCF.Position, &p2, &normal)
	if hit || (owner != nil && owner.DetonateRocket) {
		clearFlags()
		p2 = p2.Add(normal.Scale(.1))
		p.NeedToBeDeleted = true
		s.broadcast(proto.SvclExplosion, &proto.SvclExplosionMsg{
			Position: p2, Normal: normal, Radius: zookaRadius, PlayerID: int8(p.FromID),
		})
		s.radiusHit(p2, zookaRadius, p.FromID, proto.WeaponBazooka, false)
	}
}

// molotovCollision is the molotov part of Projectile::update (GameProjectile.cpp:811): on a player
// (not its thrower) or a wall it breaks: the sound, and two flames. Kept from the original: on a
// player both flames get velocity 0 (`vel[0] = 0;(char)(vel[0] * 10);`), the random one being
// computed and thrown away; on a wall the second flame bounces off it.
func (s *Server) molotovCollision(p *projectile) {
	flame := func(vel [3]int8) {
		m := proto.PlayerProjectileMsg{
			PlayerID: int8(p.FromID), ProjectileType: proto.ProjectileFlame,
			Position: shortPos(p.CurrentCF.Position), Vel: vel,
		}
		s.spawnProjectile(&m)
	}
	sound := func(at bvmath.Vec3) {
		s.broadcast(proto.SvclPlaySound, &proto.SvclPlaySoundMsg{
			SoundID: soundMolotov, Volume: 250, Range: 5,
			Position: [3]uint8{toUChar(at[0]), toUChar(at[1]), toUChar(at[2])},
		})
	}
	if o := s.playerInRadius(p.CurrentCF.Position, .25, p.FromID); o != nil && o.ID != p.FromID {
		p.NeedToBeDeleted = true
		sound(p.CurrentCF.Position)
		flame([3]int8{})
		_ = p.CurrentCF.Vel.Scale(.5).Add(s.randVec3(bvmath.Vec3{-1, -1, 1}, bvmath.Vec3{1, 1, 2}))
		flame([3]int8{})
		return
	}
	p2 := p.CurrentCF.Position
	var normal bvmath.Vec3
	if s.m.RayTest(p.LastCF.Position, &p2, &normal) {
		p.CurrentCF.Position = p2.Add(normal.Scale(.1))
		p.NeedToBeDeleted = true
		sound(p2)
		flame([3]int8{})
		vel := bvmath.Reflect(p.CurrentCF.Vel.Scale(.5), normal).Add(s.randVec3(bvmath.Vec3{-1, -1, 0}, bvmath.Vec3{1, 1, 1}))
		flame(charVel(vel))
	}
}

// soundMolotov is SOUND_MOLOTOV (GameVar.h:46).
const soundMolotov = 2

// randVec3 is rand(CVector3f, CVector3f) (CVector.cpp): a rand(float, float) per component, built as
// `CVector3f(rand(x), rand(y), rand(z))`. C++ leaves the order of those calls to the compiler: clang
// (the openbv client and the reference drivers) draws x, y, z; MSVC on x86 likely drew z first. Which
// draw lands on which axis changes nothing a player can tell (the seed is the clock); x, y, z keeps the
// port checkable against the C++ (design/server.md §8.4).
func (s *Server) randVec3(from, to bvmath.Vec3) bvmath.Vec3 {
	var v bvmath.Vec3
	v[0] = s.rand.Float(from[0], to[0])
	v[1] = s.rand.Float(from[1], to[1])
	v[2] = s.rand.Float(from[2], to[2])
	return v
}

// The pickup kinds of NET_SVCL_PICKUP_ITEM (Game.h).
const (
	itemLifePack = 1
	itemWeapon   = 2
	itemGrenade  = 3
)

// playerInRadius is Game::playerInRadius (Game.cpp:1202): the first alive player (lowest slot), not
// ignore, within radius + 0.25 of pos.
func (s *Server) playerInRadius(pos bvmath.Vec3, radius float32, ignore int) *Player {
	r := radius + .25
	for i, o := range s.players {
		if o != nil && o.Status == proto.StatusAlive && i != ignore {
			if bvmath.DistanceSquared(pos, o.CurrentCF.Position) <= float32(r*r) {
				return o
			}
		}
	}
	return nil
}

// projectileEnum is the state dump's projectile list (ServerRecv.cpp:339): every live projectile, to
// the one who joins, with a weapon ID by type.
func (s *Server) projectileEnum(dest int32) {
	for _, p := range s.projectiles {
		m := proto.PlayerProjectileMsg{
			PlayerID: int8(p.FromID), ProjectileType: int8(p.Type),
			Position: shortPos(p.CurrentCF.Position), Vel: charVel(p.CurrentCF.Vel), UniqueID: p.UniqueID,
		}
		switch p.Type {
		case proto.ProjectileRocket:
			m.WeaponID = proto.WeaponBazooka
		case proto.ProjectileGrenade:
			m.WeaponID = proto.WeaponGrenade
		case proto.ProjectileMolotov:
			m.WeaponID = proto.WeaponMolotov
		case proto.ProjectileFlame:
			m.WeaponID = -2
		default:
			m.WeaponID = -1
		}
		s.send(dest, proto.ClsvSvclPlayerProjectile, &m)
	}
}

// shortPos is (short)(x * 100) per component, as every position goes on the wire.
func shortPos(v bvmath.Vec3) [3]int16 {
	return [3]int16{toShort(v[0] * 100), toShort(v[1] * 100), toShort(v[2] * 100)}
}

// charVel is (char)(x * 10) per component.
func charVel(v bvmath.Vec3) [3]int8 {
	return [3]int8{toChar(v[0] * 10), toChar(v[1] * 10), toChar(v[2] * 10)}
}

// toShort and toChar are C's float-to-short and float-to-char conversions as the wasm client does
// them: truncation toward zero to 32 bits (saturating), then the low bits.
func toShort(f float32) int16 { return int16(toInt32(f)) }
func toChar(f float32) int8   { return int8(toInt32(f)) }
func toUChar(f float32) uint8 { return uint8(toInt32(f)) }

func toInt32(f float32) int32 {
	switch {
	case f != f:
		return 0
	case f >= 2147483647:
		return 2147483647
	case f <= -2147483648:
		return -2147483648
	}
	return int32(f)
}
