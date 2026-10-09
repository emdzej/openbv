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

// ProjEvent is one thing Projectile::update did: a message (Send: the type ID, B: the bytes in hex,
// padding zeroed) or a radiusHit call.
type ProjEvent struct {
	Send   *int   `json:"send"`
	B      string `json:"b"`
	Radius *V     `json:"radius"`
	R      F      `json:"r"`
	From   int    `json:"from"`
	W      int    `json:"w"`
	Same   bool   `json:"same"`
}

// ProjFrame is the projectile after one update. Flags: per player rocketInAir, detonateRocket,
// nbGrenadeLeft, life (bits).
type ProjFrame struct {
	Ev          []ProjEvent `json:"ev"`
	Pos         V           `json:"pos"`
	Vel         V           `json:"vel"`
	Del         bool        `json:"del"`
	Lock        bool        `json:"lock"`
	Stick       int         `json:"stick"`
	StickFor    F           `json:"stickFor"`
	Duration    F           `json:"duration"`
	DamageTime  int         `json:"damageTime"`
	ServerType  int         `json:"serverType"`
	ZookaDamage F           `json:"zookaDamage"`
	Flags       [][]any     `json:"flags"`
	Rand        uint32      `json:"rand"`
}

type ProjPlayer struct {
	Status      int  `json:"status"`
	Grenades    int  `json:"grenades"`
	Life        F    `json:"life"`
	RocketInAir bool `json:"rocketInAir"`
	Detonate    bool `json:"detonate"`
	Pos         V    `json:"pos"`
}

// ProjCase is a map, players and one projectile run through the original Projectile::update
// (cpp/proj_main.cpp).
type ProjCase struct {
	Width  int   `json:"w"`
	Height int   `json:"h"`
	Cells  []int `json:"cells"`
	SV     struct {
		RemoteDet   bool `json:"remoteDet"`
		ServerType  int  `json:"serverType"`
		ZookaRadius F    `json:"zookaRadius"`
		ZookaDamage F    `json:"zookaDamage"`
	} `json:"sv"`
	Players    []ProjPlayer `json:"players"`
	KillAt     int          `json:"killAt"`
	KillWho    int          `json:"killWho"`
	Seed       uint32       `json:"seed"`
	Type       int          `json:"type"`
	Pos        V            `json:"pos"`
	Vel        V            `json:"vel"`
	From       int          `json:"from"`
	Stick      int          `json:"stick"`
	StickFor   F            `json:"stickFor"`
	Thrown     F            `json:"thrown"`
	Lock       bool         `json:"lock"`
	DamageTime int          `json:"damageTime"`
	Index      int          `json:"index"`
	Frames     []ProjFrame  `json:"frames"`
}

// RefMap is a map in the cases: cells -1 passable, else the wall height.
type RefMap struct {
	Width  int   `json:"w"`
	Height int   `json:"h"`
	Cells  []int `json:"cells"`
}

// MinibotCase is a minibot thinking for 24 frames (CMiniBot::Think, Game::shootMinibotSV): events are
// hitSV calls ({hit, w, from, damage}) and messages.
type MinibotCase struct {
	RefMap
	GameType int `json:"gameType"`
	Gun      F   `json:"gun"`
	Players  []struct {
		Status int `json:"status"`
		Team   int `json:"team"`
		Pos    V   `json:"pos"`
	} `json:"players"`
	Bot      V      `json:"bot"`
	FireRate F      `json:"fireRate"`
	Nuke     bool   `json:"nuke"`
	Seed     uint32 `json:"seed"`
	Frames   []struct {
		Ev       []HitEvent `json:"ev"`
		Mouse    V          `json:"mouse"`
		FireRate F          `json:"fireRate"`
		Rand     uint32     `json:"rand"`
	} `json:"frames"`
}

// HitEvent is a hitSV call or a message.
type HitEvent struct {
	Hit    *int   `json:"hit"`
	W      int    `json:"w"`
	From   int    `json:"from"`
	Damage F      `json:"damage"`
	Send   *int   `json:"send"`
	B      string `json:"b"`
}

// RadiusCase is one Game::radiusHit.
type RadiusCase struct {
	RefMap
	Weapon  int  `json:"weapon"`
	Damage  F    `json:"damage"`
	Pos     V    `json:"pos"`
	R       F    `json:"r"`
	From    int  `json:"from"`
	Same    bool `json:"same"`
	Players []struct {
		Status int `json:"status"`
		Pos    V   `json:"pos"`
	} `json:"players"`
	Ev []HitEvent `json:"ev"`
}

// CollisionCase is Map::performCollision then Map::collisionClip.
type CollisionCase struct {
	RefMap
	Last      V `json:"last"`
	Pos       V `json:"pos"`
	Vel       V `json:"vel"`
	R         F `json:"r"`
	AfterPos  V `json:"afterPos"`
	AfterVel  V `json:"afterVel"`
	AfterLast V `json:"afterLast"`
	Clipped   V `json:"clipped"`
}

// Projectiles is testdata/projectiles.json.gz (cpp/proj_main.cpp).
type Projectiles struct {
	Proj      []ProjCase      `json:"proj"`
	Minibot   []MinibotCase   `json:"minibot"`
	Radius    []RadiusCase    `json:"radius"`
	Collision []CollisionCase `json:"collision"`
}

// LoadProjectiles reads testdata/projectiles.json.gz.
func LoadProjectiles() (*Projectiles, error) {
	_, file, _, _ := runtime.Caller(0)
	f, err := os.Open(filepath.Join(filepath.Dir(file), "testdata", "projectiles.json.gz"))
	if err != nil {
		return nil, err
	}
	defer f.Close()
	z, err := gzip.NewReader(f)
	if err != nil {
		return nil, err
	}
	var g Projectiles
	if err := json.NewDecoder(z).Decode(&g); err != nil {
		return nil, err
	}
	return &g, nil
}

// TeamPlayer is a player in the team cases (cpp/team_main.cpp), in slot order.
type TeamPlayer struct {
	ID     int `json:"id"`
	Team   int `json:"team"`
	Status int `json:"status"`
	Pos    V   `json:"pos"`
	Played F   `json:"played"`
	Score  int `json:"score"`
	FA     int `json:"fa"`
	Ret    int `json:"ret"`
	Slot   int `json:"slot"`
	TTS    F   `json:"tts"`
}

// TeamState is the game after a step: per player [team, status, score, flagAttempts, returns,
// spawnSlot, timeToSpawn bits, position], the flags, the scores (blue, red, blueWin, redWin) and the
// rand() state.
type TeamState struct {
	State     [][]any `json:"state"`
	FlagState [2]int  `json:"flagState"`
	FlagPos   [2]V    `json:"flagPos"`
	Scores    [4]int  `json:"scores"`
	Rand      uint32  `json:"rand"`
	Ev        []Event `json:"ev"`
}

// Event is a message sent (type ID, bytes in hex with the padding zeroed).
type Event struct {
	Send int    `json:"send"`
	B    string `json:"b"`
}

// CTFCase is players walking past the pods and dropped flags, frame by frame (Server::updateCTF).
type CTFCase struct {
	Pods      [2]V         `json:"pods"`
	FlagState [2]int       `json:"flagState"`
	FlagPos   [2]V         `json:"flagPos"`
	Wins      [2]int       `json:"wins"`
	Players   []TeamPlayer `json:"players"`
	Frames    []struct {
		Pos  []V       `json:"pos"`  // the players' positions before the update, in slot order
		Kill []int     `json:"kill"` // slots killed this frame (Player::kill), after their move
		Full bool      `json:"full"` // the state was recorded
		Ev   []Event   `json:"ev"`
		St   TeamState `json:"st"`
	} `json:"frames"`
}

// BalanceCase is Server::update's auto-balance block run for NFrames frames; Frames has the ones
// where something was sent, and the last.
type BalanceCase struct {
	GameType    int          `json:"gameType"`
	AutoBalance bool         `json:"autoBalance"`
	Time        int          `json:"time"`
	TTS         F            `json:"tts"`
	Timer       F            `json:"timer"`
	FlagState   [2]int       `json:"flagState"`
	Players     []TeamPlayer `json:"players"`
	NFrames     int          `json:"nframes"`
	Frames      []struct {
		Frame int       `json:"frame"`
		Timer F         `json:"timer"`
		Ev    []Event   `json:"ev"`
		St    TeamState `json:"st"`
	} `json:"frames"`
}

// AssignCase is one Game::assignPlayerTeam.
type AssignCase struct {
	Seed      uint32       `json:"seed"`
	Scores    [2]int       `json:"scores"`
	TTS       F            `json:"tts"`
	FlagState [2]int       `json:"flagState"`
	ID        int          `json:"id"`
	Req       int          `json:"req"`
	Players   []TeamPlayer `json:"players"`
	Ret       int          `json:"ret"`
	Ev        []Event      `json:"ev"`
	St        TeamState    `json:"st"`
}

// SpawnCase is one Game::spawnPlayer.
type SpawnCase struct {
	Seed      uint32       `json:"seed"`
	GameType  int          `json:"gameType"`
	SpawnType int          `json:"spawnType"`
	Limit     F            `json:"limit"`
	Left      F            `json:"left"`
	Pods      [2]V         `json:"pods"`
	Spawns    []V          `json:"spawns"`
	ID        int          `json:"id"`
	Players   []TeamPlayer `json:"players"`
	OK        bool         `json:"ok"`
	Spawned   bool         `json:"spawned"`
	At        V            `json:"at"`
	Slot      int          `json:"slot"`
	Past      bool         `json:"past"` // read dm_spawns[size]: undefined in the original
	Rand      uint32       `json:"rand"`
}

// ChampionCase is type 3's round reset (Server::update, Pro).
type ChampionCase struct {
	Limit   F            `json:"limit"`
	Left    F            `json:"left"`
	Players []TeamPlayer `json:"players"`
	After   F            `json:"after"`
	Ev      []Event      `json:"ev"`
	St      TeamState    `json:"st"`
}

// Team is testdata/team.json.gz (cpp/team_main.cpp).
type Team struct {
	CTF      []CTFCase      `json:"ctf"`
	Balance  []BalanceCase  `json:"balance"`
	Assign   []AssignCase   `json:"assign"`
	Spawn    []SpawnCase    `json:"spawn"`
	Champion []ChampionCase `json:"champion"`
}

// LoadTeam reads testdata/team.json.gz.
func LoadTeam() (*Team, error) {
	_, file, _, _ := runtime.Caller(0)
	f, err := os.Open(filepath.Join(filepath.Dir(file), "testdata", "team.json.gz"))
	if err != nil {
		return nil, err
	}
	defer f.Close()
	z, err := gzip.NewReader(f)
	if err != nil {
		return nil, err
	}
	var g Team
	if err := json.NewDecoder(z).Decode(&g); err != nil {
		return nil, err
	}
	return &g, nil
}
