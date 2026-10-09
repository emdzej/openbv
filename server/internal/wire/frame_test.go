package wire

import (
	"bytes"
	"testing"
)

func TestRoundTrip(t *testing.T) {
	p := Packet{Type: 0x1234, Protocol: UDP, Data: []byte{1, 2, 3}}
	msg := Encode(p)
	if !bytes.Equal(msg, []byte{0x34, 0x12, 1, 0, 1, 2, 3}) {
		t.Fatalf("encoded % x", msg)
	}
	q, err := Decode(msg)
	if err != nil || q.Type != p.Type || q.Protocol != p.Protocol || !bytes.Equal(q.Data, p.Data) {
		t.Fatalf("decoded %+v, %v", q, err)
	}
}

func TestEmptyPayload(t *testing.T) {
	q, err := Decode(Encode(Packet{Type: 7}))
	if err != nil || q.Type != 7 || len(q.Data) != 0 {
		t.Fatalf("%+v %v", q, err)
	}
}

func TestShort(t *testing.T) {
	if _, err := Decode([]byte{1, 2, 3}); err != ErrShort {
		t.Fatalf("got %v", err)
	}
}
