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

String http_time_host = "https://api.uukit.com/";
String http_time_url = "/time";

String http_sentence_host = "https://v1.hitokoto.cn/";
String http_sentence_url = "/";

int starttime, stoptime;

int cnt_check_net = 0;

int cnt_sync_time = 0;
int cnt_sync_sentence = 0;
bool  state_sync_time = false;
bool  state_sync_sentence = false;

char daysOfTheWeek[7][12] = {"星期日", "星期一", "星期二", "星期三", "星期四", "星期五", "星期六"};

#define uS_TO_S_FACTOR 1000000ULL  /* Conversion factor for micro seconds to seconds */
hw_timer_t *timer = NULL;

String buff_split[20];

/*
  通过esp32墨水屏+sim7020c
  每12小时从 https://v1.hitokoto.cn/ 处获得一句话并显示到墨水屏上显示
 
  sim7020c vin接5v, 3.5V偶尔可以用，但不稳定
  如果3.3V， 接VBAT引脚

  电流：50ma

  编译大小: 2.4M
  开发板选择: TTGO-W-WATCH (仅用到分区定义，原因为汉字库较大)
*/

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

//readStringUntil 有阻塞，不好用
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
      //此句容易被阻塞
      tmp_str = mySerial.readStringUntil('\n');
      tmp_str.replace("\r", "");
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


//readStringUntil 有阻塞，不好用
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
      //此句容易被阻塞
      tmp_str = mySerial.readStringUntil('\n');
      //tmp_str.replace("\r","");
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

  error_cnt = 0;
  //网络信号质量查询，返回信号值
  while (true)
  {
    ret = send_at("AT+CPIN?", "+CPIN: READY", 1);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+CPIN: READY") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> SIM 卡状态 ok ...");


  error_cnt = 0;
  //查询网络注册状态
  while (true)
  {
    ret = send_at("AT+CGREG?", "+CGREG: 0,1", 1);
    Serial.println("ret=" + ret);

    if (ret.indexOf("+CGREG: 0,1") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> PS 业务附着 ok ...");
  error_cnt = 0;
  //查询PDP状态
  while (true)
  {
    ret = send_at("AT+CGACT?", "+CGACT: 1,1", 1);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+CGACT: 1,1") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 5)
      return false;
  }
  Serial.println(">>> PDN 激活 OK ...");
  error_cnt = 0;
  //查询网络信息
  while (true)
  {
    ret = send_at("AT+COPS?", "+COPS: 0,2,\"46000\",9", 1);
    Serial.println("ret=" + ret);
    if (ret.indexOf("+COPS: 0,2,\"46000\",9") > -1)
      break;
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }
  Serial.println(">>> 网络信息，运营商及网络制式 OK...");
  error_cnt = 0;
  while (true)
  {
    //查询网络状态
    //ret = send_at("AT+CGCONTRDP", "cmnbiot", 1);
    ret = send_at2("AT+CGCONTRDP", "cmnbiot", "CMIOT", 1);
    Serial.println("ret=" + ret);

    //分配到IP
    if (ret.indexOf("cmnbiot") > -1 || ret.indexOf("CMIOT") > -1)
    {
      ret_bool = true;
      break;
    }
    delay(2000);

    error_cnt++;
    if (error_cnt >= 10)
      return false;
  }

  ret = send_at("AT+CDNSGIP=www.baidu.com", "+CDNSGIP: 1,", 10);
  Serial.println("ret=" + ret);

  Serial.println(">>> 获取IP OK...");

  cnt_check_net = 0;
  return ret_bool;
}

String Strhex_char(char ch) {

  return String(ch, HEX);;
}

String Strhex_convert(String data_str) {
  String tmpstr = "";

  for (int loop1 = 0; loop1 < data_str.length() ; loop1++)
  {
    tmpstr = tmpstr + Strhex_char(data_str[loop1]);
  }
  return tmpstr;
}

char hexStr_char(String data_str) {
  //int tmpint = data_str.toInt();
  //Serial.println("tmpint="+String(tmpint));
  //String(data[i], HEX);

  char ch;
  sscanf(data_str.c_str(), "%x", &ch);
  return ch;
}

String hexStr_convert(String data_str) {
  char ch;
  String  tmpstr = "";
  for (int loop1 = 0; loop1 < data_str.length() / 2; loop1++)
  {
    ch = hexStr_char(data_str.substring(loop1 * 2, loop1 * 2 + 2));
    //Serial.print("ch="+String(ch));
    tmpstr = tmpstr + ch;
  }
  return tmpstr;
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

String parse_CHTTPNMIC(String in_str)
{
  //+CHTTPNMIC: 0,0,462,462,7b22726573756c7473223a5b7b226c6f636174696f6e223a7b226964223a2257583446425858464b453446222c226e616d65223a22e58c97e4baac222c22636f756e747279223a22434e222c2270617468223a22e58c97e4baac2ce58c97e4baac2ce4b8ade59bbd222c2274696d657a6f6e65223a22417369612f5368616e67686169222c2274696d657a6f6e655f6f6666736574223a222b30383a3030227d2c226461696c79223a5b7b2264617465223a22323032312d30332d3133222c22746578745f646179223a22e99cbe222c22636f64655f646179223a223331222c22746578745f6e69676874223a22e998b4222c22636f64655f6e69676874223a2239222c2268696768223a223136222c226c6f77223a2238222c227261696e66616c6c223a22302e30222c22707265636970223a22222c2277696e645f646972656374696f6e223a22e58d97222c2277696e645f646972656374696f6e5f646567726565223a22313830222c2277696e645f7370656564223a22382e34222c2277696e645f7363616c65223a2232222c2268756d6964697479223a223539227d5d2c226c6173745f757064617465223a22323032312d30332d31335430383a30303a30302b30383a3030227d5d7d

  String out_str = "";
  int cnt = 0;
  splitString(in_str, ",", buff_split, 5);
  //注意： 要乘2
  cnt = buff_split[3].toInt() * 2;
  out_str = buff_split[4].substring(0, cnt);
  //Serial.println("out_str1=" + out_str);
  out_str = hexStr_convert(out_str);
  //Serial.println("out_str2=" + out_str);
  return out_str;
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
  while (millis() / 1000 - start_time < delay_sec)
  {

    if (mySerial.available() > 0)
    {
      tmp_str = mySerial.readStringUntil('\n');
      Serial.println(tmp_str);

      //结束标志，退出
      if (tmp_str.indexOf("+CHTTPERR: 0,-2") > -1) break;

      if (tmp_str.indexOf("+CHTTPNMIC: 0,0,") > -1)
      {
        //Serial.println("&");
        ret_str = ret_str + parse_CHTTPNMIC(tmp_str);
      }
      else if (tmp_str.indexOf("+CHTTPNMIC: 0,1,") > -1)
      {
        //Serial.println("&");
        ret_str = ret_str + parse_CHTTPNMIC(tmp_str);
      }

    }
    delay(10);
  }

  return ret_str;
}


void free_http()
{
  String ret;
  ret = send_at2("AT+CHTTPDISCON=0", "OK", "ERROR" , 5);
  Serial.println("ret=" + ret);
  Serial.println(">>> 断开http连接  ok ...");

  ret = send_at2("AT+CHTTPDESTROY=0", "OK", "ERROR" , 5);
  Serial.println("ret=" + ret);
  Serial.println(">>> 释放 HTTP ok ...");
}




void ShowStr( String mystring, int x0, int y0, int font_size, uint8_t * framebuffer )
{
  y0 = y0 + 60;

  if (font_size >= 100)
    write_string( (GFXfont *)&msyh36, (char *) mystring.c_str(), &x0, &y0, framebuffer);
  else
    write_string( (GFXfont *)&msyh16, (char *) mystring.c_str(), &x0, &y0, framebuffer);
}



bool get_sim7020c_time_byhttp()
{
  bool succ_flag = false;

  cnt_sync_time = cnt_sync_time + 1;
  String ret;

  free_http();
  delay(2000);


  ret = send_at("AT+CHTTPCREATE=\"" + http_time_host + "\"", "+CHTTPCREATE: 0", 30);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("+CHTTPCREATE: 0") > -1))
    return false;

  Serial.println(">>> 创建HTTP Host ok ...");
  delay(200);

  ret = send_at("AT+CHTTPCON=0", "OK", 30);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("OK") > -1))
    return false;

  Serial.println(">>> 连接 http  ok ...");

  delay(200);
  //最长20秒内获得数据
  ret = send_at_httpget("AT+CHTTPSEND=0,0,\"" + http_time_url + "\"", 20);
  //Serial.println("ret=" + ret);

  if (ret.indexOf("{\"status\":1,") > -1)
  {

    //Serial.println("len=" + String(ret.length()));

    String http_time_str = ret;
    Serial.println("http_time_str=" + http_time_str);

    //{"status":1,"data":{"timestamp":1654917896,"microtime":1654917896.320721,"gmt":"2022-06-11 03:24:56","utc":"2022-06-11T03:24:56Z","timezone":"Shanghai"},"req_id":"27527cc2b07f976f6878"}
    //AT+CCLK="20/03/20,15:34:15+32"

    int pos1 = http_time_str.indexOf("\"gmt\":");

    if (pos1 > -1)
    {
      http_time_str = http_time_str.substring(pos1 + 7, http_time_str.length());
      Serial.println("http_time_str=" + http_time_str);
      String now_time = http_time_str.substring(2, 4) + "/" + http_time_str.substring(5, 7) + "/" + http_time_str.substring(8, 10) +
                        ',' + http_time_str.substring(11, 19) + "+32";
      Serial.println("now_time:" + now_time );

      DateTime now = DateTime(http_time_str.substring(2, 4).toInt() + 2000, http_time_str.substring(5, 7).toInt(), http_time_str.substring(8, 10).toInt(),
                              http_time_str.substring(11, 13).toInt(), http_time_str.substring(14, 16).toInt(), http_time_str.substring(17, 19).toInt());
      // calculate a date which is 7 days and 30 seconds into the future
      //增加8小时
      DateTime future (now.unixtime() + 28800L);

      rtc.adjust(future);
      succ_flag = true;
      cnt_sync_time = 0;
    }
  }

  free_http();
  return succ_flag;
}



bool get_sentence()
{
  bool succ_flag = false;

  String ret;
  cnt_sync_sentence = cnt_sync_sentence + 1;
  free_http();
  delay(2000);

  ret = send_at("AT+CHTTPCREATE=\"" + http_sentence_host + "\"", "+CHTTPCREATE: 0", 30);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("+CHTTPCREATE: 0") > -1))
    return false;

  Serial.println(">>> 创建HTTP Host ok ...");
  delay(200);

  ret = send_at("AT+CHTTPCON=0", "OK", 30);
  Serial.println("ret=" + ret);
  if (not (ret.indexOf("OK") > -1))
    return false;

  Serial.println(">>> 连接 http  ok ...");

  delay(200);
  //最长20秒内获得数据
  String sentence_result = send_at_httpget("AT+CHTTPSEND=0,0,\"" + http_sentence_url + "\"", 20);

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

  free_http();

  return succ_flag;
}

String Get_softrtc_time(int flag)
{
  if (state_sync_time==false)
    return "";
    
  DateTime now = rtc.now();
  char buf[50];
  buf[0]='\0';
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

//每天1次,早7点整
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
  

  //建议时间 10秒
  delay(10000);  //等待一会，确保网络连接上
  Serial.println(">>> 开启 nb-iot ...");

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

}


void loop() {

  //3次失败检测网络连接
  if (cnt_check_net >= 3)
    goto_sleep();

  //3次失败获取网络时间
  if (cnt_sync_time >= 3)
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

    if (state_sync_time == false)
    {
      delay(5000);
      state_sync_time = get_sim7020c_time_byhttp();
      return;
    }


  if (state_sync_sentence == false)
  {
    delay(5000);
    state_sync_sentence = get_sentence();
    return;
  }

  delay(1000);
}
