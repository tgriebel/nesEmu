#include "stdafx.h"
#include <iostream>
#include <iomanip>
#include <Windows.h>
#include <chrono>
#include <thread>
#include <fstream>
#include <string>
#include <sstream>
#include <assert.h>
#include <map>
#include <bitset>
#include "common.h"
#include "debug.h"
#include "mos6502.h"
#include "NesSystem.h"

using namespace std;

template <class AddrFunctor>
uint8_t Cpu6502::Read()
{
	cpuAddrInfo_t addrInfo;
	AddrFunctor(*this)( addrInfo );

	return addrInfo.value;
}


template <class AddrFunctor>
void Cpu6502::Write( const uint8_t value )
{
	cpuAddrInfo_t addrInfo;
	AddrFunctor( *this )( addrInfo );

	if ( addrInfo.isAccumulator )
	{
		A = value;
	}
	else
	{
		system->WriteMemory( addrInfo.addr, addrInfo.offset, value );
	}
}


ADDR_MODE_DEF( None )
{
	addrInfo = cpuAddrInfo_t{ Cpu6502::InvalidAddress, 0, 0, false, false };
}


ADDR_MODE_DEF( IndexedIndirect )
{
	const uint8_t targetAddress = ( cpu.ReadOperand(0) + cpu.X );
	uint32_t address = cpu.CombineIndirect( targetAddress, 0x00, wtSystem::ZeroPageWrap );

	uint8_t value = cpu.system->ReadMemory( address );
	addrInfo = cpuAddrInfo_t{ address, 0, value, false, false };

	DEBUG_ADDR_INDEXED_INDIRECT
}


ADDR_MODE_DEF( IndirectIndexed )
{
	uint32_t address = cpu.CombineIndirect( cpu.ReadOperand(0), 0x00, wtSystem::ZeroPageWrap );

	const uint16_t offset = ( address + cpu.Y ) % wtSystem::MemoryWrap;

	uint8_t value = cpu.system->ReadMemory( offset );

	cpu.instructionCycles += cpuCycle_t( cpu.AddressCrossesPage( address, cpu.Y ) ); // TODO: make consistent

	addrInfo = cpuAddrInfo_t{ address, cpu.Y, value, false, false };

	DEBUG_ADDR_INDIRECT_INDEXED
}


ADDR_MODE_DEF( Absolute )
{
	uint32_t address = cpu.ReadAddressOperand();

	uint8_t value = cpu.system->ReadMemory( address );

	addrInfo = cpuAddrInfo_t{ address, 0, value, false, false };

	DEBUG_ADDR_ABS
}


ADDR_MODE_DEF( IndexedAbsoluteX )
{
	cpu.IndexedAbsolute( cpu.X, addrInfo );
}


ADDR_MODE_DEF( IndexedAbsoluteY )
{
	cpu.IndexedAbsolute( cpu.Y, addrInfo );
}


ADDR_MODE_DEF( Zero )
{
	const uint16_t targetAddresss = Combine( cpu.ReadOperand(0), 0x00 );

	uint32_t address = ( targetAddresss % wtSystem::ZeroPageWrap );

	uint8_t value = cpu.system->ReadMemory( address );

	addrInfo = cpuAddrInfo_t{ address, 0, value, false, false };

	DEBUG_ADDR_ZERO
}


ADDR_MODE_DEF( IndexedZeroX )
{
	cpu.IndexedZero( cpu.X, addrInfo );
}


ADDR_MODE_DEF( IndexedZeroY )
{
	cpu.IndexedZero( cpu.Y, addrInfo );
}


ADDR_MODE_DEF( Immediate )
{
	uint8_t value = cpu.ReadOperand(0);

	uint32_t address = Cpu6502::InvalidAddress;

	addrInfo = cpuAddrInfo_t{ address, 0, value, false, true };

	DEBUG_ADDR_IMMEDIATE
}


ADDR_MODE_DEF( Accumulator )
{
	addrInfo = cpuAddrInfo_t{ Cpu6502::InvalidAddress, 0, cpu.A, true, false };

	DEBUG_ADDR_ACCUMULATOR
}


uint16_t Cpu6502::JumpImmediateAddr( const uint16_t addr )
{
	// Hardware bug - http://wiki.nesdev.com/w/index.php/Errata
	if ( ( addr & 0xFF ) == 0xFF )
	{
		return ( Combine( system->ReadMemory( addr ), system->ReadMemory( addr & 0xFF00 ) ) );
	}
	else
	{
		return ( Combine( system->ReadMemory( addr ), system->ReadMemory( addr + 1 ) ) );
	}
}


void Cpu6502::Push( const uint8_t value )
{
	system->GetStack() = value;
	SP--;
}


void Cpu6502::PushWord( const uint16_t value )
{
	Push( ( value >> 8 ) & 0xFF );
	Push( value & 0xFF );
}


uint8_t Cpu6502::Pull()
{
	SP++;
	return system->GetStack();
}


uint16_t Cpu6502::PullWord()
{
	const uint8_t loByte = Pull();
	const uint8_t hiByte = Pull();

	return Combine( loByte, hiByte );
}


uint8_t Cpu6502::Branch( const bool takeBranch )
{
	const uint16_t offset		= static_cast< int8_t >( ReadOperand(0) );
	const uint16_t branchedPC	= PC + offset + 1; // TODO: used in debug print, clean-up

	uint8_t cycles = 0;

	if ( takeBranch )
	{
		cycles = 1 + AddressCrossesPage( PC + 1, offset );

		PC = branchedPC;
	}
	else
	{
		PC += 1;
	}

	DEBUG_ADDR_BRANCH

	instructionCycles += cpuCycle_t(cycles);
	return 0;
}


void Cpu6502::SetAluFlags( const uint16_t value )
{
	P.bit.z = CheckZero( value );
	P.bit.n = CheckSign( value );
}


bool Cpu6502::CheckSign( const uint16_t checkValue )
{
	return ( checkValue & 0x0080 );
}


bool Cpu6502::CheckCarry( const uint16_t checkValue )
{
	return ( checkValue > 0x00ff );
}


bool Cpu6502::CheckZero( const uint16_t checkValue )
{
	return ( checkValue == 0 );
}


bool Cpu6502::CheckOverflow( const uint16_t src, const uint16_t temp, const uint8_t finalValue )
{
	const uint8_t signedBound = 0x80;
	return CheckSign( finalValue ^ src ) && CheckSign( temp ^ src ) && !CheckCarry( temp );
}


uint8_t Cpu6502::AddressCrossesPage( const uint16_t address, const uint16_t offset )
{
	if( opCode == 0x9D || opCode == 0x99 ) // FIXME: massive hack
	{
		return 0;
	}

	const uint32_t targetAddress = ( address + offset ) % wtSystem::MemoryWrap;

	return ( ( targetAddress & 0xFF00 ) == ( address & 0xFF00 ) ) ? 0 : 1;
}


uint16_t Cpu6502::CombineIndirect( const uint8_t lsb, const uint8_t msb, const uint32_t wrap )
{
	const uint16_t address = Combine( lsb, msb );
	const uint8_t loByte = system->ReadMemory( address % wrap );
	const uint8_t hiByte = system->ReadMemory( ( address + 1 ) % wrap );
	const uint16_t value = Combine( loByte, hiByte );

	return value;
}


void Cpu6502::IndexedZero( const uint8_t& reg, cpuAddrInfo_t& addrInfo )
{
	const uint16_t targetAddress = Combine( ReadOperand(0), 0x00 );

	uint32_t address = ( targetAddress + reg ) % wtSystem::ZeroPageWrap;

	uint8_t value = system->ReadMemory( address );

	addrInfo = cpuAddrInfo_t{ address, 0, value, false, false };

	DEBUG_ADDR_INDEXED_ZERO
}


void Cpu6502::IndexedAbsolute( const uint8_t& reg, cpuAddrInfo_t& addrInfo )
{
	const uint16_t targetAddress = ReadAddressOperand();

	uint32_t address = ( targetAddress + reg ) % wtSystem::MemoryWrap;

	uint8_t value = system->ReadMemory( address );

	instructionCycles += cpuCycle_t( AddressCrossesPage( targetAddress, reg ) ); // TODO: make consistent

	addrInfo = cpuAddrInfo_t{ address, 0, value, false, false };

	DEBUG_ADDR_INDEXED_ABS
}


void Cpu6502::NMI()
{
	PushWord( PC - 1 );
	// http://wiki.nesdev.com/w/index.php/CPU_status_flag_behavior
	Push( P.byte | STATUS_BREAK );

	P.bit.i = 1;

	PC = nmiVector;
}


void Cpu6502::IRQ()
{
	if( !P.bit.i )
		return;

	PushWord( PC - 1 );
	Push( P.byte | STATUS_BREAK );

	P.bit.i = 1;

	PC = irqVector;
}


void Cpu6502::AdvancePC( const uint16_t places )
{
	PC += places;
}


uint8_t Cpu6502::ReadOperand( const uint16_t offset ) const
{
	return system->ReadMemory( PC + offset );
}


uint16_t Cpu6502::ReadAddressOperand() const
{
	const uint8_t loByte = ReadOperand(0);
	const uint8_t hiByte = ReadOperand(1);

	return Combine( loByte, hiByte );
}


cpuCycle_t Cpu6502::Exec()
{
	instructionCycles = cpuCycle_t( 0 );

	if ( oamInProcess )
	{
		// http://wiki.nesdev.com/w/index.php/PPU_registers#OAMDMA
		if ( ( cycle % 2 ) == cpuCycle_t( 0 ) )
			instructionCycles += cpuCycle_t( 514 );
		else
			instructionCycles += cpuCycle_t( 513 );

		oamInProcess = false;

		return instructionCycles;
	}

	const uint16_t instrBegin = PC;

	uint8_t curbyte = system->ReadMemory( instrBegin );

	AdvancePC( 1 );

	if ( interruptRequestNMI )
	{
		NMI();
		interruptRequestNMI = false;
		return cpuCycle_t( 0 );
	}
	else if( interruptRequest )
	{
		IRQ();
		interruptRequest = false;
		return cpuCycle_t( 0 );
	}

	instructionCycles += OpLookup( instrBegin, curbyte );

	return instructionCycles;
}


bool Cpu6502::Step( const cpuCycle_t& nextCycle )
{
	dbgStartCycle		= cycle;
	dbgTargetCycle		= nextCycle;
	dbgSysStartCycle	= chrono::duration_cast<masterCycles_t>( dbgStartCycle );
	dbgSysTargetCycle	= chrono::duration_cast<masterCycles_t>( dbgTargetCycle );

	while ( ( cycle < nextCycle ) && !forceStop )
	{
		cycle += cpuCycle_t( Exec() );
	}

	return !forceStop;
}


cpuCycle_t Cpu6502::OpLookup( const uint16_t instrBegin, const uint8_t opCode )
{
#if DEBUG_ADDR == 1
	debugAddr.str( std::string() );
	string regStr;

	dbgMetrics.push_back( OpDebugInfo() );
	OpDebugInfo& instrDbg	= dbgMetrics.back();
	instrDbg.loadCnt		= 0;
	instrDbg.storeCnt		= 0;
	instrDbg.byteCode		= opCode;
	instrDbg.regInfo		= { X, Y, A, SP, (uint8_t)P.byte, PC };
	instrDbg.instrBegin		= instrBegin;
	instrDbg.cpuCycles		= cycle;
	instrDbg.ppuCycles		= system->ppu.scanelineCycle;
	instrDbg.curScanline	= system->ppu.currentScanline;
	instrDbg.instrCycles	= instructionCycles;
	instrDbg.instrBegin		= instrBegin;
	instrDbg.op0			= system->ReadMemory( instrBegin + 1 );
	instrDbg.op1			= system->ReadMemory( instrBegin + 2 );
#else
	OpDebugInfo instrDbg	= {};
#endif // #if DEBUG_ADDR == 1

	if( false )
	{
		// FIXME: linker error when removed
		BuildOpLUT();
	}

	std::invoke( opLUT[opCode].func, this );
	AdvancePC( opLUT[opCode].pcInc );

	instrDbg.mnemonic = opLUT[opCode].mnemonic;
	instrDbg.operands = opLUT[opCode].operands;

	return cpuCycle_t( opLUT[opCode].baseCycles );
}