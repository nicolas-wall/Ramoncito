// =============================================================
//  face.cpp — Motor de cara y expresiones para espToy
//  Plataforma : Seeed XIAO ESP32-S3
//  Display    : OLED SSD1309 128×64, librería U8g2
//  Referencia : docs/03-EXPRESIONES.md
// =============================================================

#include "face.h"
#include "config.h"   // EASING_EXPRESION, EASING_MIRADA, PARPADEO_*, MIRADA_*
#include <math.h>     // sinf

// ----------------------------------------------------------------
//  Instancia global
// ----------------------------------------------------------------
Face face;

// ----------------------------------------------------------------
//  Tabla de expresiones (doc 03 §2)
//  Índice = valor del enum Expression (uint8_t).
//  Campos: cx, cy, w, h, r, pTop, pBot, slopeTop, slopeBot, offX, offY
//
//  Ojo izquierdo: cx ≈ 38   Ojo derecho: cx ≈ 90   cy ≈ 35
//  Base: w=28, h=22, r=6
//
//  NOTA: offX/offY se fijan en 0 aquí; los controla la mirada errante.
// ----------------------------------------------------------------
struct ExprDef {
    // Ojo izquierdo
    float lPTop, lPBot, lSlopeTop, lSlopeBot;
    // Ojo derecho
    float rPTop, rPBot, rSlopeTop, rSlopeBot;
    // Forma compartida
    float w, h, r;
};

// Orden: NEUTRAL, FELIZ, TRISTE, ENOJADO, SORPRENDIDO,
//        ABURRIDO, DORMIDO, SOSPECHOSO, AMOR, GUINO
static const ExprDef EXPR_TABLE[10] = {
    // NEUTRAL   pTop  pBot  sTop  sBot | pTop  pBot  sTop  sBot |  w    h    r
    {             0,   0,    0,    0,     0,   0,    0,    0,     28,  22,   6 },
    // FELIZ      párpado INFERIOR alto → medialunas hacia arriba "^^"
    //            (con pTop alto se leía como ojos entrecerrados/dormilones)
    {             0,   9,    0,    0,     0,   9,    0,    0,     28,  20,  10 },
    // TRISTE     pTop=3, slopeTop izq=-4, der=+4
    {             3,   0,   -4,    0,     3,   0,   +4,    0,     26,  20,   5 },
    // ENOJADO    pTop=7, slopeTop izq=+5, der=-5
    {             7,   0,   +5,    0,     7,   0,   -5,    0,     28,  20,   4 },
    // SORPRENDIDO  ojos más grandes, sin párpados
    {             0,   0,    0,    0,     0,   0,    0,    0,     30,  28,  10 },
    // ABURRIDO   pTop=9, pBot=2
    {             9,   2,    0,    0,     9,   2,    0,    0,     28,  18,   6 },
    // DORMIDO    ojo casi cerrado h=4; pTop/pBot altos para taparlo del todo
    {            11,  11,    0,    0,    11,  11,    0,    0,     28,   4,   2 },
    // SOSPECHOSO ojo izq entrecerrado (pTop=5), der entrecerrado más (pTop=10)
    {             5,   0,   +3,    0,    10,   0,   -3,    0,     28,  22,   5 },
    // AMOR/MIMO  ojo casi circular w=h=26, r=13
    {             0,   0,    0,    0,     0,   0,    0,    0,     26,  26,  13 },
    // GUINO      ojo izq cerrado (párpados que colapsan), der feliz "^"
    {            10,  10,    0,    0,     0,   9,    0,    0,     28,  22,   9 },
};

// ----------------------------------------------------------------
//  Posiciones fijas de los ojos (centro base)
// ----------------------------------------------------------------
static const float EYE_LEFT_CX  = 38.0f;
static const float EYE_RIGHT_CX = 90.0f;
static const float EYE_CY       = 35.0f;

// ----------------------------------------------------------------
//  Helpers de clamping
// ----------------------------------------------------------------
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ----------------------------------------------------------------
//  Carga los EyeParams destino para la expresión dada
// ----------------------------------------------------------------
static void exprToTargets(Expression e,
                          EyeParams &lt, EyeParams &rt)
{
    uint8_t idx = (uint8_t)e;
    if (idx >= 10) idx = 0;
    const ExprDef &d = EXPR_TABLE[idx];

    lt.cx       = EYE_LEFT_CX;
    lt.cy       = EYE_CY;
    lt.w        = d.w;
    lt.h        = d.h;
    lt.r        = d.r;
    lt.pTop     = d.lPTop;
    lt.pBot     = d.lPBot;
    lt.slopeTop = d.lSlopeTop;
    lt.slopeBot = d.lSlopeBot;
    lt.offX     = 0.0f;
    lt.offY     = 0.0f;

    rt.cx       = EYE_RIGHT_CX;
    rt.cy       = EYE_CY;
    rt.w        = d.w;
    rt.h        = d.h;
    rt.r        = d.r;
    rt.pTop     = d.rPTop;
    rt.pBot     = d.rPBot;
    rt.slopeTop = d.rSlopeTop;
    rt.slopeBot = d.rSlopeBot;
    rt.offX     = 0.0f;
    rt.offY     = 0.0f;
}

// ----------------------------------------------------------------
//  Face::lerpEye  (ease-out: cur += (tgt-cur)*t)
//  offX/offY NO se interpolan aquí; los controla la mirada errante
// ----------------------------------------------------------------
void Face::lerpEye(EyeParams &cur, const EyeParams &tgt, float t)
{
    cur.cx       += (tgt.cx       - cur.cx)       * t;
    cur.cy       += (tgt.cy       - cur.cy)       * t;
    cur.w        += (tgt.w        - cur.w)        * t;
    cur.h        += (tgt.h        - cur.h)        * t;
    cur.r        += (tgt.r        - cur.r)        * t;
    cur.pTop     += (tgt.pTop     - cur.pTop)     * t;
    cur.pBot     += (tgt.pBot     - cur.pBot)     * t;
    cur.slopeTop += (tgt.slopeTop - cur.slopeTop) * t;
    cur.slopeBot += (tgt.slopeBot - cur.slopeBot) * t;
    // offX/offY se manejan por separado
}

// ----------------------------------------------------------------
//  Face::scheduleNextBlink  —  programa el próximo parpadeo
// ----------------------------------------------------------------
void Face::scheduleNextBlink(uint32_t now)
{
    uint32_t rango = PARPADEO_MAX_MS - PARPADEO_MIN_MS;
    _blinkNextMs = now + PARPADEO_MIN_MS + (uint32_t)(random((long)rango));
}

// ----------------------------------------------------------------
//  Face::scheduleNextGaze  —  programa el próximo cambio de mirada
// ----------------------------------------------------------------
void Face::scheduleNextGaze(uint32_t now)
{
    uint32_t rango = MIRADA_MAX_MS - MIRADA_MIN_MS;
    _gazeNextMs = now + MIRADA_MIN_MS + (uint32_t)(random((long)rango));
}

// ----------------------------------------------------------------
//  Face::begin  —  estado inicial NEUTRAL
// ----------------------------------------------------------------
void Face::begin()
{
    _expr = Expression::NEUTRAL;

    exprToTargets(_expr, _leftTgt, _rightTgt);

    // Arrancar el current igual al target (sin transición inicial)
    _leftCur  = _leftTgt;
    _rightCur = _rightTgt;

    _gazeOffX = 0.0f;
    _gazeOffY = 0.0f;
    _gazeTgtX = 0.0f;
    _gazeTgtY = 0.0f;
    _breathPhase = 0.0f;

    _blinkState = BlinkState::IDLE;
    _blinkFrame = 0;

    uint32_t now = millis();
    scheduleNextBlink(now);
    scheduleNextGaze(now);
}

// ----------------------------------------------------------------
//  Face::setExpression
// ----------------------------------------------------------------
void Face::setExpression(Expression e)
{
    _expr = e;
    exprToTargets(e, _leftTgt, _rightTgt);
    // offX/offY del target no se tocan; la mirada errante los maneja
}

// ----------------------------------------------------------------
//  Face::expression
// ----------------------------------------------------------------
Expression Face::expression() const
{
    return _expr;
}

// ----------------------------------------------------------------
//  Face::update  —  llamar una vez por frame (now = millis())
// ----------------------------------------------------------------
void Face::update(uint32_t now)
{
    // --- 1. Interpolación expresión (ease-out) -------------------
    lerpEye(_leftCur,  _leftTgt,  EASING_EXPRESION);
    lerpEye(_rightCur, _rightTgt, EASING_EXPRESION);

    // --- 2. Mirada errante --------------------------------------
    if (now >= _gazeNextMs) {
        // Nuevo offset aleatorio dentro de ±MIRADA_RANGO_PX
        float rango = MIRADA_RANGO_PX * 2.0f;
        _gazeTgtX = -MIRADA_RANGO_PX + (float)(random((long)(rango * 100))) / 100.0f;
        _gazeTgtY = -MIRADA_RANGO_PX + (float)(random((long)(rango * 100))) / 100.0f;
        scheduleNextGaze(now);
    }
    // Interpolar suavemente hacia el offset objetivo
    _gazeOffX += (_gazeTgtX - _gazeOffX) * EASING_MIRADA;
    _gazeOffY += (_gazeTgtY - _gazeOffY) * EASING_MIRADA;

    // Aplicar offset de mirada a los parámetros actuales
    _leftCur.offX  = _gazeOffX;
    _leftCur.offY  = _gazeOffY;
    _rightCur.offX = _gazeOffX;
    _rightCur.offY = _gazeOffY;

    // --- 3. Micro-movimiento senoidal (respiración) -------------
    // ≈0.3 Hz → avanza ~0.063 rad/frame a 30 fps (2π × 0.3 / 30)
    _breathPhase += 0.063f;
    if (_breathPhase > 6.2832f) _breathPhase -= 6.2832f;
    float breathOff = sinf(_breathPhase) * 1.0f;  // ±1 px
    _leftCur.offY  += breathOff;
    _rightCur.offY += breathOff;

    // --- 4. Parpadeo --------------------------------------------
    // Suprimir parpadeo si la expresión es DORMIDO
    bool dormido = (_expr == Expression::DORMIDO);

    if (!dormido) {
        switch (_blinkState) {

        case BlinkState::IDLE:
            if (now >= _blinkNextMs) {
                _blinkState = BlinkState::CLOSING;
                _blinkFrame = 0;
            }
            break;

        case BlinkState::CLOSING:
            // Frames 0-1: llevar pTop a h*0.9
            {
                float hBase = _leftTgt.h;   // alto del ojo actual
                float pCierre = hBase * 0.9f;
                _leftCur.pTop  = pCierre;
                _rightCur.pTop = pCierre;
                _blinkFrame++;
                if (_blinkFrame >= 2) {
                    _blinkState = BlinkState::CLOSED;
                    _blinkFrame = 0;
                }
            }
            break;

        case BlinkState::CLOSED:
            // Frame 2: cerrado completo
            {
                float hBase = _leftTgt.h;
                _leftCur.pTop  = hBase;
                _rightCur.pTop = hBase;
                _blinkFrame++;
                if (_blinkFrame >= 1) {
                    _blinkState = BlinkState::OPENING;
                    _blinkFrame = 0;
                }
            }
            break;

        case BlinkState::OPENING:
            // Frames 3-4: retornar pTop al valor objetivo
            _leftCur.pTop  = _leftTgt.pTop;
            _rightCur.pTop = _rightTgt.pTop;
            _blinkFrame++;
            if (_blinkFrame >= 2) {
                _blinkState = BlinkState::IDLE;
                _blinkFrame = 0;
                scheduleNextBlink(now);
            }
            break;
        }
    }
}

// ----------------------------------------------------------------
//  Face::drawEye  —  dibuja UN ojo con sus párpados
//
//  Estrategia de párpados (doc 03 §1.2):
//    1. Dibujar rBox blanco (cuerpo del ojo)
//    2. Párpado superior negro: rect + triángulo para la pendiente
//    3. Párpado inferior negro: rect + triángulo para la pendiente
//    4. Restaurar color a 1 (blanco)
//
//  Geometría del párpado superior (ejemplo, slope positivo = izq más alto):
//
//    El rectángulo de párpado cubre [x0, x1] × [eyeTop, eyeTop+pTop].
//    El triángulo de pendiente tapa el ángulo inclinado del borde inferior
//    del párpado (la diagonal entre la esquina izquierda y derecha).
//
//    slopeTop > 0 → borde izquierdo del párpado inferior más arriba:
//       pto izq del borde inferior está en y = eyeTop + pTop
//       pto der del borde inferior está en y = eyeTop + pTop - slopeTop
//    El área triangular extra tapa la zona entre la línea horizontal
//    y la diagonal inclinada.
// ----------------------------------------------------------------
void Face::drawEye(U8G2 &u8, const EyeParams &p)
{
    // Clamping seguro antes de convertir a enteros
    float w    = clampf(p.w, 4.0f, 60.0f);
    float h    = clampf(p.h, 2.0f, 40.0f);
    float r    = clampf(p.r, 0.0f, (w < h ? w : h) * 0.5f);
    float pTop = clampf(p.pTop, 0.0f, h);
    float pBot = clampf(p.pBot, 0.0f, h - pTop);  // no solapar

    // Posición de la esquina superior izquierda del ojo
    float eyeX = p.cx + p.offX - w * 0.5f;
    float eyeY = p.cy + p.offY - h * 0.5f;

    int16_t ix  = (int16_t)eyeX;
    int16_t iy  = (int16_t)eyeY;
    int16_t iw  = (int16_t)w;
    int16_t ih  = (int16_t)h;
    int16_t ir  = (int16_t)r;

    // Asegurar dimensiones mínimas para drawRBox
    if (iw < 1) iw = 1;
    if (ih < 1) ih = 1;
    if (ir < 0) ir = 0;
    // r no puede superar min(w,h)/2 para drawRBox
    int16_t rmax = (iw < ih ? iw : ih) / 2;
    if (ir > rmax) ir = rmax;

    // 1. Cuerpo del ojo: rBox blanco
    u8.setDrawColor(1);
    u8.drawRBox((u8g2_uint_t)ix, (u8g2_uint_t)iy,
                (u8g2_uint_t)iw, (u8g2_uint_t)ih,
                (u8g2_uint_t)ir);

    // 2. Párpado superior (si hay algo que tapar)
    if (pTop > 0.5f) {
        u8.setDrawColor(0);  // negro

        int16_t ptop = (int16_t)pTop;

        // Borde izquierdo Y del párpado inferior (donde termina el párpado)
        // slopeTop > 0 → izquierda más alta (Y menor), derecha más baja
        // yLeft  = eyeTop + pTop          (y base del borde inferior izq)
        // yRight = eyeTop + pTop - slope  (borde inferior der, subido)
        // Pero en pantalla Y crece hacia abajo, así que:
        //   slope > 0 → la izquierda queda más arriba en la imagen (menor Y).
        // Convenio doc: "+= sube izq" → izquierda tiene Y más pequeña.
        int16_t yEyeTop  = iy;
        int16_t yLidBase = yEyeTop + ptop;       // borde inferior del rect de párpado
        int16_t yLidL    = yLidBase;             // borde inferior en la esquina izquierda
        int16_t yLidR    = yLidBase - (int16_t)p.slopeTop;  // borde inferior en la der

        // Rect negro que cubre la parte horizontal del párpado
        // Usamos el mínimo de yLidL y yLidR para la altura del rect,
        // y el triángulo tapa la parte sobrante.
        int16_t yLidMin = yLidL < yLidR ? yLidL : yLidR;
        int16_t rectH   = yLidMin - yEyeTop;
        if (rectH > 0) {
            u8.drawBox((u8g2_uint_t)ix, (u8g2_uint_t)yEyeTop,
                       (u8g2_uint_t)iw, (u8g2_uint_t)rectH);
        }

        // Triángulo de pendiente: cubre el área entre yLidMin y el borde inclinado
        // Vértices (en sentido horario):
        //   A = (ix,    yLidMin)   esquina sup-izq del área triangular
        //   B = (ix+iw, yLidMin)   esquina sup-der
        //   C = punto más bajo de la inclinación
        // Si slopeTop > 0 (izq sube → izq tiene menor Y, der tiene mayor Y):
        //   yLidL < yLidR → el rect cubre hasta yLidL, el triángulo baja en la der
        //   vértice C está en (ix+iw, yLidR)
        // Si slopeTop < 0 (der sube → der tiene menor Y, izq tiene mayor Y):
        //   yLidR < yLidL → rect cubre hasta yLidR, triángulo baja en la izq
        //   vértice C está en (ix, yLidL)
        if ((int16_t)p.slopeTop != 0) {
            int16_t cx, cy;
            if (p.slopeTop > 0) {
                // La derecha baja más
                cx = ix + iw - 1;
                cy = yLidR;
            } else {
                // La izquierda baja más
                cx = ix;
                cy = yLidL;
            }
            // Triángulo: (ix, yLidMin), (ix+iw-1, yLidMin), (cx, cy)
            u8.drawTriangle(ix, yLidMin,
                            ix + iw - 1, yLidMin,
                            cx, cy);
        }
    }

    // 3. Párpado inferior (si hay algo que tapar)
    if (pBot > 0.5f) {
        u8.setDrawColor(0);  // negro

        int16_t pbot     = (int16_t)pBot;
        int16_t yEyeBot  = iy + ih;              // borde inferior del ojo
        int16_t yLidTop  = yEyeBot - pbot;       // borde superior del párpado inferior
        int16_t yLidL    = yLidTop;
        int16_t yLidR    = yLidTop - (int16_t)p.slopeBot;

        // Rect negro: desde el punto más alto del párpado hasta el borde inferior
        int16_t yLidMin  = yLidL < yLidR ? yLidL : yLidR;
        int16_t rectH    = yEyeBot - yLidMin;
        if (rectH > 0) {
            u8.drawBox((u8g2_uint_t)ix, (u8g2_uint_t)yLidMin,
                       (u8g2_uint_t)iw, (u8g2_uint_t)rectH);
        }

        // Triángulo de pendiente para el párpado inferior
        if ((int16_t)p.slopeBot != 0) {
            int16_t cx, cy;
            if (p.slopeBot > 0) {
                // Izquierda más alta (menor Y), der baja
                cx = ix + iw - 1;
                cy = yLidR;
            } else {
                cx = ix;
                cy = yLidL;
            }
            u8.drawTriangle(ix, yLidMin,
                            ix + iw - 1, yLidMin,
                            cx, cy);
        }
    }

    // 4. Restaurar color blanco
    u8.setDrawColor(1);
}

// ----------------------------------------------------------------
//  Face::render  —  dibuja ambos ojos en el buffer (sin clear/send)
// ----------------------------------------------------------------
void Face::render(U8G2 &u8)
{
    drawEye(u8, _leftCur);
    drawEye(u8, _rightCur);

    // Texto "Zzz" para la expresión DORMIDO
    if (_expr == Expression::DORMIDO) {
        u8.setDrawColor(1);
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(100, 12, "Zzz");
    }
}
