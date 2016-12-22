#include "BikeLib.h"

#define RC_RST_PIN      5   // RC522: RST引脚
#define RC_SS_PIN      53   // RC522: SS引脚（UNO: 10; MEGA: 53）
#define LOCK_PIN       30   // 开锁用引脚

//////////////////////////////////////
// --------- 调用工具实例 --------- //
//////////////////////////////////////
RFID rfid(RC_SS_PIN, RC_RST_PIN);   // 读卡模块底层操作工具实例
                                    // 使用SPI通讯
DFRobot_SIM808 sim808(&Serial);     // 通讯定位模块底层操作工具实例
U8GLIB_SH1106_128X64 u8g(U8G_I2C_OPT_NONE);
                                    // 显示模块底层操作工具实例
                                    // 使用TWI通讯


//////////////////////////////////////
// ---------- RentState ----------- //
//////////////////////////////////////
/**
 * 车辆借还状态工具构造函数
 */
RentState::RentState(RENT_STATE rentState) {
    _rentState = rentState;
}

// public:
/**
 * 更改全局车辆借还状态
 * @param newState 新的车辆借还状态
 */
void RentState::changeState(const RENT_STATE newState) {
    _rentState = newState;
}

/**
 * 获取全局车辆借还状态
 * @return 车辆借还状态
 */
RENT_STATE RentState::getState() {
    return _rentState;
}

/**
 * 检查车辆是否可用
 * @return true - 可用; false - 不可用
 */
bool RentState::isAvailable() {
    if (_rentState == NOT_AVAILABLE) {
        return false;
    } else {
        return true;
    }
}


//////////////////////////////////////
// ------------- Card ------------- //
//////////////////////////////////////
/**
 * 读卡工具构造函数
 */
Card::Card() {
    cardSerNum = 0;
    _cardState = CARD_NOT_FOUND;
    _cardCounter = 0;
}

// public:
/**
 * 寻卡并读取卡片信息
 * @return 寻卡结果
 */
CARD_MSG Card::searchCard(RENT_STATE state) {

    if ((rfid.isCard()) && (rfid.readCardSerial())) {

        if (state != NOT_AVAILABLE) {
            // 车辆可用
            switch (getState()) {
                case CARD_NOT_FOUND:
                    addCounter();
                    changeState(CARD_READING);
                    return NEW_CARD_DETECTED;
                break;
                case CARD_READING:
                    addCounter();
                    if (getCounter() > MIN_CARD_READING) {
                        clearCounter();
                        changeState(CARD_FOUND);
                        if (cardSerNum != rfid.cardSerNum()) {
                            // 发现新卡
                            cardSerNum = rfid.cardSerNum();
                            return NEW_CARD_CONFIRMED;
                        } else {
                            // 同一张卡
                            return SAME_CARD_AGAIN;
                        }
                    }
                break;
                case CARD_FOUND:
                    // 判断卡片是否改变
                    if (cardSerNum != rfid.cardSerNum()) {
                        clearCounter();
                        changeState(CARD_READING);
                        cardSerNum = rfid.cardSerNum();
                        // 返回错误
                        return ERROR_DIFFERENT_CARD;
                    }
                break;
                case CARD_DETATCHING:
                    clearCounter();
                    addCounter();
                    changeState(CARD_READING);
                    return NEW_CARD_DETECTED;
                break;
                default:
                break;
            }
        } else {
            // 车辆不可用
            return ERROR_NOT_AVAILABLE_CARD;
        }
    } else {
        switch (getState()) {
            case CARD_NOT_FOUND:
                // 无操作
            break;
            case CARD_READING:
                clearCounter();
                addCounter();
                changeState(CARD_DETATCHING);
                return CARD_DETATCHED;
            break;
            case CARD_FOUND:
                addCounter();
                changeState(CARD_DETATCHING);
                return CARD_DETATCHED;
            break;
            case CARD_DETATCHING:
                addCounter();
                if (getCounter() > MIN_CARD_DETATCH) {
                    if (cardSerNum != 0) {
                        // 之前存在卡片
                        reset();
                        return CARD_DETATCH_CONFIRMED;
                    } else {
                        // 之前无卡片
                        reset();
                        return CARD_READ_STOP;
                    }
                }
            break;
            default:
            break;
        }
    }

    return NOTHING;
}

/**
 * 获取卡片序列号
 * @return 卡片序列号
 */
unsigned long Card::getSerNum() {
    return cardSerNum;
}

/**
 * 重置读卡工具（改变系统状态 谨慎使用 建议在无卡且未借车状态下使用）
 */
void Card::reset() {
    cardSerNum = 0;
    changeState(CARD_NOT_FOUND);
    clearCounter();
}

// private:
/**
 * 更改读卡状态（内部操作 私有）
 * @param newState 新的读卡状态
 */
void Card::changeState(const CARD_STATE newState) {
    _cardState = newState;
}

/**
 * 获取读卡状态（内部操作 私有）
 * @return 读卡状态
 */
CARD_STATE Card::getState() {
    return _cardState;
}

/**
 * 获取读卡次数计数器
 * @return 读卡次数计数器
 */
inline int Card::getCounter() {
    return _cardCounter;
}

/**
 * 读卡次数计数器加值
 */
inline void Card::addCounter() {
    _cardCounter++;
}

/**
 * 读卡次数计数器清零
 */
inline void Card::clearCounter() {
    _cardCounter = 0;
}


//////////////////////////////////////
// -------- LocationUpdate -------- //
//////////////////////////////////////
/**
 * 定时定位操作工具构造函数
 */
LocationUpdate::LocationUpdate() {
    _lastUpdate = sysTime();
    _updatePaused = false;

    // 默认定位信息
    _latitude = 1000;
    _longitude = 1000;
}

// public:
/**
 * 获取上次定位时间
 * @return 上次定位时间
 */
unsigned long LocationUpdate::getLastUpdate() {
    return _lastUpdate;
}

/**
 * 改变上次定位时间
 * param 当前时刻
*/
void RoutineUpdate::setLastUpdate(unsigned long currentTime){
    _lastUpdate = currentTime;
}


/**
 * 检查是否需要进行定位（依现在借车状态而定）
 * @return true - 需要; false - 不需要
 */
bool LocationUpdate::needUpdate(const RENT_STATE state) {

    // 检查更新是否被暂停
    if (_updatePaused) {
        return false;
    }

    // 检查车辆状态
    switch (state) {
        case RENT:
            if (!withinInterval(_lastUpdate, sysTime(), UPDATE_INTERVAL_RENT)) {
                // 与上次更新相差时间足够
                return true;
            }
        break;
        case NOT_RENT:
            if (!withinInterval(_lastUpdate, sysTime(), UPDATE_INTERVAL_NOT_RENT)) {
                // 与上次更新相差时间足够
                return true;
            }
        break;
        case NOT_AVAILABLE:
            if (!withinInterval(_lastUpdate, sysTime(), UPDATE_INTERVAL_NOT_AVAILABLE)) {
                // 与上次更新相差时间足够
                return true;
            }
        break;
        default:
        break;
    }

    return false;
}

/**
 * 暂停定位和检查电量操作
 */
void LocationUpdate::pauseUpdate() {
    Log(TAG_LOCATION, "Location Update Paused");
    _updatePaused = true;
}

/**
 * 恢复定位和检查电量操作
 */
void LocationUpdate::resumeUpdate() {
    Log(TAG_LOCATION, "Location Update Resumed");
    _updatePaused = false;
}

/**
 * 定位和检查电量操作 成功后更新时间
 * @return 预留
 */
bool LocationUpdate::doUpdate() {
    
    // 重置定位信息
    resetLocation();

    bool locateSuccess = false;

    // 打开GPS
    locateSuccess = sim808.attachGPS();
    if (locateSuccess) {
        // 获取初始时间
        unsigned long startGPS = sysTime();
        locateSuccess = false;

        // 如果定位未超时
        while(withinInterval(startGPS, sysTime(), GPS_OVERTIME)) {
            // 是否接收到GPS数据
            if (sim808.getGPS()) {
                _latitude = sim808.GPSdata.lat;
                _longitude = sim808.GPSdata.lon;
                locateSuccess = true;
                break;
            }
        }
        Log(TAG_LOCATION, "GPS Overtime!");
    }
    else {
        Log(TAG_LOCATION, "GPS Init Fail!");
    }

    // 关闭GPS
    sim808.detachGPS();
    // 更新时间
    setLastUpdate(sysTime());

    return locateSuccess;
}

/**
 * 重置定位信息（定位前重置）
 */
void LocationUpdate::resetLocation() {
    setLastUpdate(sysTime());
    _updatePaused = false;

    // 重置定位信息
    _latitude = 1000;
    _longitude = 1000;
}

/**
 * 重置工具（全部重置 谨慎使用）
 */
void LocationUpdate::reset() {

    // 重置定位信息
    _latitude = 1000;
    _longitude = 1000;
}

/**
 * 获取纬度信息
 * @return 纬度
 */
float LocationUpdate::getLatitude() {
    return _latitude;
}

/**
 * 获取经度信息
 * @return 经度
 */
float LocationUpdate::getLongitude() {
    return _longitude;
}

// private:



//////////////////////////////////////
// ----------- HttpCom ------------ //
//////////////////////////////////////
/**
 * 通讯工具构造函数
 */
HTTPCom::HTTPCom() {
    _hasResponse = false;
    _userID = "";
    _balance = "";
    _duration = "";
    _request = "";
    _state = RESPONSE_NULL;
}

// public:
/**
 * 发送借车请求
 * @param  bikeID     自行车编号
 * @param  cardSerial 卡片序列号
 * @return            请求是否成功（仅包括通讯及解码层）
 *                    true - 成功; false - 失败
 */
bool HTTPCom::requestRent(const int bikeID, const unsigned long cardSerial) {
    // request清空
    _request = "";
    // 指令开始
    _request = String(REQUEST_CMD_HEADER);
    // 借车还车请求
    _request += REQUEST_CMD_RENT_RETURN;
    // 请求码
    _request += (String(KEY_STATE) + "=");
    _request += (String(REQUEST_RENT) + ",");
    // 车辆编号
    _request += (String(KEY_BIKEID) + "=");
    _request += (String(bikeID) + ",");
    // 卡片序列号
    _request += (String(KEY_CARDSERIAL) + "=");
    _request += String(cardSerial);
    // 指令截止符
    _request += REQUEST_CMD_ENDER;

    // 发送请求
    return sendRequest();
}

/**
 * 发送还车请求
 * @param  bikeID     自行车编号
 * @param  cardSerial 卡片序列号
 * @return            请求是否成功（仅包括通讯及解码层）
 *                    true - 成功; false - 失败
 */
bool HTTPCom::requestReturn(const int bikeID, const unsigned long cardSerial) {
    // request清空
    _request = "";
    // 指令开始
    _request = String(REQUEST_CMD_HEADER);
    // 借车还车请求
    _request += REQUEST_CMD_RENT_RETURN;
    // 请求码
    _request += (String(KEY_STATE) + "=");
    _request += (String(REQUEST_RETURN) + ",");
    // 车辆编号
    _request += (String(KEY_BIKEID) + "=");
    _request += (String(bikeID) + ",");
    // 卡片序列号
    _request += (String(KEY_CARDSERIAL) + "=");
    _request += String(cardSerial);
    // 指令截止符
    _request += REQUEST_CMD_ENDER;

    // 发送请求
    return sendRequest();
}

/**
 * 发送定位信息
 * @param  bikeID       自行车编号
 * @param  longitude    经度
 * @param  latitude     纬度
 * @param  batteryLevel 电量信息
 * @return              请求是否成功（仅包括通讯及解码层）
 *                      true - 成功; false - 失败
 */
bool HTTPCom::requestLocation(const int bikeID, const float longitude, const float latitude, const float batteryLevel) {
    // request清空
    _request = "";
    // 指令开始
    _request = String(REQUEST_CMD_HEADER);
    // 定位成功请求
    _request += REQUEST_CMD_LOCATION;
    // 请求码
    _request += (String(KEY_STATE) + "=");
    _request += (String(REQUEST_LOCATION) + ",");
    // 车辆编号
    _request += (String(KEY_BIKEID) + "=");
    _request += (String(bikeID) + ",");
    // 定位经度
    _request += (String(KEY_LONGITUDE) + "=");
    _request += (String(longitude) + ",");
    // 定位纬度
    _request += (String(KEY_LATITUDE) + "=");
    _request += (String(latitude) + ",");
    // 电池电量
    _request += (String(KEY_BATTERYLEVEL) + "=");
    _request += String(batteryLevel);
    // 指令截止符
    _request += REQUEST_CMD_ENDER;

    // 发送请求
    return sendRequest();
}

/**
 * 发送定位失败信息
 * @param  bikeID       自行车编号
 * @param  batteryLevel 电量信息
 * @return              请求是否成功（仅包括通讯及解码层）
 *                      true - 成功; false - 失败
 */
bool HTTPCom::requestLocationFail(const int bikeID, const float batteryLevel) {
    // request清空
    _request = "";
    // 指令开始
    _request = String(REQUEST_CMD_HEADER);
    // 定位失败或低电量请求
    _request += REQUEST_CMD_BATTERY;
    // 请求码
    _request += (String(KEY_STATE) + "=");
    _request += (String(REQUEST_LOCATION_FAIL) + ",");
    // 车辆编号
    _request += (String(KEY_BIKEID) + "=");
    _request += (String(bikeID) + ",");
    // 电池电量
    _request += (String(KEY_BATTERYLEVEL) + "=");
    _request += String(batteryLevel);
    // 指令截止符
    _request += REQUEST_CMD_ENDER;

    // 发送请求
    return sendRequest();
}

/**
 * 发送低电量信息
 * @param  bikeID       自行车编号
 * @param  batteryLevel 电量信息
 * @return              请求是否成功（仅包括通讯及解码层）
 *                      true - 成功; false - 失败
 */
bool HTTPCom::requestLowBattery(const int bikeID, const float batteryLevel) {
    // request清空
    _request = "";
    // 指令开始
    _request = String(REQUEST_CMD_HEADER);
    // 定位失败或低电量请求
    _request += REQUEST_CMD_BATTERY;
    // 请求码
    _request += (String(KEY_STATE) + "=");
    _request += (String(REQUEST_LOWBATTERY) + ",");
    // 车辆编号
    _request += (String(KEY_BIKEID) + "=");
    _request += (String(bikeID) + ",");
    // 电池电量
    _request += (String(KEY_BATTERYLEVEL) + "=");
    _request += String(batteryLevel);
    // 指令截止符
    _request += REQUEST_CMD_ENDER;

    // 发送请求
    return sendRequest();
}

/**
 * 检查是否有回复信息（获取回复信息前使用）
 * @return true - 有; false - 没有
 */
bool HTTPCom::hasResponse() {
    return _hasResponse;
}

/**
 * 获取回复信息编码
 * （获取前请检查是否有回复 hasResponse）
 * @return state 回复信息编码
 */
RESPONSE_MSG HTTPCom::getResponse() {
    if (_hasResponse) {
        return _state;
    } else {
        return RESPONSE_NULL;
    }
}

/**
 * 获取错误信息编码
 * （存在通讯及解码层错误 无法获得信息层信息）
 * @return state 回复信息编码
 */
RESPONSE_MSG HTTPCom::getError() {
    if (_state >= ERROR_STATUS) {
        _hasResponse = false;
        return _state;
    } else {
        return RESPONSE_NULL;
    }
}

/**
 * 获取回复内容：userID 学生证号
 * （获取前请检查是否有回复 hasResponse）
 * @return userID 学生证号
 */
String HTTPCom::getResponse_UserID() {
    if (_hasResponse) {
        return _userID;
    } else {
        return "";
    }
}

/**
 * 获取回复内容：balance 余额
 * （获取前请检查是否有回复 hasResponse）
 * @return balance 余额
 */
String HTTPCom::getResponse_Balance() {
    if (_hasResponse) {
        return _balance;
    } else {
        return "";
    }
}

/**
 * 获取回复内容：duration 用车时长
 * （获取前请检查是否有回复 hasResponse）
 * @return duration 用车时长
 */
String HTTPCom::getResponse_Duration() {
    if (_hasResponse) {
        return _duration;
    } else {
        return "";
    }
}

/**
 * 重置回复 清空回复信息（完成回复信息处理后务必调用）
 */
void HTTPCom::resetResponse() {
    _hasResponse = false;
    _userID = "";
    _balance = "";
    _duration = "";
    _state = RESPONSE_NULL;
    sim808.reset_HTTP_HTTPTERM();
    sim808.reset_HTTP_SAPBR_0_1();
}

// private:
/**
 * 使用通讯模块向服务器发送请求
 * @param  request 通讯模块请求指令（包括AT指令与请求内容）
 * @return         请求是否成功（仅包括通讯及解码层）
 *                 true - 成功; false - 失败
 */
bool HTTPCom::sendRequest() {
    resetResponse();
    bool requestSuccess = true;

    // 联网相关
    requestSuccess = sim808.HTTP_SAPBR_3_1();
    delay(INTERVAL_SHORT);
    if (!requestSuccess) {
        Error("SAPBR_3_1 FAIL!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_REQUEST_OVERTIME;
        return requestSuccess;
    }

    // 打开承载
    requestSuccess = sim808.HTTP_SAPBR_1_1();
    if (requestSuccess) {
        // 长时间停顿
        delay(INTERVAL_LONG);
    } else {
        delay(INTERVAL_SHORT);
        Error("SAPBR_1_1 FAIL!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_REQUEST_OVERTIME;
        return requestSuccess;
    }

    // 初始化HTTP功能
    requestSuccess = sim808.HTTP_HTTPINIT();
    delay(INTERVAL_SHORT);
    if (!requestSuccess) {
        Error("HTTPINIT FAIL!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_REQUEST_OVERTIME;
        return requestSuccess;
    }

    requestSuccess = sim808.HTTP_HTTPPARA_CID();
    delay(INTERVAL_SHORT);
    if (!requestSuccess) {
        Error("HTTPPARA_CID FAIL!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_REQUEST_OVERTIME;
        return requestSuccess;
    }

    // 连接到指定URL
    requestSuccess = sim808.HTTP_HTTPPARA_URL(_request);
    delay(INTERVAL_SHORT);
    if (!requestSuccess) {
        Error("HTTPPARA_URL FAIL!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_REQUEST_OVERTIME;
        return requestSuccess;
    }
    /*Log("Send request!");*/

    // 发送请求
    requestSuccess = sim808.HTTP_HTTPACTION();
    delay(INTERVAL_SHORT);
    if (!requestSuccess) {
        Error("HTTPACTION FAIL!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_REQUEST_OVERTIME;
        return requestSuccess;
    }

    // 读取服务器返回信息
    String msg = sim808.HTTP_HTTPREAD();
    delay(INTERVAL_SHORT);
    if (msg == "") {
        requestSuccess = false;
        Log("Empty response!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_INVALID_RESPONSE;
        return requestSuccess;
    }

    // 回复信息解码
    requestSuccess = decodeResponse(msg);
    if (requestSuccess) {
        Log("decodeResponse SUCCESS!");
        // 关闭HTTP功能
        requestSuccess = requestSuccess && sim808.HTTP_HTTPTERM();
        if (requestSuccess) delay(INTERVAL_SHORT);
        // 关闭承载
        requestSuccess = requestSuccess && sim808.HTTP_SAPBR_0_1();
        if (requestSuccess) delay(INTERVAL_SHORT);
    } else {
        requestSuccess = false;
        Log("decodeResponse FAIL!");
        sim808.HTTP_HTTPTERM();
        delay(INTERVAL_SHORT);
        sim808.HTTP_SAPBR_0_1();
        delay(INTERVAL_SHORT);
        _state = ERROR_DECODE;
    }

    return requestSuccess;
}

/**
 * 按照JSON格式解码回复信息
 * @param  response 回复信息
 * @return          true - 解码成功; false - 解码失败
 */
bool HTTPCom::decodeResponse(String response) {

    int start = response.indexOf("{");
    int end = response.lastIndexOf("}");
    response = response.substring(start, end + 1);
    const char *responseStr = response.c_str();
    aJsonObject *msg = aJson.parse(responseStr);
    aJsonObject *Jstate = aJson.getObjectItem(msg, KEY_STATE);
    int state = (Jstate->valueint);
    bool decodeSuccess = true;
    if ((state == RENT_SUCCESS) || (state == RETURN_SUCCESS)) {
        aJsonObject *JuserID = aJson.getObjectItem(msg, KEY_USERID);
        aJsonObject *Jbalance = aJson.getObjectItem(msg, KEY_BALANCE);
        aJsonObject *Jduration = aJson.getObjectItem(msg, KEY_DURATION);
        _state = (RESPONSE_MSG) state;
        _userID = (JuserID->valuestring);
        _balance = (Jbalance->valuestring);
        _duration = (Jduration->valuestring);
    } else {
        switch(state) {
            case RENT_FAIL_USER_OCCUPIED:
            case RENT_FAIL_USER_NONEXISTENT:
            case RENT_FAIL_NEGATIVE_BALANCE:
            case RENT_FAIL_BIKE_OCCUPIED:
            case RENT_FAIL_BIKE_UNAVAILABLE:
            case RETURN_FAIL_USER_NOT_MATCH:
            case RETURN_FAIL_ORDER_NONEXISTENT:
            case LOCATION_SUCCESS:
            case LOCATION_SUCCESS_NOT_AVAILABLE:
            case LOCATION_FAIL:
            case LOWBATTERY_SUCCESS:
            case LOWBATTERY_FAIL:
            case ERROR_OTHER:
                _state = (RESPONSE_MSG) state;
            break;
            default:
                decodeSuccess = false;
            break;
        }
    }
    aJson.deleteItem(msg);

    if (decodeSuccess) {
        _hasResponse = true;
    }

    return decodeSuccess;
}


//////////////////////////////////////
// ----------- Display ------------ //
//////////////////////////////////////
/**
 * 显示工具构造函数
 */
Display::Display() {
    if (u8g.getMode() == U8G_MODE_R3G3B2) {
        // 八位色模式下使用白色
        u8g.setColorIndex(255);
    } else if (u8g.getMode() == U8G_MODE_GRAY2BIT) {
        // 二位灰度模式下使用最大灰度（白色）
        u8g.setColorIndex(3);
    } else if (u8g.getMode() == U8G_MODE_BW) {
        // 一位黑白模式下使用白色
        u8g.setColorIndex(1);        
    } else if (u8g.getMode() == U8G_MODE_HICOLOR) {
        // 16位色模式下使用白色
        u8g.setHiColorByRGB(255,255,255);
    }

    _isDisplaying = false;
}

// public:
/**
 * 检查显示屏是否正在显示信息
 * @return true - 正在显示; false - 无显示
 */
bool Display::isDisplaying() {
    return _isDisplaying;
}

/**
 * 显示等待信息
 */
void Display::displayWait() {
    // 更改显示信息标志
    _isDisplaying = true;

    // 显示内容
    u8g.firstPage(); 
    do {
        // 设置字体
        u8g.setFont(u8g_font_unifont);
        // 选择信息
        u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, "Please Wait...");
    } while(u8g.nextPage());

    delay(2000);
}

/**
 * 清空显示信息
 */
void Display::displayClear() {
    // 显示内容
    u8g.firstPage(); 
    do {
        // 设置字体
        u8g.setFont(u8g_font_unifont);
        // 选择信息
        u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
    } while(u8g.nextPage());

    // 更改显示信息标志
    _isDisplaying = false;

    delay(DURATION_SHORT);
}

/**
 * 显示通讯信息层回复编码
 * @param msg 通讯信息层回复编码
 */
void Display::displayComMSG(const RESPONSE_MSG msg) {
    // 更改显示信息标志
    _isDisplaying = true;

    // 显示内容
    u8g.firstPage();
    do {
        // 设置字体
        u8g.setFont(u8g_font_unifont);

        // 选择信息
        switch(msg) {
            case RESPONSE_NULL:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case RENT_SUCCESS:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, "Rent Succeed!");
                }
            break;
            case RENT_FAIL_USER_OCCUPIED:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Rent Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "System Error");
                }
            break;
            case RENT_FAIL_USER_NONEXISTENT:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Rent Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "System Error");
                }
            break;
            case RENT_FAIL_NEGATIVE_BALANCE:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Rent Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "No Balance");
                }
            break;
            case RENT_FAIL_BIKE_OCCUPIED:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Rent Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "Bike Occupied");
                }
            break;
            case RENT_FAIL_BIKE_UNAVAILABLE:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Rent Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "Bike Broken");
                }
            break;
            case RETURN_SUCCESS:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, "Return Success!");
                }
            break;
            case RETURN_FAIL_USER_NOT_MATCH:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Return Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "System Error");
                }
            break;
            case RETURN_FAIL_ORDER_NONEXISTENT:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Return Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "System Error");
                }
            break;
            case LOCATION_SUCCESS:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case LOCATION_FAIL:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case LOWBATTERY_SUCCESS:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case LOWBATTERY_FAIL:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case ERROR_REQUEST_OVERTIME:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "No Network");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "Try Again!");
                }
            break;
            case ERROR_STATUS:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_MSG_ERROR);
                }
            break;
            case ERROR_INVALID_RESPONSE:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_MSG_ERROR);
                }
            break;
            case ERROR_DECODE:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_MSG_ERROR);
                }
            break;
            case ERROR_OTHER:
            default:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_RES);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_MSG_ERROR);
                }
            break;
        }
    } while(u8g.nextPage());

    if (isDisplaying()) {
        // 信息显示时间
        delay(DURATION);

        // 清除显示信息
        displayClear();
    }
}

/**
 * 显示通讯信息层回复详细信息（借还车成功时）
 * @param  msg      显示通讯信息层回复编码
 * @param  userID   用户学号
 * @param  balance  可用余额
 * @param  duration 用车时长
 */
void Display::displayDetails(const RESPONSE_MSG msg, const char* userID, const char* balance, const char* duration) {  
    // 更改显示信息标志
    _isDisplaying = true;

    // 显示内容
    u8g.firstPage();
    do {
        // 设置字体
        u8g.setFont(u8g_font_unifont);

        // 选择信息
        switch(msg) {
            case RENT_SUCCESS:
                u8g.drawStr(LEFT_EDGE, LINE_1_OF_3, "ID:");
                u8g.setPrintPos(LEFT_TAB_1, LINE_1_OF_3);
                u8g.print(userID);
                u8g.drawStr(LEFT_EDGE, LINE_2_OF_3, "Balance:");
                u8g.setPrintPos(LEFT_TAB_USERID, LINE_2_OF_3);
                u8g.print(balance);
            break;
            case RETURN_SUCCESS:
                u8g.drawStr(LEFT_EDGE, LINE_1_OF_3, "ID:");
                u8g.setPrintPos(LEFT_TAB_1, LINE_1_OF_3);
                u8g.print(userID);
                u8g.drawStr(LEFT_EDGE, LINE_2_OF_3, "Balance:");
                u8g.setPrintPos(LEFT_TAB_USERID, LINE_2_OF_3);
                u8g.print(balance);
                u8g.drawStr(LEFT_EDGE, LINE_3_OF_3, "Duration:");
                u8g.setPrintPos(LEFT_TAB_USERID, LINE_3_OF_3);
                u8g.print(duration);
            break;
            default:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, CHAR_COM_DETAILS);
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, CHAR_MSG_ERROR);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_MSG_ERROR);
                }
            break;
        }
    } while(u8g.nextPage());

    // 信息显示时间
    delay(DURATION_LONG);

    // 清除显示信息
    displayClear();

}




/**
 * 显示读卡消息
 * @param msg 读卡消息
 */
void Display::displayCardMSG(const CARD_MSG msg) {
    // 更改显示信息标志
    _isDisplaying = true;

    // 显示内容
    u8g.firstPage();
    do {
        // 设置字体
        u8g.setFont(u8g_font_unifont);

        // 选择信息
        switch(msg) {
            case NOTHING:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case NEW_CARD_DETECTED:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    displayWait();
                }
            break;
            case NEW_CARD_CONFIRMED:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case SAME_CARD_AGAIN:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case CARD_DETATCHED:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_EDGE, TOP_EDGE, "");
                    _isDisplaying = false;
                }
            break;
            case CARD_DETATCH_CONFIRMED:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, "Returning...");
                }
            break;
            case CARD_READ_STOP:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "No Card Found");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "Try Again!");
                }
            break;
            case ERROR_DIFFERENT_CARD:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, CHAR_MSG_ERROR);
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "Try Another One");
                }
            break;
            case ERROR_NOT_AVAILABLE_CARD:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, "Rent Fail!");
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "Bike Broken");
                }
            break;
            case ERROR_OTHER_CARD:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_COM_CARD);
                    u8g.setPrintPos(LEFT_TAB_2, LINE_1_OF_1);
                    u8g.print(msg);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, CHAR_MSG_ERROR);
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, "Try Another One");
                }
            break;
            default:
                if (isDebug) {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_2, CHAR_COM_CARD);
                    u8g.drawStr(LEFT_INDENT, LINE_2_OF_2, CHAR_MSG_ERROR);
                } else {
                    u8g.drawStr(LEFT_INDENT, LINE_1_OF_1, CHAR_MSG_ERROR);
                }
            break;
        }
    } while(u8g.nextPage());

    if (isDisplaying()) {
        // 信息显示时间
        delay(DURATION);

        // 清除显示信息
        displayClear();
    }
}

// private:


//////////////////////////////////////
// ------------- Lock ------------- //
//////////////////////////////////////
/**
 * 车锁构造函数
 */
Lock::Lock() {
    pinMode(LOCK_PIN, OUTPUT);
}

/**
 * 开锁
 */
void Lock::unlock() {
    digitalWrite(LOCK_PIN, HIGH);
    delay(DURATION);
    digitalWrite(LOCK_PIN, LOW);
}



//////////////////////////////////////
// --------- 全局工具实例 --------- //
//////////////////////////////////////
RentState        RENTSTATE;
Card             CARD;
LocationUpdate   LOCATION;
HTTPCom          HTTPCOM;
Display          DISPLAYS;
Lock             LOCK;

//////////////////////////////////////
// ----------- 全局函数 ----------- //
//////////////////////////////////////
/**
 * 获取系统时间（可能溢出）
 * @return 系统时间
 */
unsigned long sysTime() {
    return millis();
}

/**
 * 判断两个时间点之间是否满足间隔（考虑溢出）
 * @param  start    开始时间
 * @param  end      结束时间
 * @param  interval 间隔时间
 * @return          true - 在间隔内
 *                  false - 不在间隔内 / 时间溢出
 */
inline bool withinInterval(const unsigned long start, const unsigned long end, const unsigned long interval) {
    // 时间溢出
    if (end < start) {

        // 时间溢出后需要重置_lastUpdate
        setLastUpdate(sysTime());
        return false;
    }

    // 判断间隔
    if (end - start < interval) {
        return true;
    } else {
        return false;
    }
}


/**
 * 获取电池电量
 * @return 电池电量（小数点后保留两位）
 *         （0.00 ~ 1.00 读取错误时返回 -1.00）
 */
float readBatteryLevel() {
    // 读取电量
    
    // 读取失败
    return 1.00;
}


/**
 * 记录日志
 * @param tag 日志标签
 * @param log 日志信息
 */
void Log(const String tag, const String log) {
    if (isDebug) {
        Serial.println(tag + ":");
        Serial.println(log);
    }
}
/**
 * 记录日志
 * @param log 日志信息
 */
void Log(const String log) {
    Log("LOG", log);
}
/**
 * 记录日志
 * @param log 日志信息
 */
void Log(const RESPONSE_MSG log) {
    Log(TAG_COM_MSG, String((int) log));
}
/**
 * 记录日志
 * @param log 日志信息
 */
void Log(const CARD_MSG log) {
    Log(TAG_CARD_MSG, String((int) log));
}
/**
 * 记录日志
 * @param log 日志信息
 */
void Log(const RENT_STATE log) {
    Log(TAG_RENTSTATE, String((int) log));
}

/**
 * 记录错误
 * @param error 错误信息
 */
void Error(const String error) {
    if (isDebug) {
        Serial.println("");
        Serial.println("********************");
        Serial.println("ERROR:");
        Serial.println(error);
        Serial.println("");
    }
}


/**
 * 系统初始化（setup函数内调用）
 * @return true - 成功; false - 失败
 */
bool setupInit() {
    delay(1000);
    rfid.init();
    return sim808.init();
}

/**
 * 每循环终止时操作（loop函数截止时调用）
 * @return true - 成功; false - 失败
 */
bool loopTerm() {
    rfid.halt();
    // 如果有内容显示 则清空显示内容
    if (DISPLAYS.isDisplaying()) {
        DISPLAYS.displayClear();
    }
    delay(200);
    return true;
}