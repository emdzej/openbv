package game

import (
	"math"

	"github.com/emdzej/openbv/server/internal/bvmath"
	"github.com/emdzej/openbv/server/internal/proto"
)

// CoordFrame is the game's CoordFrame (Player.h:47).
type CoordFrame struct {
	Position      bvmath.Vec3
	Vel           bvmath.Vec3
	FrameID       int32
	Angle         float32
	MousePosOnMap bvmath.Vec3
	CamPosZ       float32
}

// assign is CoordFrame::operator= (Player.h:69): everything but camPosZ.
func (c *CoordFrame) assign(o CoordFrame) {
	c.Position = o.Position
	c.Vel = o.Vel
	c.Angle = o.Angle
	c.FrameID = o.FrameID
	c.MousePosOnMap = o.MousePosOnMap
}

// reset is CoordFrame::reset.
func (c *CoordFrame) reset() {
	c.FrameID = 0
	c.Angle = 0
}

// interpolate is CoordFrame::interpolate (Player.h:78), the server's copy of every player's motion
// between the last two frames the client sent: cubic (cl_cubicMotion, the server's own, default
// true), extrapolated with the last velocity for up to 15 frames past the newest.
func (c *CoordFrame) interpolate(progression *int32, from, to *CoordFrame, delay float32, cubic bool) {
	*progression++
	size := to.FrameID - from.FrameID
	if *progression > size {
		if *progression < 15 {
			c.Position = c.Position.Add(c.Vel.Scale(delay))
		} else {
			c.Position = to.Position
		}
	} else if *progression >= 0 {
		t := float32(*progression) / float32(size)
		animTime := float32(size) / (30.0 * 3.0)
		if cubic {
			c.Position = bvmath.CubicSpline(
				from.Position,
				from.Position.Add(from.Vel.Scale(animTime)),
				to.Position.Sub(to.Vel.Scale(animTime)),
				to.Position,
				t)
			c.MousePosOnMap = bvmath.CubicSpline(
				from.MousePosOnMap,
				from.MousePosOnMap.Add(to.MousePosOnMap.Sub(from.MousePosOnMap).Scale(animTime)),
				to.MousePosOnMap.Sub(from.MousePosOnMap.Sub(to.MousePosOnMap).Scale(animTime)),
				to.MousePosOnMap,
				t)
		} else {
			c.Position = to.Position
			c.Vel = to.Vel
		}
	}
}

const pingLogSize = 60 // PING_LOG_SIZE

// Player is the server's Player (Player.h), the parts the server uses.
type Player struct {
	ID        int
	BabonetID uint32
	IP        string
	Name      string
	TeamID    int
	Status    int

	CurrentCF, LastCF, NetCF0, NetCF1 CoordFrame
	CFProgression                     int32

	Life         float32
	Kills        int32
	Deaths       int32
	Score        int32
	Returns      int32
	Damage       int32
	Dmg          float32
	FlagAttempts int32

	Skin                            string
	RedDecal, GreenDecal, BlueDecal bvmath.Vec3

	WeaponID, MeleeID                int // the current weapons (weapon->weaponID; -1: none)
	NextSpawnWeapon, NextMeleeWeapon int

	TimeToSpawn, ImmuneTime, Protection                            float32
	TimeDead, TimeAlive, TimeIdle, TimeInServer, TimePlayedCurGame float32
	BabySitTime, PingOverMax                                       float32
	SpawnSlot                                                      int

	// ping, in frames (Server.cpp:1027, PlayerUpdate.cpp:29)
	Ping, AvgPing, PingSum int32
	CurrentPingFrame       int32
	WaitForPong            bool
	ConnectionInterrupted  bool
	SendPosFrame           int32
	pingLog                [pingLogSize]int32
	pingLogID              int
	nextPingLogTime        float32
	pingLogInterval        float32

	// speed-hack check (ServerRecv.cpp:777)
	FrameSinceLast, LastFrame, CurrentFrame, SpeedHackCount int32

	GrenadeLeft, MolotovLeft int
	IsAdmin                  bool
	UserID                   int
}

// newPlayer is Player::Player (Player.cpp:61).
func newPlayer(id int, babonetID uint32, timeToSpawn float32) *Player {
	p := &Player{
		ID:              id,
		BabonetID:       babonetID,
		Name:            "Unnamed Babo",
		TeamID:          proto.TeamSpectator,
		Status:          proto.StatusLoading,
		Ping:            -1,
		BabySitTime:     5,
		SpawnSlot:       -1,
		TimeToSpawn:     timeToSpawn,
		WeaponID:        -1,
		MeleeID:         -1,
		NextSpawnWeapon: proto.WeaponSMG,
		NextMeleeWeapon: proto.WeaponKnives,
	}
	p.pingLogInterval = float32(1.0) / 30
	p.nextPingLogTime = p.pingLogInterval
	return p
}

// updatePing is Player::updatePing (PlayerUpdate.cpp:29). With nextPingLogTime starting at the step
// itself, 1/30 - 1/30 is 0, not < 0: the log takes a sample every second frame.
func (p *Player) updatePing(delay float32) {
	p.nextPingLogTime -= delay
	if p.nextPingLogTime < 0 {
		if p.pingLogID >= pingLogSize {
			p.pingLogID = 0
		}
		p.pingLog[p.pingLogID] = p.Ping
		p.PingSum += p.pingLog[p.pingLogID]
		if p.pingLogID+1 < pingLogSize {
			p.PingSum -= p.pingLog[p.pingLogID+1]
		} else {
			p.PingSum -= p.pingLog[0]
		}
		p.AvgPing = p.PingSum / pingLogSize
		if p.AvgPing < 1 {
			p.AvgPing = 1
		}
		p.nextPingLogTime = p.pingLogInterval
		p.pingLogID++
	}
}

// update is the server's part of Player::update (PlayerUpdate.cpp:53); weapons and the minibot come
// with the combat milestones.
func (p *Player) update(delay float32, cubic bool) {
	p.updatePing(delay)
	if p.TeamID != proto.TeamSpectator {
		p.TimeIdle += delay
	} else {
		p.TimeIdle = 0
	}
	if p.BabySitTime >= 0 {
		p.BabySitTime -= delay
	}
	if p.Protection > 0 {
		p.Protection -= delay
		if p.Protection < 0 {
			p.Protection = 0
		}
	}
	if p.ImmuneTime > 0 {
		p.ImmuneTime -= delay
		if p.ImmuneTime < 0 {
			p.ImmuneTime = 0
		}
	}
	p.FrameSinceLast++
	p.LastCF.assign(p.CurrentCF)
	p.CurrentCF.FrameID++

	if p.Status == proto.StatusDead {
		p.TimeDead += delay
	}
	p.TimeInServer += delay
	if p.Status == proto.StatusAlive {
		p.TimeAlive += delay
		p.TimePlayedCurGame += delay
		// a remote entity on the server: interpolated from the client's frames
		p.CurrentCF.interpolate(&p.CFProgression, &p.NetCF0, &p.NetCF1, delay, cubic)
		p.CurrentCF.Position[2] = .25
		// the aim angle (PlayerUpdate.cpp:342), used by the hit tests later
		dir := p.CurrentCF.MousePosOnMap.Sub(p.CurrentCF.Position)
		dir[2] = 0
		dir = normalize(dir)
		p.CurrentCF.Angle = float32(math.Acos(float64(dir[1]))) * toDegree
		if dir[0] > 0 {
			p.CurrentCF.Angle = -p.CurrentCF.Angle
		}
	} else if (p.TeamID == proto.TeamBlue || p.TeamID == proto.TeamRed) && p.Status == proto.StatusDead && p.TimeToSpawn >= 0 {
		p.TimeToSpawn -= delay
		if p.TimeToSpawn <= 0 {
			p.TimeToSpawn = 0
		}
	}
}

const toDegree = 57.295780 // TO_DEGREE (CVector.h)

// normalize is the game's normalize(CVector3f&) (CVector.cpp:304): times the reciprocal of the
// length (not divided by it: the float results differ), times 0 for a zero vector.
func normalize(v bvmath.Vec3) bvmath.Vec3 {
	var x float32
	if y := v.Length(); y != 0 {
		x = 1 / y
	}
	return bvmath.Vec3{v[0] * x, v[1] * x, v[2] * x}
}

// setCoordFrame is Player::setCoordFrame (Player.cpp:1505).
func (p *Player) setCoordFrame(m *proto.PlayerCoordFrameMsg) {
	if int(m.PlayerID) != p.ID {
		return
	}
	if p.NetCF1.FrameID > m.FrameID {
		return // older than the last one
	}
	p.NetCF0.assign(p.CurrentCF)
	p.NetCF0.FrameID = p.NetCF1.FrameID
	p.CFProgression = 0
	p.CurrentCF.Vel = bvmath.Vec3{float32(m.Vel[0]) / 10, float32(m.Vel[1]) / 10, float32(m.Vel[2]) / 10}
	p.CurrentCF.CamPosZ = float32(m.CamPosZ)
	p.NetCF1.FrameID = m.FrameID
	p.NetCF1.Position = bvmath.Vec3{float32(m.Position[0]) / 100, float32(m.Position[1]) / 100, float32(m.Position[2]) / 100}
	p.NetCF1.Vel = bvmath.Vec3{float32(m.Vel[0]) / 10, float32(m.Vel[1]) / 10, float32(m.Vel[2]) / 10}
	p.NetCF1.MousePosOnMap = bvmath.Vec3{float32(m.MousePos[0]) / 100, float32(m.MousePos[1]) / 100, float32(m.MousePos[2]) / 100}
	if p.NetCF0.FrameID == 0 {
		p.NetCF0.assign(p.NetCF1)
	}
}

// spawn is Player::spawn (Player.cpp:822).
func (p *Player) spawn(at bvmath.Vec3, timeToSpawn, immunity float32) {
	p.Status = proto.StatusAlive
	p.Life = 1
	p.TimeToSpawn = timeToSpawn
	p.ImmuneTime = immunity
	p.TimeDead, p.TimeAlive, p.TimeIdle = 0, 0, 0
	p.CurrentCF.Position = at
	p.CurrentCF.Vel = bvmath.Vec3{}
	p.CurrentCF.Angle = 0
	p.LastCF.assign(p.CurrentCF)
	p.NetCF0.assign(p.CurrentCF)
	p.NetCF1.assign(p.CurrentCF)
	p.NetCF0.reset()
	p.NetCF1.reset()
	p.CFProgression = 0
	p.GrenadeLeft = 2
	p.MolotovLeft = 1
	p.WeaponID = p.NextSpawnWeapon
	p.MeleeID = p.NextMeleeWeapon
}

// reinit is Player::reinit (Player.cpp:873): the stats for a new game.
func (p *Player) reinit() {
	p.TimeIdle = 0
	p.Dmg = 0
	p.Kills, p.Deaths, p.Score, p.Returns, p.Damage, p.FlagAttempts = 0, 0, 0, 0, 0, 0
	p.Ping, p.PingSum, p.AvgPing = 0, 0, 0
	p.BabySitTime = 5
	p.PingOverMax = 0
	p.TimePlayedCurGame = 0
	p.SpawnSlot = -1
}
