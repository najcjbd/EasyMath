package com.easymath.app;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.view.MotionEvent;
import android.view.View;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * 手绘方格: 内部用 1024×1024 的高分辨率坐标(y 向上, 左下角是原点),
 * 画完把笔画中心线交给 C++ 侧拟合函数(与文字模式同一套还原逻辑)。
 * 支持多指同时画、撤销、清空。
 */
public class DrawView extends View {
    public static final int GRID = 1024;   // 内部坐标分辨率(越高越细腻)

    private final List<List<float[]>> strokes = new ArrayList<>();
    private final Map<Integer, List<float[]>> live = new HashMap<>();
    private final Paint pen = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint grid = new Paint();
    private final Paint bg = new Paint();

    public DrawView(Context ctx) {
        super(ctx);
        pen.setStyle(Paint.Style.STROKE);
        pen.setStrokeWidth(6f);
        pen.setStrokeCap(Paint.Cap.ROUND);
        pen.setStrokeJoin(Paint.Join.ROUND);
        pen.setColor(0xFF1F6FEB);
        grid.setStyle(Paint.Style.STROKE);
        grid.setStrokeWidth(1f);
        grid.setColor(0x22000000);
        bg.setStyle(Paint.Style.FILL);
        bg.setColor(0xFFFFFFFF);
        setLayerType(View.LAYER_TYPE_SOFTWARE, null); // 结果可被截图/导出一致
    }

    public void clearAll() {
        strokes.clear();
        live.clear();
        invalidate();
    }

    public void undo() {
        if (!strokes.isEmpty()) strokes.remove(strokes.size() - 1);
        invalidate();
    }

    public boolean isEmpty() {
        return strokes.isEmpty() && live.isEmpty();
    }

    public int strokeCount() {
        return strokes.size();
    }

    /** 转成 C++ 侧吃的格式: "x,y;x,y;…|x,y;…" (1024 坐标, y 向上) */
    public String toStrokeString() {
        StringBuilder sb = new StringBuilder();
        for (List<float[]> st : strokes) {
            if (st.size() < 2) continue;
            if (sb.length() > 0) sb.append('|');
            for (int i = 0; i < st.size(); i++) {
                float[] p = st.get(i);
                if (i > 0) sb.append(';');
                sb.append(String.format(Locale.US, "%.1f,%.1f", p[0], p[1]));
            }
        }
        return sb.toString();
    }

    private float[] mapPoint(float vx, float vy) {
        float w = Math.max(1, getWidth()), h = Math.max(1, getHeight());
        return new float[]{vx / w * GRID, GRID - vy / h * GRID};
    }

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);
        c.drawRect(0, 0, getWidth(), getHeight(), bg);
        int cells = 8;
        for (int i = 1; i < cells; i++) {
            float x = getWidth() * i / (float) cells;
            float y = getHeight() * i / (float) cells;
            c.drawLine(x, 0, x, getHeight(), grid);
            c.drawLine(0, y, getWidth(), y, grid);
        }
        c.drawRect(1, 1, getWidth() - 1, getHeight() - 1, grid);
        for (List<float[]> st : strokes) drawStroke(c, st);
        for (List<float[]> st : live.values()) drawStroke(c, st);
    }

    private void drawStroke(Canvas c, List<float[]> st) {
        if (st.size() < 2) {
            if (st.size() == 1) {
                float[] p = st.get(0);
                float w = Math.max(1, getWidth()), h = Math.max(1, getHeight());
                c.drawPoint(p[0] / GRID * w, (GRID - p[1]) / GRID * h, pen);
            }
            return;
        }
        Path path = new Path();
        float w = Math.max(1, getWidth()), h = Math.max(1, getHeight());
        for (int i = 0; i < st.size(); i++) {
            float[] p = st.get(i);
            float x = p[0] / GRID * w, y = (GRID - p[1]) / GRID * h;
            if (i == 0) path.moveTo(x, y);
            else path.lineTo(x, y);
        }
        c.drawPath(path, pen);
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int idx = e.getActionIndex();
                int id = e.getPointerId(idx);
                List<float[]> st = new ArrayList<>();
                st.add(mapPoint(e.getX(idx), e.getY(idx)));
                live.put(id, st);
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    List<float[]> st = live.get(e.getPointerId(i));
                    if (st == null) continue;
                    float[] p = mapPoint(e.getX(i), e.getY(i));
                    float[] last = st.get(st.size() - 1);
                    // 抖动的点不记(降采样), 让"杂线"从源头上少一点
                    if (Math.hypot(p[0] - last[0], p[1] - last[1]) < 4) continue;
                    st.add(p);
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int idx = e.getActionIndex();
                int id = e.getPointerId(idx);
                List<float[]> st = live.remove(id);
                if (st != null) {
                    if (st.size() == 1) st.add(mapPoint(e.getX(idx), e.getY(idx) + 1));
                    if (st.size() >= 2) strokes.add(st);
                }
                invalidate();
                return true;
            }
            case MotionEvent.ACTION_CANCEL:
                live.clear();
                invalidate();
                return true;
            default:
                return super.onTouchEvent(e);
        }
    }
}
