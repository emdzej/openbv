// Package proto is Babo Violent 2.11's game protocol (src/game/netPacket.h): message IDs and the
// structs, laid out exactly as the 32-bit client and server have them in memory (natural alignment,
// little-endian), which is what goes on the wire: each message is `sizeof(struct)` bytes, padding
// included. design/server.md Appendix A has the layouts; proto_test.go checks every struct here
// against it.
//
// Field types: C char = int8 (signed, as MSVC and the wasm client have it), unsigned char and bool =
// uint8, short = int16, int/long/int32_t = int32, unsigned = uint32, float = float32. Blank `_` fields
// are padding: written as zeros, ignored on read.
package proto

import (
	"bytes"
	"encoding/binary"
	"fmt"
)

// Message type IDs (netPacket.h, ChecksumQuery for the hash seed ones).
const (
	ClsvPong                = 1
	ClsvSpawnRequest        = 2
	ClsvPlayerShoot         = 3
	ClsvGameVersionAccepted = 4
	ClsvPickupRequest       = 5
	ClsvAdminRequest        = 6
	ClsvVote                = 7
	ClsvMapListRequest      = 8

	SvclNewPlayer            = 101
	SvclServerInfo           = 102
	SvclServerDisconnect     = 103
	SvclPlayerDisconnect     = 104
	SvclPlayerEnumState      = 105
	SvclPing                 = 106
	SvclPlayerPing           = 107
	SvclPlayerSpawn          = 108
	SvclSvChange             = 109
	SvclPlayerShoot          = 110
	SvclDeleteProjectile     = 111
	SvclProjectileCoordFrame = 112
	SvclExplosion            = 113
	SvclPlayerHit            = 114
	SvclPlaySound            = 115
	SvclGameVersion          = 116
	SvclSynchronizeTimer     = 117
	SvclChangeFlagState      = 118
	SvclDropFlag             = 119
	SvclFlagEnum             = 120
	SvclGameState            = 121
	SvclChangeGameType       = 122
	SvclMapChange            = 123
	SvclPickupItem           = 124
	SvclFlameStickToPlayer   = 125
	SvclConsole              = 126
	SvclAdminAccepted        = 127
	SvclAutobalance          = 128
	SvclEndVote              = 129
	SvclUpdateVote           = 130
	SvclVoteResult           = 131
	SvclMsg                  = 132
	SvclPlayerUpdateStats    = 133
	SvclBadChecksumEntity    = 134
	SvclBadChecksumInfo      = 135
	ClsvSvclPlayerInfo       = 201
	ClsvSvclChat             = 202
	ClsvSvclTeamRequest      = 203
	ClsvSvclPlayerCoordFrame = 204
	ClsvSvclPlayerChangeName = 205
	ClsvSvclPlayerProjectile = 206
	ClsvSvclPlayerShootMelee = 207
	ClsvSvclVoteRequest      = 208
	ClsvMapRequest           = 209
	SvclMapChunk             = 210
	SvclMapList              = 211
	ClsvSvclPlayerUpdateSkin = 212
	BroadcastQuery           = 301
	BroadcastGameInfo        = 302
	SvclHashSeed             = 404
	SvclHashSeedReply        = 405
	SvclCreateMinibot        = 1001
	SvclMinibotCoordFrame    = 1002
)

// GameVersion is GAME_VERSION_SV of the 2.11 ("Pro") build (Server.h).
const GameVersion = 21100

// Game constants the protocol carries (Game.h, Player.h, Weapon.h).
const (
	MaxPlayer = 32

	TeamSpectator  = -1
	TeamBlue       = 0
	TeamRed        = 1
	TeamAutoAssign = 2

	StatusAlive   = 0
	StatusDead    = 1
	StatusLoading = 2

	GameTypeDM  = 0
	GameTypeTDM = 1
	GameTypeCTF = 2
	GameTypeSND = 3 // "Champion" in the Pro build

	GamePlaying   = -1
	GameBlueWin   = 0
	GameRedWin    = 1
	GameDraw      = 2
	GameDontShow  = 3
	GameMapChange = 4

	WeaponSMG            = 0
	WeaponShotgun        = 1
	WeaponSniper         = 2
	WeaponDualMachineGun = 3
	WeaponChainGun       = 4
	WeaponBazooka        = 5
	WeaponPhotonRifle    = 6
	WeaponFlameThrower   = 7
	WeaponGrenade        = 8
	WeaponMolotov        = 9
	WeaponKnives         = 10
	WeaponNuclear        = 11
	WeaponShield         = 12
	WeaponMinibot        = 13

	ProjectileRocket        = 2
	ProjectileGrenade       = 3
	ProjectileLifePack      = 4
	ProjectileDropedWeapon  = 5
	ProjectileDropedGrenade = 6
	ProjectileMolotov       = 7
	ProjectileFlame         = 8
)

// Encode returns a message's bytes (sizeof(struct), padding zeroed).
func Encode(v any) []byte {
	var b bytes.Buffer
	if err := binary.Write(&b, binary.LittleEndian, v); err != nil {
		panic(fmt.Sprintf("proto: encode %T: %v", v, err))
	}
	return b.Bytes()
}

// Decode reads a message into v. Like the original's memcpy of sizeof(struct) bytes, a shorter
// buffer is zero-extended (the original would read past the end) and extra bytes are ignored.
func Decode(data []byte, v any) {
	n := binary.Size(v)
	if len(data) < n {
		data = append(append([]byte(nil), data...), make([]byte, n-len(data))...)
	}
	if _, err := binary.Decode(data[:n], binary.LittleEndian, v); err != nil {
		panic(fmt.Sprintf("proto: decode %T: %v", v, err))
	}
}

// CString is a NUL-terminated C string field.
func CString(b []byte) string {
	if i := bytes.IndexByte(b, 0); i >= 0 {
		return string(b[:i])
	}
	return string(b)
}

// SetCString copies s into a fixed field with its NUL, truncating to fit; the rest is zero.
func SetCString(dst []byte, s string) {
	for i := range dst {
		dst[i] = 0
	}
	if len(dst) == 0 {
		return
	}
	n := copy(dst[:len(dst)-1], s)
	dst[n] = 0
}

// --- client to server

type ClsvPongMsg struct{ PlayerID int8 }

type ClsvSpawnRequestMsg struct {
	PlayerID   int8
	WeaponID   int8
	MeleeID    int8
	Skin       [7]byte
	RedDecal   [3]uint8
	GreenDecal [3]uint8
	BlueDecal  [3]uint8
}

type ClsvPlayerShootMsg struct {
	PlayerID int8
	WeaponID int8
	NuzzleID int8
	_        [1]byte
	P1       [3]int16
	P2       [3]int16
}

type ClsvGameVersionAcceptedMsg struct {
	PlayerID int8
	Password [16]byte
}

type ClsvPickupRequestMsg struct{ PlayerID int8 }

type ClsvAdminRequestMsg struct {
	Login    [33]byte
	Password [33]byte
}

type ClsvVoteMsg struct {
	Value    uint8
	PlayerID int8
}

type ClsvMapListRequestMsg struct {
	PlayerID int8
	All      uint8
}

// --- server to client

type SvclNewPlayerMsg struct {
	NewPlayerID int8
	_           [3]byte
	BaboNetID   int32
}

type SvclServerInfoMsg struct {
	MapSeed   int32
	MapName   [16]byte
	GameType  int8
	_         [1]byte
	BlueScore int16
	RedScore  int16
	BlueWin   int16
	RedWin    int16
	_         [2]byte
}

type SvclPlayerDisconnectMsg struct{ PlayerID int8 }

type SvclPlayerEnumStateMsg struct {
	PlayerID     int8
	PlayerName   [32]byte
	TeamID       int8
	Status       int8
	_            [1]byte
	Kills        int16
	Deaths       int16
	Score        int16
	Returns      int16
	FlagAttempts int16
	Damage       int16
	Life         float32
	Dmg          float32
	WeaponID     int8
	PlayerIP     [16]byte
	_            [3]byte
	BaboNetID    int32
	Skin         [7]byte
	RedDecal     [3]uint8
	GreenDecal   [3]uint8
	BlueDecal    [3]uint8
}

type SvclPingMsg struct{ PlayerID int8 }

type SvclPlayerPingMsg struct {
	PlayerID int8
	_        [1]byte
	Ping     int16
}

type SvclPlayerSpawnMsg struct {
	PlayerID   int8
	WeaponID   int8
	MeleeID    int8
	_          [1]byte
	Position   [3]int16
	Skin       [7]byte
	RedDecal   [3]uint8
	GreenDecal [3]uint8
	BlueDecal  [3]uint8
}

type SvclSvChangeMsg struct{ SvChange [80]byte }

type SvclPlayerShootMsg struct {
	PlayerID    int8
	HitPlayerID int8
	NuzzleID    int8
	WeaponID    int8
	P1          [3]int16
	P2          [3]int16
	Normal      [3]int8
	_           [1]byte
}

type SvclDeleteProjectileMsg struct{ ProjectileID int32 }

type SvclProjectileCoordFrameMsg struct {
	UniqueID     int32
	ProjectileID int16
	_            [2]byte
	FrameID      int32
	Position     [3]int16
	Vel          [3]int8
	_            [3]byte
}

type SvclExplosionMsg struct {
	Position [3]float32
	Normal   [3]float32
	Radius   float32
	PlayerID int8
	_        [3]byte
}

type SvclPlayerHitMsg struct {
	PlayerID int8
	FromID   int8
	WeaponID int8
	_        [1]byte
	Damage   float32
	Vel      [3]int8
	_        [1]byte
}

type SvclPlaySoundMsg struct {
	SoundID  int8
	Volume   uint8
	Range    int8
	Position [3]uint8
}

type SvclGameVersionMsg struct{ GameVersion uint32 }

type SvclSynchronizeTimerMsg struct {
	FrameID       int32
	GameTimeLeft  float32
	RoundTimeLeft float32
}

type SvclChangeFlagStateMsg struct {
	FlagID       int8
	NewFlagState int8
	PlayerID     int8
}

type SvclDropFlagMsg struct {
	FlagID   int8
	_        [3]byte
	Position [3]float32
}

type SvclFlagEnumMsg struct {
	FlagState    [2]int8
	_            [2]byte
	PositionBlue [3]float32
	PositionRed  [3]float32
}

type SvclRoundStateMsg struct {
	NewState int8
	ReInit   int8
}

type SvclChangeGameTypeMsg struct{ NewGameType int8 }

type SvclMapChangeMsg struct {
	MapName  [16]byte
	GameType int8
}

type SvclPickupItemMsg struct {
	PlayerID int8
	ItemType int8
	ItemFlag int8
}

type SvclFlameStickToPlayerMsg struct {
	ProjectileID int16
	PlayerID     int8
	_            [1]byte
}

type SvclUpdateVoteMsg struct {
	NbYes int8
	NbNo  int8
}

type SvclVoteResultMsg struct{ Passed uint8 }

type SvclMsgMsg struct {
	MsgDest int8
	TeamID  int8
	Message [130]byte
}

type SvclPlayerUpdateStatsMsg struct {
	PlayerID          int8
	_                 [1]byte
	Kills             int16
	Deaths            int16
	Score             int16
	Returns           int16
	FlagAttempts      int16
	TimePlayedCurGame float32
}

type SvclBadChecksumEntityMsg struct {
	ID       int32
	Name     [32]byte
	PlayerIP [16]byte
}

type SvclBadChecksumInfoMsg struct{ Number int32 }

// --- both ways

type PlayerInfoMsg struct {
	PlayerID   int8
	PlayerIP   [16]byte
	PlayerName [32]byte
	Username   [21]byte
	Password   [32]byte
	MacAddr    [20]byte
}

type ChatMsg struct {
	TeamID  int8
	Message [130]byte
}

type TeamRequestMsg struct {
	PlayerID      int8
	TeamRequested int8
}

type PlayerCoordFrameMsg struct {
	PlayerID  int8
	_         [3]byte
	FrameID   int32
	Position  [3]int16
	Vel       [3]int8
	_         [1]byte
	MousePos  [3]int16
	BaboNetID int32
	CamPosZ   int32
}

type SvclCreateMinibotMsg struct {
	PlayerID int8
	_        [1]byte
	Position [3]int16
	MousePos [3]int16
}

type SvclMinibotCoordFrameMsg struct {
	PlayerID  int8
	_         [3]byte
	FrameID   int32
	Position  [3]int16
	Vel       [3]int8
	_         [1]byte
	MousePos  [3]int16
	BaboNetID int32
}

type PlayerChangeNameMsg struct {
	PlayerID   int8
	PlayerName [32]byte
}

type PlayerProjectileMsg struct {
	PlayerID       int8
	WeaponID       int8
	NuzzleID       int8
	ProjectileType int8
	Position       [3]int16
	Vel            [3]int8
	_              [3]byte
	UniqueID       int32
}

type PlayerShootMeleeMsg struct{ PlayerID int8 }

type VoteRequestMsg struct {
	Vote     [80]byte
	PlayerID int8
}

type ClsvMapRequestMsg struct {
	MapName        [16]byte
	UniqueClientID uint32
}

type SvclMapChunkMsg struct {
	Size uint16
	Data [250]byte
}

type SvclMapListMsg struct{ MapName [16]byte }

type PlayerUpdateSkinMsg struct {
	PlayerID   int8
	Skin       [7]byte
	RedDecal   [3]uint8
	GreenDecal [3]uint8
	BlueDecal  [3]uint8
}

type BroadcastQueryMsg struct{ Key [12]byte }

type HashSeedMsg struct{ S1, S2, S3, S4 int16 }
