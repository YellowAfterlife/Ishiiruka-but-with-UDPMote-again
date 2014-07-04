// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.
#include "VideoCommon/VideoCommon.h"
#include "VideoCommon/VertexLoader_Position.h"
#include "VideoCommon/VertexLoader_PositionFuncs.h"

// Thoughts on the implementation of a vertex loader compiler.
// s_pCurBufferPointer should definitely be in a register.
// Could load the position scale factor in XMM7, for example.

// The pointer inside DataReadU8 in another.
// Let's check out Pos_ReadDirect_UByte(). For Byte, replace MOVZX with MOVSX.

/*
MOVZX(32, R(EAX), MOffset(ESI, 0));
MOVZX(32, R(EBX), MOffset(ESI, 1));
MOVZX(32, R(ECX), MOffset(ESI, 2));
MOVD(XMM0, R(EAX));
MOVD(XMM1, R(EBX));
MOVD(XMM2, R(ECX));
CVTDQ2PS(XMM0, XMM0);
CVTDQ2PS(XMM1, XMM1);
CVTDQ2PS(XMM2, XMM2);
MULSS(XMM0, XMM7);
MULSS(XMM1, XMM7);
MULSS(XMM2, XMM7);
MOVSS(MOffset(EDI, 0), XMM0);
MOVSS(MOffset(EDI, 4), XMM1);
MOVSS(MOffset(EDI, 8), XMM2);

Alternatively, lookup table:
MOVZX(32, R(EAX), MOffset(ESI, 0));
MOVZX(32, R(EBX), MOffset(ESI, 1));
MOVZX(32, R(ECX), MOffset(ESI, 2));
MOV(32, R(EAX), MComplex(LUTREG, EAX, 4));
MOV(32, R(EBX), MComplex(LUTREG, EBX, 4));
MOV(32, R(ECX), MComplex(LUTREG, ECX, 4));
MOV(MOffset(EDI, 0), XMM0);
MOV(MOffset(EDI, 4), XMM1);
MOV(MOffset(EDI, 8), XMM2);

SSE4:
PINSRB(XMM0, MOffset(ESI, 0), 0);
PINSRB(XMM0, MOffset(ESI, 1), 4);
PINSRB(XMM0, MOffset(ESI, 2), 8);
CVTDQ2PS(XMM0, XMM0);
<two unpacks here to sign extend>
MULPS(XMM0, XMM7);
MOVUPS(MOffset(EDI, 0), XMM0);

*/

bool VertexLoader_Position::Initialized = false;


template <typename T, int N>
void LOADERDECL Pos_ReadDirect()
{
	_Pos_ReadDirect<T, N>()
	LOG_VTX();
}

template <typename I, typename T, int N>
void LOADERDECL Pos_ReadIndex()
{
	_Pos_ReadIndex<I,T,N>();
	LOG_VTX();	
}

#if _M_SSE >= 0x301
template <typename I, bool three>
void LOADERDECL Pos_ReadIndex_Float_SSSE3()
{
	_Pos_ReadIndex_Float_SSSE3<I, three>();
	LOG_VTX();
}

template <bool three>
void LOADERDECL Pos_ReadDirect_Float_SSSE3()
{
	_Pos_ReadDirect_Float_SSSE3<three>();
	LOG_VTX();
}
#endif

#if _M_SSE >= 0x401
template <typename I, bool Signed>
void LOADERDECL Pos_ReadIndex_16x2_SSE4()
{
	_Pos_ReadIndex_16x2_SSE4<I, Signed>();
	LOG_VTX();
}

template <typename I, bool Signed>
void LOADERDECL Pos_ReadIndex_16x3_SSE4()
{
	_Pos_ReadIndex_16x3_SSE4<I, Signed>();
	LOG_VTX();
}

template <bool Signed>
void LOADERDECL Pos_ReadDirect_16x2_SSE4()
{
	_Pos_ReadDirect_16x2_SSE4<Signed>();
	LOG_VTX();
}

template <bool Signed>
void LOADERDECL Pos_ReadDirect_16x3_SSE4()
{
	_Pos_ReadDirect_16x3_SSE4<Signed>();
	LOG_VTX();
}
#endif

static TPipelineFunction tableReadPosition[4][8][2] = {
	{
		{NULL, NULL,},
		{NULL, NULL,},
		{NULL, NULL,},
		{NULL, NULL,},
		{NULL, NULL,},
	},
	{
		{Pos_ReadDirect<u8, 2>, Pos_ReadDirect<u8, 3>,},
		{Pos_ReadDirect<s8, 2>, Pos_ReadDirect<s8, 3>,},
		{Pos_ReadDirect<u16, 2>, Pos_ReadDirect<u16, 3>,},
		{Pos_ReadDirect<s16, 2>, Pos_ReadDirect<s16, 3>,},
		{Pos_ReadDirect<float, 2>, Pos_ReadDirect<float, 3>,},
	},
	{
		{Pos_ReadIndex<u8, u8, 2>, Pos_ReadIndex<u8, u8, 3>,},
		{Pos_ReadIndex<u8, s8, 2>, Pos_ReadIndex<u8, s8, 3>,},
		{Pos_ReadIndex<u8, u16, 2>, Pos_ReadIndex<u8, u16, 3>,},
		{Pos_ReadIndex<u8, s16, 2>, Pos_ReadIndex<u8, s16, 3>,},
		{Pos_ReadIndex<u8, float, 2>, Pos_ReadIndex<u8, float, 3>,},
	},
	{
		{Pos_ReadIndex<u16, u8, 2>, Pos_ReadIndex<u16, u8, 3>,},
		{Pos_ReadIndex<u16, s8, 2>, Pos_ReadIndex<u16, s8, 3>,},
		{Pos_ReadIndex<u16, u16, 2>, Pos_ReadIndex<u16, u16, 3>,},
		{Pos_ReadIndex<u16, s16, 2>, Pos_ReadIndex<u16, s16, 3>,},
		{Pos_ReadIndex<u16, float, 2>, Pos_ReadIndex<u16, float, 3>,},
	},
};

static int tableReadPositionVertexSize[4][8][2] = {
	{
		{0, 0,}, {0, 0,}, {0, 0,}, {0, 0,}, {0, 0,},
	},
	{
		{2, 3,}, {2, 3,}, {4, 6,}, {4, 6,}, {8, 12,},
	},
	{
		{1, 1,}, {1, 1,}, {1, 1,}, {1, 1,}, {1, 1,},
	},
	{
		{2, 2,}, {2, 2,}, {2, 2,}, {2, 2,}, {2, 2,},
	},
};


void VertexLoader_Position::Init(void)
{
	if (Initialized)
	{
		return;
	}
	Initialized = true;
#if _M_SSE >= 0x301

	if (cpu_info.bSSSE3)
	{
		tableReadPosition[1][4][0] = Pos_ReadDirect_Float_SSSE3<false>;
		tableReadPosition[1][4][1] = Pos_ReadDirect_Float_SSSE3<true>;
		tableReadPosition[2][4][0] = Pos_ReadIndex_Float_SSSE3<u8, false>;
		tableReadPosition[2][4][1] = Pos_ReadIndex_Float_SSSE3<u8, true>;
		tableReadPosition[3][4][0] = Pos_ReadIndex_Float_SSSE3<u16, false>;
		tableReadPosition[3][4][1] = Pos_ReadIndex_Float_SSSE3<u16, true>;
	}

#endif
#if _M_SSE >= 0x401
	if (cpu_info.bSSE4_1)
	{
		tableReadPosition[1][2][0] = Pos_ReadDirect_16x2_SSE4<false>;
		tableReadPosition[1][2][1] = Pos_ReadDirect_16x3_SSE4<false>;
		tableReadPosition[1][3][0] = Pos_ReadDirect_16x2_SSE4<true>;
		tableReadPosition[1][3][1] = Pos_ReadDirect_16x3_SSE4<true>;

		tableReadPosition[2][2][0] = Pos_ReadIndex_16x2_SSE4<u8, false>;
		tableReadPosition[2][2][1] = Pos_ReadIndex_16x3_SSE4<u8, false>;
		tableReadPosition[2][3][0] = Pos_ReadIndex_16x2_SSE4<u8, true>;
		tableReadPosition[2][3][1] = Pos_ReadIndex_16x3_SSE4<u8, true>;

		tableReadPosition[3][2][0] = Pos_ReadIndex_16x2_SSE4<u16, false>;
		tableReadPosition[3][2][1] = Pos_ReadIndex_16x3_SSE4<u16, false>;
		tableReadPosition[3][3][0] = Pos_ReadIndex_16x2_SSE4<u16, true>;
		tableReadPosition[3][3][1] = Pos_ReadIndex_16x3_SSE4<u16, true>;
	}
#endif
}

u32 VertexLoader_Position::GetSize(u32 _type, u32 _format, u32 _elements)
{
	return tableReadPositionVertexSize[_type][_format][_elements];
}

TPipelineFunction VertexLoader_Position::GetFunction(u32 _type, u32 _format, u32 _elements)
{
	return tableReadPosition[_type][_format][_elements];
}
