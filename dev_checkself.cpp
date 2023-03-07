#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <errno.h>
#include "dev_checkself.h"


using namespace std;

///////////////////////////////////4G CLASS//////////////////////////////////////
/*运营商APN*/
typedef std::map<std::string, std::string>	SingleOperatorCode_M;
std::map<std::string, SingleOperatorCode_M> g_OperatorCodeMap =
{
	{"中国联通",
		{{"46001", "wonet"},{"46006", "wonet"},{"CHN-UNICOM","3gnet"}}
	},
	{"中国移动",
		{{"46000", "cmnet"},{"46002", "cmnet"},{"46007", "cmnet"},{"CMCC", "cmnet"}}
	},
	{"中国电信",
		{{"46003", "ctnet"},{"46005", "ctnet"},{"46011", "ctnet"},{"CHN-TELECOM", "ctnet"}}
	}
};

CSimOperator::CSimOperator()/*:simStatusInfo_(""), statusState_(""),statusSignalQuality_(""),n3GPPoperatorId_(""), modesSupported_("")*/
{
	std::cout << GREEN_START << "Class CSimOperator " << COLOR_END << std::endl;
	modemMap_.clear();
	bearerPathsV_.clear();
}
CSimOperator::~CSimOperator()
{}

/*
	@brief 查询当前服务状态
	@params hs_resp[OUT] 根据pipe数据确认
	@return		0-成功 非0-失败
*/
int CSimOperator::checkModemManageStatus(std::string &hs_resp )
{
	int ret = 0;
	std::string pipe_resp;
	const std::string cmd = "systemctl status ModemManager";
	ret = CDevCheckSelf::inst()->pipeMulti(cmd, pipe_resp);
	if(0 == ret)
	{
		std::cout << pipe_resp << std::endl;
		size_t fond = pipe_resp.find("(running");
		if(fond != std::string::npos)
		{
			hs_resp = "Active: active (running) ";
			return 0;
		}else
		{
			hs_resp = "Active: inactive (dead)";		//后续获取实时Active
			return -1;
		}

	}else
	{
		hs_resp = pipe_resp;		//pipe error
	}
	
	return ret;
}

/**
	@brief 获取活动的调制解调器
	@params [out]		hs_resp: pipe ok response 
	@return  0-成功 !0-失败
	ok resp:"
	0000000                   /   o   r   g   /   f   r   e   e   d   e   s
	0000020   k   t   o   p   /   M   o   d   e   m   M   a   n   a   g   e
	0000040   r   1   /   M   o   d   e   m   /   0       [   Q   u   e   c
	0000060   t   e   l   ]       E   C   2   0   F  \n"

	@private  modemMap_ 输出
	@notice 
 *
 */
int CSimOperator::updateActiveModemList(std :: string & hs_resp)
{
	int ret ;
	const std::string cmd = "mmcli --list-modems";
	std::string pipe_resp;
	ret = CDevCheckSelf::inst()->pipeMulti(cmd, pipe_resp);
	if(0 == ret)
	{	
		
		std::cout << pipe_resp << std::endl;
		size_t fond =  pipe_resp.find("/Modem/");
		if(fond != std::string::npos)
		{
			//解析
			std::size_t npos = pipe_resp.find("/Modem/");
			if(npos != std::string::npos)
			{
				std::string stIndex = pipe_resp.substr(npos+strlen("/modem/"));
				if(!stIndex.empty())
				{
					//std::cout << "stIndex:" << stIndex << std::endl;
					if(1)//(isdigit(stIndex[0]))
					{
						
						int index = stIndex[0] - '0';
						
						
						modemMap_[index] = pipe_resp;
						auto it = modemMap_.cbegin();
						
						for(; it!=modemMap_.cend(); ++it)
						{
							std::cout << it->first << ":" << it->second << std::endl;
							hs_resp = "检测到Modem/" + std::to_string(it->first);
						}
						std::cout << hs_resp << std::endl;
					}else
					{	
						hs_resp = std::string("解析失败!");
						std::cout << hs_resp << std::endl;
						ret = -3;
					}
					
				}
			}
			

		}else
		{
			std::cout << pipe_resp << std::endl;
			ret = -2; 		//未发现模块
		}
		
	}else
	{
		hs_resp = pipe_resp;
		std::cout << pipe_resp << std::endl;
	}

	return ret ;
}


/**	@brief 注册或取消注册一个基站(启用/取消Modem)
	@params[in]	enable  启用或取消Modem
	@params[out] lrs_resp	如果返回失败, 对应错误MSG
	@return 0-成功 非0-失败

	@notice  chown -root(sudo)
 */
int CSimOperator::setModelEnable(bool enable, std::string &lrs_resp)
{
	int ret = 0;
	std::string cmd;
	//FIXME: 取消判断， 支持热拔插
	//if(modemMap_.empty())
	{
		ret = updateActiveModemList(lrs_resp);
		if(ret)
		{
			return ret;
		}
	}
	auto it = modemMap_.cbegin(); 
	for(; it != modemMap_.cend(); ++it)
	{
		cmd = "sudo mmcli --modem=" + std::to_string(it->first);
		cmd += (true == enable) ? " --enable" : "--disable";
		std::cout << cmd << std::endl;
		lrs_resp = "";
		ret = CDevCheckSelf::inst()->pipeMulti(cmd, lrs_resp);
		if(ret)
		{
			return ret;
		}else
		{
			std::size_t found =  lrs_resp.find("successfully");
			if(found != std::string::npos)
			{
				std::string str;
				ret = updateModemStatus(str);
				
								
				return 0;
			}else
			{
				return -1;
			}
			
		}
		
	}

	return 0;
}



/**
		@biref

		@notice:
		"error: couldn't connect the modem: 'GDBus.Error:org.freedesktop.ModemManager1.Error.Serial.OpenFailed: 
		Could not open serial device ttyUSB2: port is connected' "表示已连接

		操作前需要确认APN CODE
		3)连接前确认bearerpath是否存在，不然存在多个path
		4) 断电重启设备发现设备默认不会主动连到APN，可能需要一个配置文件记录客户喜好
**
**/
int CSimOperator::connAPN(std::string &lrs_resp)
{
	int ret = 0;
	std::string statusStateResp;
	
	
	//FIXME: 避免热拔插的变化，因此取消empty条件
//	if(modemMap_.empty())
	{
		ret = updateActiveModemList(lrs_resp);
		if(ret)
		{
			return ret;
		}
	}
	auto it = modemMap_.cbegin(); 
	for(; it != modemMap_.cend(); ++it)
	{
		
		if(updateModemStatus(statusStateResp))
		{
			std::cout << "{updateModemStatus {\"errmsg:\"}" << statusStateResp << std::endl;
			return -1;	
		}

		/*1. 判断是否已连接APN*/
		std::size_t pos = statusState_.find("connected");
		if(pos != std::string::npos)
		{
			lrs_resp = "已连接APN,请勿重复连接";
			std::cout << YELLOW_START << lrs_resp << COLOR_END << std::endl;
			return 0;
		}

		//TODO : 是否判断bearer path 待测试
		//FIXME: ???? 
		
		std::string cmd = "sudo mmcli --modem=" + std::to_string(it->first) + " --simple-connect=";

		auto it = g_OperatorCodeMap.cbegin();
		for(; it != g_OperatorCodeMap.cend(); ++it)
		{
			auto iter01 = (it->second).cbegin();
			for(; iter01 != (it->second).cend(); ++iter01)
			{
				if(n3GPPoperatorId_ == iter01->first)
				{
					simStatusInfo_.operatorName = it->first;
					cmd += "\"apn="+iter01->second +"\"";
					std::cout << "cmd:{ " << cmd << "}" << std::endl;
					break;
				}
			}
		}

		ret = CDevCheckSelf::inst()->pipeMulti(cmd, lrs_resp);
		if(0 == ret)
		{
			/// succuess: "successfully connected the modem"
			if(lrs_resp == "successfully")			
			{
				updateModemStatus(lrs_resp);
				std::cout << "已连接APN:" << simStatusInfo_.operatorName << std::endl;
			}
		}else
		{
			return -1;			//失败
		}
		

		if(ret)
		{
			return -1;
		}
	}
	
	return ret;
}

/**
		status connected->registered  
			bearer Status|connected		yes->no
**/
int CSimOperator::disconnectAPN(std :: string &errMsg)
{
	int ret = 0;
	std::string statusStateResp;
	if(modemMap_.empty())
	{
		ret = updateActiveModemList(errMsg);
		if(ret)
		{
			return ret;
		}
	}
	auto it = modemMap_.cbegin(); 
	for(; it != modemMap_.cend(); ++it)
	{
		
		if(updateModemStatus(statusStateResp))
		{
			std::cout << "{updateModemStatus {\"errmsg:\"}" << statusStateResp << std::endl;
		}
		std::string cmd = "sudo mmcli --modem=" + std::to_string(it->first) + " --simple-disconnect";

		ret = CDevCheckSelf::inst()->pipeMulti(cmd, errMsg);
		if(0 == ret)
		{
			/// succuess: "successfully connected the modem"
			if(errMsg == "successfully")			
			{
				updateActiveModemList(errMsg);
				std::cout << "已断开APN:" << simStatusInfo_.operatorName << std::endl;
			}
		}else
		{
			return -1;			//失败
		}
		

		if(ret)
		{
			return -1;
		}
	}
	
	return ret;
}


/*	@brief 更新当前的SIM卡信息
	@params	lhs_resp[out] 如果方法放回错误, 该值为错误码
	@return 0-成功 非0-失败

	@notice   
*/
int CSimOperator::updateModemStatus(std::string &lhs_resp)
{
	int ret = 0;
	std::string cmd;
	
	if(modemMap_.empty())
	{
		ret = updateActiveModemList(lhs_resp);
		if(ret)
		{
			return ret;
		}
	}
	
	auto it = modemMap_.cbegin();
	for(; it != modemMap_.cend(); ++it)
	{	
		cmd = "sudo mmcli --modem=" + std::to_string(it->first);
		ret = CDevCheckSelf::inst()->pipeMulti(cmd, lhs_resp);
		//std::cout << lhs_resp <<std::endl;
		if(ret)
		{
			return ret;
		}else			//Succ
		{

			// StatusState
			std::size_t pos = lhs_resp.find("  state:");		//notice :"power state: on"
			if(pos != std::string::npos)
			{
				std::string line = lhs_resp.substr(pos+strlen("  state:"));
				statusState_.clear();
				for(unsigned int index = 0 ; index < line.size(); index++)
				{
					if(line[index] == '\n')break;
					if(line[index] != ' ' )
					{
						statusState_.push_back(line[index]);
						
					}
				}
				if(strstr(statusState_.c_str(), "connected"))
				{
					simStatusInfo_.isConnAPN = true;
				}else
				{
					simStatusInfo_.isConnAPN = false;
				}
				/*如果连接上，是带绿色颜色的 `od -c`可以查看,字符串比较的时候注意*/
				std::cout << "\033[1;32m" << "status|state]\033[0m:" << statusState_ << ", siz: " << statusState_.size() << std::endl;
			}else
			{
				return -1;
			}
			//statusSignalQuality_
			 pos = lhs_resp.find("signal quality:");		
			if(pos != std::string::npos)
			{
				std::string line = lhs_resp.substr(pos+strlen("signal quality:")+strlen(" "));
				statusSignalQuality_.clear();
				for(unsigned int index = 0 ; index < line.size(); index++)
				{
					if(line[index] == '\n' || line[index] == '%')break;
					if(line[index] != ' ' )
					{
						statusSignalQuality_.push_back(line[index]);
					}
				}
				simStatusInfo_.u8signalQuality = atoi(statusSignalQuality_.c_str());
				printf("信号:%d\n", simStatusInfo_.u8signalQuality);
				std::cout << "\033[1;32m" << "Status|signal quality]\033[0m:" << statusSignalQuality_ << ", siz: " << statusSignalQuality_.size() << std::endl;
			}else
			{
				return -1;
			}

			//n3GPPoperatorId_
			 pos = lhs_resp.find("operator id:");		
			if(pos != std::string::npos)
			{
				std::string line = lhs_resp.substr(pos+strlen("operator id:"));
				n3GPPoperatorId_.clear();
				for(unsigned int index = 0 ; index < line.size(); index++)
				{
					if(line[index] == '\n')break;
					if(line[index] != ' ' )
					{
						n3GPPoperatorId_.push_back(line[index]);
					}
				}
				//更新
				auto it = g_OperatorCodeMap.cbegin();
				for(; it != g_OperatorCodeMap.cend(); ++it)
				{
					auto iter01 = (it->second).cbegin();
					for(; iter01 != (it->second).cend(); ++iter01)
					{
						if(n3GPPoperatorId_ == iter01->first)
						{
							simStatusInfo_.operatorName = it->first;
							
							
							break;
						}
					}
				}
				
				std::cout << "\033[1;32m" << "3GPP|operatorId]\033[0m:" << n3GPPoperatorId_ << ", siz: " << n3GPPoperatorId_.size() <<
							"APN:" << simStatusInfo_.operatorName << std::endl;
			}else
			{
				return -1;
			}


			//modesSupported_
			 pos = lhs_resp.find("Modes  ");		
			if(pos != std::string::npos)
			{
				std::string line = lhs_resp.substr(pos+strlen("Modes  "));
				std::size_t pos1 = line.find("supported:");
				modesSupported_.clear();
				if(pos1 != std::string::npos)
				{
					for(unsigned int index = 0 ; index < line.size(); index++)
					{
						if(line[index] == '\n')break;
						if(line[index] != ' ' )
						{
							modesSupported_.push_back(line[index]);
						}
					}
					std::cout << "\033[1;32m" << "Modes|Supported]\033[0m:" << modesSupported_ << ", siz: " << modesSupported_.size() << std::endl;
					if(strstr(modesSupported_.c_str(), "4g"))
					{
						simStatusInfo_.strNetworkType = "4G";
					}else if(strstr(modesSupported_.c_str(), "5g"))
					{
						simStatusInfo_.strNetworkType = "5G";
					}
				}
			}else
			{
				return -1;
			}
			//bearerPathsMap_			
			 pos = lhs_resp.find("Bearer");		
			if(pos != std::string::npos)
			{
				std::string line = lhs_resp.substr(pos+strlen("Bearer"));
				std::size_t pos1 = line.find("paths:");
				bearerPathsV_.clear();
				if(pos1 != std::string::npos)
				{
					std::string pureStr ;
					std::string paths = line.substr(pos1+strlen("paths:"));
					for(unsigned int index = 0 ; index < paths.size(); index++)
					{
						if(paths[index] == '\n')break;
						if(paths[index] != ' ' )
						{
							
							pureStr.push_back(paths[index]);
							
						}
					}
					bearerPathsV_.push_back(pureStr);
					auto it = bearerPathsV_.cbegin();
					for(; it != bearerPathsV_.cend(); ++it)
					std::cout << "\033[1;32m" << "Bearer|Paths]\033[0m:" << *it << ", siz: " << it->size() << std::endl;
				}
			}else
			{
				simStatusInfo_.isConnAPN = false;
				std::cout <<"<-- 未连接APN,请连APN -->"  << std::endl;
				
			}
		}
		
	}
	

	

	return 0;
}


/**
	@brief 	
		@notice : 不要做重复的APN连接 否则有多个bearer path, path的差异性为Bearer/X  
 */
int CSimOperator::updateBearerStatus(std::string &lhs_resp)
{
	int ret = 0;
	if(bearerPathsV_.empty())
	{
		std::cout << RED_START << "无APN连接的BearerPaths" << COLOR_END << std::endl;
		return -1;
	}
	auto it = bearerPathsV_.cbegin();
	
	for(; it != bearerPathsV_.cend(); ++it)
	{
		
		std::string cmd = std::string("mmcli --bearer=") + *it;
		std::cout << cmd << std::endl;
		ret = CDevCheckSelf::inst()->pipeMulti(cmd, lhs_resp);
		if(ret)
		{
			continue;
		}

		std::size_t pos = lhs_resp.find("connected:");
		if(pos != std::string::npos)
		{
			std::string line = lhs_resp.substr(pos+strlen("connected:"));
			for(unsigned int index = 0; index < line.size(); ++index)
			{
				if(line[index] == '\n') break;
				if(line[index] != ' ')
				{
					bearerStatusState_.push_back(line[index]);	
				}
			}
			std::cout << "Status |connected:" << bearerStatusState_ << std::endl;
		}else
		{
			return -1;
		}

	}

	return ret;

}


inline const std::string &CSimOperator::getStatusStates()const 
{
	return statusState_ ;
}



/////////////////////////////////////////dev check ///////////////////////////
static const MV_DEV_CHECKSELF::checkself_t gDefaultDevCheckSelf =
{
	{"mcuMaster", 		false},

	{"socWifi", 		false},
	{"soc4g", 			false},
	{"socBlueT", 		false},
	{"soc2dRadar", 		false},
	{"socRGBD", 		false},
	{"socUsbCamera",	false},
	{"socImu", 			false},
	{"socMic", 			false},
	{"socScreen",		false}
};

CDevCheckSelf::CDevCheckSelf():m_checkResult(MV_DEV_CHECKSELF::ERROR_NODE)
{
	m_isStart = false;
	
	m_McuCtlPeripheralCheck.bMaster = false;
	
	m_SocPeripheralCheck.bWifi = false;
	m_SocPeripheralCheck.b4G = false;
	m_SocPeripheralCheck.bBlueT =false;
	m_SocPeripheralCheck.b2DRadar = false;
	m_SocPeripheralCheck.bRGBD = false;
	m_SocPeripheralCheck.bUsbCamera = false;
	m_SocPeripheralCheck.bImu = false;
	m_SocPeripheralCheck.bMic = false;
	m_SocPeripheralCheck.bScreen = false;

//	m_checkResultMaps = gDefaultDevCheckSelf;
	
	 
}

int CDevCheckSelf::start()
{
	int ret = 0;
	std::string resp;
	ret = CDevCheckSelf::inst()->getSimModemInst().updateModemStatus(resp);
	if(ret)
	{
		std::cout << RED_START << resp << COLOR_END << std::endl;
	}

	
	
	return ret;
}

void CDevCheckSelf::millsecondsSleep(int m_sec)
{
	int err;
	struct timeval tv;
	tv.tv_sec = m_sec/1000;tv.tv_usec = (m_sec%1000)*1000;
	do
	{
		err = select(0, NULL,NULL,NULL, &tv);
	}while(err<0&&EINTR == errno);
}

/**
*	@brief WIFI状态检测
**	@params[out]	hs_wifi_status_Map wifi硬软件状态获取			hs_errMsg WIFI的错误信息
*/
int CDevCheckSelf::checkWifiStatus(std::map<std::string, bool> &hs_wifi_status_Map, std::string &hs_errMsg)
{
	int ret = 0;
	const std::string cmd = "nmcli r | awk 'NR==2{print $0}'";
	std::string pipe_resp;
	
	if(pipeMulti(cmd, pipe_resp))
	{
		hs_errMsg = "PIPI 调用异常!";
	
		return -1;
		
	}
	std::cout << "[CMD: ]" << cmd << ", response: " << pipe_resp <<std::endl;
	char wifi_hw_status[CMIN_STRING] = {0}, wifi_status[CMIN_STRING] = {0};
	sscanf(pipe_resp.c_str(), "%s%s", wifi_hw_status, wifi_status);
	printf("hw:%s siz:%ld, wifi_status:%s,\n", wifi_hw_status, strlen(wifi_hw_status), wifi_status);
	if( !strcmp(wifi_hw_status, "disabled"))
	{
		
		hs_errMsg = "WIFI硬件异常!";
		std::cout << hs_errMsg <<std::endl;
		m_wifi_status["WIFI-HW"] = false;
		setCheckResult(MV_DEV_CHECKSELF::ERROR_WIFI);
		
		
	}else{m_wifi_status["WIFI-HW"] = true;}

	if(!strcmp(wifi_status, "disabled"))
	{
		std::cout << "WIFI使能[OFF]." << std::endl;
		m_wifi_status["WIFI"] = false;
		
		
	}else
	{
		std::cout << "WIFI使能[ON]." << std::endl;
		m_wifi_status["WIFI"] = true;
		
	}
	hs_wifi_status_Map = m_wifi_status;
	for(const auto &x : hs_wifi_status_Map)
		std::cout << ">>>>>>" << x.first << ":" << x.second << std::endl;
//	showWifiStatus();
	return ret;
}

void CDevCheckSelf::showWifiStatus()const
{
#if 0	
	auto it = m_wifi_status.cbegin();
	for (; it != m_wifi_status.cend();++it)	{
		std::cout << it->first << ":" << it->second << std::endl;
	}

#else 
	for(auto x : m_wifi_status)
	{
		std::cout << x.first << ":" << x.second << std::endl;
	}
#endif 
	return;	
}

/**
 *	@brief 增强程序健壮 该接口不允许大量轮训占用时间片
 **/

int CDevCheckSelf::findWlan(std::vector<std::string> &wifiInet, std::string &hs_errMsg)
{
	(void)(wifiInet);
	(void)(hs_errMsg);

	std::string cmd = "ifconfig -a |grep wlan";
	
	if( pipeMulti(cmd, hs_errMsg))
	{
		std::cout << RED_START << "未发现wifi硬件模块" << COLOR_END << std::endl;
		return -1;	
	}

	//decltype(hs_errMsg.size()) pos = hs_errMsg.find("wlan");
	int wifiInx = 0;
	sscanf(hs_errMsg.c_str()+4, "%d", &wifiInx);

	std::cout << "wifi Index : " << wifiInx << std::endl;
	m_whichWifi.push_back(std::to_string(wifiInx));

	for(const auto &x: m_whichWifi)
			std::cout << GREEN_START << "wifi:" << x  << std::endl;;

	return 0;
}

int CDevCheckSelf::updateWifiScanList(WifiSsid_V &hs_ssid_list,std::string  &hs_response)
{
	
	unsigned int lineIndex = 0, ssidIndex = 0;
	char cmd_string[CMIN_STRING] = {0};
	char sline[CMIN_STRING], ssid[CMIN_STRING];
	
	m_ssid_list.clear();			//清空
	memset(cmd_string, 0, CMIN_STRING);

	std::vector<std::string> wifiInet;

	int wifiIndex = 0;
	if(m_whichWifi.empty())
	{
		std::cout << RED_START << "未发现wifi,需要调用findWifi接口" << COLOR_END << std::endl;
		
	}else 
	{
		for(const auto &x : m_whichWifi)
			wifiIndex = atoi(x.c_str());
	}
	
	snprintf(cmd_string, CMIN_STRING, "sudo iw wlan%d scan |grep SSID", wifiIndex);
	std::cout << "[CMD] " << cmd_string << std::endl;
	FILE *pPipe = (FILE*)0;
	pPipe = popen(cmd_string, "r");
	if(!pPipe)
	{
		hs_response = std::string("[PIPE ERROR]") + strerror(errno);
		
		return -1;
	}
	
	while(!feof(pPipe))
	{
		lineIndex = 0, ssidIndex = 0;
		memset(sline, 0, CMIN_STRING);memset (ssid, 0, CMIN_STRING);
		fgets(sline, CMIN_STRING, pPipe);
		
		for(; lineIndex < strlen(sline); ++lineIndex)
		{	//踢出pipe中的空格，换行和制表符
			if(sline[lineIndex] == '\t' || sline[lineIndex] == '\n' || sline[lineIndex] == ' ')
			{
			
				continue ;
			}
			ssid[ssidIndex] = sline[lineIndex];
			ssidIndex++;
					
		}
		//无'SSID:'字段 或无ssid或含有x00的
		if(0 == strlen(ssid) - strlen("SSID:") || strlen(sline) == 0 || strstr(ssid, "x00"))
		{
			
			continue;
		}

		

		m_ssid_list.push_back(ssid+strlen("SSID:"));
		
		
	}

	
	
	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}
	if(!m_ssid_list.empty()){hs_ssid_list = m_ssid_list;}
	else
	{
		hs_response  = std::string("未扫到SSID,可能命令异常");
		
		return -1;
	}

	return 0;
}


#if 1

int CDevCheckSelf::pipeMulti(const std::string &cmd, std::string &hs_pipe_resp)
{
	char read_buf[CMAX_STRING*10] = {0};		//FIXME	增加栈为10K 收敛mmcli --modem=0的pipe
	FILE *pPipe = popen(cmd.c_str(), "r");
	if(!pPipe)
	{
		hs_pipe_resp = std::string("[PIPE ERROR] ") + strerror(errno);
		std::cout << hs_pipe_resp <<std::endl;
		return -1;
	}

	fread(read_buf, 1, 10*CMAX_STRING, pPipe);
	hs_pipe_resp = read_buf;
	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}

	return 0;

}
#endif

/** 
*		@brief  设置Wifi
		@param[in]	rhs_ssid 用户名 rhs_passwd 密码 
		@param[out]		hs_response 返回错误字符串
		@return			0-成功 -1失败

		@debug	hs_response 
				"Error: Connection activation failed: (7) Secrets were required, but not provided." 可能无WIFI天线
*/
int CDevCheckSelf::setWifiInfo2SystemNetwork(const std::string &rhs_ssid, const std::string &rhs_passwd, std::string &hs_response )
{
	
	bool isScanSSID = false;
	char cmd_string[CMAX_STRING] = {0}, read_buf[CMAX_STRING] = {0};
	
	
	memset(cmd_string, 0, CMAX_STRING);
	FILE *pPipe = (FILE*)0;

	//更新WIFI扫描 
	std::vector<std::string> ssid_list;

//	char errorStr[CMAX_STRING] = {0};
	updateWifiScanList(ssid_list, hs_response);					
	auto vit = m_ssid_list.begin();
	for(; vit != m_ssid_list.end(); ++vit)
	{
		if(*vit == rhs_ssid)
		{
			std::cout << "搜到该SSID!\n" << std::endl;
			isScanSSID = true;
			break;
		}
	}
	
	if(false == isScanSSID)
	{
		hs_response = std::string("未扫描到SSID") + rhs_ssid;
		
		return -1;
	}
	
	std::cout << "SSID: " << rhs_ssid << ",siz: " << rhs_ssid.size() << ". passwd: " << rhs_passwd << 
			",siz:" << rhs_passwd.size() << std::endl;
	snprintf(cmd_string, CMAX_STRING-1, "sudo nmcli device wifi connect %s password %s", rhs_ssid.c_str(), rhs_passwd.c_str());
	std::cout << cmd_string << std::endl;
 	pPipe = popen(cmd_string, "r"); //阻塞
 	if(!pPipe)
 	{
 		hs_response = std::string("[PIPE ERROR]") + strerror(errno);
 	
		return -1;
 	}
	fread(read_buf, 1, CMAX_STRING-1, pPipe);
	std::cout << "[PIPE READ]: " << read_buf << "." << std::endl;
	pclose(pPipe); pPipe = (FILE*)0;
	if(!strstr(read_buf, "successfully"))			
	{
		hs_response = "设置WIFI失败:" + std::string(read_buf);
		return -1;
	}
	printf("创建WIFI%s成功!\n", rhs_ssid.c_str());
	

	/*重置wifi为设置的ssid和信息，更新 "nmcli con show"*/
	//1. 关闭之前的连接
	int wifiIndex = 0;		
	if(m_whichWifi.empty())
	{
		std::cout << RED_START << "未发现wifi,需要调用findWifi接口" << COLOR_END << std::endl;
		
	}else 
	{
		for(const auto &x : m_whichWifi)
			wifiIndex = atoi(x.c_str());
	}
	memset(cmd_string, 0, CMAX_STRING);
	snprintf(cmd_string, CMAX_STRING-1, "sudo nmcli device dis wlan%d", wifiIndex);
	std::cout << cmd_string << std::endl;
	
	pPipe = popen(cmd_string, "r");
	if(!pPipe)
	{
		hs_response = std::string("[PIPE ERROR]") + std::string(strerror(errno));
		
		return -1;
	}
	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}
	//2 开启设置的连接
	memset(cmd_string, 0, CMAX_STRING);
	snprintf(cmd_string, CMAX_STRING, "sudo nmcli connection up %s", rhs_ssid.c_str());
	pPipe = popen(cmd_string, "r");
	if(!pPipe)
	{
		hs_response = std::string("[PIPE ERROR]") + std::string(strerror(errno));
		return -1;
	}
	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}

	
	return 0;
}

int  CDevCheckSelf::getSystemWlanXInfo(std::string &rhs_ssid, std::string &rhs_passwd, std::string &sys_response)
{
	(void)(rhs_passwd);
	char pipeBuf[CMAX_STRING] = {0};
	char tssid[CMIN_STRING] = {0};
	std::string errStr = "";
	const char *nmcli_con_show_active = "nmcli con show -active |grep wifi |grep wlan";;
	FILE * pPipe = popen(nmcli_con_show_active, "r");
	if(!pPipe)
	{
		sys_response = std::string("[PIPE ERROR]") + std::string(strerror(errno));
		return -1;
	}
	size_t pipeSiz = ftell(pPipe);
	fseek(pPipe, 0, SEEK_SET);
	fread(pipeBuf, 1, pipeSiz, pPipe);

	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}
	if(!strlen(pipeBuf))
	{
		sys_response = std::string("无连接上的WIFI!");
		
		return -1;
	}
	
	sscanf(pipeBuf, "%s", tssid);
	rhs_ssid = tssid;
	
#if 0		//TODO 无权限读root权限文件
	/*根据SSID查看Passwd*/
	char ssidFileConfigure[CMIN_STRING] = {0};
	//通过SSID获取密码，密码也可不做获取
	snprintf(ssidFileConfigure, CMIN_STRING-1, "/etc/NetworkManager/system-connections/%s.nmconnection", tssid);
	FILE *fp = fopen(ssidFileConfigure, "r");
	if(!fp)
	{	sys_response = "打开"   +  std::string(ssidFileConfigure) + "失败"  ;
		
		return -1;
	}

	char line_buf[CMIN_STRING] = {0};
	char tpasswd[CMIN_STRING] = {0};
	
	while(!feof(fp))
	{
		memset(line_buf, 0, CMIN_STRING);
		fgets(line_buf, CMIN_STRING, fp);
		if(strstr(line_buf, "psk="))
		{
			
			size_t line_siz = strlen(line_buf);
			if(line_buf[line_siz-1] == '\n') line_buf[line_siz-1] = '\0';
			memcpy(tpasswd, line_buf+strlen("psk="), strlen(line_buf+strlen("psk=")));
			
			std::cout << "passwd: " << tpasswd << ",siz: " << strlen(tpasswd) << ".\n" ;
		}
	}

	fclose(fp); fp = (FILE*)0;

	if(strlen(tssid)&& strlen(tpasswd)){rhs_ssid = tssid; rhs_passwd = tpasswd;}
#endif
	return 0;
	
}

/*开启wifi服务*/
int CDevCheckSelf::startWifiServer(std::string &pErrMsg)
{
	const std::string cmd_string = "sudo nmcli r wifi on";
	FILE * pPipe = popen(cmd_string.c_str(), "r");
	if(!pPipe)
	{
		pErrMsg = std::string("[PIPE ERROR]") + std::string(strerror(errno));
		std::cout << pErrMsg << std::endl;
		return -1;
	}
	
	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}
	
	

	return 0;
}

 /*停止wifi服务*/
int CDevCheckSelf::stopWifiServer(std::string &pErrMsg)
{
	const std::string cmd_string = "sudo nmcli r wifi off";
	FILE * pPipe = popen(cmd_string.c_str(), "r");
	if(!pPipe)
	{
		pErrMsg = std::string("[PIPE ERROR]") + std::string(strerror(errno));
		std::cout << "ErrMsg: " << pErrMsg << std::endl;
		return -1;
	}
	
	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}
	

	return 0;
}


bool CDevCheckSelf::isFindSsid(const std::string &hs_ssid)
{
	auto it = m_ssid_list.cbegin();
	for(; it != m_ssid_list.cend(); ++it)
	{
		if((*it) == hs_ssid)
		{
			return true;
		}
	}

	return false;
}

/*忘记账号   -Connection 'HONOR-Hello-Ouyang_2.4' (7e9704b0-b769-49de-ab9d-e3f80464e898) successfully deleted.*/
int CDevCheckSelf::forgetCurWifi(const std::string &hs_ssid, std::string &pErrMsg)
{
	WifiSsid_V ssidV;
	char read_buf[CMAX_STRING] = {0};	

	if(false == isFindSsid(hs_ssid))
	{
		//std::cout << "未发现SSID[\" " << hs_ssid << "\"]" << std::endl; 
		if(updateWifiScanList(ssidV, pErrMsg))
		{
			pErrMsg = "重新扫描未发现SSID[\" " + hs_ssid + "\"]";
			return -1;
		}
		
	} 

	std::string cmd = "sudo nmcli c delete " + hs_ssid ;
	std::cout << "cmd: " << cmd << std::endl;
	
	FILE * pPipe = popen(cmd.c_str(), "r");
	if(!pPipe)
	{
		pErrMsg = std::string("[PIPE ERROR]") + strerror(errno);
		return -2;
	}

	fread(read_buf, 1, CMAX_STRING-1, pPipe);
	if(!strstr(read_buf, "successfully"))
	{
		pErrMsg = read_buf ;
		return -3;
	}
	// TODO : 此处可以增加其他校验，后续根据测试需要再做
	if(pPipe){pclose(pPipe); pPipe = (FILE*)0;}
	

	
	return 0;
}


unsigned int CDevCheckSelf::firstCheckSelf()
{
	m_isStart = true;
	unsigned int errorCode = 0x00;
	std::string resp;

	
	if(false == wifiCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_WIFI;
		//m_checkResultMaps["socWifi"] = false;
	}else 
	{
		//m_checkResultMaps["socWifi"] = true;
		std::cout << GREEN_START  << "wifi模块正常" << COLOR_END << std::endl;
	}
	
	if(false == c4GCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_4G;
		//m_checkResultMaps["soc4g"] = false;
		
	}else{
		//m_checkResultMaps["soc4g"] = true;
	}
	
	if(false == bluetoothCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_BLUETOOTH;
	//	m_checkResultMaps["socBlueT"] = false;
		
	}else
	{
	//	m_checkResultMaps["socBlueT"] = true;
	}
	
	if(false == radarCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_2D_RADAR;
		//m_checkResultMaps["soc2dRadar"] = false;
		
	}else
	{
		//m_checkResultMaps["soc2dRadar"] = true;
	}
	
	if(false == rgbdCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_RGBD;
		//m_checkResultMaps["socRGBD"] = false;
	}else
	{
		//m_checkResultMaps["socRGBD"] = true;
	}
	
	if(false == usbCameraCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_USB_CAMERA;
		//m_checkResultMaps["socUsbCamera"] = false;
	}else
	{
		//m_checkResultMaps["socUsbCamera"] = true;
	}
	
	if(false == imuCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_IMU;
		//m_checkResultMaps["socImu"] = false;
	}else
	{
		//m_checkResultMaps["socImu"] = true;
	}
	
	if(false == micCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_MIC;
		//m_checkResultMaps["socMic"] = false;
	}else
	{
		//m_checkResultMaps["socMic"] = true;
	}
	
	if(false == screenCheck())
	{
		errorCode += MV_DEV_CHECKSELF::ERROR_SCREEN;
		//m_checkResultMaps["socScreen"] = false;
	}else
	{
		//m_checkResultMaps["socScreen"] = true;
	}
	
	m_checkResult = errorCode;	/*首次自检结果*/
	
	return errorCode;
	
}

bool CDevCheckSelf::wifiCheck()
{
	int ret = 0;
	std::vector<std::string> wifi_v;
	std::string resp;
	ret = CDevCheckSelf::inst()->findWlan(wifi_v, resp);
	
	return 0 == ret ? true : false; 
}

bool CDevCheckSelf::c4GCheck()
{
	return false; 
	
}

bool CDevCheckSelf::bluetoothCheck()
{
	
	return false ;
}

bool CDevCheckSelf::radarCheck()
{
	setPeripheReset(MV_DEV_CHECKSELF::ERROR_2D_RADAR);
	return true;
}

bool CDevCheckSelf::rgbdCheck()
{
	return false ;
}

bool CDevCheckSelf::usbCameraCheck()
{
	return false;
}

//
bool CDevCheckSelf::imuCheck()
{
	//return (F_OK == access("/dev/HI229", F_OK)) ? true : false;	
	setPeripheReset(MV_DEV_CHECKSELF::ERROR_IMU);
	return true;
}

bool CDevCheckSelf::micCheck()
{
	
	return false ;
}


bool CDevCheckSelf::screenCheck()
{
	return false;
}

void CDevCheckSelf::MonitorPorc()
{
	while(m_isStart)
	{

		
		usleep(40 * 1000);
	}
}
CDevCheckSelf::~CDevCheckSelf()
{
	static unsigned int u32DestructionTime = 0;
	u32DestructionTime++;
	printf("[Destruction] >> %u \n", u32DestructionTime);

	m_isStart = false;
	if(m_MonitorHandle.joinable())
	{
		m_MonitorHandle.join();

	}
}


#if 1
/**
 *		@brief 	一个简单测试WIFI/MODEM的程序
 */
enum {
	FUNCTION_WIFI_SCAN = 0x00,
	FUNCTION_WIFI_SET_WIFI,
	FUNCTION_WIFI_GET_WIFI,
	FUNCTION_WIFI_ON,
	FUNCTION_WIFI_OFF = 0x04,
	FUNCTION_WIFI_FORGOT,
	FUNCTION_WIFI_WIFI_ENABLE =0x06,			//wifi使能开关
	FUNCTION_WIFI_WIFI_FIND  = 0x07,
	//modem
	FUNCTION_SIM_checkModemManageStatus = 0X10,// 16
	FUNCTION_SIM_activeModemList = 0X11,			//17
	FUNCTION_SIM_setModelEnable = 0x12,			//18

	FUNCTION_SIM_connAPN = 0x13			//19

	
};
int main(int argc, char *argv[])
{
	int sel = 0;	
	if(argc < 2)
	{
		sel = atoi(argv[1]);
		std::cout <<"Usage: ./" << argv[0] << " 1" <<std::endl;  
		exit(0);
	}
	switch(sel)
	{
	case FUNCTION_WIFI_SCAN:
	{
		std::cout << "获取WIFI 列表" << std::endl;
		CDevCheckSelf::WifiSsid_V v;
		std::string e;
		
		std::vector<std::string> wifiV;
		std::string resp;
		if(CDevCheckSelf::inst()->findWlan(wifiV, resp))
		{
			std::cout << RED_START << "未发现wifi模块" << COLOR_END << std::endl;
		}
		
		CDevCheckSelf::inst()->updateWifiScanList(v,e);
		auto it = v.begin();
		for(; it != v.end(); ++it)
		{
			std::cout << *it << std::endl;
		}
	}break;
	case FUNCTION_WIFI_SET_WIFI:
	{
		std::cout << "设置wifi" << std::endl;
		std::string ssid, passwd, sys_response;
		std::vector<std::string> wifiInet;std::string hs_errMsg;
		CDevCheckSelf::inst()->findWlan(wifiInet, hs_errMsg);
		ssid="HONOR-Hello-Ouyang_2.4";
		passwd = "woshisunouyangaaaa";
		CDevCheckSelf::inst()->setWifiInfo2SystemNetwork(ssid, passwd, sys_response);
		std::cout << "Done." << std::endl;
	}break;
	case FUNCTION_WIFI_GET_WIFI:
	{	
		std::cout << "获取当前WIFI信息" << std::endl;
		std::string ssid, passwd, resp;
		char buf[1024];
		int ret = CDevCheckSelf::inst()->getSystemWlanXInfo(ssid, passwd, resp);
		if(ret)
		{
			std::cout << resp <<std::endl;
			return -1;
		}else
		{
			std::cout << "ssid: " << ssid << ",passwd: " << passwd << std::endl;
		}
	}break;

	case FUNCTION_WIFI_ON:
	{
		std::string e;
		CDevCheckSelf::inst()->startWifiServer(e);
	}break;
	case FUNCTION_WIFI_OFF:
	{
		std::string e;
		CDevCheckSelf::inst()->stopWifiServer(e);
	}break;
	case FUNCTION_WIFI_FORGOT:
	{
		std::string e;
		std::string ssid = "HONOR-Hello-Ouyang_2.4";
		CDevCheckSelf::inst()->forgetCurWifi(ssid, e);
	}break;
	case FUNCTION_WIFI_WIFI_ENABLE:
	{
		
		std::map<std::string, bool> wifistatus;
		std::string errMsg;
		CDevCheckSelf::inst()->checkWifiStatus(wifistatus,errMsg);
		
		
	}break;
	case FUNCTION_WIFI_WIFI_FIND:
	{
		std::string mesg;
		std::vector<std::string> wifiInet;
		CDevCheckSelf::inst()->firstCheckSelf();
		CDevCheckSelf::inst()->findWlan(wifiInet, mesg);
	}break;


	//SIM
	case FUNCTION_SIM_checkModemManageStatus:
	{
		std::string resp ;
		
		if(!CDevCheckSelf::inst()->getSimModemInst().checkModemManageStatus(resp))
		{
			std::cout << "modemMgr已开启" << std::endl;
		}
	}break;

	case FUNCTION_SIM_activeModemList:
	{
		std::string resp;
		CDevCheckSelf::inst()->getSimModemInst().updateActiveModemList(resp);
	}break;
	case FUNCTION_SIM_setModelEnable:
	{
		std::string resp;
		bool enable = true;
		if(0 == CDevCheckSelf::inst()->getSimModemInst().setModelEnable(enable, resp))
		{
			std::cout << "设置成功:" << resp << std::endl;
		}
	}break;

	case FUNCTION_SIM_connAPN:	//19
	{
		std::string resp;
		
		if(0 == CDevCheckSelf::inst()->getSimModemInst().connAPN(resp))
		{
			std::cout << resp << std::endl;
		}
		CDevCheckSelf::inst()->getSimModemInst().updateBearerStatus(resp);
	}break;
	
	}
	while(1)
	{
		sleep(1);
	}
}
#endif
