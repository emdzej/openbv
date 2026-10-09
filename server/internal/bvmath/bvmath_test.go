package bvmath

import "testing"

// MSVC's rand() after srand(1): the well-known first values.
func TestMSVCRand(t *testing.T) {
	r := NewRand(1)
	want := []int32{41, 18467, 6334, 26500, 19169, 15724, 11478, 29358, 26962, 24464}
	for i, w := range want {
		if got := r.Int(); got != w {
			t.Fatalf("rand #%d = %d, want %d", i, got, w)
		}
	}
}

func TestRangeExcludesUpperBound(t *testing.T) {
	r := NewRand(7)
	for i := 0; i < 1000; i++ {
		if v := r.Range(0, 5); v < 0 || v >= 5 {
			t.Fatalf("rand(0,5) = %d", v)
		}
	}
	before := *r
	if v := r.Range(3, 3); v != 3 || *r != before {
		t.Fatalf("rand(3,3) = %d or consumed a value", v)
	}
}

func TestFloatNoCallOnEmptyRange(t *testing.T) {
	r := NewRand(9)
	before := *r
	if v := r.Float(0, 0); v != 0 || *r != before {
		t.Fatal("rand(0.f, 0.f) consumed a value")
	}
	for i := 0; i < 1000; i++ {
		if v := r.Float(-1, 1); v < -1 || v >= 1 {
			t.Fatalf("rand(-1,1) = %v", v)
		}
	}
}

func TestCubicSplineEnds(t *testing.T) {
	a, b, c, d := Vec3{1, 2, 3}, Vec3{4, 5, 6}, Vec3{7, 8, 9}, Vec3{10, 11, 12}
	if CubicSpline(a, b, c, d, 0) != a || CubicSpline(a, b, c, d, 1) != d {
		t.Fatal("spline ends")
	}
}
