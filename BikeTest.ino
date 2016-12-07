#include "BikeLib.h"

const int BIKEID = 10000;
unsigned long serNum;

void setup() {

    Serial.begin(9600);
    SPI.begin();
    rfid.init();
    while(!sim808.init()){
    delay(1000);
    }
    Serial.println("Init Success!");  

}

void loop() {

  // 定位及检查电量
    /*if (UPDATE.needUpdate()) {
        // 定位及检查电量
        UPDATE.doUpdate();
        // 发送信息
    }*/
    delay(2000);
    switch (CARD.searchCard(RENTSTATE.getState())) {
        case NEW_CARD_DETECTED:              // 发现新卡片（亦可能识别错误）
            UPDATE.pauseUpdate();
            // 交互模块：wait
            /*DISPLAYS.displayWait();*/
            Log("NEW_CARD_DETECTED");
        break;
        case NEW_CARD_CONFIRMED:             // 确认新卡片
            UPDATE.resumeUpdate();
            Log("NEW_CARD_CONFIRMED");
            // 借车
            if (RENTSTATE.getState() == NOT_RENT) {
              RENTSTATE.changeState(RENT_UNDER_WAY);
              serNum = CARD.getSerNum();
              if (HTTPCOM.requestRent(BIKEID, serNum)) {
                if (HTTPCOM.hasResponse()) {
                  /*DISPLAYS.display(HTTPCOM.getResponse());*/ 
                  switch (HTTPCOM.getResponse()) {
                    case RENT_SUCCESS                   :  // 借车成功
                        RENTSTATE.changeState(RENT);
                        // 开锁
                        /*LOCK.unlock();*/
                        Log("Unlock Success!");                       
                        delay(2000);
                        LOCK.unlock();
                        // 交互模块：用户信息
                        Log(HTTPCOM.getResponse_UserID());p
                        Log(HTTPCOM.getResponse_Balance());
                        /*DISPLAYS.display(HTTPCOM.getResponse(), HTTPCOM.getResponse_UserID().c_str(), HTTPCOM.getResponse_Balance().c_str());*/
                    break;                                
                    case RENT_FAIL_USER_OCCUPIED        :  // 借车失败：用户正在使用其它车辆
                    case RENT_FAIL_USER_NONEXISTENT     :  // 借车失败：用户不存在
                    case RENT_FAIL_NEGATIVE_BALANCE     :  // 借车失败：用户欠费
                        Log(String((int) HTTPCOM.getResponse()));
                        RENTSTATE.changeState(NOT_RENT);
                    break;
                    case RENT_FAIL_BIKE_OCCUPIED        :  // 借车失败：车辆被其他用户占用
                                // 待查
                    case RENT_FAIL_BIKE_UNAVAILABLE     :  // 借车失败：车辆故障
                        Log(String((int) HTTPCOM.getResponse()));
                        RENTSTATE.changeState(NOT_AVAILABLE);
                    break;
                    default:
                        Log(String((int) HTTPCOM.getResponse()));
                    break;
                  }
                }
                else {
                  Log("FATAL ERROR: hasResponse FALSE");
                }
              }
              else {
                switch (HTTPCOM.getError()) {
                  case ERROR_STATUS                    :  // 请求状态错误
                  case ERROR_REQUEST_OVERTIME          :  // 请求超时            
                  case ERROR_INVALID_RESPONSE          :  // 接收信息无效
                  case ERROR_DECODE                    :  // 回复信息解码错误
                      Log(String((int) HTTPCOM.getResponse()));                     
                      RENTSTATE.changeState(NOT_RENT);
                  break; 
                }
              }
            }
            else {
              Log(String((int) RENTSTATE.getState()));
              // 待查 
            }          
        break;
        case CARD_DETATCHED:                 // 卡片已取走（亦可能接触不良）
            UPDATE.pauseUpdate();
            Log("CARD_DETATCHED");
        break;
        case CARD_DETATCH_CONFIRMED:         // 确认卡片已取走
            UPDATE.resumeUpdate();
            Log("CARD_DETATCH_CONFIRMED");
            // 还车            
        break;
        case SAME_CARD_AGAIN:                // 再次读到同一张卡
            UPDATE.resumeUpdate();
            Log("SAME_CARD_AGAIN");
        break;
        case CARD_READ_STOP:                 // 卡片读取中断
            UPDATE.resumeUpdate();
            Log("CARD_READ_STOP");
            // 交互模块：读取中断
            /*DISPLAYS.display(CARD_READ_STOP);*/
        break;
        case ERROR_NOT_AVAILABLE_CARD:
            /*DISPLAYS.display(ERROR_NOT_AVAILABLE_CARD);*/
        break;
        case NOTHING:                        // 无新信息
            Log("NOTHING");
        case ERROR_DIFFERENT_CARD:           // 错误：探测到不同卡片
        case ERROR_OTHER_CARD:               // 错误：预留
        default:
        break;
    }
    rfid.halt();
    
}
