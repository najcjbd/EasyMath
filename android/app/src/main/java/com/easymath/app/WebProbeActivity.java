package com.easymath.app;

import android.app.Activity;
import android.os.Bundle;
import android.view.ViewGroup;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.FrameLayout;
import android.widget.TextView;

/**
 * WebView 探针: 在一个最简单的容器里创建 WebView 并加载 KaTeX 测试页。
 * 目的: 判断"闪退"到底是不是 WebView 本身(而不是正式界面里的其他控件/自定义滚动容器)。
 */
public class WebProbeActivity extends Activity {
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        FrameLayout root = new FrameLayout(this);
        TextView status = new TextView(this);
        status.setText("正在初始化 WebView…");
        root.addView(status, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(root);
        try {
            WebView wv = new WebView(this);
            wv.getSettings().setJavaScriptEnabled(true);
            wv.setWebViewClient(new WebViewClient());
            root.addView(wv, new FrameLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            String html = "<!doctype html><meta charset='utf-8'>"
                    + "<link rel='stylesheet' href='katex/katex.min.css'>"
                    + "<script src='katex/katex.min.js'></script>"
                    + "<script>window.addEventListener('load',function(){"
                    + "try{katex.render('x=\\\\frac{-b\\\\pm\\\\sqrt{b^2-4ac}}{2a}',document.getElementById('f'));"
                    + "document.title='RENDER_OK';}catch(e){document.title='RENDER_FAIL:'+e;}});</script>"
                    + "<body style='padding:16px'><h3>WebView 探针</h3>"
                    + "<div id='f'>加载中…</div>"
                    + "<p>如果你看到上面的公式，说明 WebView 本身正常。</p></body>";
            wv.loadDataWithBaseURL("file:///android_asset/", html, "text/html", "UTF-8", null);
            status.setText("WebView 已创建并加载（若本页没崩，说明 WebView 正常）");
        } catch (Throwable t) {
            status.setText("WebView 创建失败：\n" + android.util.Log.getStackTraceString(t));
        }
    }
}
