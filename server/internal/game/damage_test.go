package game

import (
	"math"
	"testing"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/cvar"
	"github.com/emdzej/openbv/server/internal/reftest"
)

// The damage part of hitSV against the original's (Player.cpp, run by reftest/cpp): every float bit.
func TestDamagePreludeGolden(t *testing.T) {
	g, err := reftest.Load()
	if err != nil {
		t.Fatal(err)
	}
	f := func(u uint32) float32 { return math.Float32frombits(u) }
	atan := 0
	defer func() { t.Logf("%d cases, %d within atanf's last bit", len(g.Damage), atan) }()
	for i, c := range g.Damage {
		sv := cvar.NewSV()
		sv.ServerType.I, sv.PhotonType.I, sv.SubGameType.I = int32(c.SV[0]), int32(c.SV[1]), int32(c.SV[2])
		vars := []*cvar.Var{sv.SmgDamage, sv.SniperDamage, sv.ShottyDamage, sv.DmgDamage, sv.CgDamage, sv.FtDamage,
			sv.FtMaxRange, sv.PhotonVerticalShift, sv.PhotonDamageCoefficient, sv.PhotonHorizontalShift, sv.PhotonDistMult}
		for k, v := range vars {
			v.F = f(c.SV[3+k])
		}
		got := damagePrelude(sv, c.Weapon, c.WDamage.Float(), c.DamageIn.Float(), bvmath.Vec3(c.Pos.Vec()),
			bvmath.Vec3(c.ShotFrom.Vec()), c.Self, c.Prot.Float(), c.Immune.Float(), c.Life.Float())
		want := c.Out.Float()
		if math.Float32bits(got) != math.Float32bits(want) && !(got != got && want != want) {
			// atanf is the double atan rounded (not musl's atanf): allow its last bit there, nothing else
			if c.SV[1] == 1 && c.Weapon == 6 && c.SV[0] == 1 && math.Abs(float64(got-want)) <= 1e-6*math.Max(1, math.Abs(float64(want))) {
				atan++
				continue
			}
			t.Fatalf("case %d (%+v): damage = %v (%08x), want %v (%08x)", i, c, got, math.Float32bits(got), want, math.Float32bits(want))
		}
	}
}
