// Package reftest holds values computed by the original C++ code, for the Go port's tests to match
// bit for bit. testdata/golden.json.gz is written by cpp/build.sh, which compiles the original
// functions (extracted verbatim from src/game) into a driver; every float is stored as its IEEE-754
// bits.
package reftest

import (
	"compress/gzip"
	"encoding/json"
	"math"
	"os"
	"path/filepath"
	"runtime"
	"sync"
)

// F is a float32 by its bits.
type F uint32

func (f F) Float() float32 { return math.Float32frombits(uint32(f)) }

// V is a CVector3f by its bits.
type V [3]F

func (v V) Vec() [3]float32 { return [3]float32{v[0].Float(), v[1].Float(), v[2].Float()} }

type Rotate struct {
	P V `json:"p"`
	A F `json:"a"`
	X V `json:"x"`
	R V `json:"r"`
}

type Segment struct {
	P1  V    `json:"p1"`
	P2  V    `json:"p2"`
	C   V    `json:"c"`
	R   F    `json:"r"`
	Hit bool `json:"hit"`
	Out V    `json:"out"`
}

type Ray struct {
	P1  V    `json:"p1"`
	P2  V    `json:"p2"`
	Hit bool `json:"hit"`
	Out V    `json:"out"`
	N   V    `json:"n"` // starts as (7,7,7): unchanged unless a tile set it
}

type RayMap struct {
	W     int   `json:"w"`
	H     int   `json:"h"`
	Cells []int `json:"cells"` // -1 passable, else the wall height
	Rays  []Ray `json:"rays"`
}

type Spread struct {
	Seed uint32 `json:"seed"`
	Imp  F      `json:"imp"`
	Dir  V      `json:"dir"`
	P1   V      `json:"p1"`
	P2   V      `json:"p2"`
	Next int32  `json:"next"` // the rand() after the spread: the number of calls matches
}

type Reflect struct {
	U V `json:"u"`
	N V `json:"n"`
	R V `json:"r"` // reflect(u, n) * .65, as a bounce
}

// Damage is one call of hitSV's damage part. SV: serverType, photonType, subGameType, then smg,
// sniper, shotty, dmg, cg, ft damages, ftMaxRange, photon vertical shift, coefficient, horizontal
// shift, distance multiplier (the last eleven as float bits).
type Damage struct {
	SV       []uint32 `json:"sv"`
	Weapon   int      `json:"weapon"`
	WDamage  F        `json:"wdamage"`
	ShotFrom V        `json:"shotFrom"`
	Pos      V        `json:"pos"`
	Self     bool     `json:"self"`
	Prot     F        `json:"prot"`
	Immune   F        `json:"immune"`
	Life     F        `json:"life"`
	DamageIn F        `json:"damage"`
	Out      F        `json:"out"`
}

type Golden struct {
	Rotate  []Rotate  `json:"rotate"`
	Segment []Segment `json:"segment"`
	Ray     []RayMap  `json:"ray"`
	Spread  []Spread  `json:"spread"`
	Reflect []Reflect `json:"reflect"`
	Damage  []Damage  `json:"damage"`
}

var (
	once   sync.Once
	golden *Golden
	err    error
)

// Load reads testdata/golden.json.gz (once).
func Load() (*Golden, error) {
	once.Do(func() {
		_, file, _, _ := runtime.Caller(0)
		f, e := os.Open(filepath.Join(filepath.Dir(file), "testdata", "golden.json.gz"))
		if e != nil {
			err = e
			return
		}
		defer f.Close()
		z, e := gzip.NewReader(f)
		if e != nil {
			err = e
			return
		}
		var g Golden
		if e := json.NewDecoder(z).Decode(&g); e != nil {
			err = e
			return
		}
		golden = &g
	})
	return golden, err
}
