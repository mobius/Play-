#include <boost/lexical_cast.hpp>
#include "Spu2_Core.h"
#include "Log.h"

#define LOG_NAME_PREFIX ("spu2_core_")

using namespace PS2::Spu2;
using namespace std;
using namespace std::tr1;
using namespace Framework;
using namespace boost;

CCore::CCore(unsigned int coreId) :
m_coreId(coreId)
{
	m_logName = LOG_NAME_PREFIX + lexical_cast<string>(m_coreId);

	m_readDispatch.core		= &CCore::ReadRegisterCore;
	m_readDispatch.channel	= &CCore::ReadRegisterChannel;

	m_writeDispatch.core	= &CCore::WriteRegisterCore;
	m_writeDispatch.channel = &CCore::WriteRegisterChannel;

	Reset();
}

CCore::~CCore()
{
    
}

void CCore::Reset()
{
	m_transferAddress.w = 0;
}

uint32 CCore::ReadRegister(uint32 address, uint32 value)
{
	return ProcessRegisterAccess(m_readDispatch, address, value);
}

uint32 CCore::WriteRegister(uint32 address, uint32 value)
{
	return ProcessRegisterAccess(m_writeDispatch, address, value);
}

uint32 CCore::ProcessRegisterAccess(const REGISTER_DISPATCH_INFO& dispatchInfo, uint32 address, uint32 value)
{
	if(address < S_REG_BASE)
	{
		//Channel access
		unsigned int channelId = (address >> 4) & 0x3F;
		address &= ~(0x3F << 4);
		return ((this)->*(dispatchInfo.channel))(channelId, address, value);
	}
	else if(address >= VA_REG_BASE && address < R_REG_BASE)
	{
		//Channel access
		unsigned int channelId = (address - VA_REG_BASE) / 12;
		address -= channelId * 12;
		return ((this)->*(dispatchInfo.channel))(channelId, address, value);
	}
	else
	{
		//Core write
		return ((this)->*(dispatchInfo.core))(0, address, value);
	}
}

uint32 CCore::ReadRegisterCore(unsigned int channelId, uint32 address, uint32 value)
{
	uint32 result = 0;
	switch(address)
	{
	case STATX:
		result = 0x0000;
		break;
	}
#ifdef _DEBUG
    LogRead(address);
#endif
	return result;
}

uint32 CCore::WriteRegisterCore(unsigned int channelId, uint32 address, uint32 value)
{
	switch(address)
	{
	case A_TSA_HI:
		m_transferAddress.h1 = static_cast<uint16>(value);
		break;
	case A_TSA_LO:
		m_transferAddress.h0 = static_cast<uint16>(value);
		break;
	}
	LogWrite(address, value);
	return 0;
}

uint32 CCore::ReadRegisterChannel(unsigned int channelId, uint32 address, uint32 value)
{
	LogChannelRead(channelId, address);
	return 0;
}

uint32 CCore::WriteRegisterChannel(unsigned int channelId, uint32 address, uint32 value)
{
	assert(channelId < MAX_CHANNEL);
	if(channelId >= MAX_CHANNEL)
	{
		return 0;
	}
	CChannel& channel(m_channel[channelId]);
	switch(address)
	{
	case VP_VOLL:
		channel.volLeft = static_cast<uint16>(value);
		break;
	case VP_VOLR:
		channel.volRight = static_cast<uint16>(value);
		break;
	case VP_PITCH:
		channel.pitch = static_cast<uint16>(value);
		break;
	case VP_ADSR1:
		channel.adsr1 = static_cast<uint16>(value);
		break;
	case VP_ADSR2:
		channel.adsr2 = static_cast<uint16>(value);
		break;
	case VP_ENVX:
		channel.envx = static_cast<uint16>(value);
		break;
	case VP_VOLXL:
		channel.volxLeft = static_cast<uint16>(value);
		break;
	case VP_VOLXR:
		channel.volxRight = static_cast<uint16>(value);
		break;
	}
	LogChannelWrite(channelId, address, value);
	return 0;
}

void CCore::LogRead(uint32 address)
{
	const char* logName = m_logName.c_str();
    switch(address)
    {
    case STATX:
		CLog::GetInstance().Print(logName, "= STATX\r\n");
        break;
    default:
		CLog::GetInstance().Print(logName, "Read an unknown register 0x%0.4X.\r\n", address);
        break;
    }
}

void CCore::LogWrite(uint32 address, uint32 value)
{
	const char* logName = m_logName.c_str();
    switch(address)
    {
	case A_TSA_HI:
		CLog::GetInstance().Print(logName, "A_TSA_HI = 0x%0.4X\r\n", value);
		break;
	case A_TSA_LO:
		CLog::GetInstance().Print(logName, "A_TSA_LO = 0x%0.4X\r\n", value);
		break;
	case A_STD:
		CLog::GetInstance().Print(logName, "A_STD = 0x%0.4X\r\n", value);
		break;
    default:
		CLog::GetInstance().Print(logName, "Write 0x%0.4X to an unknown register 0x%0.4X.\r\n", value, address);
        break;
    }
}

void CCore::LogChannelRead(unsigned int channelId, uint32 address)
{
	const char* logName = m_logName.c_str();
    switch(address)
    {
    default:
		CLog::GetInstance().Print(logName, "ch%0.2i: Read an unknown register 0x%0.4X.\r\n", 
			channelId, address);
        break;
    }
}

void CCore::LogChannelWrite(unsigned int channelId, uint32 address, uint32 value)
{
	const char* logName = m_logName.c_str();
    switch(address)
    {
	case VP_VOLL:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_VOLL = %0.4X.\r\n", 
			channelId, value);
		break;
	case VP_VOLR:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_VOLR = %0.4X.\r\n", 
			channelId, value);
		break;
	case VP_PITCH:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_PITCH = %0.4X.\r\n", 
			channelId, value);
		break;
	case VP_ADSR1:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_ADSR1 = %0.4X.\r\n", 
			channelId, value);
		break;
	case VP_ADSR2:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_ADSR2 = %0.4X.\r\n", 
			channelId, value);
		break;
	case VP_ENVX:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_ENVX = %0.4X.\r\n", 
			channelId, value);
		break;
	case VP_VOLXL:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_VOLXL = %0.4X.\r\n", 
			channelId, value);
		break;
	case VP_VOLXR:
		CLog::GetInstance().Print(logName, "ch%0.2i: VP_VOLXR = %0.4X.\r\n", 
			channelId, value);
		break;
    default:
		CLog::GetInstance().Print(logName, "ch%0.2i: Wrote %0.4X an unknown register 0x%0.4X.\r\n", 
			channelId, value, address);
        break;
    }
}