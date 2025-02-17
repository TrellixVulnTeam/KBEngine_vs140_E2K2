// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com


#include "debug_option.h"

namespace KBEngine { 
	
namespace Network
{

bool g_packetAlwaysContainLength = false;

uint8 g_trace_packet = 0;
bool g_trace_encrypted_packet = true;
bool g_trace_packet_use_logfile = false;
std::vector<std::string> g_trace_packet_disables;

}

//add by huyf 2019.07.10
bool g_debugEntity = true;
//add end
int8 g_appPublish = 1;

}
