#ifndef BIKELIB_H
#define BIKELIB_H

#include <Arduino.h>
#include <DFRobot_sim808.h>
#include <aJSON.h>
#include <RFID.h>
#include <U8glib.h>

// 调试标志
static bool isDebug = true;

//////////////////////////////////////
// --------- 调用工具实例 --------- //
//////////////////////////////////////
extern RFID rfid;                   // 读卡模块底层操作工具实例
extern DFRobot_SIM808 sim808;       // 通讯定位模块底层操作工具实例
extern U8GLIB_SH1106_128X64 u8g;    // 显示模块底层操作工具实例


//////////////////////////////////////
// --------- 工具状态定义 --------- //
//////////////////////////////////////

// 车辆借还状态
enum RENT_STATE {
    NOT_AVAILABLE,                  // 车辆不可用
    NOT_RENT,                       // 未借用
    RENT_UNDER_WAY,                 // 借用操作中
    RENT,                           // 已借用
    RETURN_UNDER_WAY,               // 还车操作中
};

// 读卡状态
enum CARD_STATE {
    CARD_NOT_FOUND,             // 未发现卡片
    CARD_READING,               // 卡片读取中
    CARD_FOUND,                 // 已发现卡片
    CARD_DETATCHING,            // 卡片取走中
};

// 读卡消息
enum CARD_MSG {
    NOTHING,                        = 0,    // 无新信息
    NEW_CARD_DETECTED,              = 10,   // 发现新卡片（亦可能识别错误）
    NEW_CARD_CONFIRMED,             = 11,   // 确认新卡片
    SAME_CARD_AGAIN,                = 20,   // 再次读到同一张卡
    CARD_DETATCHED,                 = 30,   // 卡片已取走（亦可能接触不良）
    CARD_DETATCH_CONFIRMED,         = 31,   // 确认卡片已取走
    CARD_READ_STOP,                 = 40,   // 卡片读取中断
    ERROR_DIFFERENT_CARD,           = 97,   // 错误：探测到不同卡片
    ERROR_NOT_AVAILABLE_CARD,       = 98,   // 错误：车辆不可用
    ERROR_OTHER_CARD,               = 99,   // 错误：预留
};

// 通讯请求编码
enum REQUEST_MSG {
    REQUEST_NULL                    = 0,    // 无信息
    REQUEST_RENT                    = 10,   // 请求借车
    REQUEST_RETURN                  = 20,   // 请求还车
    REQUEST_LOCATION                = 30,   // 发送定位信息
    REQUEST_LOCATION_FAIL           = 31,   // 发送定位失败
    REQUEST_LOWBATTERY              = 40,   // 发送低电量信息
};

// 通讯信息层回复编码
enum RESPONSE_MSG {
    RESPONSE_NULL                   = 0,    // 无信息
    // 信息层
    RENT_SUCCESS                    = 110,  // 借车成功
    RENT_FAIL_USER_OCCUPIED         = 120,  // 借车失败：用户正在使用其它车辆
    RENT_FAIL_USER_NONEXISTENT      = 121,  // 借车失败：用户不存在
    RENT_FAIL_NEGATIVE_BALANCE      = 122,  // 借车失败：用户欠费
    RENT_FAIL_BIKE_OCCUPIED         = 130,  // 借车失败：车辆被其他用户占用
    RENT_FAIL_BIKE_UNAVAILABLE      = 131,  // 借车失败：车辆故障
    RETURN_SUCCESS                  = 210,  // 还车成功
    RETURN_FAIL_USER_NOT_MATCH      = 220,  // 还车失败：用户信息冲突
    RETURN_FAIL_ORDER_NONEXISTENT   = 230,  // 还车失败：车辆未借出
    LOCATION_SUCCESS                = 310,  // 定位信息发送成功
    LOCATION_FAIL                   = 320,  // 定位信息发送失败：预留
    LOWBATTERY_SUCCESS              = 410,  // 低电量信息发送成功
    LOWBATTERY_FAIL                 = 420,  // 低电量信息发送失败：预留
    ERROR_OTHER                     = 100,  // 错误：服务器其它错误
    // 通讯及解码层
    ERROR_STATUS                    = 900,  // 请求状态错误
    ERROR_REQUEST_OVERTIME          = 910,  // 请求超时
    ERROR_INVALID_RESPONSE          = 920,  // 接收信息无效
    ERROR_DECODE                    = 930,  // 回复信息解码错误
};


//////////////////////////////////////
// ----------- 工具定义 ----------- //
//////////////////////////////////////
/**
 * 车辆借还状态工具
 */
class RentState {
    public:
        RentState(RENT_STATE rentState = NOT_RENT);

        void changeState(const RENT_STATE newState);
        RENT_STATE getState();

        bool isAvailable();

    private:
        RENT_STATE _rentState;
};


/**
 * 读卡工具
 * 使用流程：寻卡       -> 依照状态：获取序列号 / 其他操作
 *           searchCard -> getSerNum / [OTHER OPERATION]
 */
class Card {

    public:
        Card();

        CARD_MSG searchCard(RENT_STATE state);
        unsigned long getSerNum();

        void reset();

    private:

        // 满足操作条件最低读卡次数
        const int MIN_CARD_READING = 3;
        const int MIN_CARD_DETATCH = 5;

        unsigned long cardSerNum;
        CARD_STATE _cardState;
        int _cardCounter;

        void changeState(const CARD_STATE newState);
        CARD_STATE getState();

        inline int getCounter();
        inline void addCounter();
        inline void clearCounter();
};


/**
 * 定时反馈通讯操作工具
 */
class RoutineUpdate {
    public:
        RoutineUpdate();

        unsigned long getLastUpdate();
        bool needUpdate();
        void pauseUpdate();
        void resumeUpdate();
        int doUpdate();

        void reset();

    private:
        unsigned long _lastUpdate;
        bool updatePaused;

        void setLastUpdate(const unsigned long newUpdate);
};


/**
 * 通讯工具
 * 使用流程：发送请求   -> 成功：检查是否有回复 -> 获取回复    -> 重置
 *           requestXXX    true: hasResponse    -> getResponse -> resetResponse
 *                      -> 失败：获取错误信息 -> 重置
 *                         false: getError    -> resetResponse
 * 通讯及解码层：本地通讯模块及信息解码
 * 信息层：服务器回复信息意图
 */
class HTTPCom {
    public:
        HTTPCom();

        bool requestRent(const int bikeID, const unsigned long cardSerial);
        bool requestReturn(const int bikeID, const unsigned long cardSerial);
        bool requestLocation(const int bikeID, const float longitude, const float latitude, const float batteryLevel);
        bool requestLocationFail(const int bikeID, const float batteryLevel);
        bool requestLowBattery(const int bikeID, const float batteryLevel);

        bool hasResponse();

        RESPONSE_MSG getResponse();
        RESPONSE_MSG getError();
        
        String getResponse_UserID();
        String getResponse_Balance();
        String getResponse_Duration();

        void resetResponse();

    private:
        RESPONSE_MSG _state;
        String _userID;
        String _balance;
        String _duration;
        String _request;

        const unsigned long INTERVAL_SHORT = 200;
        const unsigned long INTERVAL_LONG = 1000;

        // GET指令头尾（包括访问URL）
        const String REQUEST_CMD_HEADER = "AT+HTTPPARA=\"URL\",\"http://52.197.101.234/test.php";
        const String REQUEST_CMD_ENDER = "\"\r\n";

        // 通讯操作关键字
        const String REQUEST_CMD_RENT_RETURN = "?get1=";
        const String REQUEST_CMD_LOCATION = "?get2=";
        const String REQUEST_CMD_BATTERY = "?get3=";

        // 通讯接口变量关键字
        const char* KEY_STATE = "state";
        const char* KEY_BIKEID = "bikeID";
        const char* KEY_USERID = "userID";
        const char* KEY_BALANCE = "balance";
        const char* KEY_DURATION = "duration";
        const char* KEY_CARDSERIAL = "cardSerial";
        const char* KEY_BATTERYLEVEL = "batteryLevel";
        const char* KEY_LONGITUDE = "longitude";
        const char* KEY_LATITUDE = "latitude";

        bool _hasResponse;

        bool sendRequest();
        bool decodeResponse(String response);
};


/**
 * 交互工具
 */
class Display {
    public:
        Display();

        bool isDisplaying();

        void displayWait();
        void displayClear();
        void displayComMSG(const RESPONSE_MSG msg);
        void displayDetails(const RESPONSE_MSG msg, const char* userID, const char* balance, const char* duration = "");
        void displayCardMSG(const CARD_MSG msg);

    private:
        bool _isDisplaying;

        const unsigned long DURATION = 5000;
        const unsigned long DURATION_LONG = 10000;
};


/**
 * 车锁控制工具
 */
class Lock {
    public:
        Lock();

        void unlock();

    private:
        const unsigned long DURATION = 10000;
};


//////////////////////////////////////
// --------- 全局工具实例 --------- //
//////////////////////////////////////
extern RentState        RENTSTATE;
extern Card             CARD;
extern RoutineUpdate    UPDATE;
extern HTTPCom          HTTPCOM;
extern Display          DISPLAYS;
extern Lock             LOCK;

//////////////////////////////////////
// ----------- 全局函数 ----------- //
//////////////////////////////////////
/**
 * 记录日志
 */
void Log(String tag, String log);
void Log(String log);
void Log(RESPONSE_MSG log);
void Log(CARD_MSG log);
void Log(RENT_STATE log);

void Error(String error);

/**
 * 系统初始化
 */
bool setupInit();

/**
 * 每循环终止操作
 */
bool loopTerm();

#endif