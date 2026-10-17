package com.easymath.app;

import org.json.JSONArray;
import org.json.JSONObject;

/** 从 NativeBridge.run() 的 JSON 里提取纯文本结果(最小界面用, 不依赖 KaTeX) */
public final class JsonText {
    private JsonText() {}

    public static String extract(String json) {
        StringBuilder sb = new StringBuilder();
        try {
            JSONObject o = new JSONObject(json);
            if (!o.optBoolean("ok", true)) {
                sb.append("错误: ").append(o.optString("error", "未知")).append('\n');
            }
            String title = o.optString("title", "");
            if (!title.isEmpty()) sb.append(title).append("\n\n");
            JSONArray secs = o.optJSONArray("sections");
            if (secs != null) {
                for (int i = 0; i < secs.length(); ++i) {
                    JSONObject s = secs.getJSONObject(i);
                    String nm = s.optString("title", "");
                    if (!nm.isEmpty()) sb.append("[").append(nm).append("]\n");
                    JSONArray lines = s.optJSONArray("lines");
                    if (lines != null) {
                        for (int k = 0; k < lines.length(); ++k) {
                            sb.append(lines.getJSONObject(k).optString("plain", "")).append('\n');
                        }
                    }
                    sb.append('\n');
                }
            }
        } catch (Throwable t) {
            return json;   // 解析失败就把原始 JSON 显示出来(至少不是白屏)
        }
        return sb.toString();
    }
}
