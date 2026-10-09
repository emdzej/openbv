// Package wire is baboNet's packet framing over WebSockets, as the openbv client speaks it
// (src/port/babonet_gasm.cpp): one binary message per baboNet packet,
//
//	u16 type ID (little-endian), u8 protocol (0 = TCP, 1 = UDP, as the sender asked), u8 0, payload
//
// The original sent the same packets over its own TCP and UDP framing; over a WebSocket everything
// arrives in order, so the protocol byte is informational.
package wire

import (
	"encoding/binary"
	"errors"
)

// Protocol is the channel the sender asked baboNet for.
type Protocol uint8

const (
	TCP Protocol = 0
	UDP Protocol = 1
)

// HeaderSize is the bytes before the payload.
const HeaderSize = 4

// MaxPayload bounds a packet; the game's largest messages are a few kilobytes.
const MaxPayload = 64 << 10

// Packet is one baboNet message.
type Packet struct {
	Type     uint16
	Protocol Protocol
	Data     []byte
}

var (
	ErrShort    = errors.New("wire: message shorter than its header")
	ErrTooLarge = errors.New("wire: payload too large")
)

// Encode returns the WebSocket message for p.
func Encode(p Packet) []byte {
	b := make([]byte, HeaderSize+len(p.Data))
	binary.LittleEndian.PutUint16(b, p.Type)
	b[2] = byte(p.Protocol)
	copy(b[HeaderSize:], p.Data)
	return b
}

// Decode parses a WebSocket message. Data aliases msg.
func Decode(msg []byte) (Packet, error) {
	if len(msg) < HeaderSize {
		return Packet{}, ErrShort
	}
	if len(msg)-HeaderSize > MaxPayload {
		return Packet{}, ErrTooLarge
	}
	proto := TCP
	if msg[2] != 0 {
		proto = UDP
	}
	return Packet{Type: binary.LittleEndian.Uint16(msg), Protocol: proto, Data: msg[HeaderSize:]}, nil
}
