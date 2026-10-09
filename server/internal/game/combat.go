package game

import (
	"math"
	"strconv"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/cvar"
	"github.com/emdzej/openbv/server/internal/proto"
)

func acosf(x float32) float32 { return float32(math.Acos(float64(x))) }
func atanf(x float32) float32 { return float32(math.Atan(float64(x))) }

// flt_epsilon is std::numeric_limits<float>::epsilon(), the death threshold of hitSV.
const fltEpsilon = 1.1920929e-07

var zAxis = bvmath.Vec3{0, 0, 1}

// playerShoot is NET_CLSV_PLAYER_SHOOT (ServerRecv.cpp:944): the rate checks, then the shot.
func (s *Server) playerShoot(p *Player, m *proto.ClsvPlayerShootMsg) {
	if !(p.Status == proto.StatusAlive || (p.Status == proto.StatusDead && p.TimeDead < .2)) {
		return
	}
	// the fire delay is read by the weapon ID the client sends (design/server.md §5.3); the original
	// indexed its table with any value, here it must be a weapon (§8.4)
	if m.WeaponID < 0 || int(m.WeaponID) > proto.WeaponMinibot || p.Weapon == nil {
		return
	}
	fireDelay := s.weapons[m.WeaponID].FireDelay
	if m.WeaponID == proto.WeaponShotgun || m.WeaponID == proto.WeaponSniper {
		// the shotgun and the sniper send their 5 and 2-3 bullets in the same frame
		if p.Weapon.ID == proto.WeaponSniper {
			if p.CurrentCF.CamPosZ >= 10 {
				p.Weapon.NbShot = 3
			} else {
				p.Weapon.NbShot = 2
			}
		}
		if p.MfElapsedSinceLastShot+.061 > fireDelay {
			p.ShotCount = 1
			p.MfElapsedSinceLastShot = 0
		} else {
			limit := int32(3)
			if m.WeaponID == proto.WeaponShotgun {
				limit = 5
			}
			if p.ShotCount < limit {
				p.ShotCount++
			} else {
				return
			}
		}
	} else {
		if p.MfElapsedSinceLastShot < fireDelay+.05 {
			p.SecondsFired += fireDelay
		} else {
			p.SecondsFired = 0
		}
		slack := float32(.051)
		if m.WeaponID == proto.WeaponChainGun {
			slack = .04
		}
		if p.MfElapsedSinceLastShot+slack > fireDelay {
			p.MfElapsedSinceLastShot = 0
		} else {
			return
		}
	}
	s.shootSV(p, m)
	p.FiredShowDelay = 2
}

// shootSV is Game::shootSV(net_clsv_player_shoot&) (Game.cpp:1221), the Pro build's: the shotgun's
// five pellets fan out by ±5° and ±10°, the sniper fires nbShot bullets, the chain gun's spread halves
// when standing still on Pro servers.
func (s *Server) shootSV(p *Player, m *proto.ClsvPlayerShootMsg) {
	p1 := bvmath.Vec3{float32(m.P1[0]) / 100, float32(m.P1[1]) / 100, float32(m.P1[2]) / 100}
	p2 := bvmath.Vec3{float32(m.P2[0]) / 100, float32(m.P2[1]) / 100, float32(m.P2[2]) / 100}
	w := p.Weapon
	w.ShotFrom = p1
	switch w.ID {
	case proto.WeaponShotgun:
		directionAngles := [5]float32{-10, -5, 0, 5, 10}
		deviationAngles := [5]float32{1, 2, 3, 4, 5} // only names the pellet; shootOne fixes the spread
		for i := 0; i < w.NbShot && i < 5; i++ {
			s.shootOne(p, m.NuzzleID, deviationAngles[i], p1, bvmath.RotateAboutAxis(p2, directionAngles[i], zAxis))
		}
	case proto.WeaponSniper:
		for i := 0; i < w.NbShot; i++ {
			s.shootOne(p, m.NuzzleID, 0, p1, p2)
		}
	case proto.WeaponChainGun:
		w.CurrentImp += 3
		if w.CurrentImp > w.Impressision {
			w.CurrentImp = w.Impressision
		}
		imp := w.CurrentImp
		if s.SV.ServerType.I == serverTypePro && p.CurrentCF.Vel.Length() < 1.15 {
			imp /= 2.70
		}
		for i := 0; i < w.NbShot; i++ {
			s.shootOne(p, m.NuzzleID, imp, p1, p2)
		}
	default:
		w.CurrentImp += 3
		if w.CurrentImp > w.Impressision {
			w.CurrentImp = w.Impressision
		}
		for i := 0; i < w.NbShot; i++ {
			s.shootOne(p, m.NuzzleID, w.CurrentImp, p1, p2)
		}
	}
}

// shootOne is Game::shootSV(playerID, nuzzleID, imp, p1, p2) (Game.cpp:1359): one bullet. p2 comes
// as the aim direction; the spread turns it about z by up to imp degrees and then about itself at
// random, the vertical part is halved, and the ray is tested against the walls and the players.
func (s *Server) shootOne(p *Player, nuzzleID int8, imp float32, p1, p2 bvmath.Vec3) {
	w := p.Weapon
	ident := int(imp)
	var oldP2 bvmath.Vec3
	if w.ID == proto.WeaponShotgun {
		switch ident {
		case 1:
			oldP2 = bvmath.RotateAboutAxis(p2, 10, zAxis)
		case 2:
			oldP2 = bvmath.RotateAboutAxis(p2, 5, zAxis)
		case 3:
			oldP2 = bvmath.RotateAboutAxis(p2, 0, zAxis)
		case 4:
			oldP2 = bvmath.RotateAboutAxis(p2, -5, zAxis)
		case 5:
			oldP2 = bvmath.RotateAboutAxis(p2, -10, zAxis)
		}
		oldP2 = bvmath.Normalize(oldP2)
		imp = 3.5
	}
	dir := p2
	if w.ProjectileType == projectileDirect && w.ID == proto.WeaponFlameThrower {
		if s.SV.FtExpirationTimer.F > 0 {
			mult := float32((1 - p.SecondsFired/s.SV.FtExpirationTimer.F) * s.SV.FtMaxRange.F)
			if mult < s.SV.FtMinRange.F {
				mult = s.SV.FtMinRange.F
			}
			p2 = p2.Scale(mult)
			if s.SV.ExplodingFT.B && p.SecondsFired/s.SV.FtExpirationTimer.F > 1 {
				// Server::nukePlayer: comes with the nuke (milestone 3); sv_explodingFT is off by default
				s.log.Debug("exploding flame thrower (not yet ported)", "player", p.ID)
			}
		} else {
			p2 = p2.Scale(s.SV.FtMaxRange.F)
		}
	} else {
		p2 = p2.Scale(128)
	}
	p2 = bvmath.RotateAboutAxis(p2, s.rand.Float(-imp, imp), zAxis)
	p2 = bvmath.RotateAboutAxis(p2, s.rand.Float(0, 360), dir)
	p2[2] *= .5
	p2 = p2.Add(p1)

	var normal bvmath.Vec3
	if w.ID == proto.WeaponShotgun {
		d := bvmath.Normalize(p2.Sub(p1))
		var clampShot float32
		const variation = .01
		if s.SV.ServerType.I == serverTypePro {
			sinTheta := bvmath.Cross(d, oldP2).Length()
			clampShot = s.SV.ShottyDropRadius.F / sinTheta
		} else {
			r := s.SV.ShottyRange.F
			switch ident {
			case 1, 5:
				clampShot = float32(r * (.333 + s.rand.Float(-variation, variation)))
			case 2, 4:
				clampShot = float32(r * (.667 + s.rand.Float(-variation, variation)))
			case 3:
				clampShot = r
			}
		}
		p2 = p1.Add(d.Scale(clampShot))
	}

	// the muzzle inside a wall: start at the wall (rayTest moves its second point, here p1)
	if s.m.RayTest(p.CurrentCF.Position, &p1, &normal) {
		p1 = p1.Add(normal.Scale(.01))
	}
	s.m.RayTest(p1, &p2, &normal)

	if w.ID == proto.WeaponPhotonRifle || w.ID == proto.WeaponFlameThrower {
		if w.ID == proto.WeaponPhotonRifle {
			p.P1 = p1
			p.P2 = p2
			p.IncShot = 30
		}
		radius := float32(.25)
		if w.ID == proto.WeaponFlameThrower {
			radius = .50
		}
		p3 := p2
		for i := 0; i < proto.MaxPlayer; i++ {
			o := s.players[i]
			if o == nil || i == p.ID {
				continue
			}
			if o.Status == proto.StatusAlive && (o.TeamID != p.TeamID || s.gameType == proto.GameTypeDM ||
				s.gameType == proto.GameTypeSND || s.SV.FriendlyFire.B || s.SV.ReflectedDamage.B) {
				if bvmath.SegmentToSphere(p1, &p3, o.CurrentCF.Position, radius) {
					normal = p3.Sub(p1)
					p3 = p2 // the beam goes on: every player along it is hit
					normal = bvmath.Normalize(normal)
					s.hitSV(o, w.ID, p, s.weapons[w.ID].Damage)
				}
			}
		}
		s.broadcast(proto.SvclPlayerShoot, &proto.SvclPlayerShootMsg{
			PlayerID: int8(p.ID), HitPlayerID: -1, NuzzleID: nuzzleID, WeaponID: int8(w.ID),
			P1: shortPos(p1), P2: shortPos(p2), Normal: charNormal(normal),
		})
		return
	}

	// p2 shortens to each hit player's closest point, so the last slot along the shortened ray wins,
	// not the nearest player (design/server.md §5.3, §8.3)
	var hit *Player
	for i := 0; i < proto.MaxPlayer; i++ {
		o := s.players[i]
		if o == nil || i == p.ID || o.Status != proto.StatusAlive {
			continue
		}
		if bvmath.SegmentToSphere(p1, &p2, o.CurrentCF.Position, .25) {
			hit = o
			normal = bvmath.Normalize(p2.Sub(p1))
		}
	}
	msg := proto.SvclPlayerShootMsg{
		PlayerID: int8(p.ID), HitPlayerID: -1, NuzzleID: nuzzleID, WeaponID: int8(w.ID),
		P1: shortPos(p1), P2: shortPos(p2), Normal: charNormal(normal),
	}
	if hit != nil {
		msg.HitPlayerID = int8(hit.ID)
		s.hitSV(hit, w.ID, p, -1)
	}
	s.broadcast(proto.SvclPlayerShoot, &msg)
}

func charNormal(n bvmath.Vec3) [3]int8 {
	return [3]int8{toChar(n[0] * 120), toChar(n[1] * 120), toChar(n[2] * 120)}
}

// photonTick is the photon beam's lasting damage (Game::update, Game.cpp:397): for 30 frames after a
// shot, every third frame, half the damage to anyone on the beam (radius 0.35).
func (s *Server) photonTick(i int) {
	p := s.players[i]
	if p.IncShot <= 0 {
		return
	}
	p.IncShot--
	if p.IncShot%3 != 0 {
		return
	}
	p3 := p.P2
	for j := 0; j < proto.MaxPlayer; j++ {
		o := s.players[j]
		if o == nil || j == i {
			continue
		}
		if o.Status == proto.StatusAlive && (o.TeamID != p.TeamID || s.gameType == proto.GameTypeDM ||
			s.gameType == proto.GameTypeSND || s.SV.FriendlyFire.B || s.SV.ReflectedDamage.B) {
			if bvmath.SegmentToSphere(p.P1, &p3, o.CurrentCF.Position, .35) {
				p3 = p.P2
				s.hitSV(o, proto.WeaponPhotonRifle, p, s.weapons[proto.WeaponPhotonRifle].Damage/2)
			}
		}
	}
}

// radiusHit is Game::radiusHit (Game.cpp:1592): everyone within radius of pos with no wall between
// (the dead too; hitSV ignores them), knives sparing their owner.
func (s *Server) radiusHit(pos bvmath.Vec3, radius float32, fromID, weaponID int, sameDmg bool) {
	if fromID < 0 || fromID >= proto.MaxPlayer || s.players[fromID] == nil {
		return
	}
	from := s.players[fromID]
	for i := 0; i < proto.MaxPlayer; i++ {
		if i == fromID && weaponID == proto.WeaponKnives {
			continue
		}
		o := s.players[i]
		if o == nil {
			continue
		}
		dis := bvmath.Distance(o.CurrentCF.Position, pos)
		if dis < radius {
			p2 := o.CurrentCF.Position
			var normal bvmath.Vec3
			if !s.m.RayTest(pos, &p2, &normal) {
				k := float32(1)
				if !sameDmg {
					k = 1 - dis/radius
				}
				s.hitSV(o, weaponID, from, float32(k*s.weapons[weaponID].Damage))
			}
		}
	}
}

// hitSV is Player::hitSV (Player.cpp:1111): the damage (the Pro values, the photon and flame
// falloff, the shield, spawn immunity, instagib), the hit message, and on death the drops and the
// scores (design/server.md §5.4). damage -1 is the weapon's default.
func (s *Server) hitSV(v *Player, weaponID int, from *Player, damage float32) {
	var shotFrom bvmath.Vec3
	if from.Weapon != nil {
		shotFrom = from.Weapon.ShotFrom
	}
	cdamage := damagePrelude(s.SV, weaponID, s.weapons[weaponID].Damage, damage, v.CurrentCF.Position, shotFrom,
		from == v, v.Protection, v.ImmuneTime, v.Life)
	if v.Status != proto.StatusAlive {
		return
	}

	hit := func() {
		if from != v {
			if cdamage < v.Life {
				from.Dmg += cdamage
			} else {
				from.Dmg += v.Life
			}
		}
		v.Life -= cdamage
		v.ScreenHit += cdamage
		s.broadcast(proto.SvclPlayerHit, &proto.SvclPlayerHitMsg{
			PlayerID: int8(v.ID), FromID: int8(from.ID), WeaponID: int8(weaponID), Damage: v.Life,
			Vel: [3]int8{0, 0, 1},
		})
	}

	if from.TeamID == v.TeamID && s.gameType != proto.GameTypeDM && s.gameType != proto.GameTypeSND {
		if s.SV.FriendlyFire.B || from.ID == v.ID {
			hit()
		}
		if s.SV.ReflectedDamage.B && from.ID != v.ID {
			s.hitSV(from, weaponID, from, cdamage)
		}
		if v.Life <= fltEpsilon {
			s.die(v, from, weaponID)
			// a team kill: the killer's team loses a point, and the killer takes the death
			// (the victim's deaths don't change: design/server.md §8.3)
			if s.gameType == proto.GameTypeTDM {
				switch from.TeamID {
				case proto.TeamBlue:
					s.blueScore--
				case proto.TeamRed:
					s.redScore--
				}
			}
			from.Deaths++
			if s.gameType != proto.GameTypeCTF {
				from.Score--
			}
		}
		return
	}

	hit()
	if v.Life <= fltEpsilon {
		s.die(v, from, weaponID)
		if from != v {
			if s.gameType != proto.GameTypeCTF {
				switch from.TeamID {
				case proto.TeamBlue:
					s.blueScore++
				case proto.TeamRed:
					s.redScore++
				}
				from.Score++
			}
			from.Kills++
			v.Deaths++
		} else {
			from.Deaths++
			if s.gameType != proto.GameTypeCTF {
				from.Kills--
				from.Score--
			}
		}
	}
}

const subGameTypeInstagib = 1 // SUBGAMETYPE_INSTAGIB

// damagePrelude is the first part of Player::hitSV (Player.cpp:1111): the damage a hit does, before
// the alive check. damage -1 is the weapon's default (or its sv_*Damage on Pro servers); the photon
// rifle's falloff (sv_photonType) and the flame thrower's run from where the shooter fired.
func damagePrelude(sv *cvar.SV, weaponID int, weaponDamage, damage float32, pos, shotFrom bvmath.Vec3,
	self bool, protection, immune, life float32) float32 {
	cdamage := damage
	if damage == -1 {
		cdamage = weaponDamage
		if sv.ServerType.I == 1 {
			switch weaponID {
			case proto.WeaponSMG:
				cdamage = sv.SmgDamage.F
			case proto.WeaponSniper:
				cdamage = sv.SniperDamage.F
			case proto.WeaponShotgun:
				cdamage = sv.ShottyDamage.F
			case proto.WeaponDualMachineGun:
				cdamage = sv.DmgDamage.F
			case proto.WeaponChainGun:
				cdamage = sv.CgDamage.F
			}
		}
	}
	if weaponID == proto.WeaponPhotonRifle && sv.ServerType.I == 1 && !self {
		distance := pos.Sub(shotFrom).Length()
		c, vs, hs, mult := sv.PhotonDamageCoefficient.F, sv.PhotonVerticalShift.F, sv.PhotonHorizontalShift.F, sv.PhotonDistMult.F
		switch sv.PhotonType.I {
		case 1:
			cdamage = float32(cdamage * (vs + float32(c*(bvmath.Pi/2-atanf(float32((distance-hs)*mult))))))
		case 2:
			if distance == 0 {
				distance = float32(1e-58) // the original's constant: 0 in a float
			}
			cdamage = float32(cdamage * (vs + c/float32((distance-hs)*mult)))
		case 3:
			// pow promotes to double in the C++ the client is built with; the rest follows in double
			x := float64(float32((distance - hs) * mult))
			cdamage = float32(float64(cdamage) * (float64(vs) + float64(c)/(1+math.Pow(x, 2))))
		default:
			cdamage = float32(cdamage * c)
		}
	}
	if weaponID == proto.WeaponFlameThrower && sv.ServerType.I == 1 && !self {
		cdamage = sv.FtDamage.F
		distance := pos.Sub(shotFrom).Length()
		cdamage = float32((1 - distance/sv.FtMaxRange.F) * cdamage)
	}
	if protection > .6 {
		cdamage *= .50 // the shield
	}
	if immune > .3 {
		cdamage = 0
	}
	if sv.SubGameType.I == subGameTypeInstagib && weaponID != proto.WeaponGrenade &&
		weaponID != proto.WeaponKnives && weaponID != proto.WeaponMolotov {
		cdamage = life
	}
	return cdamage
}

// die is the death part of hitSV: the dedicated server's kill log (sv_showKills), the drops (a life
// pack, the weapon, one per grenade left, each broadcast as a projectile) and kill(true).
func (s *Server) die(v, from *Player, weaponID int) {
	if s.SV.ShowKills.B {
		s.log.Info("Player id:" + strconv.Itoa(v.ID) + " killed player id:" + strconv.Itoa(from.ID) + " with weapon id:" + strconv.Itoa(weaponID))
	}
	drop := func(typ, weapon int) {
		pVel := bvmath.RotateAboutAxis(bvmath.Vec3{0, 0, 1}, s.rand.Float(-45, 45), bvmath.Vec3{1, 0, 0})
		pVel = bvmath.RotateAboutAxis(pVel, s.rand.Float(-0, 360), zAxis).Scale(3)
		pVel = pVel.Add(v.CurrentCF.Vel.Scale(.25))
		m := proto.PlayerProjectileMsg{
			PlayerID: int8(v.ID), ProjectileType: int8(typ), WeaponID: int8(weapon),
			Position: shortPos(v.CurrentCF.Position), Vel: charVel(pVel),
		}
		s.spawnProjectile(&m)
		s.broadcast(proto.ClsvSvclPlayerProjectile, &m)
	}
	drop(proto.ProjectileLifePack, 0)
	drop(proto.ProjectileDropedWeapon, v.weaponID())
	for i := 0; i < v.GrenadeLeft; i++ {
		drop(proto.ProjectileDropedGrenade, 0)
	}
	s.killPlayer(v)
}

// pickupRequest is NET_CLSV_PICKUP_REQUEST (ServerRecv.cpp:1103): the first dropped weapon within
// 0.5 (2D) of an alive player: his own is dropped (with his velocity) and he takes it.
func (s *Server) pickupRequest(p *Player) {
	if p.Status != proto.StatusAlive {
		return
	}
	for _, pr := range s.projectiles {
		if pr.Type != proto.ProjectileDropedWeapon || pr.NeedToBeDeleted {
			continue
		}
		a := bvmath.Vec3{pr.CurrentCF.Position[0], pr.CurrentCF.Position[1], 0}
		b := bvmath.Vec3{p.CurrentCF.Position[0], p.CurrentCF.Position[1], 0}
		if bvmath.DistanceSquared(a, b) <= .5*.5 {
			m := proto.PlayerProjectileMsg{
				PlayerID: int8(p.ID), ProjectileType: proto.ProjectileDropedWeapon, WeaponID: int8(p.weaponID()),
				Position: shortPos(p.CurrentCF.Position), Vel: charVel(p.CurrentCF.Vel),
			}
			s.spawnProjectile(&m)
			s.broadcast(proto.ClsvSvclPlayerProjectile, &m)
			if pr.FromID >= 0 && pr.FromID <= proto.WeaponMinibot { // a weapon ID (§8.4: the original took any)
				p.switchWeapon(&s.weapons, pr.FromID, false)
			}
			s.broadcast(proto.SvclPickupItem, &proto.SvclPickupItemMsg{PlayerID: int8(p.ID), ItemType: itemWeapon, ItemFlag: int8(pr.FromID)})
			pr.NeedToBeDeleted = true
			return
		}
	}
}
