package com.easymath.app;

import android.content.Context;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;
import android.widget.ScrollView;

/**
 * 整页滚动容器, 但把"落在结果区内、且结果自己还能滚"的竖向手势让给结果区(WebView)。
 *
 * 为什么需要它: 框架的 ScrollView 不是嵌套滚动父容器(那是 AndroidX NestedScrollView 的能力),
 * 默认会在超过 touch slop 时抢走手势, 于是 WebView 永远滚不动; 反过来若整页不可滚,
 * 小屏/键盘弹出时上方表单又够不到。这里按"手势起点是否落在结果区" + "结果区在该方向
 * 是否还能滚"来决定谁处理:
 *   - 落在结果区 且 还能滚  -> 不拦截, 交给 WebView(内容单独滚动)
 *   - 落在结果区 但 已到边缘 -> 交给整页滚动(整体跟着动, 手势可继续)
 *   - 落在其它区域        -> 整页滚动
 * 命中判定用屏幕坐标(内容滚动后子视图的 top/bottom 会变, 屏幕坐标最稳), 边界取闭区间。
 */
public class PageScrollView extends ScrollView {

    private View inner;          // 结果区 WebView
    private float downY;
    private final int slop;

    public PageScrollView(Context ctx) {
        super(ctx);
        slop = ViewConfiguration.get(ctx).getScaledTouchSlop();
    }

    /** 指定"内部可滚动视图"(结果区 WebView) */
    public void setInnerScrollable(View v) {
        inner = v;
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                downY = e.getY();
                break;
            case MotionEvent.ACTION_MOVE: {
                float dy = e.getY() - downY;
                if (Math.abs(dy) > slop && inner != null && hitTest(inner, e)) {
                    // 手指下移 => 内容向上滚 => 检查"能否向上滚"
                    int dir = dy > 0 ? -1 : 1;
                    if (inner.canScrollVertically(dir)) {
                        return false; // 让 WebView 处理这次手势
                    }
                }
                break;
            }
            default:
                break;
        }
        return super.onInterceptTouchEvent(e);
    }

    /** 事件坐标(相对本 View)转屏幕坐标后, 判断是否落在 v 的范围内(含边界) */
    private boolean hitTest(View v, MotionEvent e) {
        int[] self = new int[2];
        int[] target = new int[2];
        getLocationOnScreen(self);
        v.getLocationOnScreen(target);
        float x = e.getX() + self[0];
        float y = e.getY() + self[1];
        return x >= target[0] && x <= target[0] + v.getWidth()
                && y >= target[1] && y <= target[1] + v.getHeight();
    }
}
