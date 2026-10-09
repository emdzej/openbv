package game

import (
	"bufio"
	"bytes"
	"encoding/hex"
	"os"
	"strings"
	"testing"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
)

// testdata/listen-sniper-walls.netlog is the client's packet log (--param netlog=1) of a game hosted
// in openbv.wasm, the original C++ server running in the client: one player on DM-MiniArena with
// the sniper, shooting walls. Each NET_CLSV_PLAYER_SHOOT the client sent is followed by the server's
// NET_SVCL_PLAYER_SHOOT replies (two or three bullets).
//
// The sniper has no spread, but each bullet still turns about its own aim by rand(0,360), and the
// rounding of that turn decides the last digit of a hit point's (short)(z*100). The listen server's
// rand() is shared with the client's effects, so its sequence can't be replayed; instead each bullet
// is checked against all 30000 values rand(0,360) can take: one of them must give the C++ reply byte
// for byte (all but the struct's last padding byte, which the C++ leaves uninitialised). That pins
// everything after the draw: the scaling, both rotations, the halved z, the muzzle and wall ray tests,
// the hit test and the encoding.
func TestListenServerSniperWalls(t *testing.T) {
	f, err := os.Open("testdata/listen-sniper-walls.netlog")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	type exchange struct {
		shot    []byte
		replies [][]byte
	}
	var ex []exchange
	sc := bufio.NewScanner(f)
	for sc.Scan() {
		line := sc.Text()
		data, err := hex.DecodeString(line[strings.Index(line, "data=")+5:])
		if err != nil {
			t.Fatal(err)
		}
		if strings.Contains(line, "send type=3 ") {
			ex = append(ex, exchange{shot: data})
		} else if len(ex) > 0 {
			ex[len(ex)-1].replies = append(ex[len(ex)-1].replies, data)
		}
	}
	if len(ex) < 10 {
		t.Fatalf("%d shots in the recording", len(ex))
	}
	s := newTestServer(t, proto.GameTypeTDM, "DM-MiniArena")
	var sent [][]byte
	s.sent = func(dest int32, typ uint16, msg any) {
		if typ == proto.SvclPlayerShoot {
			sent = append(sent, proto.Encode(msg))
		}
	}
	matched, inWall := 0, 0
	for i, e := range ex {
		var m proto.ClsvPlayerShootMsg
		proto.Decode(e.shot, &m)
		// a muzzle inside a wall is pulled out along the ray from the server's interpolated player
		// position, which the recording doesn't pin down: those bullets aren't comparable
		if c := s.m.Cells[int(m.P1[1]/100)*s.m.Width+int(m.P1[0]/100)]; !c.Passable {
			inWall += len(e.replies)
			continue
		}
		p := newPlayer(int(m.PlayerID), 1, 5)
		p.TeamID = proto.TeamBlue
		p.NextSpawnWeapon = proto.WeaponSniper
		// the player stands at the muzzle: the request's p1 (no wall between them)
		at := bvmath.Vec3{float32(m.P1[0]) / 100, float32(m.P1[1]) / 100, .25}
		p.spawn(at, 5, 0, &s.weapons)
		s.players[m.PlayerID] = p
		p.Weapon.NbShot = 1
		for k, want := range e.replies {
			found := false
			for v := int32(0); v < 30000 && !found; v++ {
				s.rand.Seed(seedBefore(v))
				sent = nil
				s.shootSV(p, &m)
				found = len(sent) == 1 && bytes.Equal(sent[0][:19], want[:19])
			}
			if !found {
				sent = nil
				s.shootSV(p, &m)
				t.Fatalf("shot %d bullet %d: no rand(0,360) gives the C++ reply\n  go  % x\n  c++ % x", i, k, sent[0], want)
			}
			matched++
		}
	}
	if matched < 20 {
		t.Fatalf("only %d comparable bullets", matched)
	}
	t.Logf("%d shots: %d bullets each the listen server's reply byte for byte, %d with the muzzle in a wall skipped", len(ex), matched, inWall)
}

// seedBefore is the MSVC rand() state whose next rand() returns v (seed*214013 + 2531011, bits 16-30).
func seedBefore(v int32) uint32 {
	after := uint32(v) << 16
	return (after - 2531011) * inverse214013
}

// inverse214013 is 214013's inverse modulo 2^32 (Newton's iteration).
var inverse214013 = func() uint32 {
	x := uint32(214013)
	for i := 0; i < 5; i++ {
		x *= 2 - 214013*x
	}
	return x
}()
