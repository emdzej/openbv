// Package bvmath is the arithmetic the original game does, bit for bit where it matters: float32
// vectors (the game's CVector3f), cubicSpline (Helper.cpp:435) and the C runtime's rand() as the
// Windows server had it (MSVC's LCG) with the game's rand(int,int) and rand(float,float) helpers
// (CVector.cpp).
package bvmath

import "math"

// Epsilon is the game's CVector EPSILON (src/game/CVector.h: 0.0001), used by its comparisons.
const Epsilon = 0.0001

// Vec3 is CVector3f.
//
// Bit for bit: Go may fuse a*b+c into one rounding (it does on arm64); the C++ (and the wasm client
// that hosts listen servers) rounds every operation. Products are therefore wrapped in float32(...),
// which the spec defines as a rounding that prevents fusion.
type Vec3 [3]float32

func (a Vec3) Add(b Vec3) Vec3 { return Vec3{a[0] + b[0], a[1] + b[1], a[2] + b[2]} }
func (a Vec3) Sub(b Vec3) Vec3 { return Vec3{a[0] - b[0], a[1] - b[1], a[2] - b[2]} }
func (a Vec3) Scale(s float32) Vec3 {
	return Vec3{float32(a[0] * s), float32(a[1] * s), float32(a[2] * s)}
}
func (a Vec3) Div(s float32) Vec3 { return Vec3{a[0] / s, a[1] / s, a[2] / s} }
func (a Vec3) Neg() Vec3          { return Vec3{-a[0], -a[1], -a[2]} }

// Length is CVector3f::length: sqrtf(x*x + y*y + z*z) (a double sqrt rounded to float is sqrtf).
func (a Vec3) Length() float32 {
	return float32(math.Sqrt(float64(float32(a[0]*a[0]) + float32(a[1]*a[1]) + float32(a[2]*a[2]))))
}

func (a Vec3) IsZero() bool { return a.Equal(Vec3{}) }

// DistanceSquared is distanceSquared (CVector.cpp:255).
func DistanceSquared(a, b Vec3) float32 {
	d0, d1, d2 := a[0]-b[0], a[1]-b[1], a[2]-b[2]
	return float32(d0*d0) + float32(d1*d1) + float32(d2*d2)
}

// Distance is distance (CVector.cpp:239).
func Distance(a, b Vec3) float32 { return float32(math.Sqrt(float64(DistanceSquared(a, b)))) }

// Dot is dot (CVector.cpp:268).
func Dot(a, b Vec3) float32 { return float32(a[0]*b[0]) + float32(a[1]*b[1]) + float32(a[2]*b[2]) }

// Cross is cross (CVector.cpp:347).
func Cross(u, v Vec3) Vec3 {
	return Vec3{
		float32(u[1]*v[2]) - float32(u[2]*v[1]),
		-(float32(u[0]*v[2]) - float32(u[2]*v[0])),
		float32(u[0]*v[1]) - float32(u[1]*v[0]),
	}
}

// Normalize is normalize(CVector3f&) (CVector.cpp:304): times the reciprocal of the length (not
// divided by it: the float results differ), times 0 for a zero vector.
func Normalize(v Vec3) Vec3 {
	var x float32
	if y := v.Length(); y != 0 {
		x = 1 / y
	}
	return v.Scale(x)
}

// Projection is projection (CVector.cpp:283): (Onv * dot(u, Onv)) / |Onv|.
func Projection(u, onv Vec3) Vec3 {
	a := Dot(u, onv)
	return onv.Scale(a).Div(onv.Length())
}

// Reflect is reflect (CVector.cpp:293): u - projection(u, normal) * 2.
func Reflect(u, normal Vec3) Vec3 { return u.Sub(Projection(u, normal).Scale(2)) }

// ToRadiant and ToDegree are CVector.h's TO_RADIANT and TO_DEGREE.
const (
	ToRadiant = float32(0.017453)
	ToDegree  = float32(57.295780)
	Pi        = float32(3.141593)
)

// RotateAboutAxis is rotateAboutAxis (CVector.cpp:365), term by term, with musl's cosf and sinf
// (muslmath.go), as the wasm client computes it.
func RotateAboutAxis(point Vec3, angle float32, axis Vec3) Vec3 {
	angle = float32(angle * ToRadiant)
	c := Cosf(angle)
	s := Sinf(angle)
	m := 1 - c
	x, y, z := axis[0], axis[1], axis[2]
	r00 := c + float32(float32(m*x)*x)
	r01 := float32(float32(m*x)*y) - float32(z*s)
	r02 := float32(float32(m*x)*z) + float32(y*s)
	r10 := float32(float32(m*x)*y) + float32(z*s)
	r11 := c + float32(float32(m*y)*y)
	r12 := float32(float32(m*y)*z) - float32(x*s)
	r20 := float32(float32(m*x)*z) - float32(y*s)
	r21 := float32(float32(m*y)*z) + float32(x*s)
	r22 := c + float32(float32(m*z)*z)
	return Vec3{
		float32(r00*point[0]) + float32(r01*point[1]) + float32(r02*point[2]),
		float32(r10*point[0]) + float32(r11*point[1]) + float32(r12*point[2]),
		float32(r20*point[0]) + float32(r21*point[1]) + float32(r22*point[2]),
	}
}

// SegmentToSphere is segmentToSphere (Helper.cpp:476): whether segment p1-p2 passes within radius
// of c; on a hit *p2 moves to the segment's closest point to c. A zero-length segment divides by
// zero (NaN: no hit), as in the original.
func SegmentToSphere(p1 Vec3, p2 *Vec3, c Vec3, radius float32) bool {
	u := p2.Sub(p1)
	l := u.Length()
	u = u.Div(l)
	p := c.Sub(p1)
	d := Dot(p, u)
	var r Vec3
	if d < 0 {
		r = p1
	} else if d > l {
		r = *p2
	} else {
		r = p1.Add(u.Scale(d))
	}
	if DistanceSquared(r, c) <= float32(radius*radius) {
		*p2 = r
		return true
	}
	return false
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
	a := float32(float32((1-t)*(1-t)) * (1 - t))
	b := float32((1 - t) * (1 - t))
	r0 := x0.Scale(a)
	r1 := x1.Scale(3).Scale(t).Scale(b)
	r2 := x2.Scale(3).Scale(float32(t * t)).Scale(1 - t)
	r3 := x3.Scale(float32(float32(t*t) * t))
	return r0.Add(r1).Add(r2).Add(r3)
}

// Rand is the C library's rand() of the Windows build (MSVC): seed = seed*214013 + 2531011,
// result (seed >> 16) & 0x7fff. One per session; the original seeded it with srand(time(0)).
type Rand struct{ seed uint32 }

// NewRand seeds a generator (srand).
func NewRand(seed uint32) *Rand { return &Rand{seed: seed} }

// Seed is srand.
func (r *Rand) Seed(seed uint32) { r.seed = seed }

// State is the generator's state (tests compare it with the C++'s).
func (r *Rand) State() uint32 { return r.seed }

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
	return from + float32(r.Int()%steps)/precision // a division: nothing to fuse
}
