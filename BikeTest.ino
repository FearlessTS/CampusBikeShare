#include "BikeLib.h"

// 车辆编号
const int BIKEID = 1;

// 最大请求次数
const int MAX_REQUEST_COUNT = 5;

// 低电量阈值
const float LOW_BATTERY_THRESHOLD = 0.5;

// 全局变量
unsigned long serNum;
float batteryLevel = 1.00;


// 初始化
void setup() {
    Serial.begin(9600);
    SPI.begin();

    Log(TAG_SETUP, "Seting up...");
    while(!setupInit()) {
        Log(TAG_SETUP, "Setup Fail! Retrying...");  
    }

    Log(TAG_SETUP, "Setup Success!");
}


// 主程序
void loop() {

    // 检查电量
    batteryLevel = readBatteryLevel();
    Log(TAG_LOOP, "battery Level Read!");

    // 检查电量是否过低
    if (batteryLevel <= LOW_BATTERY_THRESHOLD && RENTSTATE.getState() != RENT) {
      Log("Low Battery Level");// 删掉

        // 车辆状态：不可用
        RENTSTATE.changeState(NOT_AVAILABLE);

        bool lowBatSuccess = false;
        int returnCount = 0;

        // 反复请求低电量直至成功
        // 待查：死循环
        while (!lowBatSuccess) {
            // 请求低电量
            lowBatSuccess = HTTPCOM.requestLowBattery(BIKEID, batteryLevel);

            if (lowBatSuccess) {
                if (HTTPCOM.hasResponse()) {
                    Log(TAG_LOCATION, "hasResponse");

                    switch (HTTPCOM.getResponse()) {
                        case LOWBATTERY_SUCCESS:
                        // 低电量信息发送成功
                        Log(TAG_COM_RES, "Low Battery Success!");
                        break;

                        case LOWBATTERY_FAIL:
                        // 低电量信息发送失败
                        Log(TAG_COM_RES, "Backend Error! Retrying...");

                            lowBatSuccess = false;
                        break;

                        default:
                        Error(TAG_COM_MSG + ": " + (int) HTTPCOM.getResponse());
                        break;
                    }
                } else {
                    Error(TAG_COM + ": hasResponse FALSE");
                }
            } else {
                // 获取错误信息
                switch (HTTPCOM.getError()) {
                    case ERROR_STATUS:              // 请求状态错误
                    case ERROR_REQUEST_OVERTIME:    // 请求超时
                    case ERROR_INVALID_RESPONSE:    // 接收信息无效
                    case ERROR_DECODE:              // 回复信息解码错误
                    Log(TAG_COM_RES, "Backend Not Reached! Retrying...");
                    Log(HTTPCOM.getResponse());
                    break;
                    default:
                    Error(TAG_COM_RES + ": " + (int) HTTPCOM.getResponse());
                    break;
                }
            }
        }
    }
    
    // 定位及反馈
   if (LOCATION.needUpdate(RENTSTATE.getState())) {
       Log(TAG_LOCATION, "Location Updating...");

       // 定位
       bool updateSuccess = LOCATION.doUpdate();
       Log(TAG_LOCATION, "Location Update Complete " + (int) updateSuccess);

       // 发送信息
       bool requestUpdateSuccess;
       if (updateSuccess) {
           requestUpdateSuccess = HTTPCOM.requestLocation(BIKEID, LOCATION.getLongitude(), LOCATION.getLatitude(), batteryLevel);
       } else {
           requestUpdateSuccess = HTTPCOM.requestLocationFail(BIKEID, batteryLevel);
       }

       // 检查回复
       if (requestUpdateSuccess) {
           if (HTTPCOM.hasResponse()) {
               Log(TAG_LOCATION, "hasResponse");

               switch (HTTPCOM.getResponse()) {
                   case LOCATION_SUCCESS:
                   // 定位信息发送成功
                   Log(TAG_COM_RES, "Location Success!");

                       if (!RENTSTATE.isAvailable()) {
                           // 车辆状态：未借用
                           RENTSTATE.changeState(NOT_RENT);
                       }
                   break;

                   case LOCATION_SUCCESS_NOT_AVAILABLE:
                   // 定位信息发送成功 车辆不可用
                   Log(TAG_COM_RES, "Backend Reached! Bike Not Available");
                        
                       // 骑行状态中车辆状态不应发生改变，否则无法还车
                       if (RENTSTATE.getState() != RENT) {
                           // 车辆状态：不可用
                           RENTSTATE.changeState(NOT_AVAILABLE);
                       }
                   break;

                   case LOCATION_FAIL:
                   // 定位信息发送失败
                   Log(TAG_COM_RES, "Backend Error! Bike Not Available");
                        
/*                     定位失败不需要改变车辆状态，否则一旦定位失败车辆就再也无法使用  
                       if (RENTSTATE.getState() != RENT) {
                           // 车辆状态：不可用
                           RENTSTATE.changeState(NOT_AVAILABLE);
                       }*/
                   break;

                   default:
                   Error(TAG_COM_MSG + ": " + (int) HTTPCOM.getResponse());
                   break;
               }
           } else {
               Error(TAG_COM + ": hasResponse FALSE");
           }
       } else {
           // 获取错误信息
           switch (HTTPCOM.getError()) {
               case ERROR_STATUS:              // 请求状态错误
               case ERROR_REQUEST_OVERTIME:    // 请求超时
               case ERROR_INVALID_RESPONSE:    // 接收信息无效
               case ERROR_DECODE:              // 回复信息解码错误
               Log(TAG_COM_RES, "Backend Not Reached! Bike Not Available");
               Log(HTTPCOM.getResponse());
                       // 骑行状态中车辆状态不应发生改变，否则无法还车  
                       if (RENTSTATE.getState() != RENT) {
                           // 车辆状态：不可用
                           RENTSTATE.changeState(NOT_AVAILABLE);
                       }
               break;
               default:
               Error(TAG_COM_RES + ": " + (int) HTTPCOM.getResponse());
               break;
           }
       }

   }

    // 读卡操作
    switch (CARD.searchCard(RENTSTATE.getState())) {
        case NEW_CARD_DETECTED:
        // 发现新卡片（亦可能识别错误）
        Log(TAG_CARD_MSG, "NEW_CARD_DETECTED");
            
            LOCATION.pauseUpdate();
            // 交互模块：等待
            DISPLAYS.displayWait();
        break;

        case NEW_CARD_CONFIRMED:
        // 确认新卡片
        Log(TAG_CARD_MSG, "NEW_CARD_CONFIRMED");

            LOCATION.resumeUpdate();

            // 借车
            if (RENTSTATE.getState() == NOT_RENT) {

                // 车辆状态：借车中
                RENTSTATE.changeState(RENT_UNDER_WAY);
                Log(TAG_LOOP, "Rent underway...");

                serNum = CARD.getSerNum();
                if (HTTPCOM.requestRent(BIKEID, serNum)) {
                    if (HTTPCOM.hasResponse()) {
                        // 交互模块：返回信息
                        DISPLAYS.displayComMSG(HTTPCOM.getResponse()); 
                        switch (HTTPCOM.getResponse()) {
                            case RENT_SUCCESS:
                            // 借车成功
                            Log(TAG_COM_RES, "Rent Success!");
                                
                                // 车辆状态：已借车
                                RENTSTATE.changeState(RENT);
                                
                                // 开锁
                                LOCK.unlock();
                                Log(TAG_LOCK, "Unlock Success!");

                                // 交互模块：用户信息
                                Log(TAG_COM_RES, HTTPCOM.getResponse_UserID());
                                Log(TAG_COM_RES, HTTPCOM.getResponse_Balance());
                                DISPLAYS.displayDetails(HTTPCOM.getResponse(), HTTPCOM.getResponse_UserID().c_str(), HTTPCOM.getResponse_Balance().c_str());
                            break;

                            case RENT_FAIL_USER_OCCUPIED:       // 借车失败：用户正在使用其它车辆
                            case RENT_FAIL_USER_NONEXISTENT:    // 借车失败：用户不存在
                            case RENT_FAIL_NEGATIVE_BALANCE:    // 借车失败：用户欠费
                            Log(TAG_COM_RES, "Rent Fail!");
                            Log(HTTPCOM.getResponse());

                                // 车辆状态：未借车
                                RENTSTATE.changeState(NOT_RENT);
                            break;

                            case RENT_FAIL_BIKE_OCCUPIED:
                            // 借车失败：车辆被其他用户占用
                            Error(TAG_COM_RES + ": Rent Fail! Bike Occupied");
                            // 待查
                            
                                // 车辆状态：不可用
                                RENTSTATE.changeState(NOT_AVAILABLE);
                            break;

                            case RENT_FAIL_BIKE_UNAVAILABLE:
                            // 借车失败：车辆故障
                            Log(TAG_COM_RES, "Rent Fail! Not Available");
                            Log(HTTPCOM.getResponse());
                                
                                // 车辆状态：不可用
                                RENTSTATE.changeState(NOT_AVAILABLE);
                            break;

                            default:
                            Error(TAG_COM_MSG + ": " + (int) HTTPCOM.getResponse());
                            break;
                        }
                    } else {
                        Error(TAG_COM + ": hasResponse FALSE");
                    }
                } else {
                    // 获取错误信息
                    switch (HTTPCOM.getError()) {
                        case ERROR_STATUS:              // 请求状态错误
                        case ERROR_REQUEST_OVERTIME:    // 请求超时
                        case ERROR_INVALID_RESPONSE:    // 接收信息无效
                        case ERROR_DECODE:              // 回复信息解码错误
                        Log(TAG_COM_RES, "Rent Fail! Backend Not Reached");
                        Log(HTTPCOM.getResponse());

                            // 车辆状态：未借车
                            RENTSTATE.changeState(NOT_RENT);
                        break;
                        default:
                        Error(TAG_COM_RES + ": " + (int) HTTPCOM.getResponse());
                        break;
                    }
                }
            } else {
                Error(TAG_RENTSTATE + ": " + (int) RENTSTATE.getState());
                // 待查
            }
        break;

        case CARD_DETATCHED:
        // 卡片已取走（亦可能接触不良）
        Log(TAG_CARD_MSG, "CARD_DETATCHED");
            
            LOCATION.pauseUpdate();
            // 交互模块：等待
            DISPLAYS.displayWait();
        break;

        case CARD_DETATCH_CONFIRMED:
        // 确认卡片已取走
        Log(TAG_CARD_MSG, "CARD_DETATCH_CONFIRMED");

            LOCATION.resumeUpdate();

            // 还车
            if (RENTSTATE.getState() == RENT) {

                // 车辆状态：还车中
                RENTSTATE.changeState(RETURN_UNDER_WAY);
                Log(TAG_LOOP, "Return underway...");

                bool returnSuccess = false;
                int returnCount = 0;

                // 反复请求还车直至成功
                while (!returnSuccess && returnCount++ <= MAX_REQUEST_COUNT) {
                    // 请求还车
                    returnSuccess = HTTPCOM.requestReturn(BIKEID, serNum);

                    if (returnSuccess) {
                        if (HTTPCOM.hasResponse()) {
                            // 交互模块：返回信息
                            DISPLAYS.displayComMSG(HTTPCOM.getResponse()); 
                            switch (HTTPCOM.getResponse()) {
                                case RETURN_SUCCESS:
                                // 还车成功
                                Log(TAG_COM_RES, "Return Success!");
                                    
                                    // 车辆状态：未借车
                                    RENTSTATE.changeState(NOT_RENT);

                                    // 交互模块：用户信息
                                    Log(TAG_COM_RES, HTTPCOM.getResponse_UserID());
                                    Log(TAG_COM_RES, HTTPCOM.getResponse_Balance());
                                    Log(TAG_COM_RES, HTTPCOM.getResponse_Duration());
                                    DISPLAYS.displayDetails(HTTPCOM.getResponse(), HTTPCOM.getResponse_UserID().c_str(), HTTPCOM.getResponse_Balance().c_str(), HTTPCOM.getResponse_Duration().c_str());
                                break;

                                case RETURN_FAIL_USER_NOT_MATCH:        // 还车失败：用户信息冲突
                                case RETURN_FAIL_ORDER_NONEXISTENT:     // 还车失败：车辆未借出
                                Log(TAG_COM_RES, "Return Fail! Info Mismatch");
                                Log(HTTPCOM.getResponse());
                                // 待查

                                    // 车辆状态：不可用
                                    RENTSTATE.changeState(NOT_AVAILABLE);
                                break;

                                default:
                                Error(TAG_COM_MSG + ": " + (int) HTTPCOM.getResponse());
                                break;
                            }
                        } else {
                            Error(TAG_COM + ": hasResponse FALSE");
                        }
                    } else {
                        // 获取错误信息
                        switch (HTTPCOM.getError()) {
                            case ERROR_STATUS:              // 请求状态错误
                            case ERROR_REQUEST_OVERTIME:    // 请求超时
                            case ERROR_INVALID_RESPONSE:    // 接收信息无效
                            case ERROR_DECODE:              // 回复信息解码错误
                            Log(TAG_COM_RES, "Backend Not Reached! Retrying...");
                            Log(HTTPCOM.getResponse());
                            break;
                            default:
                            Error(TAG_COM_RES + ": " + (int) HTTPCOM.getResponse());
                            break;
                        }
                    }
                }

                // 多次尝试失败 车辆不可用
                if (!returnSuccess && returnCount > MAX_REQUEST_COUNT) {
                    Error(TAG_COM + ": hasResponse FALSE");

                    // 车辆状态：不可用
                    RENTSTATE.changeState(NOT_AVAILABLE);
                }
            } else {
                Error(TAG_RENTSTATE + ": " + (int) RENTSTATE.getState());
                // 待查
            }
        break;

        case SAME_CARD_AGAIN:
        // 再次读到同一张卡
        Log(TAG_CARD_MSG, "SAME_CARD_AGAIN");

            LOCATION.resumeUpdate();
        break;

        case CARD_READ_STOP:
        // 卡片读取中断
        Log(TAG_CARD_MSG, "CARD_READ_STOP");

            LOCATION.resumeUpdate();

            // 交互模块：读取中断
            DISPLAYS.displayCardMSG(CARD_READ_STOP);
        break;

        case ERROR_NOT_AVAILABLE_CARD:
        // 车辆不可用
        Log(TAG_CARD_MSG, "ERROR_NOT_AVAILABLE_CARD");

            // 交互模块：车辆不可用
            DISPLAYS.displayCardMSG(ERROR_NOT_AVAILABLE_CARD);
        break;

        case NOTHING:
        // 无新信息
        Log(TAG_CARD_MSG, "NOTHING");
        break;

        case ERROR_DIFFERENT_CARD:      // 错误：探测到不同卡片
        case ERROR_OTHER_CARD:          // 错误：预留
        default:
        Error(TAG_CARD_MSG);
        break;
    }
    
    // 循环终止操作
    loopTerm();
}

