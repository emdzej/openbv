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

// updateProjectile is Projectile::update (GameProjectile.cpp:234) on the server (design/server.md
// §5.6). Milestone 2 has the items and the shared motion; the rocket, molotov and flame rules come
// with the projectiles milestone, along with the client requests that create them.
func (s *Server) updateProjectile(p *projectile) {
	p.LastCF = p.CurrentCF
	p.CurrentCF.FrameID++
	p.TimeSinceThrown += delay

	speed := p.CurrentCF.Vel.Length()
	switch {
	case p.Type == proto.ProjectileRocket:
		if speed > 10 {
			p.CurrentCF.Vel = p.CurrentCF.Vel.Div(speed)
			speed = 10
			p.CurrentCF.Vel = p.CurrentCF.Vel.Scale(speed)
		}
		p.CurrentCF.Position = p.CurrentCF.Position.Add(p.CurrentCF.Vel.Scale(delay))
		p.CurrentCF.Vel = p.CurrentCF.Vel.Add(p.CurrentCF.Vel.Scale(delay).Scale(3))
	case p.Type == proto.ProjectileMolotov, p.Type == proto.ProjectileFlame && !p.MovementLock:
		p.CurrentCF.Position = p.CurrentCF.Position.Add(p.CurrentCF.Vel.Scale(delay))
		p.CurrentCF.Vel[2] -= float32(9.8 * delay)
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

	// `if (gameVar.sv_serverType = 1)` (GameProjectile.cpp:718): an assignment, so every projectile
	// update switches the server to Pro rules and sets the bazooka damage (design/server.md §8.3.1)
	s.SV.ServerType.I = serverTypePro
	s.weapons[proto.WeaponBazooka].Damage = s.SV.ZookaDamage.F

	switch p.Type {
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
