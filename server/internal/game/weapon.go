package game

import (
	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
)

const (
	projectileDirect = 1  // PROJECTILE_DIRECT
	projectileNone   = 10 // PROJECTILE_NONE
)

// weaponDef is a Weapon of the shared table, gameVar.weapons[] (Weapon.h). The server reads it
// (fire delays, damage) and changes it at run time (Game::UpdateProSettings, the bazooka damage
// written by every projectile update: design/server.md §5.1, §8).
type weaponDef struct {
	ID             int
	FireDelay      float32
	Damage         float32
	Impressision   float32 // the maximum spread (the original's spelling)
	NbShot         int
	ReculVel       float32
	StartImp       float32
	ProjectileType int
}

// defaultWeapons is the dedicated server's table (GameVar.cpp:303, the CONSOLE branch: the flame
// thrower's spread is 6 there, 10 in the client's listen-server table).
func defaultWeapons() [proto.WeaponMinibot + 1]weaponDef {
	w := func(id int, fireDelay, damage, imp float32, nbShot int, recul, startImp float32, pt int) weaponDef {
		return weaponDef{id, fireDelay, damage, imp, nbShot, recul, startImp, pt}
	}
	return [...]weaponDef{
		proto.WeaponSMG:            w(proto.WeaponSMG, .1, .1, 8, 1, .5, 1, projectileDirect),
		proto.WeaponShotgun:        w(proto.WeaponShotgun, .85, .21, 20, 5, 3, 12, projectileDirect),
		proto.WeaponSniper:         w(proto.WeaponSniper, 2, .30, 0, 1, 3, 0, projectileDirect),
		proto.WeaponDualMachineGun: w(proto.WeaponDualMachineGun, .1, .13, 10, 1, .8, 2, projectileDirect),
		proto.WeaponChainGun:       w(proto.WeaponChainGun, .1, .19, 15, 1, 2, 5, projectileDirect),
		proto.WeaponBazooka:        w(proto.WeaponBazooka, 1.75, .75, 0, 1, 3, 0, proto.ProjectileRocket),
		proto.WeaponPhotonRifle:    w(proto.WeaponPhotonRifle, 1.5, .24, 0, 1, 5, 0, projectileDirect),
		proto.WeaponFlameThrower:   w(proto.WeaponFlameThrower, .1, .08, 6, 1, 0, 6, projectileDirect),
		proto.WeaponGrenade:        w(proto.WeaponGrenade, 1, 1.5, 0, 1, -1, 0, proto.ProjectileGrenade),
		proto.WeaponMolotov:        w(proto.WeaponMolotov, 1, .15, 0, 1, -1, 0, proto.ProjectileMolotov),
		proto.WeaponKnives:         w(proto.WeaponKnives, 1, .60, 0, 1, 0, 0, projectileNone),
		proto.WeaponNuclear:        w(proto.WeaponNuclear, 12, 8, 0, 1, 0, 0, projectileNone),
		proto.WeaponShield:         w(proto.WeaponShield, 3, 0, 0, 1, 0, 0, projectileNone),
		proto.WeaponMinibot:        w(proto.WeaponMinibot, 1, .05, 0, 1, 0, 0, projectileNone),
	}
}

// weapon is a player's instance of a weapon (Weapon::Weapon(Weapon*), Weapon.cpp:75): a copy of the
// table's values when it was handed out, and the spread that grows with each shot.
type weapon struct {
	weaponDef
	CurrentImp       float32
	CurrentFireDelay float32
	ShotFrom         bvmath.Vec3 // where the last shot started (hitSV's photon and flame falloff)
}

func newWeapon(d weaponDef) *weapon {
	return &weapon{weaponDef: d, CurrentImp: d.StartImp}
}

// update is Weapon::update (Weapon.cpp:146), the server's part: the spread settles back while the
// owner is alive; the fire delay counts down (the nuke's timer comes with the secondaries).
func (w *weapon) update(delay float32, ownerAlive bool) {
	if !ownerAlive {
		return
	}
	if w.CurrentImp > w.StartImp {
		w.CurrentImp -= float32(delay * 10)
		if w.CurrentImp < w.StartImp {
			w.CurrentImp = w.StartImp
		}
	}
	if w.CurrentFireDelay > 0 {
		w.CurrentFireDelay -= delay
	}
}

// selectAvailableWeapon is SelectToAvailableWeapon (Game.cpp:226): the first enabled primary.
func (s *Server) selectAvailableWeapon() int {
	v := s.SV
	switch {
	case v.EnableSMG.B:
		return proto.WeaponSMG
	case v.EnableShotgun.B:
		return proto.WeaponShotgun
	case v.EnableSniper.B:
		return proto.WeaponSniper
	case v.EnableDualMachineGun.B:
		return proto.WeaponDualMachineGun
	case v.EnableChainGun.B:
		return proto.WeaponChainGun
	case v.EnableBazooka.B:
		return proto.WeaponBazooka
	case v.EnablePhotonRifle.B:
		return proto.WeaponPhotonRifle
	case v.EnableFlameThrower.B:
		return proto.WeaponFlameThrower
	}
	return proto.WeaponSMG
}

// selectAvailableMelee is SelectToAvailableMeleeWeapon (Game.cpp:245).
func (s *Server) selectAvailableMelee() int {
	v := s.SV
	switch {
	case v.EnableKnives.B:
		return proto.WeaponKnives
	case v.EnableNuclear.B:
		return proto.WeaponNuclear
	case v.EnableShield.B:
		return proto.WeaponShield
	case v.EnableMinibot.B:
		return proto.WeaponMinibot
	}
	return proto.WeaponKnives
}

// weaponEnabled is the sv_enable* switch of a primary (Game::update, Game.cpp:395).
func (s *Server) weaponEnabled(id int) bool {
	v := s.SV
	switch id {
	case proto.WeaponSMG:
		return v.EnableSMG.B
	case proto.WeaponShotgun:
		return v.EnableShotgun.B
	case proto.WeaponSniper:
		return v.EnableSniper.B
	case proto.WeaponDualMachineGun:
		return v.EnableDualMachineGun.B
	case proto.WeaponChainGun:
		return v.EnableChainGun.B
	case proto.WeaponBazooka:
		return v.EnableBazooka.B
	case proto.WeaponPhotonRifle:
		return v.EnablePhotonRifle.B
	case proto.WeaponFlameThrower:
		return v.EnableFlameThrower.B
	}
	return true
}

// meleeEnabled is the secondary's switch (Game.cpp:440).
func (s *Server) meleeEnabled(id int) bool {
	v := s.SV
	if !v.EnableSecondary.B {
		return false
	}
	switch id {
	case proto.WeaponKnives:
		return v.EnableKnives.B
	case proto.WeaponNuclear:
		return v.EnableNuclear.B
	case proto.WeaponShield:
		return v.EnableShield.B
	case proto.WeaponMinibot:
		return v.EnableMinibot.B
	}
	return true
}

// updateProSettings is Game::UpdateProSettings (Game.cpp:131), at Game construction.
func (s *Server) updateProSettings() {
	s.weapons[proto.WeaponNuclear].FireDelay = 12
	if s.SV.ServerType.I == serverTypePro {
		s.weapons[proto.WeaponShield].FireDelay = 2.5
		s.weapons[proto.WeaponChainGun].ReculVel = 1
	} else {
		s.weapons[proto.WeaponShield].FireDelay = 3
		s.weapons[proto.WeaponChainGun].ReculVel = 2
	}
}

const serverTypePro = 1 // SERVER_TYPE_PRO
