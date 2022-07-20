#include <HardwareSerial.h>
#include "RTClib.h"
#include "hz3500_36.h"
#include "hz3500_16.h"
#include <ArduinoJson.h>
//memo缓存管理
#include "memo_historyManager.h"


//编译文件大小 1.1M
HardwareSerial mySerial(1);
RTC_Millis rtc;


//墨水屏缓存区
uint8_t *framebuffer;
memo_historyManager* objmemo_historyManager;


const int short_time_segment = 60;  //休眠唤醒最小分钟时间间隔
uint32_t TIME_TO_SLEEP = 3600; //下次唤醒间隔时间(3600秒）

bool net_connect_succ = false;


//http, https协议都支持
//用https协议需要证书算法
String http_sentence_host = "http://v1.hitokoto.cn";
String http_sentence_host_https= "https://v1.hitokoto.cn";
String http_sentence_url = "/";

int starttime, stoptime;

int cnt_check_net = 0;


int cnt_sync_sentence = 0;
bool  state_sync_time = false;
bool  state_sync_sentence = false;

char daysOfTheWeek[7][12] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
hw_timer_t *timer = NULL;

String buff_split[20];



/*
  通过esp32墨水屏+qs100(nbiot模块)
  每12小时从 https://v1.hitokoto.cn/ 处获得一句话并显示到墨水屏上显示

  电流：50ma

  编译大小: 2.4M
  开发板选择: TTGO-W-WATCH (仅用到分区定义，原因为汉字库较大)

  偶尔发现usb供电时，qs100不供电，但电池供电没事！ 可能是墨水屏3.3V电流供电输出的问题
  
  关于证书获取:
  1.要用firefox导出pem格式证书, 不能用chrome导出的cer证书
  2.不能用第一个证书,用第2或3个.
  
  证书获取方法参考:
  https://www.bilibili.com/read/cv12995807/
  https://blog.csdn.net/liyong_sbcel/article/details/122748277
  https://www.bilibili.com/video/BV14W4y1S7WA
*/

const char* rootCACertificate = \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDdTCCAl2gAwIBAgILBAAAAAABFUtaw5QwDQYJKoZIhvcNAQEFBQAwVzELMAkG\n" \
"A1UEBhMCQkUxGTAXBgNVBAoTEEdsb2JhbFNpZ24gbnYtc2ExEDAOBgNVBAsTB1Jv\n" \
"b3QgQ0ExGzAZBgNVBAMTEkdsb2JhbFNpZ24gUm9vdCBDQTAeFw05ODA5MDExMjAw\n" \
"MDBaFw0yODAxMjgxMjAwMDBaMFcxCzAJBgNVBAYTAkJFMRkwFwYDVQQKExBHbG9i\n" \
"YWxTaWduIG52LXNhMRAwDgYDVQQLEwdSb290IENBMRswGQYDVQQDExJHbG9iYWxT\n" \
"aWduIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDaDuaZ\n" \
"jc6j40+Kfvvxi4Mla+pIH/EqsLmVEQS98GPR4mdmzxzdzxtIK+6NiY6arymAZavp\n" \
"xy0Sy6scTHAHoT0KMM0VjU/43dSMUBUc71DuxC73/OlS8pF94G3VNTCOXkNz8kHp\n" \
"1Wrjsok6Vjk4bwY8iGlbKk3Fp1S4bInMm/k8yuX9ifUSPJJ4ltbcdG6TRGHRjcdG\n" \
"snUOhugZitVtbNV4FpWi6cgKOOvyJBNPc1STE4U6G7weNLWLBYy5d4ux2x8gkasJ\n" \
"U26Qzns3dLlwR5EiUWMWea6xrkEmCMgZK9FGqkjWZCrXgzT/LCrBbBlDSgeF59N8\n" \
"9iFo7+ryUp9/k5DPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNVHRMBAf8E\n" \
"BTADAQH/MB0GA1UdDgQWBBRge2YaRQ2XyolQL30EzTSo//z9SzANBgkqhkiG9w0B\n" \
"AQUFAAOCAQEA1nPnfE920I2/7LqivjTFKDK1fPxsnCwrvQmeU79rXqoRSLblCKOz\n" \
"yj1hTdNGCbM+w6DjY1Ub8rrvrTnhQ7k4o+YviiY776BQVvnGCv04zcQLcFGUl5gE\n" \
"38NflNUVyRRBnMRddWQVDf9VMOyGj/8N7yy5Y0b2qvzfvGn9LhJIZJrglfCm7ymP\n" \
"AbEVtQwdpf5pLGkkeB6zpxxxYu7KyJesF12KwvhHhm4qxFYxldBniYUr+WymXUad\n" \
"DKqC5JlR3XC321Y9YeRq4VzW9v493kHMB65jUr9TU/Qr6cf9tveCX4XSQRjbgbME\n" \
"HMUfpIBvFSDJ3gyICh3WZlXi/EjJKSZp4A==\n" \
"-----END CERTIFICATE-----\n" ;

void IRAM_ATTR resetModule() {
  ets_printf("resetModule reboot\n");
  delay(100);
  //esp_restart_noos(); 旧api
  esp_restart();
}


//GetCharwidth函数本来应放在 memo_historyManager类内部
//但因为引用了 msyh24海量字库变量，会造成编译失败,所以使用了一些技巧
//将函数当指针供类memo_historyManager 使用
//计算墨水屏显示的单个字的长度
int GetCharwidth(String ch)
{
  //修正，空格计算的的宽度为0, 强制36 字体不一样可能需要修改！
  if (ch == " ") return 28;

  char buf[50];
  int x1 = 0, y1 = 0, w = 0, h = 0;
  int tmp_cur_x = 0;
  int tmp_cur_y = 0;
  FontProperties properties;
  get_text_bounds((GFXfont *)&msyh36, (char *) ch.c_str(), &tmp_cur_x, &tmp_cur_y, &x1, &y1, &w, &h, &properties);
  //sprintf(buf, "x1=%d,y1=%d,w=%d,h=%d", x1, y1, w, h);
  //Serial.println("ch="+ ch + ","+ buf);

  //负数说明没找到这个字,会不显示出来
  if (w <= 0)
    w = 0;
  return (w);
}

//文字显示
void Show_hz(String rec_text, bool loadbutton)
{
  //最长限制160字节，40汉字
  //6个字串，最长约在 960字节，小于1024, json字串最大不超过1024
  rec_text = rec_text.substring(0, 160);
  Serial.println("begin Showhz:" + rec_text);


  epd_poweron();
  //uint32_t t1 = millis();
  //全局刷
  epd_clear();
  //局刷,一样闪屏
  //epd_clear_area(screen_area);
  //epd_full_screen()

  //此句不要缺少，否则显示会乱码
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  //特殊标志符
  if (rec_text != "[clear]")
  {
    //更正汉字符号显示的bug
    rec_text.replace("，", ",");
    rec_text.replace("。", ".");
    rec_text.replace("？", "?");


    //uint32_t t2 = millis();
    //printf("EPD clear took %dms.\n", t2 - t1);
    int cursor_x = 10;
    int cursor_y = 80;

    //多行文本换行显示算法。
    if (!loadbutton)
      objmemo_historyManager->multi_append_txt_list(rec_text);

    String now_string = "";
    int i;
    //write_string 能根据手工加的 "\n"换行，但不能自由控制行距，此处我自行控制了.
    for ( i = 0; i < objmemo_historyManager->memolist.size() ; i++)
    {
      now_string = objmemo_historyManager->memolist.get(i);
      //Serial.println("Show_hz line:" + String((now_index + i) % TXT_LIST_NUM) + " " + now_string);

      if (now_string.length() > 0)
      {
        //加">"字符，规避epd47的bug,当所有字库不在字库时，esp32会异常重启
        // “Guru Meditation Error: Core 1 panic'ed (LoadProhibited). Exception was unhandled."
        now_string = ">" + now_string;
        //墨水屏writeln不支持自动换行
        //delay(200);
        //一定要用framebuffer参数，否则当最后一行数据过长时，会导致代码在此句阻塞，无法休眠，原因不明！

        writeln((GFXfont *)&msyh36, (char *)now_string.c_str(), &cursor_x, &cursor_y, framebuffer);
        //writeln调用后，cursor_x会改变，需要重新赋值
        cursor_x = 10;
        cursor_y = cursor_y + 85;
      }
    }
  }
  //前面不要用writeln，有一定机率阻塞，无法休眠
  epd_draw_grayscale_image(epd_full_screen(), framebuffer);

  //delay(500);
  epd_poweroff();

  //清空显示
  objmemo_historyManager->memolist.clear();
  Serial.println("end Showhz:" + rec_text );
}

//readStringUntil 注意：如果一直等不到结束符会阻塞
String send_at2(String p_char, String break_str, String break_str2, int delay_sec) {

  String ret_str = "";
  String tmp_str = "";
  if (p_char.length() > 0)
  {
    Serial.println(String("cmd=") + p_char);
    mySerial.println(p_char);
  }

  //发完命令立即退出
  //if (break_str=="") return "";

  mySerial.setTimeout(1000);

  uint32_t start_time = millis() / 1000;
  while (millis() / 1000 - start_time < delay_sec)
  {
    if (mySerial.available() > 0)
    {

      tmp_str = mySerial.readStringUntil('\n');
      //tmp_str.replace("\r", "");
      tmp_str = tmp_str.substring(0, tmp_str.length() - 1); //去掉串尾的 \r
      //tmp_str.trim()  ;
      Serial.println(">" + tmp_str);
      //如果字符中有特殊字符，用 ret_str=ret_str+tmp_str会出现古怪问题，最好用concat函数
      ret_str.concat(tmp_str);
      if (break_str.length() > 0 && tmp_str.indexOf(break_str) > -1 )
        break;
      if (break_str2.length() > 0 &&  tmp_str.indexOf(break_str2) > -1 )
        break;
    }
    delay(10);
  }
  return ret_str;
}


//readStringUntil 注意：如果一直等不到结束符会阻塞
String send_at(String p_char, String break_str, int delay_sec) {

  String ret_str = "";
  String tmp_str = "";
  if (p_char.length() > 0)
  {
    Serial.println(String("cmd=") + p_char);
    mySerial.println(p_char);
  }

  //发完命令立即退出
  //if (break_str=="") return "";

  mySerial.setTimeout(1000);

  uint32_t start_time = millis() / 1000;
  while (millis() / 1000 - start_time < delay_sec)
  {
    if (mySerial.available() > 0)
    {
      tmp_str = mySerial.readStringUntil('\n');
      //tmp_str.replace("\r", "");
      tmp_str = tmp_str.substring(0, tmp_str.length() - 1); //去掉串尾的 \r
      //tmp_str.trim()  ;
      Serial.println(">" + tmp_str);
      //如果字符中有特殊字符，用 ret_str=ret_str+tmp_str会出现古怪问题，最好用concat函数
      ret_str.concat(tmp_str);
      if (break_str.length() > 0 && tmp_str.indexOf(break_str) > -1)
        break;
    }
    delay(10);
  }
  return ret_str;
}

//仅检查是否关机状态
bool check_waker_7020()
{
  String ret = "";
  delay(1000);
  int cnt = 0;
  bool check_ok = false;
  //通过AT命令检查是否关机，共检查3次
  while (true)
  {
    cnt++;
    ret = send_at("AT", "", 2);
    Serial.println("ret=" + ret);
    if (ret.length() > 0)
    {
      check_ok = true;
      break;
    }
    if (cnt >= 5) break;
    delay(1000);
  }
  return check_ok;
}



bool connect_nb()
{
  bool  ret_bool = false;

  int error_cnt = 0;
  String ret;

  cnt_check_net = cnt_check_net + 1;
  Serial.println(">>> 检查网络连接状态 ...");
  error_cnt = 0;
  //网络注册状态
  while (true)
  {
    ret = send_at("AT+CEREG?", "OK", 3);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+CEREG:0,1") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 5)
      return false;
  }
  Serial.println(">>> 网络注册状态 ok ...");
  delay(1000);


  error_cnt = 0;
  //查询附着状态
  while (true)
  {
    ret = send_at("AT+CGATT?", "OK", 3);
    Serial.println("ret=" + ret);

    if (ret.indexOf("+CGATT:1") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 5)
      return false;
  }
  Serial.println(">>> 附着状态 ok ...");

  delay(1000);

  error_cnt = 0;
  //同步时间
  while (true)
  {
    ret = send_at("AT+CCLK?", "OK", 3);
    Serial.println("ret=" + ret);

    if (ret.indexOf("+CCLK:") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 5)
      return false;
  }

  Serial.println(">>> 获取时间成功 ...");
  //+CCLK:22/07/15,13:36:45+32OK
  state_sync_time = false;
  if (ret.startsWith("+CCLK:") && ret.length() > 10 )
  {
    state_sync_time = true;
    ret.replace("OK", "");
    Serial.println("获取时间:" + ret);
    sync_esp32_rtc(ret);
  }

  cnt_check_net = 0;
  return true;
}

//把 +CCLK:22/07/14,14:50:41+32 转换成esp32内的时间
void sync_esp32_rtc(String now_time)
{
  now_time.replace("+CCLK:", "##");
  DateTime now = DateTime(now_time.substring(2, 4).toInt() + 2000, now_time.substring(5, 7).toInt(), now_time.substring(8, 10).toInt(),
                          now_time.substring(11, 13).toInt(), now_time.substring(14, 16).toInt(), now_time.substring(17, 19).toInt());
  // calculate a date which is 7 days and 30 seconds into the future
  //增加8小时
  DateTime future (now.unixtime() + 28800L);
  rtc.adjust(future);
  Serial.println("now_time:" + Get_softrtc_time(6));
}

void splitString(String message, String dot, String outmsg[], int len)
{
  int commaPosition, outindex = 0;
  for (int loop1 = 0; loop1 < len; loop1++)
    outmsg[loop1] = "";
  do {
    commaPosition = message.indexOf(dot);
    if (commaPosition != -1)
    {
      outmsg[outindex] = message.substring(0, commaPosition);
      outindex = outindex + 1;
      message = message.substring(commaPosition + 1, message.length());
    }
    if (outindex >= len) break;
  }
  while (commaPosition >= 0);

  if (outindex < len)
    outmsg[outindex] = message;
}

int parse_CHTTPNMIC(String in_str)
{
  //+HTTPNMIC:0,1,5231,815
  String out_str = "";
  int cnt = 0;
  splitString(in_str, ",", buff_split, 4);
  cnt = buff_split[3].toInt() ;
  return cnt;
}


String send_at_httpget(String p_char, int delay_sec) {
  String ret_str = "";
  String tmp_str = "";
  if (p_char.length() > 0)
  {
    Serial.println(String("cmd=") + p_char);
    mySerial.println(p_char);
  }
  ret_str = "";
  mySerial.setTimeout(5000);
  uint32_t start_time = millis() / 1000;
  int content_len;
  while (millis() / 1000 - start_time < delay_sec)
  {

    if (mySerial.available() > 0)
    {
      tmp_str = mySerial.readStringUntil('\n');
      tmp_str = tmp_str.substring(0, tmp_str.length() - 1); //去掉串尾的 \r

      Serial.println(tmp_str);
      //数据接收完成，正常退出
      if (tmp_str.startsWith("+HTTPDICONN:0,-2"))
      {
        Serial.println("数据接收完毕,break");
        break;
      }

      if (tmp_str.startsWith("+REQUESTSUCCESS") )
      {
        Serial.println("开始数据接收...");
        //break;
      }

      //没有获得数据
      if (tmp_str.startsWith("+BADREQUEST"))
      {
        Serial.println("未获得数据,break");
        break;
      }

      if (tmp_str.startsWith("+HTTPNMIC:0,"))
      {
        content_len = parse_CHTTPNMIC(tmp_str);
        tmp_str = mySerial.readStringUntil('\n');
        tmp_str = tmp_str.substring(0, tmp_str.length() - 1); //去掉最后的/r
        Serial.println("content_len=" + String(content_len));
        Serial.println("str_len=" + String(tmp_str.length()));
        ret_str = ret_str + tmp_str;
      }

    }
    delay(10);
  }

  return ret_str;
}


void free_http()
{
  String ret;
  ret = send_at2("AT+HTTPCLOSE=0", "OK", "ERROR" , 5);
  Serial.println("ret=" + ret);
  Serial.println(">>> 断开http连接  ok ...");
}

void ShowStr( String mystring, int x0, int y0, int font_size, uint8_t * framebuffer )
{
  y0 = y0 + 60;

  if (font_size >= 100)
    write_string( (GFXfont *)&msyh36, (char *) mystring.c_str(), &x0, &y0, framebuffer);
  else
    write_string( (GFXfont *)&msyh16, (char *) mystring.c_str(), &x0, &y0, framebuffer);
}



bool get_sentence()
{
  bool succ_flag = false;

  String ret;
  cnt_sync_sentence = cnt_sync_sentence + 1;
  free_http();
  delay(1000);

  //注意： qs-100 最后不需要 "/"
  ret = send_at("AT+HTTPCREATE=\"" + http_sentence_host + "\"",  "OK", 3);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("+HTTPCREATE:0") > -1))
    return false;

  Serial.println(">>> 创建HTTP Host ok ...");
  delay(2000);

  Serial.println(">>> 获取数据 ...");
  //最长20秒内获得数据
  String sentence_result = send_at_httpget("AT+HTTPSEND=0,0,\"" + http_sentence_url + "\"", 30);

  //{"id":570,"uuid":"31608d22-da0d-43e2-ac98-7d18589ccfe2","hitokoto":"世间本无公平可言，除非公平掌握在自己手中。","type":"d","from":"冰与火之歌","from_who":null,"creator":"yeye","creator_uid":29,"reviewer":0,"commit_from":"web","created_at":"1473562680","length":21}
  if (sentence_result.length() > 50 &&  sentence_result.indexOf("\"hitokoto\":") > -1)
  {
    Serial.println("sentence_result:" + sentence_result);
    cal_waker_seconds_by12hour();
    stoptime = millis() / 1000;
    Serial.println("deserializeJson");
    //字符长度约1020B上下，为保险，3KB，
    //必须用 DynamicJsonDocument
    DynamicJsonDocument  root(3 * 1024);
    deserializeJson(root, sentence_result);

    String show_txt = "waker after " + String(TIME_TO_SLEEP)   + "秒 " +  String(stoptime - starttime) + "\n" + Get_softrtc_time(6) + "\n" +
                      root["hitokoto"].as<String>() + "\n" +
                      "from:" + root["from"].as<String>();

    Show_hz(show_txt, false);

    cnt_sync_sentence = 0;
    succ_flag = true;
  }
  else
    Serial.println("无效的天气数据");

  //free_http();

  return succ_flag;
}



bool get_sentence_https()
{
  bool succ_flag = false;

  String ret;
  cnt_sync_sentence = cnt_sync_sentence + 1;
  free_http();
  delay(1000);

  //注意： qs-100 最后不需要 "/"
  ret = send_at("AT+HTTPCREATE=\"" + http_sentence_host_https + "\"",  "OK", 3);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("+HTTPCREATE:0") > -1))
    return false;

  Serial.println(">>> 创建HTTP Host ok ...");
  delay(1000);

  /*配置服务器 cert 证书*/
  ret = send_at("AT+HTTPCFG=0,1,\"" + String(rootCACertificate) + "\"",  "OK", 10);

  delay(1000);
  Serial.println(">>> 获取数据 ...");
  //最长20秒内获得数据
  String sentence_result = send_at_httpget("AT+HTTPSEND=0,0,\"" + http_sentence_url + "\"", 30);

  //{"id":570,"uuid":"31608d22-da0d-43e2-ac98-7d18589ccfe2","hitokoto":"世间本无公平可言，除非公平掌握在自己手中。","type":"d","from":"冰与火之歌","from_who":null,"creator":"yeye","creator_uid":29,"reviewer":0,"commit_from":"web","created_at":"1473562680","length":21}
  if (sentence_result.length() > 50 &&  sentence_result.indexOf("\"hitokoto\":") > -1)
  {
    Serial.println("sentence_result:" + sentence_result);
    cal_waker_seconds_by12hour();
    stoptime = millis() / 1000;
    Serial.println("deserializeJson");
    //字符长度约1020B上下，为保险，3KB，
    //必须用 DynamicJsonDocument
    DynamicJsonDocument  root(3 * 1024);
    deserializeJson(root, sentence_result);

    String show_txt = "waker after " + String(TIME_TO_SLEEP)   + "秒 " +  String(stoptime - starttime) + "\n" + Get_softrtc_time(6) + "\n" +
                      root["hitokoto"].as<String>() + "\n" +
                      "from:" + root["from"].as<String>();

    Show_hz(show_txt, false);

    cnt_sync_sentence = 0;
    succ_flag = true;
  }
  else
    Serial.println("无效的天气数据");

  //free_http();

  return succ_flag;
}

String Get_softrtc_time(int flag)
{
  if (state_sync_time == false)
    return "";

  DateTime now = rtc.now();
  char buf[50];
  buf[0] = '\0';
  if (flag == 0)
  {
    sprintf(buf, "%02d,%02d,%02d,%02d,%02d,%02d", now.year(), now.month() , now.day(), now.hour(), now.minute(), now.second());
  }
  if (flag == 1)
  {
    sprintf(buf, "%02d:%02d", now.hour(), now.minute());
  }
  else if (flag == 2)
  {
    sprintf(buf, "%02d,%02d,%02d,%02d,%02d", now.year(), now.month() , now.day(), now.hour(), now.minute());

  }
  else if (flag == 3)
  {
    sprintf(buf, "%02d月%02d日%s",  now.month() , now.day(), daysOfTheWeek[now.dayOfTheWeek()]);
  }
  else if (flag == 4)
  {
    sprintf(buf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  }
  else if (flag == 5)
  {
    sprintf(buf, "%02d-%02d-%02d %02d:%02d", now.year(), now.month() , now.day(), now.hour(), now.minute());
  }
  else if (flag == 6)
  {
    sprintf(buf, "%02d-%02d-%02d %02d:%02d:%02d", now.year(), now.month() , now.day(), now.hour(), now.minute(), now.second());
  }
  return String(buf);
}


/*
  //计算总休眠秒数 到TIME_TO_SLEEP
  //每天一次
  void cal_waker_seconds_byday()
  {
  //如果前次有成功同步时间
  if (state_sync_time)
  {
    DateTime now = rtc.now();
    String now_time = Get_softrtc_time(1);
    Serial.println("now_time:" + now_time);

    //计算本次需要休眠秒数

    Serial.println("计算休眠时间 hour=" + String(now.hour() ) + ",minute=" + String(now.minute() ) + ",second=" + String( now.second()));

    //先计算到 00:00的秒数
    TIME_TO_SLEEP =   ((24 - now.hour()) * 60 -  now.minute() ) * 60;
    TIME_TO_SLEEP = TIME_TO_SLEEP - now.second();

    if (TIME_TO_SLEEP < 30)
      TIME_TO_SLEEP = 24 * 60 * 60 + 50;

    //再加上到 07:00的秒数
    TIME_TO_SLEEP = TIME_TO_SLEEP + 7 * 3600;

    //24小时唤醒平均会少12几分钟，所以用20分钟当误差
    //TIME_TO_SLEEP = TIME_TO_SLEEP + 1200 ;
    Serial.println("go sleep,wake after " + String(TIME_TO_SLEEP)  + "秒 AT:" + Get_softrtc_time(0));

  }
  //时间未同步，时间无效，2小时后再试
  else
  {
    TIME_TO_SLEEP = 3600 * 2 + 10;
    Serial.println("时间未同步， go sleep,wake after " + String(TIME_TO_SLEEP)  + "秒 ");
  }
  }

  //每小时一次
  void cal_waker_seconds_byhour()
  {
  //如果前次有成功同步时间
  if (state_sync_time)
  {
    DateTime now = rtc.now();
    String now_time = Get_softrtc_time(1);
    Serial.println("now_time:" + now_time);

    //计算本次需要休眠秒数

    Serial.println("计算休眠时间 hour=" + String(now.hour() ) + ",minute=" + String(now.minute() ) + ",second=" + String( now.second()));

    //如果short_time_segment是1，表示每1分钟整唤醒一次,定义的闹钟时间可随意
    //                       5，表示每5分钟整唤醒一次,这时定义的闹钟时间要是5的倍数，否则不会定时响铃
    TIME_TO_SLEEP = (short_time_segment - now.minute() % short_time_segment) * 60;
    TIME_TO_SLEEP = TIME_TO_SLEEP - now.second();

    //休眠时间过短，低于30秒直接视同0
    if (TIME_TO_SLEEP < 30)
      TIME_TO_SLEEP = 60 * short_time_segment + 50;

    //考虑到时钟误差，增加几秒，确保唤醒时间超出约定时间
    //TIME_TO_SLEEP = TIME_TO_SLEEP +10;

    //节能: 10点以后，+8小时， 直接休眠到7点再唤醒
    if (now.hour() >= 22)
    {
      Serial.println("已过22点，追加8小时到次日7点再唤醒");
      //实测时间误差较大，追加8小时，另加5分钟
      TIME_TO_SLEEP = TIME_TO_SLEEP + 8 * 3600 + 300;
    }
    Serial.println("go sleep,wake after " + String(TIME_TO_SLEEP)  + "秒 AT:" + Get_softrtc_time(0));
  }
  //时间未同步，时间无效，1小时后再试
  else
  {
    TIME_TO_SLEEP = 3600 + 10;
    Serial.println("时间未同步， go sleep,wake after " + String(TIME_TO_SLEEP)  + "秒 ");
  }
  }
*/
//每天更新2次
void cal_waker_seconds_by12hour()
{
  //如果前次有成功同步时间
  if (state_sync_time)
  {
    DateTime now = rtc.now();
    String now_time = Get_softrtc_time(1);
    Serial.println("now_time:" + now_time);

    //计算本次需要休眠秒数

    Serial.println("计算休眠时间 hour=" + String(now.hour() ) + ",minute=" + String(now.minute() ) + ",second=" + String( now.second()));

    //如果short_time_segment是1，表示每1分钟整唤醒一次,定义的闹钟时间可随意
    //                       5，表示每5分钟整唤醒一次,这时定义的闹钟时间要是5的倍数，否则不会定时响铃
    TIME_TO_SLEEP = (short_time_segment - now.minute() % short_time_segment) * 60;
    TIME_TO_SLEEP = TIME_TO_SLEEP - now.second();

    //休眠时间过短，低于30秒直接视同0
    if (TIME_TO_SLEEP < 30)
      TIME_TO_SLEEP = 60 * short_time_segment + 50;

    //考虑到时钟误差，增加几秒，确保唤醒时间超出约定时间
    //TIME_TO_SLEEP = TIME_TO_SLEEP +10;

    //追加11个小时
    TIME_TO_SLEEP = TIME_TO_SLEEP + 39600;


    //24小时唤醒平均会少15-20几分钟，所以用25分钟当误差, 确保在7:00之后
    //修正12小时累计误差 加10分钟
    TIME_TO_SLEEP = TIME_TO_SLEEP + 600;

    Serial.println("go sleep,wake after " + String(TIME_TO_SLEEP)  + "秒 AT:" + Get_softrtc_time(0));
  }
  //时间未同步，时间无效，1小时后再试
  else
  {
    TIME_TO_SLEEP = 3600 + 10;
    Serial.println("时间未同步， go sleep,wake after " + String(TIME_TO_SLEEP)  + "秒 ");
  }
}


void goto_sleep()
{
  Serial.println("goto sleep");

  //计算休眠秒数
  cal_waker_seconds_by12hour();

  stoptime = millis() / 1000;

  //平均35-80秒不等，nbiot同步时间需要时间较长
  Serial.println("wake 用时:" + String(stoptime - starttime) + "秒");

  Serial.flush();

  //It will turn off the power of the entire
  // POWER_EN control and also turn off the blue LED light
  epd_poweroff_all();

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

  // ESP进入deepSleep状态
  //最大时间间隔为 4,294,967,295 µs 约合71分钟
  //休眠后，GPIP的高，低状态将失效，无法用GPIO控制开关
  esp_deep_sleep_start();
}


//毫秒内不接收串口数据，并清缓存
void clear_uart(int ms_time)
{
  //唤醒完成后就可以正常接收串口数据了
  uint32_t starttime = 0;
  char ch;
  //5秒内有输入则输出
  starttime = millis();
  //临时接收缓存，防止无限等待
  while (true)
  {
    if  (millis()  - starttime > ms_time)
      break;
    while (mySerial.available())
    {
      ch = (char) mySerial.read();
      Serial.print(ch);
    }
    yield();
    delay(20);
  }
}

void setup() {

  starttime = millis() / 1000;

  Serial.begin(115200);
  //                               RX  TX
  mySerial.begin(9600, SERIAL_8N1, 12, 13);

  //00/01/01,00:00:01+32
  DateTime now = DateTime(2000, 01, 01, 00, 00, 00);
  rtc.adjust(now);

  //如果启动后不调用此函数，有可能电流一直保持在在60ma，起不到节能效果
  //此步骤不适合在唤醒后没有显示需求时优化掉
  epd_init();
  // framebuffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
  framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
  if (!framebuffer) {
    Serial.println("alloc memory failed !!!");
    delay(1000);
    while (1);
  }
  memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

  objmemo_historyManager = new memo_historyManager();
  objmemo_historyManager->GetCharwidth = GetCharwidth;


  //建议时间 5秒
  delay(5000);  //等待一会，确保网络连接上
  Serial.println(">>> 开启 nb-iot ...");
  clear_uart(5000);

  //为防意外，n秒后强制复位重启，一般用不到。。。
  //n秒如果任务处理不完，看门狗会让esp32自动重启,防止程序跑死...
  uint32_t wdtTimeout = 3 * 60 * 1000; //设置3分钟 watchdog
  timer = timerBegin(0, 80, true);                  //timer 0, div 80
  timerAttachInterrupt(timer, &resetModule, true);  //attach callback
  timerAlarmWrite(timer, wdtTimeout * 1000 , false); //set time in us
  timerAlarmEnable(timer);                          //enable interrupt


  //at预处理
  check_waker_7020();

  net_connect_succ = false;
  Serial.println("setup");
}


void loop() {

  //3次失败检测网络连接
  if (cnt_check_net >= 3)
    goto_sleep();

  //3次失败获取句子
  if (cnt_sync_sentence >= 3)
    goto_sleep();

  //句子已同步
  if (state_sync_sentence)
    goto_sleep();

  //上电已超过 2分钟
  //实测70秒足够
  stoptime = millis() / 1000;
  if (stoptime - starttime >= 2 * 60)
    goto_sleep();

  //如果setup时网络连接失败，重新再试
  if (net_connect_succ == false)
  {
    delay(5000);
    Serial.println(">>> 检查网络连接 ...");
    net_connect_succ = connect_nb();
    return;
  }


  if (state_sync_sentence == false)
  {
    delay(5000);
    state_sync_sentence = get_sentence();
    //state_sync_sentence = get_sentence_https(); //https方式
    return;
  }

  delay(1000);
}
