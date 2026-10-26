#include "weather.h"
#include "cJSON.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
const weather_city_t weather_cities[WEATHER_CITIES]={
    {"杭州","浙江",30.2741f,120.1551f},{"上海","直辖市",31.2304f,121.4737f},
    {"北京","直辖市",39.9042f,116.4074f},{"深圳","广东",22.5431f,114.0579f},
    {"成都","四川",30.5728f,104.0668f}
};
static bool number(const cJSON *obj,const char *key,double lo,double hi,float *out)
{
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(obj,key);
    if(!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble<lo||v->valuedouble>hi) return false;
    *out=(float)v->valuedouble; return true;
}
int weather_date_key(const struct tm *t) { return (t->tm_year+1900)*10000+(t->tm_mon+1)*100+t->tm_mday; }
bool weather_parse(const char *json,unsigned city,time_t now,weather_data_t *out)
{
    if(!json||!out||city>=WEATHER_CITIES) return false;
    cJSON *root=cJSON_Parse(json);
    if(!root) return false;
    bool ok=false; weather_data_t d={.city=city,.fetched=now};
    const cJSON *c=cJSON_GetObjectItemCaseSensitive(root,"current");
    const cJSON *daily=cJSON_GetObjectItemCaseSensitive(root,"daily");
    float code,day;
    if(!number(c,"temperature_2m",-100,70,&d.temperature)||
       !number(c,"apparent_temperature",-150,100,&d.apparent)||
       !number(c,"relative_humidity_2m",0,100,&d.humidity)||
       !number(c,"wind_speed_10m",0,200,&d.wind)||
       !number(c,"weather_code",0,999,&code)||!number(c,"is_day",0,1,&day)) goto done;
    /* Preserve timestamp precision with double; floats lose whole minutes at modern epochs. */
    const cJSON *timestamp=cJSON_GetObjectItemCaseSensitive(c,"time");
    if(!cJSON_IsNumber(timestamp)||timestamp->valuedouble<1577836800.0||
       timestamp->valuedouble>(double)now+3600||timestamp->valuedouble<(double)now-86400) goto done;
    d.data_time=(time_t)timestamp->valuedouble; d.code=(int)code; d.daytime=day==1;
    const cJSON *dates=cJSON_GetObjectItemCaseSensitive(daily,"time");
    const cJSON *codes=cJSON_GetObjectItemCaseSensitive(daily,"weather_code");
    const cJSON *lows=cJSON_GetObjectItemCaseSensitive(daily,"temperature_2m_min");
    const cJSON *highs=cJSON_GetObjectItemCaseSensitive(daily,"temperature_2m_max");
    if(cJSON_GetArraySize(dates)<WEATHER_DAYS||cJSON_GetArraySize(codes)<WEATHER_DAYS||
       cJSON_GetArraySize(lows)<WEATHER_DAYS||cJSON_GetArraySize(highs)<WEATHER_DAYS) goto done;
    for(int i=0;i<WEATHER_DAYS;i++) {
        const cJSON *date=cJSON_GetArrayItem(dates,i),*co=cJSON_GetArrayItem(codes,i),
            *lo=cJSON_GetArrayItem(lows,i),*hi=cJSON_GetArrayItem(highs,i);
        if(!cJSON_IsNumber(date)||!cJSON_IsNumber(co)||!cJSON_IsNumber(lo)||!cJSON_IsNumber(hi)||
           !isfinite(date->valuedouble)||!isfinite(co->valuedouble)||!isfinite(lo->valuedouble)||
           !isfinite(hi->valuedouble)||co->valuedouble<0||co->valuedouble>999||
           lo->valuedouble< -100||hi->valuedouble>70||lo->valuedouble>hi->valuedouble) goto done;
        /* UNIX timestamps already represent the instant of local midnight. */
        time_t epoch=(time_t)date->valuedouble;
        struct tm t; localtime_r(&epoch,&t);
        d.days[i]=(weather_day_t){weather_date_key(&t),(int)co->valuedouble,(float)lo->valuedouble,(float)hi->valuedouble};
        struct tm expected; time_t wanted=now+(time_t)i*86400; localtime_r(&wanted,&expected);
        if(d.days[i].date!=weather_date_key(&expected)) goto done;
    }
    /* Hourly data is optional: a missing forecast must not discard current weather.
     * Retain timestamps and filter at display time so refresh delays cannot show past hours. */
    const cJSON *hourly=cJSON_GetObjectItemCaseSensitive(root,"hourly");
    const char *keys[]={"time","temperature_2m","weather_code","precipitation_probability","is_day"};
    const cJSON *arrays[5];
    int count=WEATHER_HOURS;
    for(int k=0;k<5;k++) {
        arrays[k]=cJSON_GetObjectItemCaseSensitive(hourly,keys[k]);
        int size=cJSON_GetArraySize(arrays[k]); if(size<count) count=size;
    }
    for(int i=0;i<count;i++) {
        const cJSON *v[5]; bool valid=true;
        for(int k=0;k<5;k++) {
            v[k]=cJSON_GetArrayItem(arrays[k],i);
            if(!cJSON_IsNumber(v[k])||!isfinite(v[k]->valuedouble)) valid=false;
        }
        if(!valid) continue;
        double epoch=v[0]->valuedouble,temp=v[1]->valuedouble,co=v[2]->valuedouble,
            rain=v[3]->valuedouble,daytime=v[4]->valuedouble;
        if(epoch<(double)now-3600||epoch>(double)now+48*3600||floor(epoch)!=epoch||
           temp< -100||temp>70||co<0||co>999||floor(co)!=co||rain<0||rain>100||
           (daytime!=0&&daytime!=1)) continue;
        if(d.hour_count&&epoch<=(double)d.hours[d.hour_count-1].time) continue;
        d.hours[d.hour_count++]=(weather_hour_t){(time_t)epoch,(float)temp,(int)co,(int)rain,daytime==1};
    }
    d.valid=true; *out=d; ok=true;
done:
    cJSON_Delete(root); return ok;
}
const char *weather_description(int c)
{
    if(c==0) return "晴";
    if(c==1||c==2) return "多云";
    if(c==3) return "阴";
    if(c==45||c==48) return "雾";
    if(c==51||c==53||c==55) return "细雨";
    if(c==61) return "小雨";
    if(c==63) return "中雨";
    if(c==65) return "大雨";
    if(c==80||c==81||c==82) return "阵雨";
    if(c==56||c==57||c==66||c==67) return "冻雨";
    if((c>=71&&c<=77)||c==85||c==86) return "雪";
    if(c>=95&&c<=99) return "雷雨";
    return "未知";
}
