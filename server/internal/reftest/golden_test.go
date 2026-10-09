package reftest

import (
	"math"
	"testing"

	"github.com/emdzej/openbv/server/internal/bvmap"
	"github.com/emdzej/openbv/server/internal/bvmath"
)

func load(t *testing.T) *Golden {
	t.Helper()
	g, err := Load()
	if err != nil {
		t.Fatalf("golden data: %v (regenerate with cpp/build.sh)", err)
	}
	return g
}

func vec(v V) bvmath.Vec3 { return bvmath.Vec3(v.Vec()) }

// same compares bits, and treats any NaN as any NaN.
func same(a, b bvmath.Vec3) bool {
	for i := range a {
		if math.Float32bits(a[i]) != math.Float32bits(b[i]) && !(a[i] != a[i] && b[i] != b[i]) {
			return false
		}
	}
	return true
}

// rotateAboutAxis with musl's cosf and sinf on both sides (the driver links musl, the Go port is
// bvmath.Cosf/Sinf): bit for bit.
func TestRotateAboutAxis(t *testing.T) {
	g := load(t)
	for i, c := range g.Rotate {
		got := bvmath.RotateAboutAxis(vec(c.P), c.A.Float(), vec(c.X))
		if !same(got, vec(c.R)) {
			t.Fatalf("case %d: rotateAboutAxis(%v, %v, %v) = %v, want %v", i, vec(c.P), c.A.Float(), vec(c.X), got, vec(c.R))
		}
	}
}

func TestSegmentToSphere(t *testing.T) {
	g := load(t)
	for i, c := range g.Segment {
		p2 := vec(c.P2)
		hit := bvmath.SegmentToSphere(vec(c.P1), &p2, vec(c.C), c.R.Float())
		if hit != c.Hit || !same(p2, vec(c.Out)) {
			t.Fatalf("case %d: segmentToSphere = %v %v, want %v %v", i, hit, p2, c.Hit, vec(c.Out))
		}
	}
}

func TestRayTest(t *testing.T) {
	g := load(t)
	n := 0
	for mi, rm := range g.Ray {
		m := &bvmap.Map{Width: rm.W, Height: rm.H, Cells: make([]bvmap.Cell, len(rm.Cells))}
		for i, h := range rm.Cells {
			if h < 0 {
				m.Cells[i] = bvmap.Cell{Passable: true}
			} else {
				m.Cells[i] = bvmap.Cell{Height: uint8(h)}
			}
		}
		for i, r := range rm.Rays {
			p2 := vec(r.P2)
			normal := bvmath.Vec3{7, 7, 7}
			hit := m.RayTest(vec(r.P1), &p2, &normal)
			if hit != r.Hit || !same(p2, vec(r.Out)) || !same(normal, vec(r.N)) {
				t.Fatalf("map %d ray %d: rayTest(%v, %v) = %v %v %v, want %v %v %v", mi, i, vec(r.P1), vec(r.P2),
					hit, p2, normal, r.Hit, vec(r.Out), vec(r.N))
			}
			if hit {
				n++
			}
		}
	}
	t.Logf("rayTest: %d hits", n)
}

// The spread of one bullet: the same rand() calls and the same bits.
func TestSpread(t *testing.T) {
	g := load(t)
	for i, c := range g.Spread {
		r := bvmath.NewRand(c.Seed)
		imp := c.Imp.Float()
		dir := vec(c.Dir)
		p2 := dir.Scale(128)
		p2 = bvmath.RotateAboutAxis(p2, r.Float(-imp, imp), bvmath.Vec3{0, 0, 1})
		p2 = bvmath.RotateAboutAxis(p2, r.Float(0, 360), dir)
		p2[2] *= .5
		p2 = p2.Add(vec(c.P1))
		if next := r.Int(); next != c.Next {
			t.Fatalf("case %d: rand() after the spread = %d, want %d (a different number of calls)", i, next, c.Next)
		}
		if !same(p2, vec(c.P2)) {
			t.Fatalf("case %d: spread %v, want %v", i, p2, vec(c.P2))
		}
	}
}

func TestReflect(t *testing.T) {
	g := load(t)
	for i, c := range g.Reflect {
		got := bvmath.Reflect(vec(c.U), vec(c.N)).Scale(.65)
		if !same(got, vec(c.R)) {
			t.Fatalf("case %d: reflect(%v, %v)*.65 = %v, want %v", i, vec(c.U), vec(c.N), got, vec(c.R))
		}
	}
}
