#include "wifi_service.h"
#include <string.h>
void wifi_secret_clear(void *p,size_t n)
{
    volatile uint8_t *bytes=p;
    while(n--) *bytes++=0;
}
bool wifi_profile_valid(const wifi_profile_t *p)
{
    if(!p || !p->ssid_len || p->ssid_len>32 || p->security>=NET_UNSUPPORTED) return false;
    for(unsigned i=0;i<p->ssid_len;i++) if(!p->ssid[i]) return false;
    size_t n=0;
    while(n<sizeof(p->password) && p->password[n]) {
        unsigned char c=p->password[n++];
        if(c<32 || c>126) return false;
    }
    return p->security==NET_OPEN ? n==0 : n>=8 && n<=63;
}
unsigned wifi_retry_delay(unsigned attempt)
{
    static const unsigned seconds[]={2,5,10,30,60};
    return seconds[attempt<5 ? attempt : 4];
}
void wifi_display_name(const uint8_t *ssid,size_t length,char *out,size_t cap)
{
    if(!cap) return;
    size_t i=0,j=0;
    while(i<length && j+1<cap) {
        uint8_t c=ssid[i];
        unsigned n=c<128 ? 1 : (c>=0xc2 && c<=0xdf ? 2 : (c>=0xe0 && c<=0xef ? 3 : (c>=0xf0 && c<=0xf4 ? 4 : 0)));
        bool valid=n && i+n<=length;
        for(unsigned k=1;valid && k<n;k++) valid=(ssid[i+k]&0xc0)==0x80;
        if(valid && n==3) valid=!(c==0xe0 && ssid[i+1]<0xa0) && !(c==0xed && ssid[i+1]>=0xa0);
        if(valid && n==4) valid=!(c==0xf0 && ssid[i+1]<0x90) && !(c==0xf4 && ssid[i+1]>=0x90);
        if(!valid || c<32 || c==127) { out[j++]='?'; i++; }
        else {
            if(j+n>=cap) break;
            memcpy(out+j,ssid+i,n); j+=n; i+=n;
        }
    }
    out[j]=0;
}
size_t wifi_network_merge(wifi_network_t *list,size_t count,size_t limit,const wifi_network_t *item)
{
    if(count>limit) count=limit;
    for(size_t i=0;i<count;i++) {
        if(list[i].ssid_len==item->ssid_len && list[i].security==item->security &&
           !memcmp(list[i].ssid,item->ssid,item->ssid_len)) {
            if(item->rssi>list[i].rssi) list[i]=*item;
            return count;
        }
    }
    if(count<limit) list[count++]=*item;
    return count;
}
const char *wifi_security_text(unsigned s)
{
    switch(s) {
    case NET_OPEN: return "OPEN";
    case NET_WPA2: return "WPA2";
    case NET_WPA3: return "WPA3";
    case NET_WPA2_WPA3: return "WPA2/3";
    default: return "不支持";
    }
}
const char *wifi_state_text(const wifi_status_t *s)
{
    if(!s->available) return "服务不可用";
    if(!s->enabled && s->state!=NET_ERROR) return "Wi-Fi 已关闭";
    if(s->scanning) return "正在扫描";
    switch(s->state) {
    case NET_IDLE: return "请选择网络";
    case NET_STOPPING: return "正在切换";
    case NET_CONNECTING: return "正在连接";
    case NET_GETTING_IP: return "正在获取 IP";
    case NET_CONNECTED: return "已获取 IP";
    case NET_RETRY: return "等待重连";
    case NET_ERROR:
        switch(s->error) {
        case NET_ERR_AUTH: return "认证失败，请检查密码";
        case NET_ERR_INIT: return "无线初始化失败";
        case NET_ERR_SCAN: return "扫描失败，请重试";
        case NET_ERR_OVERFLOW: return "网络事件异常，请重启";
        default: return "网络操作失败";
        }
    default: return "Wi-Fi 已关闭";
    }
}

bool wifi_security_compatible(unsigned saved,unsigned visible)
{
    if(saved>=NET_UNSUPPORTED||visible>=NET_UNSUPPORTED) return false;
    return saved==visible||(saved==NET_WPA2_WPA3&&(visible==NET_WPA2||visible==NET_WPA3))||
        (visible==NET_WPA2_WPA3&&(saved==NET_WPA2||saved==NET_WPA3));
}
