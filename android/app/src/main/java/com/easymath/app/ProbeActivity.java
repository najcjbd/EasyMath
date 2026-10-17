package com.easymath.app;

import android.app.Activity;
import android.os.Bundle;
import android.widget.TextView;

/**
 * 最小探针 Activity: 只有一行文字, 不碰 Python/原生库/WebView/复杂布局。
 * 如果连它都闪退, 说明问题在 Application/主题/Manifest 层, 而不在业务代码里。
 */
public class ProbeActivity extends Activity {
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        TextView tv = new TextView(this);
        tv.setTextSize(18);
        tv.setText("EasyMath 最小探针\n\n如果你看到这段文字, 说明:\n"
                 + "· Activity 创建/主题/Manifest 都正常\n"
                 + "· 问题出在 MainActivity 的业务初始化里\n\n"
                 + "版本: " + BuildInfo.VERSION + "\n"
                 + "请把这张截图发给开发者。");
        setContentView(tv);
    }
}
