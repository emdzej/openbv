package bvmath

import "math"

// Sinf and Cosf are musl's sinf and cosf (src/math/sinf.c, cosf.c, __sindf.c, __cosdf.c,
// __rem_pio2f.c; MIT licence, Copyright © 2005-2020 Rich Felker et al.), the C library of the wasm
// client whose in-process listen server is the reference the Go server is compared with
// (design/server.md §10.3). Their results are what rotateAboutAxis gets there, bit for bit: a
// correctly rounded cosf differs in the last bit for some angles, and the game's (short)(x*100)
// truncations can turn that into a different coordinate on the wire.
//
// The kernels compute in double; every product is rounded on its own (no fusion), as in the C.

const (
	cosC0 = -0x1ffffffd0c5e81.0p-54
	cosC1 = 0x155553e1053a42.0p-57
	cosC2 = -0x16c087e80f1e27.0p-62
	cosC3 = 0x199342e0ee5069.0p-68

	sinS1 = -0x15555554cbac77.0p-55
	sinS2 = 0x111110896efbb2.0p-59
	sinS3 = -0x1a00f9e2cae774.0p-65
	sinS4 = 0x16cd878c3b46a7.0p-71

	pio2x1 = 1 * math.Pi / 2
	pio2x2 = 2 * math.Pi / 2
	pio2x3 = 3 * math.Pi / 2
	pio2x4 = 4 * math.Pi / 2

	toint   = 1.5 / (1.0 / (1 << 52)) // 1.5/DBL_EPSILON
	invpio2 = 6.36619772367581382433e-01
	pio2_1  = 1.57079631090164184570e+00
	pio2_1t = 1.58932547735281966916e-08
)

func cosdf(x float64) float32 {
	z := float64(x * x)
	w := float64(z * z)
	r := cosC2 + float64(z*cosC3)
	return float32(((1.0 + float64(z*cosC0)) + float64(w*cosC1)) + float64(float64(w*z)*r))
}

func sindf(x float64) float32 {
	z := float64(x * x)
	w := float64(z * z)
	r := sinS3 + float64(z*sinS4)
	s := float64(z * x)
	return float32((x + float64(s*(sinS1+float64(z*sinS2)))) + float64(float64(s*w)*r))
}

// remPio2f is __rem_pio2f's medium-size path (|x| < 2^28*pi/2, all the game needs); larger
// arguments fall back to Go's reduction.
func remPio2f(x float32) (int, float64) {
	ix := math.Float32bits(x) & 0x7fffffff
	if ix < 0x4dc90fdb {
		fn := float64(float64(x)*invpio2) + toint - toint
		n := int(int32(fn))
		y := float64(x) - float64(fn*pio2_1) - float64(fn*pio2_1t)
		return n, y
	}
	n := math.Floor(float64(x)/(math.Pi/2) + .5)
	return int(int64(n)), math.Remainder(float64(x), math.Pi/2)
}

// Cosf is musl's cosf.
func Cosf(x float32) float32 {
	ix := math.Float32bits(x)
	sign := ix>>31 != 0
	ix &= 0x7fffffff
	xd := float64(x)
	if ix <= 0x3f490fda { // |x| ~<= pi/4
		if ix < 0x39800000 { // |x| < 2**-12
			return 1
		}
		return cosdf(xd)
	}
	if ix <= 0x407b53d1 { // |x| ~<= 5*pi/4
		if ix > 0x4016cbe3 { // |x| ~> 3*pi/4
			if sign {
				return -cosdf(xd + pio2x2)
			}
			return -cosdf(xd - pio2x2)
		}
		if sign {
			return sindf(xd + pio2x1)
		}
		return sindf(pio2x1 - xd)
	}
	if ix <= 0x40e231d5 { // |x| ~<= 9*pi/4
		if ix > 0x40afeddf { // |x| ~> 7*pi/4
			if sign {
				return cosdf(xd + pio2x4)
			}
			return cosdf(xd - pio2x4)
		}
		if sign {
			return sindf(-xd - pio2x3)
		}
		return sindf(xd - pio2x3)
	}
	if ix >= 0x7f800000 {
		return x - x
	}
	n, y := remPio2f(x)
	switch n & 3 {
	case 0:
		return cosdf(y)
	case 1:
		return sindf(-y)
	case 2:
		return -cosdf(y)
	default:
		return sindf(y)
	}
}

// Sinf is musl's sinf.
func Sinf(x float32) float32 {
	ix := math.Float32bits(x)
	sign := ix>>31 != 0
	ix &= 0x7fffffff
	xd := float64(x)
	if ix <= 0x3f490fda { // |x| ~<= pi/4
		if ix < 0x39800000 { // |x| < 2**-12
			return x
		}
		return sindf(xd)
	}
	if ix <= 0x407b53d1 { // |x| ~<= 5*pi/4
		if ix <= 0x4016cbe3 { // |x| ~<= 3pi/4
			if sign {
				return -cosdf(xd + pio2x1)
			}
			return cosdf(xd - pio2x1)
		}
		if sign {
			return sindf(-(xd + pio2x2))
		}
		return sindf(-(xd - pio2x2))
	}
	if ix <= 0x40e231d5 { // |x| ~<= 9*pi/4
		if ix <= 0x40afeddf { // |x| ~<= 7*pi/4
			if sign {
				return cosdf(xd + pio2x3)
			}
			return -cosdf(xd - pio2x3)
		}
		if sign {
			return sindf(xd + pio2x4)
		}
		return sindf(xd - pio2x4)
	}
	if ix >= 0x7f800000 {
		return x - x
	}
	n, y := remPio2f(x)
	switch n & 3 {
	case 0:
		return sindf(y)
	case 1:
		return cosdf(y)
	case 2:
		return sindf(-y)
	default:
		return -cosdf(y)
	}
}
