轻立体天气图标

来源：OpenAI 内置 imagegen 工具生成，2026-10-19。用户选定预览方案 B「轻立体」。
正式素材：atlas.png，4×3透明图集，依次为晴、晴夜、日间多云、夜间多云、阴、雾、细雨、雨、大雨、雷雨、雪、未知。
各 PNG 和 weather_art.c 由 tools/weather_art_assets.py 按固定网格切片、缩放并转换为 RGB565+A8；保留透明度，支持浅色/深色主题。
生成提示：Create one transparent 4-column by 3-row production weather icon sprite atlas. Cohesive premium soft 3D weather icons, rounded porcelain milky clouds with cool blue underside shading, golden orange sun, dimensional blue droplets, upper-left studio lighting. Exactly twelve cells: sun, moon, partly-cloudy day, partly-cloudy night, overcast, fog, drizzle, rain, heavy rain, thunderstorm, snow, unknown. No labels, no backdrop, no faces, no glass.


2026-10-23 降雨细分：新增rain-atlas-v2.png，内置imagegen生成，4×2透明图集。依次细雨、小雨、中雨、大雨、日间阵雨、夜间阵雨、冻雨、雷雨；保留原非降雨图标，总计16种。资源经weather_art_assets.py生成196/56px RGB565+A8后写入资源区，不复制整包到RAM。
