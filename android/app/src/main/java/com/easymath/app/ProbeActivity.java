package com.easymath.app;

import android.app.Activity;
import android.os.Bundle;
import android.text.InputType;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/**
 * 最小可用界面(1.62.10): 输入框 + 计算按钮 + 纯文本结果。
 * 不创建 WebView、不引用任何复杂资源 —— 用来绕开"打开即闪退"的问题, 先让功能可用。
 * 结果直接从 NativeBridge.run() 的 JSON 里取出 text 字段显示。
 */
public class ProbeActivity extends Activity {
    private EditText input;
    private TextView output;
    private TextView status;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        int pad = (int) (12 * getResources().getDisplayMetrics().density);

        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(pad, pad, pad, pad);

        status = new TextView(this);
        status.setText("EasyMath " + BuildInfo.VERSION + "（最小界面）");
        root.addView(status);

        input = new EditText(this);
        input.setHint("输入算式，例如 x^2-2=0 或 pi");
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setSingleLine(true);
        input.setImeOptions(EditorInfo.IME_ACTION_DONE);
        root.addView(input);

        Button go = new Button(this);
        go.setText("计算");
        root.addView(go);

        output = new TextView(this);
        output.setTextIsSelectable(true);
        output.setTextSize(16);
        output.setText("结果会显示在这里。");
        ScrollView sc = new ScrollView(this);
        sc.addView(output);
        root.addView(sc);

        setContentView(root);

        View.OnClickListener doit = v -> {
            String text = input.getText().toString().trim();
            if (text.isEmpty()) return;
            output.setText("计算中…");
            status.setText("计算中…");
            new Thread(() -> {
                String result;
                try {
                    String json = NativeBridge.run("solve", text, 20, true, true, "zh", "auto",
                                                   "auto", "auto", "", "",
                                                   "0,0", "", "0,0", 24, 0.02, "", "", "");
                    result = JsonText.extract(json);
                } catch (Throwable t) {
                    result = "出错: " + t;
                }
                final String r = result;
                runOnUiThread(() -> {
                    output.setText(r);
                    status.setText("EasyMath " + BuildInfo.VERSION + "（最小界面）");
                });
            }).start();
        };
        go.setOnClickListener(doit);
    }
}
