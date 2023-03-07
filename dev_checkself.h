#ifndef MV_CHECK_SELF_
#define MV_CHECK_SELF_

#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifndef RED_START 
#define RED_START 	"\0033[1;31m"
#endif 
#ifndef GREEN_START 
#define GREEN_START "\033[1;32m"
#endif 
#ifndef YELLOW_START 
#define YELLOW_START "\033[1;33m[warm]"
#endif 

#ifndef COLOR_END
#define COLOR_END	"\033[0m"
#endif 
namespace MV_DEV_CHECKSELF
{
/*包含SOC和MCU的外围,后续如果超过了最大系统值，采用关联容器*/
typedef enum TagPeripheralCheckErrorCode
{
	ERROR_NODE 			= 0x00,
/*SOC*/
	ERROR_WIFI 			= 0x01,
	ERROR_4G   			= 0x02, 
	ERROR_BLUETOOTH	 	= 0x04,
	ERROR_2D_RADAR 		= 0x08, 
	ERROR_RGBD			= 0x10,
	ERROR_USB_CAMERA 	= 0x20, 
	ERROR_IMU			= 0x40,
	ERROR_MIC			= 0x80,
	ERROR_SCREEN 		= 0x100,
/*mcu*/
	//待补充
	
}PeripheralCheckErrorCode_E;

typedef struct TagMcuCtlPeripheralCheck
{
	bool bMaster; 
}McuCtlPeripheralCheck_S;

typedef struct TagSocPeripheralCheck
{
	bool bWifi;
	bool b4G;
	bool bBlueT;
	bool b2DRadar;
	bool bRGBD;
	bool bUsbCamera;
	bool bImu;
	bool bMic;
	bool bScreen;
}SocPeripheralCheck_S;



typedef std::map<std::string, bool> checkself_t;


}

class CSimOperator
{
public:
	typedef struct TagSimInfo_S
	{
		
		std::string operatorName;	//APN e.g 中国移动  
		unsigned char u8signalQuality;	/*信号质量		需要轮训,方便界面显示*/
		std::string strNetworkType;		/*4G/5G*/
		
		bool isConnAPN;				//true-开启    false-断开				
		
		TagSimInfo_S():u8signalQuality(0), strNetworkType(""),operatorName(""),isConnAPN(false){}
	}SimInfo_S;
	
	
	CSimOperator();
	~CSimOperator();

	// {对外接口,封装协议KEY
	const SimInfo_S &getSimInfo()const {return simStatusInfo_;}
	int setModelEnable(bool enable, std::string &lhs_resp);
	//}

	
	//连接准备工作			
	int checkModemManageStatus(std::string &);
	int updateActiveModemList(std::string &hs_resp);
	
	
	int connAPN(std::string &);
	int disconnectAPN(std::string &);

	/*更新sim modem卡实时信息*/
	int updateModemStatus(std::string &lhs_resp);
	/*更新sim bearer的实时信息*/
	int updateBearerStatus(std::string &lhs_resp);
	
	//int getSimInfo()const ;//运营商名称 流量套餐 剩余流量 网络类型
protected:
	const std::string &getStatusStates()const;
	
	
	
private:
	SimInfo_S simStatusInfo_;															//该结构对应上层业务	
	std::map<int, std::string> modemMap_;	//该值确认是否有硬件模块

	/*modem status*/
	std::string statusState_;				//状态
	std::string statusSignalQuality_;			//信号强度
	std::string n3GPPoperatorId_;				//apn code e.g "46001"
	std::string modesSupported_;			//支持的模式(2g/3g/4g/5g)
	std::vector<std::string> bearerPathsV_;		//bearer path   mmcli --bearer=[MODEM-INDEX]

	/*bearer status*/
	std::string bearerStatusState_;				//{ "yes"| "no"}
	
	

};

//////////////////////////////////////////CDevCheckSelf class/////////////////////////////////////////////////////////////////////////////

class CDevCheckSelf{
	static constexpr size_t CMIN_STRING=128;
	static constexpr size_t CMAX_STRING = 1024;
	
public:
	typedef std::vector<std::string> WifiSsid_V;
	
	static CDevCheckSelf *inst()
	{
		static CDevCheckSelf cs;
		return &cs;
	}

	int start();
	unsigned int firstCheckSelf();
	
	/*外部接口调用-主要MCU调用,确认异常设置设置bit异常*/
	void setCheckResult(const MV_DEV_CHECKSELF::PeripheralCheckErrorCode_E &rErrorCode){m_checkResult+=rErrorCode;}
	/*外部接口调用-恢复某一个外围(IO)正常*/
	void setPeripheReset(const MV_DEV_CHECKSELF::PeripheralCheckErrorCode_E &rErrorCode){m_checkResult &= ~rErrorCode;}

	void getCheckResult(unsigned int& check_result)const {check_result = m_checkResult;}

	void millsecondsSleep(int m_sec);
	
//	void getCheckResult(MV_DEV_CHECKSELF::checkself_t & check_resultmaps){ check_resultmaps = m_checkResultMaps;}

	/*功能模块,default wlan0 */
	//wlan discory
	int findWlan(std::vector<std::string> &wifiInet, std::string &hs_errMsg);
	//wifi 扫描
	int updateWifiScanList(WifiSsid_V &hs_ssid_list, std::string &hs_response);
	//设置wifi
	int setWifiInfo2SystemNetwork(const std::string &rhs_ssid, const std::string &rhs_passwd, std::string &sys_response );
	//获取当前wifi信息
	int getSystemWlanXInfo(std::string &rhs_ssid, std::string &rhs_passwd, std::string& hs_response); 
	int startWifiServer(std::string &pErrMsg);
	int stopWifiServer(std::string &pErrMsg);
	int forgetCurWifi(const std::string &hs_ssid, std::string &pErrMsg);
	
	int checkWifiStatus(std::map<std::string, bool> &hs_wifi_status_Map, std::string &hs_errMsg);//确认WIFI状态		@thread safe
	//WIFI END

	

	//comm 
	int pipeMulti(const std::string &cmd, std::string &hs_pipe_resp);

	//SIM
	
	CSimOperator &getSimModemInst(){return simOperatorInst_;}
protected:
	bool wifiCheck();
	bool c4GCheck();
	bool bluetoothCheck();
	bool radarCheck();
	bool rgbdCheck();
	bool usbCameraCheck();
	bool imuCheck();
	bool micCheck();
	bool screenCheck();

	void MonitorPorc();

	//WIFI function
	bool isFindSsid(const std::string &hs_ssid);
	
	
	void showWifiStatus()const;


private:
	CDevCheckSelf();
	~CDevCheckSelf();
private:

	bool m_isStart;
	std::thread	m_MonitorHandle;
//	MCU
	MV_DEV_CHECKSELF::McuCtlPeripheralCheck_S m_McuCtlPeripheralCheck;

//Soc挂载外围
	
	MV_DEV_CHECKSELF::SocPeripheralCheck_S m_SocPeripheralCheck;
	
	unsigned int m_checkResult;				//0为自检正常，其他表示有异常，异常使用bitmap方式将外围的设备(含IO)存储

//	MV_DEV_CHECKSELF::checkself_t m_checkResultMaps;
	//wifi 
	WifiSsid_V m_ssid_list;
	
	std::vector<std::string> m_whichWifi;
	std::map<std::string, bool> m_wifi_status;		//wifi功能状态

	CSimOperator simOperatorInst_;

};



#endif