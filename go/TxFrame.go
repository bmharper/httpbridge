// automatically generated, do not modify

package httpbridge

import (
	flatbuffers "github.com/google/flatbuffers/go"
)
type TxFrame struct {
	_tab flatbuffers.Table
}

func GetRootAsTxFrame(buf []byte, offset flatbuffers.UOffsetT) *TxFrame {
	n := flatbuffers.GetUOffsetT(buf[offset:])
	x := &TxFrame{}
	x.Init(buf, n + offset)
	return x
}

func (rcv *TxFrame) Init(buf []byte, i flatbuffers.UOffsetT) {
	rcv._tab.Bytes = buf
	rcv._tab.Pos = i
}

func (rcv *TxFrame) Frametype() int8 {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(4))
	if o != 0 {
		return rcv._tab.GetInt8(o + rcv._tab.Pos)
	}
	return 0
}

func (rcv *TxFrame) Version() int8 {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(6))
	if o != 0 {
		return rcv._tab.GetInt8(o + rcv._tab.Pos)
	}
	return 0
}

func (rcv *TxFrame) Channel() uint64 {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(8))
	if o != 0 {
		return rcv._tab.GetUint64(o + rcv._tab.Pos)
	}
	return 0
}

func (rcv *TxFrame) Stream() uint64 {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(10))
	if o != 0 {
		return rcv._tab.GetUint64(o + rcv._tab.Pos)
	}
	return 0
}

func (rcv *TxFrame) Headers(obj *TxHeaderLine, j int) bool {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(12))
	if o != 0 {
		x := rcv._tab.Vector(o)
		x += flatbuffers.UOffsetT(j) * 4
		x = rcv._tab.Indirect(x)
	if obj == nil {
		obj = new(TxHeaderLine)
	}
		obj.Init(rcv._tab.Bytes, x)
		return true
	}
	return false
}

func (rcv *TxFrame) HeadersLength() int {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(12))
	if o != 0 {
		return rcv._tab.VectorLen(o)
	}
	return 0
}

func (rcv *TxFrame) BodyTotalLength() uint64 {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(14))
	if o != 0 {
		return rcv._tab.GetUint64(o + rcv._tab.Pos)
	}
	return 0
}

func (rcv *TxFrame) BodyOffset() uint64 {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(16))
	if o != 0 {
		return rcv._tab.GetUint64(o + rcv._tab.Pos)
	}
	return 0
}

func (rcv *TxFrame) Body(j int) byte {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(18))
	if o != 0 {
		a := rcv._tab.Vector(o)
		return rcv._tab.GetByte(a + flatbuffers.UOffsetT(j * 1))
	}
	return 0
}

func (rcv *TxFrame) BodyLength() int {
	o := flatbuffers.UOffsetT(rcv._tab.Offset(18))
	if o != 0 {
		return rcv._tab.VectorLen(o)
	}
	return 0
}

func TxFrameStart(builder *flatbuffers.Builder) { builder.StartObject(8) }
func TxFrameAddFrametype(builder *flatbuffers.Builder, frametype int8) { builder.PrependInt8Slot(0, frametype, 0) }
func TxFrameAddVersion(builder *flatbuffers.Builder, version int8) { builder.PrependInt8Slot(1, version, 0) }
func TxFrameAddChannel(builder *flatbuffers.Builder, channel uint64) { builder.PrependUint64Slot(2, channel, 0) }
func TxFrameAddStream(builder *flatbuffers.Builder, stream uint64) { builder.PrependUint64Slot(3, stream, 0) }
func TxFrameAddHeaders(builder *flatbuffers.Builder, headers flatbuffers.UOffsetT) { builder.PrependUOffsetTSlot(4, flatbuffers.UOffsetT(headers), 0) }
func TxFrameStartHeadersVector(builder *flatbuffers.Builder, numElems int) flatbuffers.UOffsetT { return builder.StartVector(4, numElems, 4)
}
func TxFrameAddBodyTotalLength(builder *flatbuffers.Builder, bodyTotalLength uint64) { builder.PrependUint64Slot(5, bodyTotalLength, 0) }
func TxFrameAddBodyOffset(builder *flatbuffers.Builder, bodyOffset uint64) { builder.PrependUint64Slot(6, bodyOffset, 0) }
func TxFrameAddBody(builder *flatbuffers.Builder, body flatbuffers.UOffsetT) { builder.PrependUOffsetTSlot(7, flatbuffers.UOffsetT(body), 0) }
func TxFrameStartBodyVector(builder *flatbuffers.Builder, numElems int) flatbuffers.UOffsetT { return builder.StartVector(1, numElems, 1)
}
func TxFrameEnd(builder *flatbuffers.Builder) flatbuffers.UOffsetT { return builder.EndObject() }