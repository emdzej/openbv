package game

import (
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
)

// minibotWeapon is WEAPON_MINIBOT_WEAPON (GameVar.h:42): the minibot's gun, only ever in
// NET_SVCL_PLAYER_SHOOT.weaponID.
const minibotWeapon = 100

// minibot is CMiniBot (Minibot.cpp, _PRO_): the turret the minibot secondary drops, or the nuke bot.
// Its movement code is commented out in the original: it stays where it was put.
type minibot struct {
	CurrentCF, LastCF CoordFrame
	NukeBot           bool
	FireRate          float32 // m_fireRate
	SeekingTime       float32 // m_seekingTime: it goes after this long while its owner's secondary is the minibot
}

// newMinibot is CMiniBot::CMiniBot (Minibot.cpp:24).
func newMinibot(pos, mousePos bvmath.Vec3, nuke bool) *minibot {
	b := &minibot{NukeBot: nuke, SeekingTime: 5}
	b.CurrentCF.Position = pos
	b.CurrentCF.MousePosOnMap = mousePos
	b.LastCF = b.CurrentCF
	return b
}

// createMinibotMsg is NET_SVCL_CREATE_MINIBOT as SpawnMiniBotSV and SpawnNukeBotSV fill it: positions
// x10 (not x100 like everything else).
func createMinibotMsg(owner int, b *minibot) *proto.SvclCreateMinibotMsg {
	pos, mouse := b.CurrentCF.Position, b.CurrentCF.MousePosOnMap
	return &proto.SvclCreateMinibotMsg{
		PlayerID: int8(owner),
		Position: [3]int16{toShort(pos[0] * 10), toShort(pos[1] * 10), toShort(pos[2] * 10)},
		MousePos: [3]int16{toShort(mouse[0] * 10), toShort(mouse[1] * 10), toShort(mouse[2] * 10)},
	}
}

// spawnMiniBotSV is Player::SpawnMiniBotSV (Player.cpp:304): a turret one metre towards the aim, if
// no wall is in the way.
func (s *Server) spawnMiniBotSV(p *Player) {
	if p.Minibot != nil {
		return
	}
	dir := p.CurrentCF.MousePosOnMap.Sub(p.CurrentCF.Position)
	dir[2] = 0
	dir = bvmath.Normalize(dir)
	p2 := p.CurrentCF.Position.Add(dir)
	var normal bvmath.Vec3
	if s.m.RayTest(p.CurrentCF.Position, &p2, &normal) {
		return
	}
	botPos := p.CurrentCF.Position.Add(dir)
	botPos[2] = .15
	mouse := botPos.Add(dir)
	mouse[2] = 0
	p.Minibot = newMinibot(botPos, mouse, false)
	s.broadcast(proto.SvclCreateMinibot, createMinibotMsg(p.ID, p.Minibot))
}

// spawnNukeBotSV is Player::SpawnNukeBotSV (Player.cpp:343): the nuke bot, at the player's feet.
func (s *Server) spawnNukeBotSV(p *Player) {
	if p.Minibot != nil {
		return
	}
	botPos := p.CurrentCF.Position
	botPos[2] = .15
	mouse := botPos
	mouse[2] = 0
	p.Minibot = newMinibot(botPos, mouse, true)
	s.broadcast(proto.SvclCreateMinibot, createMinibotMsg(p.ID, p.Minibot))
}

// nukeBotExplode is the nuke's end in Weapon::update (Weapon.cpp:645): the explosion at the bot and
// sv_nukeRadius damage around it, then the bot goes. (The original passed radiusHit a reference to the
// bot's position, which a kill of the owner freed during the call; here it is a copy.)
func (s *Server) nukeBotExplode(p *Player) {
	if p.Minibot == nil {
		return
	}
	pos := p.Minibot.CurrentCF.Position
	s.broadcast(proto.SvclExplosion, &proto.SvclExplosionMsg{
		Position: pos, Normal: [3]float32{0, 0, 1}, Radius: s.SV.NukeRadius.F, PlayerID: int8(p.ID),
	})
	s.radiusHit(pos, s.SV.NukeRadius.F, p.ID, proto.WeaponNuclear, false)
	p.Minibot = nil
}

// updateMinibot is the server's minibot block of Player::update (PlayerUpdate.cpp:195), for an alive
// owner: the (always zero) velocity, the height, Think, and its lifetime.
func (s *Server) updateMinibot(p *Player, delay float32) {
	b := p.Minibot
	b.CurrentCF.Position = b.CurrentCF.Position.Add(b.CurrentCF.Vel.Scale(delay))
	if size := b.CurrentCF.Vel.Length(); size > 0 {
		size -= float32(delay * 8)
		if size < 0 {
			size = 0
		}
		b.CurrentCF.Vel = bvmath.Normalize(b.CurrentCF.Vel).Scale(size)
	}
	b.CurrentCF.Position[2] = .15
	s.minibotThink(p, b, delay)
	b.SeekingTime -= delay
	if b.SeekingTime <= 0 && p.Melee != nil && p.Melee.ID == proto.WeaponMinibot {
		p.Minibot = nil // no message: the clients run the same timer
	}
}

// mountOffset is the minibot's gun mount: a tenth of a metre to the right of its aim.
func mountOffset(shootDir bvmath.Vec3) bvmath.Vec3 {
	return bvmath.Normalize(bvmath.RotateAboutAxis(shootDir, -90, zAxis)).Scale(.1)
}

// minibotThink is CMiniBot::Think (Minibot.cpp:59): the closest visible enemy within 6 m, and a shot
// at it four times a second. Kept as the original has it: the fire check reuses the p1 and p2 of the
// last enemy the loop looked at (not necessarily the closest one), p2 as the loop's ray test left it.
func (s *Server) minibotThink(owner *Player, b *minibot, delay float32) {
	b.FireRate -= delay
	var closest *Player
	closestDis := float32(10000)
	var p1, p2, normal bvmath.Vec3
	for _, o := range s.players {
		if o == nil || o.Status != proto.StatusAlive || o == owner {
			continue
		}
		if !(o.TeamID != owner.TeamID || s.gameType == proto.GameTypeDM || s.gameType == proto.GameTypeSND) {
			continue
		}
		d := bvmath.DistanceSquared(b.CurrentCF.Position, o.CurrentCF.Position)
		if d < 6*6 && d < closestDis {
			p1 = b.CurrentCF.Position
			p2 = o.CurrentCF.Position
			shootDir := bvmath.Normalize(p2.Sub(p1))
			origin := p1.Add(mountOffset(shootDir))
			if !s.m.RayTest(origin, &p2, &normal) {
				closestDis = d
				closest = o
			}
		}
	}
	if b.FireRate <= 0 && closest != nil {
		b.FireRate = .25
		b.CurrentCF.MousePosOnMap = closest.CurrentCF.Position
		shootDir := bvmath.Normalize(closest.CurrentCF.Position.Sub(b.CurrentCF.Position))
		mount := mountOffset(shootDir)
		origin := p1.Add(mount)
		if !s.m.RayTest(origin, &p2, &normal) && !b.NukeBot {
			s.shootMinibotSV(owner, 10, b.CurrentCF.Position.Add(mount), b.CurrentCF.Position.Add(shootDir.Scale(10)))
		}
	}
}

// shootMinibotSV is Game::shootMinibotSV (Game.cpp:1290): a hitscan shot with imp degrees of spread
// against every alive player but the owner (the last one along the shrinking segment is hit), the hit,
// then NET_SVCL_PLAYER_SHOOT to all with the minibot's gun as the weapon.
func (s *Server) shootMinibotSV(owner *Player, imp float32, p1, p2 bvmath.Vec3) {
	var normal bvmath.Vec3
	dir := bvmath.Normalize(p2.Sub(p1))
	p2 = p2.Sub(p1)
	p2 = bvmath.RotateAboutAxis(p2, s.rand.Float(-imp, imp), zAxis)
	p2 = bvmath.RotateAboutAxis(p2, s.rand.Float(0, 360), dir)
	p2[2] *= .5
	p2 = p2.Add(p1)
	s.m.RayTest(p1, &p2, &normal)

	var hit *Player
	for i, o := range s.players {
		if o == nil || i == owner.ID || o.Status != proto.StatusAlive {
			continue
		}
		if bvmath.SegmentToSphere(p1, &p2, o.CurrentCF.Position, .25) {
			hit = o
			normal = bvmath.Normalize(p2.Sub(p1))
		}
	}
	m := proto.SvclPlayerShootMsg{PlayerID: int8(owner.ID), HitPlayerID: -1, WeaponID: minibotWeapon}
	if hit != nil {
		m.HitPlayerID = int8(hit.ID)
		s.hitSV(hit, proto.WeaponMinibot, owner, -1)
	}
	m.P1 = shortPos(p1)
	m.P2 = shortPos(p2)
	m.Normal = charNormal(normal)
	s.broadcast(proto.SvclPlayerShoot, &m)
}

// minibotCollisions is the bots' wall collisions of Game::update (Game.cpp:540), after the players.
func (s *Server) minibotCollisions() {
	for _, p := range s.players {
		if p == nil || p.Status != proto.StatusAlive || p.Minibot == nil {
			continue
		}
		cf := &p.Minibot.CurrentCF
		s.m.PerformCollision(&p.Minibot.LastCF.Position, &cf.Position, &cf.Vel, .15)
		s.m.CollisionClip(&cf.Position, .15)
	}
}
