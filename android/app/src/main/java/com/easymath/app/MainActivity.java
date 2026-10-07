package com.easymath.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.net.Uri;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.ColorStateList;
import android.content.res.Configuration;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.os.Bundle;
import android.text.InputType;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.util.Base64;
import android.webkit.WebView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import android.webkit.WebViewClient;
import android.widget.AdapterView;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.FrameLayout;
import android.widget.HorizontalScrollView;
import android.widget.ScrollView;
import android.widget.LinearLayout;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import org.json.JSONArray;
import org.json.JSONTokener;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicLong;

/**
 * 界面全部用代码搭(不依赖 AppCompat/Material), 但按 Material 的视觉语言手绘:
 * 卡片 + 圆角 + 单一主色 + 深色模式。
 *
 * 外观约定(改样式时请一起改):
 *   res/values/colors.xml, res/values-night/colors.xml —— 配色(浅色/深色)
 *   res/values/themes.xml, res/values-night/themes.xml —— 主题(状态栏/导航栏跟随背景)
 *   res/drawable/bg_card|bg_input|bg_chip|bg_key|bg_btn_primary|bg_btn_outline|bg_field.xml
 *   assets/result.css —— 结果页样式(WebView 与预览截图共用同一份)
 */
public class MainActivity extends Activity {

    private static final String[] MODES = {"lagrange", "solve", "eval", "line", "glyph"};
    private static final String[] MODE_NAMES = {"插值", "解方程", "求值", "直线", "字形"};
    private static final String[] HINTS = {
            "例: x=1,y=3 x=2,y=5 x=3,y=9\n也支持 x1=.. y1=.. , 5x=.. , x⁴=.. , (1,3) , P1=(1,2)",
            "例: x^4=5\n多个方程用逗号: y=5x, y=6z, x=2z\n也支持 LaTeX: \\frac{x^2}{2}=2",
            "例: 5! + 6^8 , √6 , 1/3 , 5x6 , sin(30°)",
            "例: 45° , 30 , 135 , -45 , 0.5rad",
            "例: 你好 或 Ai\n左下角坐标可写 (10,20) / 10 20 / :(10,20) 表示镜像\n也可以点「手绘」在方格里写"};
    /** 每个模式在空结果页给出的示例(点击即填入输入框) */
    private static final String[][] EXAMPLES = {
            {"x=1,y=3 x=2,y=5 x=3,y=9", "(1,1) (2,4) (3,9)"},
            {"x^4=5", "y=5x, y=6z, x=2z"},
            {"5! + 6^8", "√6", "1/3"},
            {"45°", "1rad", "-30"},
            {"你好", "Ai 好", "一"}};
    private static final String[] SYMBOLS = {
            "√", "∛", "⁴", "²", "ⁿ", "⁻", "×", "÷", "π", "°", "∠", "(", ")", "^", "=", ",", "x", "y"};
    /**
     * 小数位预设。上限 100000 与 C++ 的 kMaxDecimals 一致: 200 以内走原算法,
     * 超过 200 走高精度特殊算法, 拿不出那么多位会自动钳到本机能精确的最大位数;
     * 以前这里只到 20, 是因为当时安卓没有可用高精度后端(无 MPFR, 内嵌 SymPy 也没启动),
     * 再多只会打印 long double 的噪声位。现在走 SymPy/mpmath, 任意精度真实可用。
     */
    private static final String[] DECIMALS = {
            "4", "6", "8", "10", "12", "15", "20", "25", "30", "40", "50", "80", "100", "150", "200"};
    private static final int DECIMALS_MAX = 100000;   // 与 C++ 的 kMaxDecimals 一致

    // 用可缓存线程池: 被"打断"而放弃的请求在后台自然结束, 不会堵住下一次计算
    private final ExecutorService pool = Executors.newCachedThreadPool();
    private final AtomicLong requestSeq = new AtomicLong();
    private volatile long pendingRequest = -1;
    private final Handler ui = new Handler(Looper.getMainLooper());

    /** 默认进入「插值」并留空输入(用户预期: 开屏是空输入 + 插值界面) */
    private String mode = "lagrange";
    private SharedPreferences prefs;
    private String engineSetting = "auto";   // auto | sympy | builtin
    private String sciSetting = "auto";      // auto | always | never
    private String ineqSetting = "auto";     // auto | always | never (不等式数值解集)
    private boolean realOnly = false;
    private boolean sympyHintShown = false;
    /** >0 表示用自定义小数位(设置对话框里填), 否则用下拉预设 */
    private int decimalsCustom = 0;
    /** 下拉的初始回调(程序设置选择)不算"用户改过", 否则会清掉自定义值 */
    private boolean spinnerReady = false;
    private EditText input;
    private WebView result;
    private TextView status;
    private View statusDot;
    private TextView engineBadge;
    private Spinner decimals;
    private CheckBox showSteps;
    /** 解方程专有: 常量 / 派生量(切换模式即清空, 不持久化) */
    private LinearLayout solveCard;
    private EditText constInput;
    private EditText deriveInput;
    private CheckBox plainOnly;
    /** 字形模式专有: 左下角坐标 / 字体包 / 手绘笔画(切模式即清空) */
    private LinearLayout glyphCard;
    private EditText anchorInput;
    private TextView fontPackLabel;
    private String fontPackPath = "";
    /** 选中的字体: 空 = 默认(vivo Sans 的 Regular); 否则是字体包里的序号或名字子串 */
    private String fontPick = "";
    private String axisSetting = "";      // 可变字体轴, 例: wght=700,opsz=18
    private String instanceSetting = "";  // 命名实例: 序号或名字
    private String fontPickName = "";
    private String strokesData = "";
    /** 字形模式: C++ 侧给出的 SVG 预览 */
    private String previewSvg = "";
    /** 字形模式: 字号 / 拟合容差(与桌面端 --size / --fit-tol 同名同义) */
    private int glyphSize = 1000;
    private double glyphTol = 0.5;
    private String lastPlain = "";
    /** 渲染完成后的正文 HTML(KaTeX 已排好), 用于导出"自包含"文件 */
    private String renderedBody = "";
    /** 是否已经算过一次(避免把开屏的空状态当成结果导出) */
    private boolean hasResult = false;
    /** 内联了 CSS 与字体(data URI)的样式, 只构造一次(约几百 KB) */
    private String inlineCssCache = "";
    private String lastJson = "";
    private Button runBtn;
    private Button cancelBtn;
    /** 整页滚动容器(会把结果区的手势让给 WebView, 见 PageScrollView) */
    private PageScrollView page;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        NativeBridge.init(getApplicationContext()); // 兜底启动 Python 用
        prefs = getSharedPreferences("easymath", MODE_PRIVATE);
        engineSetting = prefs.getString("engine", "auto");
        sciSetting = prefs.getString("scientific", "auto");
        ineqSetting = prefs.getString("numericInequality", "auto");
        realOnly = prefs.getBoolean("realOnly", false);
        decimalsCustom = prefs.getInt("decimalsCustom", 0);
        setContentView(buildUi());
        setStatus("就绪", R.color.text_dim);
        showPlaceholder();
    }

    // ==================== 界面 ====================

    private View buildUi() {
        final int pad = dp(14);
        // 结构: 整页一个滚动容器(PageScrollView) + 结果卡片固定高度(内部 WebView 自己滚)。
        // PageScrollView 会把"落在结果区且结果还能滚"的手势让给 WebView, 其余情况滚整页,
        // 于是: 在表单上滑 -> 整个页面(含结果卡片)一起动; 在结果里滑 -> 只滚结果内容;
        // 结果滚到边缘后继续滑 -> 自动接管为整页滚动。
        LinearLayout outer = new LinearLayout(this); // 只负责窗口内边距
        outer.setOrientation(LinearLayout.VERTICAL);
        outer.setBackgroundColor(color(R.color.bg));
        outer.setClipToPadding(false);
        outer.setPadding(pad, 0, pad, pad);
        applyInsets(outer, pad);

        page = new PageScrollView(this);
        page.setClipToPadding(false);
        page.setFillViewport(false);
        LinearLayout root = new LinearLayout(this); // 页面内容
        root.setOrientation(LinearLayout.VERTICAL);
        page.addView(root, new ScrollView.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        outer.addView(page, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));

        root.addView(buildHeader());

        // ---- 模式选择(胶囊 chip, 选中态一眼可辨) ----
        HorizontalScrollView modeScroll = new HorizontalScrollView(this);
        modeScroll.setHorizontalScrollBarEnabled(false);
        LinearLayout modeRow = new LinearLayout(this);
        modeRow.setOrientation(LinearLayout.HORIZONTAL);
        modeRow.setPadding(0, dp(2), 0, dp(10));
        final List<TextView> chips = new ArrayList<>();
        for (int i = 0; i < MODES.length; i++) {
            final int idx = i;
            final TextView chip = chipView(MODE_NAMES[i]);
            chip.setOnClickListener(v -> selectMode(idx, chips));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.WRAP_CONTENT, dp(38));
            lp.rightMargin = dp(8);
            chips.add(chip);
            modeRow.addView(chip, lp);
        }
        modeScroll.addView(modeRow);
        root.addView(modeScroll);

        // ---- 输入卡片: 输入框 + 符号键盘 ----
        LinearLayout inputCard = card(dp(14), dp(2));
        input = new EditText(this);
        input.setHint(HINTS[2]);
        input.setTextSize(15);
        input.setTypeface(Typeface.MONOSPACE);
        input.setTextColor(color(R.color.text));
        input.setHintTextColor(color(R.color.text_dim));
        input.setBackgroundResource(R.drawable.bg_input);
        input.setPadding(dp(12), dp(10), dp(12), dp(10));
        input.setMinLines(3);
        input.setMaxLines(6);
        input.setGravity(Gravity.TOP | Gravity.START);
        input.setHorizontallyScrolling(false);
        // 数学输入不需要自动纠错/首字母大写, 也不要全屏编辑面板
        input.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
                | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        input.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        // 多行输入框要能自己上下滚: 打开滚动条, 并在手指落在输入框上时让父滚动容器别抢手势
        input.setVerticalScrollBarEnabled(true);
        input.setScrollbarFadingEnabled(false);
        input.setOnTouchListener((v, ev) -> {
            if (ev.getActionMasked() == android.view.MotionEvent.ACTION_DOWN
                    || ev.getActionMasked() == android.view.MotionEvent.ACTION_MOVE) {
                v.getParent().requestDisallowInterceptTouchEvent(true);
            }
            return false; // 不吞事件, 光标/选择照常
        });
        inputCard.addView(input, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        HorizontalScrollView symScroll = new HorizontalScrollView(this);
        symScroll.setHorizontalScrollBarEnabled(false);
        LinearLayout symRow = new LinearLayout(this);
        symRow.setOrientation(LinearLayout.HORIZONTAL);
        symRow.setPadding(0, dp(10), 0, 0);
        for (String s : SYMBOLS) {
            TextView key = keyView(s);
            key.setOnClickListener(v -> insertAtCursor(s));
            LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(dp(44), dp(40));
            lp.rightMargin = dp(6);
            symRow.addView(key, lp);
        }
        symScroll.addView(symRow);
        inputCard.addView(symScroll);
        root.addView(inputCard);

        // ---- 解方程专有: 常量 / 派生量(位置: 输入框下、小数位上) ----
        solveCard = card(dp(12), dp(1));
        constInput = solveOnlyField(solveCard, "常数（不求解的字母）",
                "如 a,b,c —— 只解其它未知量，它们当常量", "a,b,c");
        deriveInput = solveOnlyField(solveCard, "求值（由解反推）",
                "如 abc,xy —— 每个表达式各给一行结果", "abc, xy");
        root.addView(solveCard);
        solveCard.setVisibility(View.GONE); // 只在解方程模式显示

        // ---- 字形模式专有: 左下角坐标 + 字体包导入 + 手绘 ----
        glyphCard = card(dp(12), dp(1));
        anchorInput = solveOnlyField(glyphCard, "左下角坐标", 
                "整幅文字的锚点, 支持 (10,20) / 10 20 / 10,20; 前缀 : 表示左右镜像",
                "(0,0) 或 :(0,0)");
        LinearLayout fontRow = new LinearLayout(this);
        fontRow.setOrientation(LinearLayout.HORIZONTAL);
        fontRow.setGravity(Gravity.CENTER_VERTICAL);
        fontRow.setPadding(0, dp(6), 0, 0);
        Button pickFont = new Button(this);
        pickFont.setText("导入字体包");
        pickFont.setAllCaps(false);
        pickFont.setOnClickListener(v -> pickFontPack());
        fontRow.addView(pickFont);
        Button pickFace = new Button(this);
        pickFace.setText("选择字体");
        pickFace.setAllCaps(false);
        pickFace.setOnClickListener(v -> chooseFontFace());
        fontRow.addView(pickFace);
        Button pickVar = new Button(this);
        pickVar.setText("变体");
        pickVar.setAllCaps(false);
        pickVar.setOnClickListener(v -> chooseVariation());
        fontRow.addView(pickVar);
        glyphCard.addView(fontRow);
        fontPackLabel = label("", 12, R.color.text_dim);
        fontPackLabel.setSingleLine(true);
        fontPackLabel.setEllipsize(TextUtils.TruncateAt.MIDDLE);
        fontPackLabel.setPadding(0, dp(4), 0, 0);
        glyphCard.addView(fontPackLabel);
        LinearLayout drawRow = new LinearLayout(this);
        drawRow.setOrientation(LinearLayout.HORIZONTAL);
        drawRow.setGravity(Gravity.CENTER_VERTICAL);
        drawRow.setPadding(0, dp(6), 0, 0);
        Button drawBtn = new Button(this);
        drawBtn.setText("手绘");
        drawBtn.setAllCaps(false);
        drawBtn.setOnClickListener(v -> openDrawDialog());
        drawRow.addView(drawBtn);
        TextView drawTip = label("在方格里手写, 按笔画中心线还原成函数(仅安卓)", 12, R.color.text_dim);
        drawTip.setPadding(dp(10), 0, 0, 0);
        drawRow.addView(drawTip);
        glyphCard.addView(drawRow);
        root.addView(glyphCard);
        glyphCard.setVisibility(View.GONE);
        updateFontPackLabel();

        // ---- 选项卡片 ----
        LinearLayout optCard = card(dp(12), dp(1));
        LinearLayout optRow1 = new LinearLayout(this);
        optRow1.setOrientation(LinearLayout.HORIZONTAL);
        optRow1.setGravity(Gravity.CENTER_VERTICAL);
        TextView dl = label("小数位", 13, R.color.text_dim);
        optRow1.addView(dl);
        decimals = new Spinner(this);
        ArrayAdapter<String> ad = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, DECIMALS);
        ad.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        decimals.setAdapter(ad);
        // 初始选择: 优先用上次保存的值; post 一次避免个别机型上 setSelection 被布局覆盖
        final int decIdx = decimalsIndex(prefs.getInt("decimals", 8));
        decimals.setSelection(decIdx);
        decimals.post(() -> {
            decimals.setSelection(decIdx);
            spinnerReady = true; // 初始选择设好之后, 才把后续回调当作用户操作
        });
        decimals.setOnItemSelectedListener(new AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(AdapterView<?> parent, View view, int pos, long id) {
                if (!spinnerReady) return;
                // 用户选预设即取消自定义(最后改的那个生效)
                prefs.edit().putInt("decimals", Integer.parseInt(DECIMALS[pos]))
                        .putInt("decimalsCustom", 0).apply();
                if (decimalsCustom != 0) decimalsCustom = 0;
            }
            @Override public void onNothingSelected(AdapterView<?> parent) { }
        });
        decimals.setBackgroundResource(R.drawable.bg_field);
        decimals.setPadding(dp(10), 0, dp(22), 0); // 右侧留给箭头
        FrameLayout dWrap = new FrameLayout(this);
        dWrap.addView(decimals, new FrameLayout.LayoutParams(dp(84), dp(40)));
        TextView arrow = new TextView(this);
        arrow.setText("\u25be"); // ▾
        arrow.setTextSize(12);
        arrow.setTextColor(color(R.color.text_dim));
        arrow.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        FrameLayout.LayoutParams alp = new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        alp.gravity = Gravity.END | Gravity.CENTER_VERTICAL;
        alp.rightMargin = dp(9);
        dWrap.addView(arrow, alp);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(dp(84), dp(40));
        slp.leftMargin = dp(8);
        optRow1.addView(dWrap, slp);
        optCard.addView(optRow1);

        LinearLayout optRow2 = new LinearLayout(this);
        optRow2.setOrientation(LinearLayout.HORIZONTAL);
        optRow2.setGravity(Gravity.CENTER_VERTICAL);
        optRow2.setPadding(0, dp(6), 0, 0);
        showSteps = checkBox("显示步骤");
        plainOnly = checkBox("纯文本");
        plainOnly.setOnCheckedChangeListener((v, c) -> {
            if (lastJson.isEmpty()) showPlaceholder(); // 还没算过: 维持空状态
            else render(lastJson, "");
        });
        optRow2.addView(showSteps);
        LinearLayout.LayoutParams clp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        clp.leftMargin = dp(18);
        optRow2.addView(plainOnly, clp);
        optCard.addView(optRow2);
        root.addView(optCard);

        // ---- 主/次按钮 ----
        LinearLayout btnRow = new LinearLayout(this);
        btnRow.setOrientation(LinearLayout.HORIZONTAL);
        btnRow.setPadding(0, dp(10), 0, 0);
        runBtn = new Button(this);
        styleButton(runBtn, "计 算", true);
        runBtn.setOnClickListener(v -> doRun());
        LinearLayout.LayoutParams blp = new LinearLayout.LayoutParams(0, dp(50), 2f);
        blp.rightMargin = dp(10);
        btnRow.addView(runBtn, blp);
        cancelBtn = new Button(this);
        styleButton(cancelBtn, "打 断", false);
        cancelBtn.setEnabled(false);
        cancelBtn.setOnClickListener(v -> doCancel());
        btnRow.addView(cancelBtn, new LinearLayout.LayoutParams(0, dp(50), 1f));
        root.addView(btnRow);

        // ---- 状态行: 圆点 + 文本 + 右侧保存/分享 ----
        LinearLayout statusRow = new LinearLayout(this);
        statusRow.setOrientation(LinearLayout.HORIZONTAL);
        statusRow.setGravity(Gravity.CENTER_VERTICAL);
        statusRow.setPadding(dp(2), dp(10), 0, dp(6));
        statusDot = new View(this);
        GradientDrawable dot = new GradientDrawable();
        dot.setShape(GradientDrawable.OVAL);
        dot.setColor(color(R.color.text_dim));
        statusDot.setBackground(dot);
        LinearLayout.LayoutParams dlp = new LinearLayout.LayoutParams(dp(8), dp(8));
        dlp.rightMargin = dp(8);
        statusRow.addView(statusDot, dlp);
        status = new TextView(this);
        status.setTextSize(12.5f);
        status.setTextColor(color(R.color.text_dim));
        status.setMaxLines(2);
        status.setEllipsize(TextUtils.TruncateAt.END);
        statusRow.addView(status, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        statusRow.addView(textButton("保存HTML", v -> saveHtml()));
        statusRow.addView(textButton("分享", v -> sharePlain()));
        root.addView(statusRow);

        // ---- 结果卡片 ----
        LinearLayout resultCard = card(0, dp(2));
        LinearLayout resultHead = new LinearLayout(this);
        resultHead.setOrientation(LinearLayout.HORIZONTAL);
        resultHead.setGravity(Gravity.CENTER_VERTICAL);
        resultHead.setPadding(dp(14), dp(12), dp(12), dp(6));
        TextView rl = label("结果", 12.5f, R.color.text_dim);
        rl.setLetterSpacing(0.06f);
        resultHead.addView(rl);
        engineBadge = new TextView(this);
        engineBadge.setTextSize(11);
        engineBadge.setPadding(dp(8), dp(2), dp(8), dp(2));
        engineBadge.setTextColor(color(R.color.primary));
        GradientDrawable pill = new GradientDrawable();
        pill.setShape(GradientDrawable.RECTANGLE);
        pill.setCornerRadius(dp(9));
        pill.setColor(color(R.color.chip_selected_bg));
        engineBadge.setBackground(pill);
        engineBadge.setVisibility(View.GONE);
        engineBadge.setOnLongClickListener(v -> {
            showEngineDetails();
            return true;
        });
        LinearLayout.LayoutParams elp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        elp.leftMargin = dp(8);
        resultHead.addView(engineBadge, elp);
        resultCard.addView(resultHead);

        result = new WebView(this);
        result.getSettings().setJavaScriptEnabled(true); // KaTeX 需要
        result.getSettings().setSupportZoom(false);
        result.setBackgroundColor(Color.TRANSPARENT);
        result.setOverScrollMode(View.OVER_SCROLL_NEVER);
        // 页面(含 KaTeX 渲染)完成后抓一份正文 HTML, 供「保存HTML」导出用。
        // 导出时必须自带样式与字体, 否则离开应用(相对路径失效)就只剩 $$...$$ 源码。
        result.setWebViewClient(new WebViewClient() {
            @Override public void onPageFinished(WebView v, String url) {
                // 大报告里 KaTeX 要渲染几百个公式, "加载完"时常常还没渲染完; 直接抓会把未渲染的
                // $$...$$ 存进导出文件。这里轮询页面上的完成标记, 渲染好了再抓(最多 15 秒兜底)。
                final int[] tries = {0};
                final Runnable[] poll = new Runnable[1];
                poll[0] = () -> {
                    final WebView wv = v;
                    v.evaluateJavascript("window.__EM_RENDER_DONE===true?'1':'0'", val -> {
                        if ((val != null && val.contains("1")) || ++tries[0] > 60) {
                            wv.evaluateJavascript("document.body.innerHTML",
                                    value -> renderedBody = jsToHtml(value));
                        } else {
                            wv.postDelayed(poll[0], 250);
                        }
                    });
                };
                v.post(poll[0]);
            }
        });
        page.setInnerScrollable(result); // 结果区手势优先给 WebView, 到边缘再交给整页
        resultCard.addView(result, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f));
        // 结果卡片给固定高度: 高度比例与之前一致(屏幕 46%), 长结果由 WebView 自己滚
        root.addView(resultCard, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, resultCardHeight()));

        // 初始就把默认模式高亮出来(否则四个 chip 一个都不亮, 看不出当前模式)
        int start = 2; // eval
        for (int i = 0; i < MODES.length; i++) if (MODES[i].equals(mode)) start = i;
        selectMode(start, chips);
        return outer;
    }

    /** 结果卡片高度: 屏幕高度的 46%(不低于 240dp), 与之前的观感比例保持一致 */
    private int resultCardHeight() {
        int h = (int) (getResources().getDisplayMetrics().heightPixels * 0.46f);
        return Math.max(dp(240), h);
    }

    /** 解方程专有卡片里的一行: 标签 + 输入框 */
    private EditText solveOnlyField(LinearLayout parent, String label, String hint, String example) {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        if (parent.getChildCount() > 0) row.setPadding(0, dp(10), 0, 0);
        TextView lb = label(label, 12.5f, R.color.text_dim);
        lb.setMinWidth(dp(112));
        row.addView(lb);
        EditText e = new EditText(this);
        e.setHint(example);
        e.setTextSize(14);
        e.setTypeface(Typeface.MONOSPACE);
        e.setSingleLine(true);
        e.setTextColor(color(R.color.text));
        e.setHintTextColor(color(R.color.text_dim));
        e.setBackgroundResource(R.drawable.bg_input);
        e.setPadding(dp(10), dp(6), dp(10), dp(6));
        e.setInputType(android.text.InputType.TYPE_CLASS_TEXT
                | android.text.InputType.TYPE_TEXT_FLAG_NO_SUGGESTIONS);
        e.setImeOptions(EditorInfo.IME_FLAG_NO_EXTRACT_UI);
        row.addView(e, new LinearLayout.LayoutParams(0,
                ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        parent.addView(row);
        return e;
    }

    private View buildHeader() {
        LinearLayout head = new LinearLayout(this);
        head.setOrientation(LinearLayout.HORIZONTAL);
        head.setGravity(Gravity.CENTER_VERTICAL);
        head.setPadding(dp(2), dp(14), 0, dp(12));
        LinearLayout col = new LinearLayout(this);
        col.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(this);
        title.setText("EasyMath");
        title.setTextSize(21);
        title.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        title.setTextColor(color(R.color.text));
        col.addView(title);
        TextView sub = label("拉格朗日插值 · 解方程 · 求值/开方 · 过原点直线", 12, R.color.text_dim);
        sub.setPadding(0, dp(3), 0, 0);
        col.addView(sub);
        head.addView(col, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1f));
        TextView setBtn = textButton("设置", v -> openSettings());
        setBtn.setTextSize(14);
        setBtn.setPadding(dp(12), dp(10), dp(6), dp(10));
        head.addView(setBtn);
        return head;
    }

    /** 系统栏(状态栏/导航栏/输入法)内边距: targetSdk 36 是强制 edge-to-edge, 不处理会被挡住 */
    private void applyInsets(final View root, final int pad) {
        root.setOnApplyWindowInsetsListener((v, insets) -> {
            int top, bottom, left, right;
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets i = insets.getInsets(
                        WindowInsets.Type.systemBars() | WindowInsets.Type.ime());
                top = i.top;
                bottom = i.bottom;
                left = i.left;
                right = i.right;
            } else {
                top = insets.getSystemWindowInsetTop();
                bottom = insets.getSystemWindowInsetBottom();
                left = insets.getSystemWindowInsetLeft();
                right = insets.getSystemWindowInsetRight();
            }
            v.setPadding(left + pad, top + dp(2), right + pad, Math.max(bottom, pad));
            return insets;
        });
        root.requestApplyInsets();
    }

    private void selectMode(int idx, List<TextView> chips) {
        mode = MODES[idx];
        input.setHint(HINTS[idx]);
        for (int k = 0; k < chips.size(); k++) {
            boolean on = (k == idx);
            chips.get(k).setSelected(on);
            chips.get(k).setTypeface(null, on ? Typeface.BOLD : Typeface.NORMAL);
            chips.get(k).setTextColor(color(on ? R.color.primary : R.color.text_dim));
        }
        boolean isSolve = "solve".equals(mode);
        if (solveCard != null) {
            solveCard.setVisibility(isSolve ? View.VISIBLE : View.GONE);
            if (!isSolve) { // 离开解方程就不保留(用户要求)
                if (constInput != null) constInput.setText("");
                if (deriveInput != null) deriveInput.setText("");
            }
        }
        boolean isGlyph = "glyph".equals(mode);
        if (glyphCard != null) {
            glyphCard.setVisibility(isGlyph ? View.VISIBLE : View.GONE);
            if (!isGlyph) { // 离开字形模式就不保留(与解方程的常数框一致)
                strokesData = "";
                if (anchorInput != null) anchorInput.setText("");
            }
        }
        if (lastJson.isEmpty()) showPlaceholder();
    }

    // ---- 小组件工厂 ----

    private LinearLayout card(int pad, int elevation) {
        LinearLayout c = new LinearLayout(this);
        c.setOrientation(LinearLayout.VERTICAL);
        c.setBackgroundResource(R.drawable.bg_card);
        if (pad > 0) c.setPadding(pad, pad, pad, pad);
        c.setElevation(elevation);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        lp.bottomMargin = dp(10);
        c.setLayoutParams(lp);
        return c;
    }

    private TextView chipView(String text) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(14);
        t.setGravity(Gravity.CENTER);
        t.setPadding(dp(16), 0, dp(16), 0);
        t.setBackgroundResource(R.drawable.bg_chip);
        t.setTextColor(color(R.color.text_dim));
        t.setClickable(true);
        t.setFocusable(true);
        return t;
    }

    private TextView keyView(String text) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(15);
        t.setGravity(Gravity.CENTER);
        t.setBackgroundResource(R.drawable.bg_key);
        t.setTextColor(color(R.color.text));
        t.setClickable(true);
        t.setFocusable(true);
        return t;
    }

    private TextView label(String text, float size, int colorRes) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(size);
        t.setTextColor(color(colorRes));
        return t;
    }

    private CheckBox checkBox(String text) {
        CheckBox c = new CheckBox(this);
        c.setText(text);
        c.setTextSize(13);
        c.setTextColor(color(R.color.text));
        c.setButtonTintList(ColorStateList.valueOf(color(R.color.primary)));
        return c;
    }

    private TextView textButton(String text, View.OnClickListener l) {
        TextView t = new TextView(this);
        t.setText(text);
        t.setTextSize(12.5f);
        t.setTextColor(color(R.color.primary));
        t.setPadding(dp(10), dp(8), dp(2), dp(8));
        t.setClickable(true);
        t.setFocusable(true);
        t.setOnClickListener(l);
        return t;
    }

    private void styleButton(Button b, String text, boolean primary) {
        b.setText(text);
        b.setTextSize(16);
        b.setAllCaps(false);
        b.setTypeface(null, Typeface.BOLD);
        b.setBackgroundResource(primary ? R.drawable.bg_btn_primary : R.drawable.bg_btn_outline);
        b.setTextColor(color(primary ? R.color.on_primary : R.color.primary));
        b.setPadding(0, 0, 0, 0);
        b.setStateListAnimator(null); // 关掉默认的抬升动画, 保持平面观感
    }

    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    private int color(int res) {
        return getResources().getColor(res, getTheme());
    }

    private boolean isNight() {
        return (getResources().getConfiguration().uiMode & Configuration.UI_MODE_NIGHT_MASK)
                == Configuration.UI_MODE_NIGHT_YES;
    }

    // ==================== 交互 ====================

    private void insertAtCursor(String s) {
        int st = Math.max(input.getSelectionStart(), 0);
        int en = Math.max(input.getSelectionEnd(), 0);
        input.getText().replace(Math.min(st, en), Math.max(st, en), s);
    }

    private void setStatus(String text, int colorRes) {
        status.setText(text);
        if (statusDot != null) {
            GradientDrawable dot = new GradientDrawable();
            dot.setShape(GradientDrawable.OVAL);
            dot.setColor(color(colorRes));
            statusDot.setBackground(dot);
        }
    }

    private void doRun() {
        previewSvg = "";
        final String text = input.getText().toString().trim();
        if (text.isEmpty()) {
            toast("请输入内容");
            return;
        }
        final long id = requestSeq.incrementAndGet();
        pendingRequest = id;
        setStatus("计算中…（可随时点「打断」）", R.color.warn);
        runBtn.setEnabled(false);
        cancelBtn.setEnabled(true);
        final int dec = effectiveDecimals();
        final boolean steps = showSteps.isChecked();
        final String m = mode;
        pool.execute(() -> {
            String json;
            try {
                final String consts = constInput == null ? "" : constInput.getText().toString().trim();
                final String derives = deriveInput == null ? "" : deriveInput.getText().toString().trim();
                final String anchorTxt = anchorInput == null ? "" : anchorInput.getText().toString().trim();
                json = NativeBridge.run(m, text, dec, realOnly, steps, "zh", engineSetting,
                        sciSetting, ineqSetting, consts, derives,
                        anchorTxt, fontPackPath, strokesData, glyphSize, glyphTol, fontPick,
                        axisSetting, instanceSetting);
            } catch (Throwable t) {
                json = "{\"ok\":false,\"error\":\"native error: " + t + "\",\"sections\":[]}";
            }
            final String j = json;
            ui.post(() -> {
                if (id != pendingRequest) return; // 已被打断/被更新的请求取代: 丢弃结果
                pendingRequest = -1;
                runBtn.setEnabled(true);
                cancelBtn.setEnabled(false);
                lastJson = j;
                try {
                    JSONObject o = new JSONObject(j);
                    if (o.optBoolean("ok")) setStatus(describe(o), R.color.ok);
                    else setStatus("错误: " + o.optString("error"), R.color.err);
                    showEngine(o.optString("engine"), o.optString("engineVersion"),
                               o.optString("engineError"));
                    render(j, o.optString("error"));
                    result.scrollTo(0, 0); // 新结果从头看
                } catch (Exception e) {
                    setStatus("解析结果失败: " + e, R.color.err);
                }
                hideKeyboard();
            });
        });
    }

    /** 打断: 立刻让界面可用, 并请求内核/引擎停止计算 */
    private void doCancel() {
        pendingRequest = -1;          // 使在途结果作废
        NativeBridge.cancel();        // 长循环与外部引擎会尽快收手
        runBtn.setEnabled(true);
        cancelBtn.setEnabled(false);
        setStatus("已打断（如引擎正在外部求解，可能需要一两秒收尾）", R.color.warn);
    }

    // ---- 字形模式: 字体包导入(SAF) / 手绘 ----
    private static final int REQ_FONT_PACK = 0x51;

    private void updateFontPackLabel() {
        if (fontPackLabel == null) return;
        if (!fontPackPath.isEmpty() && new File(fontPackPath).exists()) {
            String face = fontPickName.isEmpty() ? "默认(vivo Sans Regular)" : fontPickName;
            fontPackLabel.setText("已导入: " + new File(fontPackPath).getName() + " · " + face);
        } else {
            File def = new File(getFilesDir(), "vivo_Sans.zip");
            if (def.exists()) {
                fontPackPath = def.getAbsolutePath();
                fontPackLabel.setText("已导入: " + def.getName());
            } else {
                fontPackLabel.setText("未导入字体包(点左边按钮选一个 .zip 或 .ttf)");
            }
        }
    }

    private void pickFontPack() {
        Intent it = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        it.addCategory(Intent.CATEGORY_OPENABLE);
        it.setType("*/*");
        try {
            startActivityForResult(it, REQ_FONT_PACK);
        } catch (Throwable t) {
            Toast.makeText(this, "打不开文件选择器: " + t, Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_FONT_PACK || res != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData();
        try {
            InputStream in = getContentResolver().openInputStream(uri);
            if (in == null) throw new Exception("读不到所选文件");
            File dst = new File(getFilesDir(), "vivo_Sans.zip");
            FileOutputStream out = new FileOutputStream(dst);
            byte[] buf = new byte[1 << 16];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            out.close();
            in.close();
            fontPackPath = dst.getAbsolutePath();
            fontPick = "";
            fontPickName = "";
            prefs.edit().putString("fontPack", fontPackPath)
                    .putString("fontPick", "").putString("fontPickName", "").apply();
            updateFontPackLabel();
            Toast.makeText(this, "字体包已导入, 可以点「运行」了", Toast.LENGTH_SHORT).show();
        } catch (Throwable t) {
            Toast.makeText(this, "导入失败: " + t, Toast.LENGTH_LONG).show();
        }
    }

    /** 列出字体包里的字体, 让用户挑一个(默认是 vivo Sans 的 Regular) */
    /** 变体选择: 列出该字体的轴/命名实例, 让用户填轴值或选实例序号 */
    private void chooseVariation() {
        String info = NativeBridge.variations(fontPackPath, fontPick);
        StringBuilder sb = new StringBuilder();
        if (info != null) {
            for (String line : info.split("\n")) {
                String[] f = line.split("\t");
                if (f.length >= 5 && "AXIS".equals(f[0]))
                    sb.append("轴 ").append(f[1]).append(": ").append(f[2]).append(" ~ ").append(f[4])
                      .append(" (默认 ").append(f[3]).append(")").append(f[5].isEmpty() ? "" : "  " + f[5]).append("\n");
                else if (f.length >= 2 && "VARIABLE".equals(f[0]) && "0".equals(f[1]))
                    sb.append("这份字体不是可变字体\n");
            }
        }
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(20), dp(6), dp(20), dp(6));
        box.addView(label(sb.length() == 0 ? "（没有读到轴信息）" : sb.toString().trim(), 12, R.color.text_dim));
        final EditText axisIn = new EditText(this);
        axisIn.setHint("轴值, 例: wght=700,opsz=18");
        axisIn.setText(axisSetting);
        box.addView(axisIn);
        final EditText instIn = new EditText(this);
        instIn.setHint("命名实例: 序号或名字(留空=用轴值)");
        instIn.setText(instanceSetting);
        box.addView(instIn);
        new AlertDialog.Builder(this)
                .setTitle("变体(可变字体)")
                .setView(box)
                .setPositiveButton("确定", (d, w) -> {
                    axisSetting = axisIn.getText().toString().trim();
                    instanceSetting = instIn.getText().toString().trim();
                    toast("变体已设置: " + (instanceSetting.isEmpty() ? axisSetting : instanceSetting));
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void chooseFontFace() {
        String pack = fontPackPath;
        if (pack.isEmpty()) {
            File def = new File(getFilesDir(), "vivo_Sans.zip");
            if (def.exists()) pack = def.getAbsolutePath();
        }
        String raw;
        try {
            raw = NativeBridge.fonts(pack);
        } catch (Throwable t) {
            toast("取字体清单失败: " + t);
            return;
        }
        final java.util.List<String> names = new java.util.ArrayList<>();
        final java.util.List<String> values = new java.util.ArrayList<>();
        String chosen = "";
        String packPath = pack;
        for (String ln : raw.split("\n")) {
            String[] f = ln.split("\t");
            if (f.length >= 2 && "CHOSEN".equals(f[0])) chosen = f[1];
            else if (f.length >= 4 && "FONT".equals(f[0])) {
                String full = f[3];
                // 第 5 段是原始名字字节(hex): C 端在安卓上没有 iconv, GBK 名字解不出来, 这里补上
                if (f.length >= 5 && f[4].length() >= 2) {
                    String dec = decodeRawName(f[4]);
                    if (dec != null && !dec.isEmpty()) full = dec;
                }
                int slash = full.lastIndexOf('/');
                String base = slash >= 0 ? full.substring(slash + 1).trim() : full.trim();
                values.add(f[1]);
                names.add(f[1] + ") " + base + "  (" + f[2] + "KB)"
                        + (f[1].equals(chosen) ? "  ← 默认" : ""));
            } else if (f.length >= 2 && "PACK".equals(f[0])) {
                packPath = f[1];
            } else if (f.length >= 2 && "ERR".equals(f[0])) {
                toast(f[1]);
                return;
            }
        }
        if (names.isEmpty()) {
            toast("这个字体包里没有可用的 .ttf");
            return;
        }
        final String packFinal = packPath;
        names.add(0, "用默认(vivo Sans Regular)");
        values.add(0, "");
        new AlertDialog.Builder(this)
                .setTitle("选择字体")
                .setItems(names.toArray(new String[0]), (d, which) -> {
                    fontPick = values.get(which);
                    fontPickName = which == 0 ? "" : names.get(which);
                    prefs.edit().putString("fontPick", fontPick)
                            .putString("fontPickName", fontPickName).apply();
                    if (!packFinal.isEmpty()) {
                        fontPackPath = packFinal;
                        prefs.edit().putString("fontPack", fontPackPath).apply();
                    }
                    updateFontPackLabel();
                    toast(fontPick.isEmpty() ? "已用默认字体" : "已选: " + fontPickName);
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private void openDrawDialog() {
        final DrawView dv = new DrawView(this);
        LinearLayout wrap = new LinearLayout(this);
        wrap.setOrientation(LinearLayout.VERTICAL);
        wrap.setPadding(dp(10), dp(10), dp(10), dp(0));
        wrap.addView(dv, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, dp(320)));
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER_VERTICAL);
        row.setPadding(0, dp(8), 0, 0);
        Button undo = new Button(this);
        undo.setText("撤销");
        undo.setAllCaps(false);
        undo.setOnClickListener(v -> dv.undo());
        Button clr = new Button(this);
        clr.setText("清空");
        clr.setAllCaps(false);
        clr.setOnClickListener(v -> dv.clearAll());
        row.addView(undo);
        row.addView(clr);
        wrap.addView(row);
        ScrollView drawScroll = new ScrollView(this);
        drawScroll.setFillViewport(true);
        drawScroll.addView(wrap);
        new AlertDialog.Builder(this)
                .setTitle("手绘(1024×1024, y 向上)")
                .setView(drawScroll)
                .setPositiveButton("确定", (d, w) -> {
                    strokesData = dv.toStrokeString();
                    if (strokesData.isEmpty()) {
                        Toast.makeText(this, "还没画东西", Toast.LENGTH_SHORT).show();
                        return;
                    }
                    if (input.getText().toString().trim().isEmpty()) input.setText("手绘");
                    doRun();
                })
                .setNegativeButton("取消", null)
                .show();
    }

    private String describe(JSONObject o) {
        String eng = "sympy".equals(o.optString("engine")) ? "SymPy" : "内置";
        return o.optString("title") + " · 引擎: " + eng;
    }

    private String lastEngineText = "";
    private String lastEngineError = "";

    private void showEngine(String engine, String version, String error) {
        boolean sympy = "sympy".equals(engine);
        lastEngineError = error == null ? "" : error;
        // 徽标上带版本号; 退回内置且探测报错时, 直接给一次提示(否则用户不知道为什么)
        if (sympy) lastEngineText = "SymPy" + (version.isEmpty() ? "" : " " + version);
        else lastEngineText = "内置";
        engineBadge.setText(lastEngineText);
        engineBadge.setVisibility(View.VISIBLE);
        if (!sympy && !lastEngineError.isEmpty() && !sympyHintShown) {
            sympyHintShown = true;
            toast("SymPy 不可用, 已退回内置引擎: " + lastEngineError + "（设置 → 后端自检 看详情）");
        }
    }

    /** 粗判是不是 LaTeX(含 \\命令): 用来识别镜像行 */
    private static boolean isLatex(String s) {
        return s != null && s.indexOf('\\') >= 0 && s.matches("(?s).*\\\\[a-zA-Z]+.*");
    }

    private static final String[] ENGINES = {"auto", "sympy", "builtin"};
    private static final String[] SCIS = {"auto", "always", "never"};
    private static final String[] INEQS = {"auto", "always", "never"};

    /** 实际生效的小数位: 自定义优先, 否则用下拉预设 */
    private int effectiveDecimals() {
        if (decimalsCustom > 0) return Math.min(decimalsCustom, DECIMALS_MAX);
        Object v = decimals.getSelectedItem();
        try {
            return Integer.parseInt(String.valueOf(v));
        } catch (Exception e) {
            return 8;
        }
    }

    private int decimalsIndex(int v) {
        for (int i = 0; i < DECIMALS.length; i++) if (DECIMALS[i].equals(String.valueOf(v))) return i;
        return 2; // 8
    }

    private Spinner dialogSpinner(String[] items, int sel) {
        Spinner sp = new Spinner(this);
        ArrayAdapter<String> ad = new ArrayAdapter<>(this, android.R.layout.simple_spinner_item, items);
        ad.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        sp.setAdapter(ad);
        sp.setSelection(Math.max(0, Math.min(sel, items.length - 1)));
        sp.setBackgroundResource(R.drawable.bg_field);
        sp.setPadding(dp(10), 0, dp(22), 0);
        return sp;
    }

    private TextView note(String text) {
        TextView t = label(text, 11.5f, R.color.text_dim);
        t.setPadding(0, dp(2), 0, dp(10));
        return t;
    }

    /** 设置: 引擎/科学计数法/只求实根 + 后端自检(桌面端的 --engine-info 等价物) */
    private void openSettings() {
        LinearLayout box = new LinearLayout(this);
        box.setOrientation(LinearLayout.VERTICAL);
        box.setPadding(dp(20), dp(6), dp(20), dp(6));

        box.addView(label("求解引擎", 13, R.color.text_dim));
        box.addView(note("自动 = 能连上内嵌 SymPy 就用它(给根式通解与 mpmath 任意精度), 否则用内置引擎"));
        final Spinner eng = dialogSpinner(new String[]{"自动（优先 SymPy）", "只用 SymPy", "只用内置"},
                engineIndex(engineSetting));
        box.addView(eng, new LinearLayout.LayoutParams(dp(210), dp(42)));

        box.addView(label("科学计数法", 13, R.color.text_dim));
        final Spinner sci = dialogSpinner(new String[]{"自动（按阈值）", "总是", "从不"},
                sciIndex(sciSetting));
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(dp(210), dp(42));
        slp.topMargin = dp(4);
        box.addView(sci, slp);

        box.addView(label("不等式的数值解集", 13, R.color.text_dim));
        box.addView(note("含周期函数的不等式只能数值求根。自动=只处理单条关系；"
                + "总是=多条也算（先求公共周期，漏解风险更高）；从不=完全不用数值方法"));
        final Spinner ineq = dialogSpinner(new String[]{"自动（单条）", "总是（含多条）", "从不"},
                ineqIndex(ineqSetting));
        LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams(dp(210), dp(42));
        ilp.topMargin = dp(4);
        box.addView(ineq, ilp);

        final CheckBox ro = checkBox("只求实根（解方程时忽略复根）");
        ro.setChecked(realOnly);
        ro.setPadding(0, dp(12), 0, 0);
        box.addView(ro);

        box.addView(label("自定义小数位", 13, R.color.text_dim));
        box.addView(note("留空 = 用主界面的下拉预设。可以手填超过 200（上限 " + DECIMALS_MAX
                + "）：200 以内走原来的算法，超过 200 走高精度特殊算法；\n"
                + "本机精度不够时会自动钳到能精确给出的最大位数并提示。位数越高越慢，长计算可随时「打断」"));
        final EditText decEdit = new EditText(this);
        decEdit.setInputType(InputType.TYPE_CLASS_NUMBER);
        decEdit.setText(decimalsCustom > 0 ? String.valueOf(decimalsCustom) : "");
        decEdit.setHint("例如 60");
        decEdit.setBackgroundResource(R.drawable.bg_input);
        decEdit.setPadding(dp(10), dp(8), dp(10), dp(8));
        box.addView(decEdit, new LinearLayout.LayoutParams(dp(150),
                ViewGroup.LayoutParams.WRAP_CONTENT));

        box.addView(label("字形模式: 字号", 13, R.color.text_dim));
        box.addView(note("输出坐标的单位大小(默认 1000, 与桌面端 --size 一致)"));
        final EditText sizeEdit = new EditText(this);
        sizeEdit.setInputType(InputType.TYPE_CLASS_NUMBER);
        sizeEdit.setText(String.valueOf(glyphSize));
        sizeEdit.setBackgroundResource(R.drawable.bg_input);
        sizeEdit.setPadding(dp(10), dp(8), dp(10), dp(8));
        box.addView(sizeEdit, new LinearLayout.LayoutParams(dp(150),
                ViewGroup.LayoutParams.WRAP_CONTENT));

        box.addView(label("字形模式: 拟合容差(字体单位)", 13, R.color.text_dim));
        box.addView(note("默认 0.5: 越小越贴合原字(函数更多), 越大函数越少; "
                + "无论多大都不会比字体自带分段更多"));
        final EditText tolEdit = new EditText(this);
        tolEdit.setInputType(InputType.TYPE_CLASS_NUMBER | InputType.TYPE_NUMBER_FLAG_DECIMAL);
        tolEdit.setText(String.valueOf(glyphTol));
        tolEdit.setBackgroundResource(R.drawable.bg_input);
        tolEdit.setPadding(dp(10), dp(8), dp(10), dp(8));
        box.addView(tolEdit, new LinearLayout.LayoutParams(dp(150),
                ViewGroup.LayoutParams.WRAP_CONTENT));

        // 设置项已经很多, 小屏会超出屏幕 —— 必须套一层滚动容器, 否则"设置不可滑动"
        ScrollView settingsScroll = new ScrollView(this);
        settingsScroll.setFillViewport(true);
        settingsScroll.addView(box);
        new AlertDialog.Builder(this)
                .setTitle("设置")
                .setView(settingsScroll)
                .setPositiveButton("保存", (d, w) -> {
                    engineSetting = ENGINES[eng.getSelectedItemPosition()];
                    sciSetting = SCIS[sci.getSelectedItemPosition()];
                    ineqSetting = INEQS[ineq.getSelectedItemPosition()];
                    realOnly = ro.isChecked();
                    String custom = decEdit.getText().toString().trim();
                    int customVal = 0;
                    if (!custom.isEmpty()) {
                        try {
                            customVal = Integer.parseInt(custom);
                        } catch (Exception e) {
                            customVal = -1;
                        }
                        if (customVal < 0 || customVal > DECIMALS_MAX) {
                            toast("小数位需在 0~" + DECIMALS_MAX + " 之间");
                            return;
                        }
                    }
                    decimalsCustom = customVal;
                    int gs = glyphSize;
                    double gt = glyphTol;
                    try {
                        String sv = sizeEdit.getText().toString().trim();
                        if (!sv.isEmpty()) gs = Integer.parseInt(sv);
                        String tv = tolEdit.getText().toString().trim();
                        if (!tv.isEmpty()) gt = Double.parseDouble(tv);
                    } catch (Exception e) {
                        toast("字号/容差填得不对");
                        return;
                    }
                    if (gs < 1 || gs > 1000000) {
                        toast("字号需在 1~1000000 之间");
                        return;
                    }
                    if (!(gt > 0) || gt > 1000) {
                        toast("拟合容差需在 0~1000 之间");
                        return;
                    }
                    glyphSize = gs;
                    glyphTol = gt;
                    prefs.edit().putString("engine", engineSetting)
                            .putString("scientific", sciSetting)
                            .putString("numericInequality", ineqSetting)
                            .putBoolean("realOnly", realOnly)
                            .putInt("decimalsCustom", decimalsCustom)
                            .putInt("glyphSize", glyphSize)
                            .putFloat("glyphTol", (float) glyphTol).apply();
                    setStatus("设置已保存：小数位 " + effectiveDecimals()
                            + (decimalsCustom > 0 ? "（自定义）" : "（预设）"), R.color.ok);
                })
                .setNeutralButton("后端自检", (d, w) -> runSelfCheck())
                .setNegativeButton("取消", null)
                .show();
    }

    private int engineIndex(String v) {
        for (int i = 0; i < ENGINES.length; i++) if (ENGINES[i].equals(v)) return i;
        return 0;
    }

    private int ineqIndex(String v) {
        for (int i = 0; i < INEQS.length; i++) if (INEQS[i].equals(v)) return i;
        return 0;
    }

    private int sciIndex(String v) {
        for (int i = 0; i < SCIS.length; i++) if (SCIS[i].equals(v)) return i;
        return 0;
    }

    /** 后端自检放到后台线程(首次启动内嵌 Python 可能要几秒), 免得卡住界面 */
    private void runSelfCheck() {
        setStatus("正在自检…", R.color.warn);
        pool.execute(() -> {
            String t;
            try {
                t = NativeBridge.info();
            } catch (Throwable e) {
                t = "自检失败: " + e;
            }
            final String text = t;
            ui.post(() -> {
                setStatus("自检完成", R.color.ok);
                new AlertDialog.Builder(this).setTitle("后端自检").setMessage(text)
                        .setPositiveButton("好", null).show();
            });
        });
    }

    private void showEngineDetails() {
        StringBuilder m = new StringBuilder("当前引擎: ").append(lastEngineText);
        if (!lastEngineError.isEmpty()) m.append("\n\nSymPy 探测失败原因: ").append(lastEngineError);
        m.append("\n\n「设置 → 后端自检」可看完整后端信息。");
        new AlertDialog.Builder(this).setTitle("引擎").setMessage(m.toString())
                .setPositiveButton("好", null).show();
    }

    private void hideKeyboard() {
        InputMethodManager imm = (InputMethodManager) getSystemService(INPUT_METHOD_SERVICE);
        if (imm != null) imm.hideSoftInputFromWindow(input.getWindowToken(), 0);
    }

    private void showPlaceholder() {
        int mi = 2;
        for (int i = 0; i < MODES.length; i++) if (MODES[i].equals(mode)) mi = i;
        StringBuilder b = new StringBuilder();
        b.append("<div class=\"empty\">输入式子后点 <b>计算</b><br>");
        for (String eg : EXAMPLES[mi]) b.append("<span class=\"eg\">").append(esc(eg)).append("</span>");
        b.append("</div>");
        result.loadDataWithBaseURL("file:///android_asset/", page(b.toString()), "text/html",
                "utf-8", null);
    }

    // ==================== 结果渲染 ====================

    /** 把结果渲染成 HTML(KaTeX 排公式) 或纯文本 */
    private void render(String json, String fallback) {
        StringBuilder html = new StringBuilder();
        StringBuilder plain = new StringBuilder();
        // 报告里除了正文行, 还有一条"LaTeX 镜像行"(桌面端直接给终端看的), 例如
        // 正文 "√5 = √5" 的镜像行本身写的是 "\sqrt{5} = \sqrt{5}"。
        // 旧版把它当正文原样显示, 于是界面上出现大量 \frac \sqrt 之类的源码。
        java.util.Set<String> seenLatex = new java.util.HashSet<>();
        try {
            JSONObject o = new JSONObject(json);
            previewSvg = o.optString("svg", "");
            JSONArray secs = o.optJSONArray("sections");
            if (secs != null) {
                for (int i = 0; i < secs.length(); i++) {
                    JSONObject s = secs.getJSONObject(i);
                    String chan = s.optString("chan");
                    if ("input".equals(chan) || "banner".equals(chan) || "tip".equals(chan)) continue;
                    String title = s.optString("title");
                    if (!title.isEmpty()) {
                        html.append("<h3>").append(esc(title)).append("</h3>");
                        plain.append("\n【").append(title).append("】\n");
                    }
                    JSONArray lines = s.optJSONArray("lines");
                    if (lines == null) continue;
                    boolean mirrorChan = "latex".equals(chan);
                    for (int k = 0; k < lines.length(); k++) {
                        JSONObject ln = lines.getJSONObject(k);
                        String p = ln.optString("plain");
                        String l = ln.optString("latex", p);
                        plain.append(p).append('\n');
                        if (l == null) l = p;
                        boolean latexLike = isLatex(l);
                        if (mirrorChan && seenLatex.contains(l)) continue; // 已经渲染过同样的公式
                        if (!mirrorChan && latexLike) seenLatex.add(l);
                        // plain 本身就是 LaTeX(镜像行)时按公式渲染, 不要再当等宽文本打出来
                        if (l.isEmpty() || (l.equals(p) && !latexLike)) {
                            html.append("<div class=\"pl\">").append(esc(p)).append("</div>");
                        } else {
                            html.append("<div class=\"math\">$$").append(esc(l)).append("$$</div>");
                        }
                    }
                }
            }
        } catch (Exception e) {
            html.append("<div class=\"pl\">").append(esc(fallback)).append("</div>");
        }
        // 字形模式: 把 C++ 侧生成的 SVG 预览贴到结果后面(自包含, 不依赖外部资源)
        if (previewSvg != null && !previewSvg.isEmpty()) {
            html.append("<h3>预览</h3><div class=\"glyph-preview\">").append(previewSvg).append("</div>");
        }
        lastPlain = plain.toString().trim();
        hasResult = true;
        // 导出用的 HTML 交给 onPageFinished 抓取(renderedBody); 这里只负责显示
        if (plainOnly != null && plainOnly.isChecked()) {
            result.loadDataWithBaseURL("file:///android_asset/",
                    page("<pre>" + esc(lastPlain) + "</pre>"), "text/html", "utf-8", null);
            return;
        }
        result.loadDataWithBaseURL("file:///android_asset/", page(html.toString()), "text/html",
                "utf-8", null);
    }

    /** evaluateJavascript 返回的是 JSON 字符串字面量, 这里解码成真正的 HTML 文本 */
    private static String jsToHtml(String jsValue) {
        if (jsValue == null || jsValue.isEmpty() || "null".equals(jsValue)) return "";
        try {
            Object o = new JSONTokener(jsValue).nextValue();
            String html = o == null ? "" : o.toString();
            // 页面里的渲染脚本也在这段正文里, 导出时去掉(导出的文件不需要再跑 JS)
            return html.replaceAll("(?s)<script.*?</script>", "").trim();
        } catch (Exception e) {
            return "";
        }
    }

    private String readAsset(String path) {
        try (InputStream in = getAssets().open(path)) {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
            return new String(bos.toByteArray(), StandardCharsets.UTF_8);
        } catch (Exception e) {
            return "";
        }
    }

    /** 把 katex.min.css 里 url(fonts/X.woff2) 换成 data URI, 并丢掉 woff/ttf 备用项 */
    private String inlineFonts(String css) {
        java.util.regex.Matcher m = java.util.regex.Pattern
                .compile("src:url\\(fonts/([^)]+?\\.woff2)\\)\\s*format\\(\"[^\"]+\"\\)"
                        + "(?:\\s*,\\s*url\\([^)]*\\)\\s*format\\(\"[^\"]+\"\\))*")
                .matcher(css);
        StringBuffer out = new StringBuffer();
        while (m.find()) {
            String data = assetDataUri("katex/fonts/" + m.group(1), "font/woff2");
            m.appendReplacement(out, java.util.regex.Matcher.quoteReplacement(
                    data.isEmpty() ? m.group(0) : "src:url(" + data + ") format(\"woff2\")"));
        }
        m.appendTail(out);
        return out.toString();
    }

    private String assetDataUri(String path, String mime) {
        try (InputStream in = getAssets().open(path)) {
            ByteArrayOutputStream bos = new ByteArrayOutputStream();
            byte[] buf = new byte[16384];
            int n;
            while ((n = in.read(buf)) > 0) bos.write(buf, 0, n);
            return "data:" + mime + ";base64," + Base64.encodeToString(bos.toByteArray(), Base64.NO_WRAP);
        } catch (Exception e) {
            return "";
        }
    }

    /** 导出用的完整样式: result.css + 内联字体的 KaTeX CSS(自带字体, 离线也能正常显示) */
    private String inlineCss() {
        if (!inlineCssCache.isEmpty()) return inlineCssCache;
        inlineCssCache = readAsset("result.css") + "\n" + inlineFonts(readAsset("katex/katex.min.css"));
        return inlineCssCache;
    }

    /** RAWHEX -> 可读名字: 先当 UTF-8, 出现替换字符(U+FFFD)说明不是 UTF-8, 再按 GBK 解 */
    private static String decodeRawName(String hex) {
        try {
            int n = hex.length() / 2;
            byte[] b = new byte[n];
            for (int i = 0; i < n; ++i)
                b[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
            String u = new String(b, "UTF-8");
            if (u.indexOf('\uFFFD') < 0) return u;
            return new String(b, "GBK");
        } catch (Exception e) {
            return null;
        }
    }

    private static String esc(String s) {
        if (s == null) return "";
        return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;");
    }

    /** 结果页: 样式在 assets/result.css(与预览截图共用同一份) */
    private String page(String body) {
        return "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
                + "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                + "<link rel=\"stylesheet\" href=\"katex/katex.min.css\">"
                + "<link rel=\"stylesheet\" href=\"result.css\">"
                + "<script src=\"katex/katex.min.js\"></script>"
                + "<script src=\"katex/auto-render.min.js\"></script>"
                + "</head><body" + (isNight() ? " class=\"dark\"" : "") + ">" + body
                + "{left:'$',right:'$',display:false}],throwOnError:false});}catch(e){}</script>"
                + "<script>window.__EM_RENDER_DONE=false;"
                + "{left:'$',right:'$',display:false}],throwOnError:false});}catch(e){}"
                + "<script>window.__EM_RENDER_DONE=false;"
                + "try{renderMathInElement(document.body,{delimiters:["
                + "{left:'$$',right:'$$',display:true},{left:'\\[',right:'\\]',display:true},"
                + "{left:'$',right:'$',display:false}],throwOnError:false});}catch(e){}"
                + "requestAnimationFrame(function(){window.__EM_RENDER_DONE=true;});</script>"
                + "</body></html>";
    }

    // ==================== 保存 / 分享 ====================

    /** 导出"自包含"HTML: 公式已渲染 + 样式与字体都内联, 发给别人/离线打开都能正常显示 */
    private void saveHtml() {
        if (!hasResult || renderedBody.isEmpty()) {
            toast("先点一次「计算」");
            return;
        }
        try {
            File dir = getExternalFilesDir("output");
            if (dir == null) dir = getFilesDir();
            if (!dir.exists() && !dir.mkdirs()) throw new Exception("无法创建目录");
            File f = new File(dir, "EasyMath-" + System.currentTimeMillis() + ".html");
            String doc = "<!DOCTYPE html><html lang=\"zh\"><head><meta charset=\"utf-8\">"
                    + "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                    + "<title>EasyMath 结果</title><style>" + inlineCss() + "</style></head>"
                    + "<body" + (isNight() ? " class=\"dark\"" : "") + ">" + renderedBody
                    + "</body></html>";
            try (FileOutputStream fos = new FileOutputStream(f)) {
                fos.write(doc.getBytes(StandardCharsets.UTF_8));
            }
            toast("已保存(自包含): " + f.getAbsolutePath());
        } catch (Exception e) {
            toast("保存失败: " + e);
        }
    }

    private void sharePlain() {
        if (lastPlain.isEmpty()) {
            toast("先点一次「计算」");
            return;
        }
        Intent it = new Intent(Intent.ACTION_SEND);
        it.setType("text/plain");
        it.putExtra(Intent.EXTRA_TEXT, lastPlain);
        startActivity(Intent.createChooser(it, "分享结果"));
    }

    private void toast(String s) {
        Toast.makeText(this, s, Toast.LENGTH_SHORT).show();
    }

    @Override
    protected void onDestroy() {
        pool.shutdownNow();
        super.onDestroy();
    }
}
