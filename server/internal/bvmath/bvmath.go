// Package bvmath is the arithmetic the original game does, bit for bit where it matters: float32
// vectors (the game's CVector3f), cubicSpline (Helper.cpp:435) and the C runtime's rand() as the
// Windows server had it (MSVC's LCG) with the game's rand(int,int) and rand(float,float) helpers
// (CVector.cpp).
package bvmath

import "math"

// Epsilon is the game's CVector EPSILON (src/game/CVector.h: 0.0001), used by its comparisons.
const Epsilon = 0.0001

// Vec3 is CVector3f.
type Vec3 [3]float32

func (a Vec3) Add(b Vec3) Vec3      { return Vec3{a[0] + b[0], a[1] + b[1], a[2] + b[2]} }
func (a Vec3) Sub(b Vec3) Vec3      { return Vec3{a[0] - b[0], a[1] - b[1], a[2] - b[2]} }
func (a Vec3) Scale(s float32) Vec3 { return Vec3{a[0] * s, a[1] * s, a[2] * s} }
func (a Vec3) Length() float32      { return float32(math.Sqrt(float64(a[0]*a[0] + a[1]*a[1] + a[2]*a[2]))) }
func (a Vec3) IsZero() bool         { return a.Equal(Vec3{}) }
func DistanceSquared(a, b Vec3) float32 {
	d := a.Sub(b)
	return d[0]*d[0] + d[1]*d[1] + d[2]*d[2]
}

// Equal is CVector3f::operator== (each component within EPSILON).
func (a Vec3) Equal(b Vec3) bool {
	for i := range a {
		if a[i] < b[i]-Epsilon || a[i] > b[i]+Epsilon {
			return false
		}
	}
	return true
}

// CubicSpline is cubicSpline(x0, x1, x2, x3, t) (Helper.cpp:435), in the same order of operations:
// x0*((1-t)^3) + x1*3*t*((1-t)^2) + x2*3*(t*t)*(1-t) + x3*(t*t*t), per component with the vector
// operators (CVector3f * float, then +).
func CubicSpline(x0, x1, x2, x3 Vec3, t float32) Vec3 {
	a := (1 - t) * (1 - t) * (1 - t)
	b := (1 - t) * (1 - t)
	r0 := x0.Scale(a)
	r1 := x1.Scale(3).Scale(t).Scale(b)
	r2 := x2.Scale(3).Scale(t * t).Scale(1 - t)
	r3 := x3.Scale(t * t * t)
	return r0.Add(r1).Add(r2).Add(r3)
}

// Rand is the C library's rand() of the Windows build (MSVC): seed = seed*214013 + 2531011,
// result (seed >> 16) & 0x7fff. One per session; the original seeded it with srand(time(0)).
type Rand struct{ seed uint32 }

// NewRand seeds a generator (srand).
func NewRand(seed uint32) *Rand { return &Rand{seed: seed} }

// Seed is srand.
func (r *Rand) Seed(seed uint32) { r.seed = seed }

// Int is rand(): 0..32767.
func (r *Rand) Int() int32 {
	r.seed = r.seed*214013 + 2531011
	return int32((r.seed >> 16) & 0x7fff)
}

// Range is the game's rand(int from, int to) (CVector.cpp): the upper bound is excluded, and no
// rand() call when from == to.
func (r *Rand) Range(from, to int32) int32 {
	if from > to {
		from, to = to, from
	}
	if from == to {
		return from
	}
	return from + r.Int()%(to-from)
}

// Float is the game's rand(float from, float to) (CVector.cpp): 30000 steps over the range, no
// rand() call when the range is empty.
func (r *Rand) Float(from, to float32) float32 {
	if from > to {
		from, to = to, from
	}
	eccart := to - from
	if eccart == 0 {
		return from
	}
	precision := float32(30000) / eccart
	steps := int32(eccart * precision)
	return from + float32(r.Int()%steps)/precision
}
