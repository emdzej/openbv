package bvmap

import "github.com/emdzej/openbv/server/internal/bvmath"

func fabsf(x float32) float32 {
	if x < 0 {
		return -x
	}
	return x
}

// lerp is p1 + (p2 - p1) * percent, rounded as the CVector3f operators round it.
func lerp(p1, p2 bvmath.Vec3, percent float32) bvmath.Vec3 {
	return p1.Add(p2.Sub(p1).Scale(percent))
}

// RayTest is Map::rayTest (Map.cpp:1434) for the tile maps (the dko branch is dead: its folder,
// main/modelmaps_______, never exists). It reports a hit and then moves *p2 to the hit point and
// sets *normal; with p1 inside a wall it sets *p2 = p1 and leaves the normal alone. A p1 outside the
// map is no hit, p2 untouched.
func (m *Map) RayTest(p1 bvmath.Vec3, p2, normal *bvmath.Vec3) bool {
	i := int(p1[0])
	j := int(p1[1])
	if i >= 0 && i < m.Width && j >= 0 && j < m.Height {
		c := m.Cells[j*m.Width+i]
		if !c.Passable && p1[2] < float32(c.Height) {
			*p2 = p1
			return true
		}
	} else {
		return false
	}

	const (
		dirX = iota
		dirXNeg
		dirY
		dirYNeg
	)
	var sens int
	if fabsf(p2[0]-p1[0]) > fabsf(p2[1]-p1[1]) {
		if p2[0] > p1[0] {
			sens = dirX
		} else {
			sens = dirXNeg
		}
	} else {
		if p2[1] > p1[1] {
			sens = dirY
		} else {
			sens = dirYNeg
		}
	}

	var percent float32
	for {
		if i < 0 || i >= m.Width || j < 0 || j >= m.Height ||
			(sens == dirX && i > int(p2[0])) ||
			(sens == dirXNeg && i < int(p2[0])) ||
			(sens == dirY && j > int(p2[1])) ||
			(sens == dirYNeg && j < int(p2[1])) {
			return false
		}
		switch sens {
		case dirX:
			if m.rayTileTest(i, j, p1, p2, normal) || m.rayTileTest(i, j-1, p1, p2, normal) || m.rayTileTest(i, j+1, p1, p2, normal) {
				return true
			}
			i++
			percent = (float32(i) - p1[0]) / fabsf(p2[0]-p1[0])
			j = int(p1[1] + float32((p2[1]-p1[1])*percent))
		case dirXNeg:
			if m.rayTileTest(i, j, p1, p2, normal) || m.rayTileTest(i, j-1, p1, p2, normal) || m.rayTileTest(i, j+1, p1, p2, normal) {
				return true
			}
			i--
			percent = (p1[0] - float32(i+1)) / fabsf(p2[0]-p1[0])
			j = int(p1[1] + float32((p2[1]-p1[1])*percent))
		case dirY:
			if m.rayTileTest(i, j, p1, p2, normal) || m.rayTileTest(i-1, j, p1, p2, normal) || m.rayTileTest(i+1, j, p1, p2, normal) {
				return true
			}
			j++
			percent = (float32(j) - p1[1]) / fabsf(p2[1]-p1[1])
			i = int(p1[0] + float32((p2[0]-p1[0])*percent))
		case dirYNeg:
			if m.rayTileTest(i, j, p1, p2, normal) || m.rayTileTest(i-1, j, p1, p2, normal) || m.rayTileTest(i+1, j, p1, p2, normal) {
				return true
			}
			j--
			percent = (p1[1] - float32(j+1)) / fabsf(p2[1]-p1[1])
			i = int(p1[0] + float32((p2[0]-p1[0])*percent))
		}
	}
}

// rayTileTest is Map::rayTileTest (Map.h:415): the segment against one tile, [x,x+1]x[y,y+1].
// Passable tiles have only the floor; walls their top and four sides, tested in this order.
func (m *Map) rayTileTest(x, y int, p1 bvmath.Vec3, p2, normal *bvmath.Vec3) bool {
	if !(x >= 0 && x < m.Width && y >= 0 && y < m.Height) {
		return false
	}
	x1 := float32(x)
	x2 := float32(x) + 1
	y1 := float32(y)
	y2 := float32(y) + 1
	c := m.Cells[y*m.Width+x]
	height := float32(c.Height)
	var percent float32
	var p bvmath.Vec3

	if c.Passable {
		if p1[2] > 0 && p2[2] <= 0 {
			percent = p1[2] / fabsf(p2[2]-p1[2])
			p = lerp(p1, *p2, percent)
			if p[0] >= x1 && p[0] <= x2 && p[1] >= y1 && p[1] <= y2 {
				*p2 = p
				*normal = bvmath.Vec3{0, 0, 1}
				return true
			}
		}
		return false
	}

	// the top
	if p1[2] > height && p2[2] <= height {
		percent = (p1[2] - height) / fabsf((p2[2]-height)-(p1[2]-height))
		p = lerp(p1, *p2, percent)
		if p[0] >= x1 && p[0] <= x2 && p[1] >= y1 && p[1] <= y2 {
			*p2 = p
			*normal = bvmath.Vec3{0, 0, 1}
			return true
		}
	}
	// side x1
	if p1[0] <= x1 && p2[0] > x1 {
		percent = fabsf(x1-p1[0]) / fabsf(p2[0]-p1[0])
		p = lerp(p1, *p2, percent)
		if p[1] <= y2 && p[1] >= y1 && p[2] < height {
			*p2 = p
			*normal = bvmath.Vec3{-1, 0, 0}
			return true
		}
	}
	// side x2
	if p1[0] >= x2 && p2[0] < x2 {
		percent = fabsf(p1[0]-x2) / fabsf(p2[0]-p1[0])
		p = lerp(p1, *p2, percent)
		if p[1] <= y2 && p[1] >= y1 && p[2] < height {
			*p2 = p
			*normal = bvmath.Vec3{1, 0, 0}
			return true
		}
	}
	// side y1
	if p1[1] <= y1 && p2[1] > y1 {
		percent = fabsf(y1-p1[1]) / fabsf(p2[1]-p1[1])
		p = lerp(p1, *p2, percent)
		if p[0] <= x2 && p[0] >= x1 && p[2] < height {
			*p2 = p
			*normal = bvmath.Vec3{0, -1, 0}
			return true
		}
	}
	// side y2
	if p1[1] >= y2 && p2[1] < y2 {
		percent = fabsf(p1[1]-y2) / fabsf(p2[1]-p1[1])
		p = lerp(p1, *p2, percent)
		if p[0] <= x2 && p[0] >= x1 && p[2] < height {
			*p2 = p
			*normal = bvmath.Vec3{0, 1, 0}
			return true
		}
	}
	return false
}
