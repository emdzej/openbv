package bvmap

import "github.com/emdzej/openbv/server/internal/bvmath"

// Collision constants (Map.h:98).
const (
	collisionEpsilon = float32(0.05) // COLLISION_EPSILON
	bounceFactor     = float32(0.45) // BOUNCE_FACTOR
)

// passableAt is cells[i].passable with the original's linear index: neighbours of edge cells wrap
// into the next or previous row as in the C++. An index outside the array (the original read past
// it) is a wall (design/server.md §8.4).
func (m *Map) passableAt(i int) bool {
	if i < 0 || i >= len(m.Cells) {
		return false
	}
	return m.Cells[i].Passable
}

// PerformCollision is Map::performCollision (MapRender.cpp:425), the cell part (maps have no
// collision mesh: main/modelmaps_______ doesn't exist): the moving circle of radius against the walls
// around its cell, Y first then X, each bouncing the velocity by BOUNCE_FACTOR; lastPos ends equal to
// pos. The server uses it for minibots (Game.cpp:543).
func (m *Map) PerformCollision(lastPos, pos, vel *bvmath.Vec3, radius float32) {
	if len(m.Cells) > 0 {
		w := m.Width
		x := int(pos[0])
		y := int(pos[1])
		if x < 1 {
			x = 1
		}
		if y < 1 {
			y = 1
		}
		if x >= m.Width-1 {
			x = m.Width - 2
		}
		if y >= m.Height-1 {
			y = m.Height - 2
		}
		// Y
		if vel[1] < 0 {
			for _, cx := range [3]int{x, x - 1, x + 1} {
				if !m.passableAt((y-1)*w + cx) {
					if lastPos[0]-radius <= float32(cx)+1 && lastPos[0]+radius >= float32(cx) &&
						pos[1]-radius <= float32(y-1)+1 && pos[1]+radius >= float32(y-1) {
						pos[1] = float32(y-1) + 1 + radius + collisionEpsilon
						vel[1] = -vel[1] * bounceFactor
					}
				}
			}
		} else if vel[1] > 0 {
			for _, cx := range [3]int{x, x - 1, x + 1} {
				if !m.passableAt((y+1)*w + cx) {
					if lastPos[0]-radius <= float32(cx)+1 && lastPos[0]+radius >= float32(cx) &&
						pos[1]-radius <= float32(y+1)+1 && pos[1]+radius >= float32(y+1) {
						pos[1] = float32(y+1) - radius - collisionEpsilon
						vel[1] = -vel[1] * bounceFactor
					}
				}
			}
		}
		// X
		if vel[0] < 0 {
			for _, cy := range [3]int{y, y - 1, y + 1} {
				if !m.passableAt(cy*w + (x - 1)) {
					if pos[0]-radius <= float32(x-1)+1 && pos[0]+radius >= float32(x-1) &&
						lastPos[1]-radius <= float32(cy)+1 && lastPos[1]+radius >= float32(cy) {
						pos[0] = float32(x-1) + 1 + radius + collisionEpsilon
						vel[0] = -vel[0] * bounceFactor
					}
				}
			}
		} else if vel[0] > 0 {
			for _, cy := range [3]int{y, y - 1, y + 1} {
				if !m.passableAt(cy*w + (x + 1)) {
					if pos[0]-radius <= float32(x+1)+1 && pos[0]+radius >= float32(x+1) &&
						lastPos[1]-radius <= float32(cy)+1 && lastPos[1]+radius >= float32(cy) {
						pos[0] = float32(x+1) - radius - collisionEpsilon
						vel[0] = -vel[0] * bounceFactor
					}
				}
			}
		}
	}
	*lastPos = *pos
}

// CollisionClip is Map::collisionClip (MapRender.cpp:650): push the circle out of the walls next to
// its cell, clamp it inside the border, and out of a wall cell it stands in, to the nearest open side.
func (m *Map) CollisionClip(pos *bvmath.Vec3, radius float32) {
	w := m.Width
	x := int(pos[0])
	y := int(pos[1])
	if len(m.Cells) > 0 {
		if pos[0]+radius+collisionEpsilon > float32(x)+1 && !m.passableAt(y*w+(x+1)) {
			pos[0] = float32(x) + 1 - radius - collisionEpsilon
		}
		if pos[0]-radius-collisionEpsilon < float32(x) && !m.passableAt(y*w+(x-1)) {
			pos[0] = float32(x) + radius + collisionEpsilon
		}
		if pos[1]+radius+collisionEpsilon > float32(y)+1 && !m.passableAt((y+1)*w+x) {
			pos[1] = float32(y) + 1 - radius - collisionEpsilon
		}
		if pos[1]-radius-collisionEpsilon < float32(y) && !m.passableAt((y-1)*w+x) {
			pos[1] = float32(y) + radius + collisionEpsilon
		}
	}
	// the universe
	if x <= 0 {
		pos[0] = 1 + radius + collisionEpsilon
	}
	if x >= m.Width-1 {
		pos[0] = float32(m.Width-1) - radius - collisionEpsilon
	}
	if y <= 0 {
		pos[1] = 1 + radius + collisionEpsilon
	}
	if y >= m.Height-1 {
		pos[1] = float32(m.Height-1) - radius - collisionEpsilon
	}
	if !m.passableAt(y*w + x) {
		possible := [4]bool{
			m.passableAt(y*w + (x - 1)),
			m.passableAt(y*w + (x + 1)),
			m.passableAt((y-1)*w + x),
			m.passableAt((y+1)*w + x),
		}
		dis := [4]float32{
			pos[0] - float32(x),
			1 - (pos[0] - float32(x)),
			pos[1] - float32(y),
			1 - (pos[1] - float32(y)),
		}
		currentMin := float32(2)
		if possible[0] && dis[0] < currentMin {
			pos[0] = float32(x) - radius - collisionEpsilon
			currentMin = dis[0]
		}
		if possible[1] && dis[1] < currentMin {
			pos[0] = float32(x) + 1 + radius + collisionEpsilon
			currentMin = dis[1]
		}
		if possible[2] && dis[2] < currentMin {
			pos[1] = float32(y) - radius - collisionEpsilon
			currentMin = dis[2]
		}
		if possible[3] && dis[3] < currentMin {
			pos[1] = float32(y) + 1 + radius + collisionEpsilon
		}
	}
}
