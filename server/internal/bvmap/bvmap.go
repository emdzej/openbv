// Package bvmap reads Babo Violent 2 maps (.bvm) and answers the server's questions about them.
// The loader follows Map::Map (src/game/Map.cpp:44, the file part from :217); FileIO's getInt is a
// 16-bit signed value (FileIO.cpp), getULong 32-bit, getVector3f three floats, all little-endian.
package bvmap

import (
	"encoding/binary"
	"errors"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"sort"
	"strings"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
)

// Cell is a map tile (map_cell): byte 0 bit 7 passable, bits 0-6 wall height; byte 1 is the dirt
// splat, which only the client draws.
type Cell struct {
	Passable bool
	Height   uint8
}

// Map is what the server keeps of a map.
type Map struct {
	Name       string // as the game names it: the file name without .bvm, at most 15 characters
	Version    uint32
	Author     string
	Width      int
	Height     int
	Cells      []Cell // row-major, y outer
	FlagPodPos [2]bvmath.Vec3
	Objective  [2]bvmath.Vec3
	DMSpawns   []bvmath.Vec3
	BlueSpawns []bvmath.Vec3
	RedSpawns  []bvmath.Vec3
	File       []byte // the .bvm as it is on disk, for map downloads
}

var ErrVersion = errors.New("bvmap: unknown map version")

type reader struct {
	b   []byte
	off int
	err error
}

func (r *reader) need(n int) bool {
	if r.err != nil {
		return false
	}
	if r.off+n > len(r.b) {
		r.err = fmt.Errorf("bvmap: truncated at %d", r.off)
		return false
	}
	return true
}

func (r *reader) u32() uint32 {
	if !r.need(4) {
		return 0
	}
	v := binary.LittleEndian.Uint32(r.b[r.off:])
	r.off += 4
	return v
}

func (r *reader) i16() int16 {
	if !r.need(2) {
		return 0
	}
	v := int16(binary.LittleEndian.Uint16(r.b[r.off:]))
	r.off += 2
	return v
}

func (r *reader) u8() uint8 {
	if !r.need(1) {
		return 0
	}
	v := r.b[r.off]
	r.off++
	return v
}

func (r *reader) vec3() bvmath.Vec3 {
	var v bvmath.Vec3
	for i := range v {
		v[i] = math.Float32frombits(r.u32())
	}
	return v
}

func (r *reader) bytes(n int) []byte {
	if !r.need(n) {
		return nil
	}
	v := r.b[r.off : r.off+n]
	r.off += n
	return v
}

func (r *reader) spawns() []bvmath.Vec3 {
	n := int(r.i16())
	var out []bvmath.Vec3
	for i := 0; i < n && r.err == nil; i++ {
		out = append(out, r.vec3())
	}
	return out
}

// Parse reads a .bvm (Map.cpp:217). Name is the game's map name.
func Parse(name string, data []byte) (*Map, error) {
	m := &Map{Name: name, File: data}
	r := &reader{b: data}
	m.Version = r.u32()
	readCells := func() {
		m.Width = int(r.i16())
		m.Height = int(r.i16())
		if m.Width <= 0 || m.Height <= 0 || m.Width*m.Height > 1<<20 {
			r.err = fmt.Errorf("bvmap: bad size %dx%d", m.Width, m.Height)
			return
		}
		m.Cells = make([]Cell, m.Width*m.Height)
		for j := 0; j < m.Height && r.err == nil; j++ {
			for i := 0; i < m.Width; i++ {
				d := r.u8()
				m.Cells[j*m.Width+i] = Cell{Passable: d&128 != 0, Height: d & 127}
				r.u8() // the dirt splat
			}
		}
	}
	switch m.Version {
	case 10010:
		readCells()
	case 10011, 20201:
		if m.Version == 20201 {
			r.i16() // theme
			r.i16() // weather
		}
		readCells()
		m.FlagPodPos[0] = r.vec3()
		m.FlagPodPos[1] = r.vec3()
		m.Objective[0] = r.vec3()
		m.Objective[1] = r.vec3()
		m.DMSpawns = r.spawns()
		m.BlueSpawns = r.spawns()
		m.RedSpawns = r.spawns()
	case 20202:
		author := r.bytes(25)
		if author != nil {
			author[24] = 0
			m.Author = proto.CString(author)
		}
		r.i16() // theme
		r.i16() // weather
		readCells()
		m.DMSpawns = r.spawns()
		// four game-type blocks, in any order (GAME_TYPE_COUNT = 4)
		for gt := 0; gt < 4 && r.err == nil; gt++ {
			switch r.i16() {
			case proto.GameTypeDM, proto.GameTypeTDM:
			case proto.GameTypeCTF:
				m.FlagPodPos[0] = r.vec3()
				m.FlagPodPos[1] = r.vec3()
			case proto.GameTypeSND:
				m.Objective[0] = r.vec3()
				m.Objective[1] = r.vec3()
				m.BlueSpawns = r.spawns()
				m.RedSpawns = r.spawns()
			default:
				// the original logs it and reads nothing more for this block (the rest is then
				// misread, which it keeps doing)
			}
		}
	default:
		// the original reads nothing and the map has no cells (it would crash on use)
		return nil, fmt.Errorf("%w %d", ErrVersion, m.Version)
	}
	if r.err != nil {
		return nil, r.err
	}
	return m, nil
}

// MapName is the game's name for a map file: without .bvm, cut to 15 characters (Map.cpp:44).
func MapName(name string) string {
	name = strings.TrimSuffix(name, ".bvm")
	if len(name) > 15 {
		name = name[:15]
	}
	return name
}

// Library finds maps in a content folder's main/maps, case-insensitively as on Windows.
type Library struct{ Dir string }

// Path returns the file of a map, if it exists. Only plain names are accepted: the original took
// any client-supplied name (path traversal, design/server.md §2.3).
func (l Library) Path(name string) (string, bool) {
	if name == "" || strings.ContainsAny(name, "/\\:") || strings.Contains(name, "..") {
		return "", false
	}
	entries, err := os.ReadDir(l.Dir)
	if err != nil {
		return "", false
	}
	for _, e := range entries {
		if !e.IsDir() && strings.EqualFold(e.Name(), name+".bvm") {
			return filepath.Join(l.Dir, e.Name()), true
		}
	}
	return "", false
}

// Load reads a map by name.
func (l Library) Load(name string) (*Map, error) {
	p, ok := l.Path(name)
	if !ok {
		return nil, fmt.Errorf("map not found: %s", name)
	}
	data, err := os.ReadFile(p)
	if err != nil {
		return nil, err
	}
	return Parse(MapName(name), data)
}

// List is every map, in NTFS order (upper-cased names), without the extension: what FindFirstFile
// gave the original (Server::populateMapList).
func (l Library) List() []string {
	entries, _ := os.ReadDir(l.Dir)
	var out []string
	for _, e := range entries {
		if !e.IsDir() && strings.HasSuffix(strings.ToLower(e.Name()), ".bvm") {
			out = append(out, e.Name()[:len(e.Name())-4])
		}
	}
	sort.Slice(out, func(i, j int) bool { return strings.ToUpper(out[i]) < strings.ToUpper(out[j]) })
	return out
}

// Valid is IsMapValid (Map.cpp:1740) for the Pro build.
func Valid(m *Map, gameType int) bool {
	switch gameType {
	case proto.GameTypeDM, proto.GameTypeTDM, proto.GameTypeSND:
		return len(m.DMSpawns) >= 1
	case proto.GameTypeCTF:
		return len(m.DMSpawns) >= 1 && !m.FlagPodPos[0].Equal(bvmath.Vec3{}) && !m.FlagPodPos[1].Equal(bvmath.Vec3{})
	}
	return true
}

// Area is the passable cells without the border, as Server::queryNextMap counts it.
func (m *Map) Area() int {
	n := 0
	for j := 1; j < m.Height-1; j++ {
		for i := 1; i < m.Width-1; i++ {
			if m.Cells[j*m.Width+i].Passable {
				n++
			}
		}
	}
	return n
}
